#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fd::devices::wavetable
{
/// A table's Formula source (docs/design/table-editor.md §3): one line in `x` (the phase, 0–1)
/// and `t` (where in the table, 0–1), parsed once and evaluated for every sample of every frame.
///
/// Numbers, `x`, `t`, `pi`, `e`; `+ - * / ^` and brackets; the functions sin, cos, tan, asin,
/// acos, atan, abs, sqrt, exp, log, log2, floor, ceil, frac, sign, min(a, b), max(a, b),
/// pow(a, b), clamp(v, lo, hi), and the unit waves tri(p), saw(p), sq(p) (one cycle per unit of
/// `p`, -1 to 1, each starting at 0 or 1 and going up as a sine does: saw rises) and rand(p) (a
/// fixed random value for each whole `p`, -1 to 1).
class Formula
{
public:
    /// Parses `text`; on failure, nothing, and `error` says where and why.
    [[nodiscard]] static std::optional<Formula> parse(const std::string& text, std::string* error = nullptr);

    [[nodiscard]] double evaluate(double x, double t) const noexcept;
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    struct Node;

private:
    std::string text_;
    std::shared_ptr<const Node> root_;
};
} // namespace fd::devices::wavetable
