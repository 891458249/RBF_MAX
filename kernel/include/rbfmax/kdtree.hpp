// =============================================================================
// rbfmax/kdtree.hpp
// -----------------------------------------------------------------------------
// Array-backed Euclidean kd-tree for k-nearest-neighbor acceleration of
// large-sample RBF interpolation.
//
// Design contract (locked in pre-slice review)
// --------------------------------------------
//   1. Sample ownership: KdTree holds only a non-owning reference to the
//      caller's MatrixX. The caller MUST keep that buffer alive (and at
//      the same address, with unchanged contents) for the whole lifetime
//      of this KdTree. Move/realloc the source matrix => undefined.
//   2. Node layout: a single std::vector<Node> with explicit child indices
//      (not heap-allocated linked nodes). Cache-friendly for queries.
//   3. Sample index ordering inside the tree: pre-shuffled in build,
//      stored as Node::point_idx. Original sample-row indices are what we
//      return from knn_search.
//   4. Output contract:
//        - knn_search returns n = min(k, size()), 0 if empty or k <= 0.
//        - out_indices[0..n)        = sample row indices, ascending in distance
//        - out_sq_distances[0..n)   = squared distances, strictly ascending
//          (ties broken by lower row-index first; see implementation note)
//        - Caller-allocated buffers must hold at least k entries; we never
//          touch entries beyond [0..n).
//   5. Split strategy: variance-based dimension selection + median split
//      via std::nth_element. See math_derivation.md §10.
//   6. Hot path: zero heap allocation per query. Uses a stack-allocated
//      max-heap (std::priority_queue) sized at most k.
//
// All methods are noexcept; pre-condition violations are reported via
// eigen_assert in Debug and trusted in Release.
// =============================================================================
#ifndef RBFMAX_KDTREE_HPP
#define RBFMAX_KDTREE_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <queue>
#include <utility>
#include <vector>

#include "rbfmax/types.hpp"

namespace rbfmax {
namespace spatial {

class KdTree {
   public:
    /// Build a kd-tree over `samples` (one sample per row). The matrix is
    /// referenced — see ownership contract in the file doc-block.
    /// Empty matrices (rows()==0) are accepted; subsequent queries return 0.
    explicit KdTree(const Eigen::Ref<const MatrixX>& samples) noexcept
        : samples_(samples),
          n_samples_(samples.rows()),
          dim_(samples.cols()) {
        if (n_samples_ == 0) {
            return;
        }
        nodes_.reserve(static_cast<std::size_t>(n_samples_));
        std::vector<Index> indices(static_cast<std::size_t>(n_samples_));
        std::iota(indices.begin(), indices.end(), Index{0});
        build_recursive(indices.data(), Index{0}, n_samples_);
    }

    /// k-nearest neighbors of `query`. Writes up to k entries (in ascending
    /// distance) to caller-allocated `out_indices` / `out_sq_distances` and
    /// returns the actual count.
    Index knn_search(const Eigen::Ref<const VectorX>& query,
                     Index k,
                     Index* out_indices,
                     Scalar* out_sq_distances) const noexcept {
        eigen_assert(query.size() == dim_);
        if (n_samples_ == 0 || k <= 0) {
            return 0;
        }
        const Index k_eff = (k < n_samples_) ? k : n_samples_;

        // Max-heap on (sq_dist, point_idx).  pair's default less-than orders
        // by .first then .second, so the heap top is the largest sq_dist —
        // exactly what we want to evict when a closer neighbor turns up.
        using HeapItem = std::pair<Scalar, Index>;
        std::priority_queue<HeapItem> heap;

        query_recursive(/*node_idx*/ 0, query, k_eff, heap);

        // Drain heap (largest-first) into the back of the output buffer; then
        // it's already in ascending order from index 0 onward.
        Index n = static_cast<Index>(heap.size());
        for (Index i = n - 1; i >= 0; --i) {
            out_sq_distances[i] = heap.top().first;
            out_indices[i] = heap.top().second;
            heap.pop();
        }
        return n;
    }

    Index size() const noexcept { return n_samples_; }
    Index dim() const noexcept { return dim_; }

   private:
    struct Node {
        Index point_idx;
        Index left_child;   // -1 = none
        Index right_child;  // -1 = none
        int split_dim;
        Scalar split_value;
    };

    // Recursive build over indices[first, last).  Returns the index of the
    // newly-created root node within nodes_, or -1 for an empty range.
    // Algorithm: variance-based axis pick + nth_element median split.
    // See math_derivation.md §10.4.
    Index build_recursive(Index* indices, Index first, Index last) {
        if (first >= last) {
            return Index{-1};
        }

        // Leaf: single sample.
        if (last - first == 1) {
            const Index node_idx = static_cast<Index>(nodes_.size());
            nodes_.push_back(Node{indices[first], Index{-1}, Index{-1},
                                  /*split_dim*/ 0, /*split_value*/ Scalar{0}});
            return node_idx;
        }

        // --- Choose split dimension: largest-variance axis (§10.4) ---
        // Use Welford-free two-pass: mean then sum-of-squared-deviations.
        int best_dim = 0;
        Scalar best_var = Scalar{-1};
        for (int d = 0; d < static_cast<int>(dim_); ++d) {
            Scalar mean = Scalar{0};
            for (Index i = first; i < last; ++i) {
                mean += samples_(indices[i], d);
            }
            mean /= static_cast<Scalar>(last - first);
            Scalar var = Scalar{0};
            for (Index i = first; i < last; ++i) {
                const Scalar diff = samples_(indices[i], d) - mean;
                var += diff * diff;
            }
            if (var > best_var) {
                best_var = var;
                best_dim = d;
            }
        }

        // --- Median split via nth_element (O(N)) — §10.1 ---
        const Index mid = first + (last - first) / 2;
        const int split_dim = best_dim;
        const auto& mat = samples_;  // capture for lambda
        std::nth_element(
            indices + first, indices + mid, indices + last,
            [&mat, split_dim](Index a, Index b) noexcept {
                return mat(a, split_dim) < mat(b, split_dim);
            });

        const Index pivot_point = indices[mid];
        const Scalar split_value = samples_(pivot_point, split_dim);

        // Reserve the slot first so children get correct child-index values.
        const Index node_idx = static_cast<Index>(nodes_.size());
        nodes_.push_back(Node{pivot_point, Index{-1}, Index{-1}, split_dim,
                              split_value});

        const Index left = build_recursive(indices, first, mid);
        const Index right = build_recursive(indices, mid + 1, last);

        nodes_[static_cast<std::size_t>(node_idx)].left_child = left;
        nodes_[static_cast<std::size_t>(node_idx)].right_child = right;
        return node_idx;
    }

    // Recursive query.  `heap` is a max-heap on squared distance, capped at k.
    // Pruning: if the squared perpendicular distance to the split plane is
    // already >= the current k-th best, the far subtree cannot contribute.
    // See math_derivation.md §10.3.
    void query_recursive(
        Index node_idx,
        const Eigen::Ref<const VectorX>& query,
        Index k,
        std::priority_queue<std::pair<Scalar, Index>>& heap) const noexcept {
        if (node_idx < 0) {
            return;
        }
        const Node& node = nodes_[static_cast<std::size_t>(node_idx)];

        // Squared distance from query to this node's sample.
        Scalar sq_dist = Scalar{0};
        for (int d = 0; d < static_cast<int>(dim_); ++d) {
            const Scalar diff = query(d) - samples_(node.point_idx, d);
            sq_dist += diff * diff;
        }

        if (static_cast<Index>(heap.size()) < k) {
            heap.emplace(sq_dist, node.point_idx);
        } else if (sq_dist < heap.top().first) {
            heap.pop();
            heap.emplace(sq_dist, node.point_idx);
        }

        // Choose near / far subtree based on which side of the split plane
        // the query lies on.
        const Scalar diff = query(node.split_dim) - node.split_value;
        const Index near_child =
            (diff < Scalar{0}) ? node.left_child : node.right_child;
        const Index far_child =
            (diff < Scalar{0}) ? node.right_child : node.left_child;

        query_recursive(near_child, query, k, heap);

        // Prune the far subtree if the split plane is farther than the
        // current k-th best (or we still have unfilled slots).
        const Scalar plane_sq = diff * diff;
        if (static_cast<Index>(heap.size()) < k || plane_sq < heap.top().first) {
            query_recursive(far_child, query, k, heap);
        }
    }

    // Non-owning reference to the caller-supplied matrix.  Eigen::Ref over
    // const promises we never mutate it; the lifetime burden is on the caller.
    Eigen::Ref<const MatrixX> samples_;
    Index n_samples_;
    Index dim_;
    std::vector<Node> nodes_;
};

}  // namespace spatial
}  // namespace rbfmax

#endif  // RBFMAX_KDTREE_HPP
