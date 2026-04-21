// =============================================================================
// maya_node/src/adapter_core_csv.cpp — Phase 2A Slice 12
// -----------------------------------------------------------------------------
// Non-inline implementation of the Slice 12 training-command helpers that
// are documented in adapter_core.hpp.  These are kept out of the header
// (and out of the node plugin's hot path) because they use std::string /
// std::stod / std::ifstream which may throw and thus cannot be noexcept.
//
// Deliberately Maya-free so the adapter GTest suite links this TU without
// pulling in the devkit.
// =============================================================================
#include "rbfmax/maya/adapter_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace rbfmax {
namespace maya {

namespace {

// Trim leading + trailing ASCII whitespace.  Returns an independent
// std::string (no pointers into the source).
std::string strip(const std::string& s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto begin = std::find_if_not(s.begin(), s.end(), is_ws);
    auto end   = std::find_if_not(s.rbegin(), s.rend(), is_ws).base();
    return (begin < end) ? std::string(begin, end) : std::string();
}

// Split a CSV line on ',' and trim each cell.  Always returns at least
// one element (an empty string, for lines containing no comma).
std::vector<std::string> split_csv_row(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            cells.push_back(strip(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    cells.push_back(strip(cur));
    return cells;
}

}  // namespace

bool parse_csv_matrix(const std::string& path,
                      MatrixX& out,
                      std::string& err_reason) {
    std::ifstream f(path.c_str());
    if (!f.is_open()) {
        err_reason = "cannot open csv file: " + path;
        return false;
    }

    std::vector<std::vector<Scalar>> rows;
    Eigen::Index cols = -1;
    std::string line;
    std::size_t line_no = 0;

    while (std::getline(f, line)) {
        ++line_no;
        const std::string trimmed = strip(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;

        const auto cells = split_csv_row(line);
        if (cols < 0) {
            cols = static_cast<Eigen::Index>(cells.size());
        } else if (static_cast<Eigen::Index>(cells.size()) != cols) {
            err_reason = "csv column count mismatch at line " +
                         std::to_string(line_no);
            return false;
        }

        std::vector<Scalar> parsed;
        parsed.reserve(cells.size());
        for (const auto& cell : cells) {
            if (cell.empty()) {
                err_reason = "empty cell at line " +
                             std::to_string(line_no);
                return false;
            }
            try {
                std::size_t pos = 0;
                const double v = std::stod(cell, &pos);
                if (pos != cell.size()) {
                    err_reason =
                        "non-numeric trailing characters at line " +
                        std::to_string(line_no) + ": \"" + cell + "\"";
                    return false;
                }
                parsed.push_back(static_cast<Scalar>(v));
            } catch (const std::exception&) {
                err_reason = "cannot parse number at line " +
                             std::to_string(line_no) + ": \"" + cell + "\"";
                return false;
            }
        }
        rows.push_back(std::move(parsed));
    }

    if (rows.empty() || cols <= 0) {
        err_reason = "csv file has no data rows: " + path;
        return false;
    }

    MatrixX mat(static_cast<Eigen::Index>(rows.size()), cols);
    for (Eigen::Index i = 0; i < mat.rows(); ++i) {
        for (Eigen::Index j = 0; j < cols; ++j) {
            mat(i, j) = rows[static_cast<std::size_t>(i)]
                            [static_cast<std::size_t>(j)];
        }
    }
    out = std::move(mat);
    err_reason.clear();
    return true;
}

bool parse_lambda_arg(const std::string& s,
                      bool& is_auto,
                      Scalar& lambda_value) {
    if (s == "auto" || s == "AUTO" || s == "Auto") {
        is_auto = true;
        lambda_value = Scalar(0);
        return true;
    }
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        is_auto = false;
        lambda_value = static_cast<Scalar>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace maya
}  // namespace rbfmax
