// =============================================================================
// maya_node/tests/test_adapter_core.cpp — Phase 2A Slice 10A
// -----------------------------------------------------------------------------
// 3 TEST blocks locking the HelloNode adapter arithmetic:
//   H1  hello_transform(0)  == 1.0 exactly  (Gaussian at r=0)
//   H2  hello_transform(1)  == exp(-1) to 1e-14
//   H3  hello_transform(-x) == hello_transform(x)  (even function)
//
// Random seed: 0xF5BFA9u (sequential after Slice 09's 0xF5BFA8u).
// Kept for future adapter helpers; H1-H3 are deterministic and do not
// use the RNG.
//
// Tolerance rationale (R-09 self-check):
//   * H1: exact equality — gaussian(r=0, eps=1) = exp(0) = 1.0,
//     bit-identical under IEEE 754.
//   * H2: 1e-14 absolute — machine epsilon ~2.22e-16, and the
//     arithmetic is a single std::exp call whose error is ≤ 1-2 ULP
//     (~5e-16).  1e-14 leaves ~45× safety margin.
//   * H3: exact equality — both sides compute std::abs first, the
//     resulting r is bit-identical, so the subsequent exp chain must
//     be as well.
// =============================================================================
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "rbfmax/maya/adapter_core.hpp"

namespace {

using rbfmax::MatrixX;
using rbfmax::Scalar;
using rbfmax::VectorX;
using rbfmax::maya::double_vector_to_eigen;
using rbfmax::maya::eigen_to_double_vector;
using rbfmax::maya::file_exists;
using rbfmax::maya::hello_transform;
using rbfmax::maya::parse_csv_matrix;
using rbfmax::maya::parse_lambda_arg;
using rbfmax::maya::unflatten_double_array;
using rbfmax::maya::validate_json_path;

// Seed reserved (since Slice 10A) for randomised adapter tests.  Slice 11's
// C1-C6 and Slice 12's D1-D8 are deterministic and do not use them; the
// anchor TEST in H3 keeps the symbols alive under -Wunused-variable.
constexpr std::uint32_t kSeed      = 0xF5BFA9u;  // Slice 10A seed
constexpr std::uint32_t kSeedS11   = 0xF5BFAAu;  // Slice 11 seed reserved
constexpr std::uint32_t kSeedS12   = 0xF5BFABu;  // Slice 12 seed reserved

// ---------------------------------------------------------------------
// Cross-platform temp-file helper, mirroring the Slice 08 pattern in
// tests/test_io_json.cpp — uses _dupenv_s on Windows (avoids C4996
// under /WX) and getpid()/tmpnam elsewhere.  File is auto-removed on
// destruction.
// ---------------------------------------------------------------------
class TempFile {
public:
    explicit TempFile(const std::string& tag) {
        static std::atomic<int> counter{0};
        std::ostringstream oss;
#if defined(_WIN32)
        char* tmp = nullptr;
        std::size_t tmp_len = 0;
        if (_dupenv_s(&tmp, &tmp_len, "TEMP") != 0 || tmp == nullptr) {
            if (_dupenv_s(&tmp, &tmp_len, "TMP") != 0 || tmp == nullptr) {
                tmp = nullptr;
            }
        }
        const char* tmp_dir = (tmp != nullptr) ? tmp : ".";
        oss << tmp_dir << "\\rbfmax_adapter_" << tag << "_"
            << static_cast<int>(::_getpid()) << "_"
            << counter.fetch_add(1) << ".json";
        if (tmp != nullptr) std::free(tmp);
#else
        oss << "/tmp/rbfmax_adapter_" << tag << "_"
            << static_cast<int>(::getpid()) << "_"
            << counter.fetch_add(1) << ".json";
#endif
        path_ = oss.str();
    }
    ~TempFile() { std::remove(path_.c_str()); }
    const std::string& path() const noexcept { return path_; }
    void write(const std::string& contents) const {
        std::ofstream f(path_.c_str());
        f << contents;
    }

private:
    std::string path_;
};

}  // namespace

TEST(HelloTransform, H1_ZeroIsUnity) {
    EXPECT_EQ(hello_transform(Scalar(0)), Scalar(1));
}

TEST(HelloTransform, H2_UnitInputIsExpMinusOne) {
    const Scalar got      = hello_transform(Scalar(1));
    const Scalar expected = std::exp(Scalar(-1));
    EXPECT_NEAR(got, expected, Scalar(1e-14));
}

TEST(HelloTransform, H3_EvenFunctionUnderSignFlip) {
    // Gaussian kernel at r = |x| is even in x.  Test across a small
    // sweep so a regression in std::abs would be caught.
    const Scalar samples[] = {Scalar(1), Scalar(0.5), Scalar(2.0),
                              Scalar(1e-3), Scalar(1e3)};
    for (Scalar x : samples) {
        EXPECT_EQ(hello_transform(x), hello_transform(-x))
            << "x = " << x;
    }
    // Anchor kSeed / kSeedS11 / kSeedS12 so -Wunused-variable does not
    // fire under /WX until Slice 11+ tests actually consume them.
    EXPECT_NE(kSeed,    0u);
    EXPECT_NE(kSeedS11, 0u);
    EXPECT_NE(kSeedS12, 0u);
}

// =============================================================================
//  Slice 11 — C group (6): attribute-adapter marshalling + JSON path probe
// =============================================================================
//
// These tests cover the three pure-C++ helpers that bridge Maya's
// MFnDoubleArrayData / MFnStringData attribute types to the Phase 1
// RBFInterpolator API.  Keeping them free of Maya lets the GTest suite
// run on any CI node without the devkit.

// C1 — Round-trip identity: double[] -> VectorX -> double[] must be
// bit-identical.  memcpy-equivalent operation; 1e-14 tolerance chosen
// to leave a 45× safety margin over double ULP, though err=0 is
// expected in practice and asserted via EXPECT_DOUBLE_EQ.
TEST(AdapterMarshalling, C1_DoubleVectorRoundTrip) {
    const std::vector<double> src = {0.0, 1.0, -2.5, 3.141592653589793,
                                     1e-300, 1e300, -0.0, 42.0, -1.0, 0.5};
    VectorX e = double_vector_to_eigen(src);
    ASSERT_EQ(e.size(), static_cast<Eigen::Index>(src.size()));

    std::vector<double> back = eigen_to_double_vector(e);
    ASSERT_EQ(back.size(), src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        EXPECT_DOUBLE_EQ(back[i], src[i]) << "i=" << i;
    }
}

// C2 — Empty-vector round-trip must not crash and must yield size 0.
TEST(AdapterMarshalling, C2_EmptyVectorRoundTrip) {
    const std::vector<double> src{};
    VectorX e = double_vector_to_eigen(src);
    EXPECT_EQ(e.size(), 0);
    std::vector<double> back = eigen_to_double_vector(e);
    EXPECT_TRUE(back.empty());
}

// C3 — validate_json_path returns false for a path that does not exist.
TEST(AdapterJsonPath, C3_NonExistentReturnsFalse) {
#if defined(_WIN32)
    const std::string bogus =
        "Z:\\definitely_does_not_exist\\rbfmax_slice11_c3.json";
#else
    const std::string bogus =
        "/tmp/__rbfmax_slice11_c3_definitely_does_not_exist.json";
#endif
    EXPECT_FALSE(validate_json_path(bogus));
}

// C4 — validate_json_path returns true for a file that exists and is
// readable.  Uses TempFile RAII to create + clean up.
TEST(AdapterJsonPath, C4_ExistentReturnsTrue) {
    TempFile tf("c4_exist");
    tf.write("{}");  // content is irrelevant; validate_json_path only
                      // probes existence + readability, not schema.
    EXPECT_TRUE(validate_json_path(tf.path()));
}

// C5 — validate_json_path rejects the empty string (the "no path set"
// state represented in Maya by an unset string attribute).
TEST(AdapterJsonPath, C5_EmptyStringReturnsFalse) {
    EXPECT_FALSE(validate_json_path(std::string{}));
}

// C6 — double precision is preserved by the round-trip.  Slice 08's
// schema v1 claim is "full double round-trip"; Slice 11's adapter layer
// must not be the weaker link.
TEST(AdapterMarshalling, C6_DoubleToEigenPreservesPrecision) {
    // Hand-picked values whose IEEE 754 representation requires all 17
    // significant decimal digits to recover uniquely (same test vectors
    // used in Slice 08 test_io_json's FullDoublePrecisionRoundTrip).
    const std::vector<double> src = {
        3.141592653589793,     // π
        2.718281828459045,     // e
        1.4142135623730951,    // √2
        1.7320508075688772,    // √3
        0.3333333333333333,    // 1/3
        0.1234567890123456};
    VectorX e = double_vector_to_eigen(src);
    std::vector<double> back = eigen_to_double_vector(e);
    for (std::size_t i = 0; i < src.size(); ++i) {
        EXPECT_EQ(back[i], src[i]) << "i=" << i;  // exact ==, not NEAR
    }
}

// =============================================================================
//  Slice 12 — D group (8): training-command helpers
// =============================================================================
//
// These tests cover the four pure-C++ utilities that back the
// rbfmaxTrainAndSave MPxCommand:
//   * unflatten_double_array    (inline mode: flat list -> N x D matrix)
//   * parse_csv_matrix          (csv mode: file -> matrix)
//   * parse_lambda_arg          ("auto" or numeric)
//   * file_exists               (same contract as validate_json_path)
//
// All deterministic; kSeedS12 reserved for future randomised additions.

// D1 — Simple unflatten: {1,2,3,4,5,6} with D=2 -> 3x2 matrix.
TEST(AdapterTrainInline, D1_UnflattenSimple) {
    const std::vector<double> flat = {1, 2, 3, 4, 5, 6};
    MatrixX m;
    ASSERT_TRUE(unflatten_double_array(flat, 2, m));
    ASSERT_EQ(m.rows(), 3);
    ASSERT_EQ(m.cols(), 2);
    EXPECT_EQ(m(0, 0), 1.0);  EXPECT_EQ(m(0, 1), 2.0);
    EXPECT_EQ(m(1, 0), 3.0);  EXPECT_EQ(m(1, 1), 4.0);
    EXPECT_EQ(m(2, 0), 5.0);  EXPECT_EQ(m(2, 1), 6.0);
}

// D2 — size not a multiple of D: must fail and leave out untouched.
TEST(AdapterTrainInline, D2_UnflattenBadLength) {
    const std::vector<double> flat = {1, 2, 3};
    MatrixX sentinel(5, 7);  // pre-existing shape
    MatrixX m = sentinel;
    EXPECT_FALSE(unflatten_double_array(flat, 2, m));
    // On failure, out is untouched (preserves atomic-update contract).
    EXPECT_EQ(m.rows(), 5);
    EXPECT_EQ(m.cols(), 7);
}

// D3 — non-positive D: must fail.
TEST(AdapterTrainInline, D3_UnflattenBadDim) {
    const std::vector<double> flat = {1, 2, 3, 4};
    MatrixX m;
    EXPECT_FALSE(unflatten_double_array(flat, 0, m));
    EXPECT_FALSE(unflatten_double_array(flat, -1, m));
}

// D4 — simple CSV: "1.0,2.0\n3.0,4.0\n" -> 2x2 matrix.
TEST(AdapterTrainCsv, D4_ParseCsvSimple) {
    TempFile tf("d4");
    tf.write("1.0,2.0\n3.0,4.0\n");
    MatrixX m;
    std::string err;
    ASSERT_TRUE(parse_csv_matrix(tf.path(), m, err)) << "err=" << err;
    ASSERT_EQ(m.rows(), 2);
    ASSERT_EQ(m.cols(), 2);
    EXPECT_EQ(m(0, 0), 1.0);  EXPECT_EQ(m(0, 1), 2.0);
    EXPECT_EQ(m(1, 0), 3.0);  EXPECT_EQ(m(1, 1), 4.0);
    EXPECT_TRUE(err.empty());
}

// D5 — comments + blank lines are skipped; data rows are collected.
TEST(AdapterTrainCsv, D5_ParseCsvWithCommentsAndBlankLines) {
    TempFile tf("d5");
    tf.write("# header comment\n"
             "1,2\n"
             "\n"
             "# another comment\n"
             "3,4\n"
             "\n");
    MatrixX m;
    std::string err;
    ASSERT_TRUE(parse_csv_matrix(tf.path(), m, err)) << "err=" << err;
    ASSERT_EQ(m.rows(), 2);
    ASSERT_EQ(m.cols(), 2);
    EXPECT_EQ(m(0, 0), 1.0);  EXPECT_EQ(m(0, 1), 2.0);
    EXPECT_EQ(m(1, 0), 3.0);  EXPECT_EQ(m(1, 1), 4.0);
}

// D6 — column-count mismatch across rows: fail, err_reason says so.
TEST(AdapterTrainCsv, D6_ParseCsvColMismatch) {
    TempFile tf("d6");
    tf.write("1,2\n3,4,5\n");
    MatrixX m;
    std::string err;
    EXPECT_FALSE(parse_csv_matrix(tf.path(), m, err));
    EXPECT_NE(err.find("mismatch"), std::string::npos)
        << "err should contain 'mismatch', got: " << err;
}

// D7 — "auto" / "AUTO" / "Auto" all resolve to is_auto=true.
TEST(AdapterTrainLambda, D7_ParseLambdaAuto) {
    const std::string variants[] = {"auto", "AUTO", "Auto"};
    for (const auto& v : variants) {
        bool is_auto = false;
        Scalar lambda_value = -1.0;
        ASSERT_TRUE(parse_lambda_arg(v, is_auto, lambda_value))
            << "variant: " << v;
        EXPECT_TRUE(is_auto) << "variant: " << v;
    }
}

// D8 — numeric strings parse end-to-end and set is_auto=false.
TEST(AdapterTrainLambda, D8_ParseLambdaNumeric) {
    bool is_auto = true;
    Scalar lambda_value = 0;
    ASSERT_TRUE(parse_lambda_arg("1e-6", is_auto, lambda_value));
    EXPECT_FALSE(is_auto);
    EXPECT_DOUBLE_EQ(lambda_value, 1e-6);

    ASSERT_TRUE(parse_lambda_arg("0.001", is_auto, lambda_value));
    EXPECT_FALSE(is_auto);
    EXPECT_DOUBLE_EQ(lambda_value, 0.001);

    // Trailing garbage should cause rejection.
    EXPECT_FALSE(parse_lambda_arg("1e-6xyz", is_auto, lambda_value));
    EXPECT_FALSE(parse_lambda_arg("not_a_number", is_auto, lambda_value));

    // file_exists sanity-check alongside (single D-group test keeps the
    // matrix small; the helper is trivially delegated to
    // validate_json_path).
    EXPECT_FALSE(file_exists(""));
    TempFile tf("d8_exists");
    tf.write("x");
    EXPECT_TRUE(file_exists(tf.path()));
}
