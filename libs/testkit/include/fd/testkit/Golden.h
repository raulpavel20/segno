#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace fd::testkit
{
/// Golden metrics (ADR 0007). A golden file is a JSON object:
///   { "metric-name": { "value": 1.23, "tol": 0.01 }, ... }
/// Metrics are compared with per-metric absolute tolerances, never as bit-exact audio,
/// so they hold across platforms.
///
/// Set the environment variable FD_UPDATE_GOLDENS=1 to (re)write golden files. Existing
/// tolerances are kept; new metrics get `defaultTolerance`. Any golden change must be
/// explained in the commit message.
struct GoldenResult
{
    bool passed = false;
    std::string report; // human-readable; lists every mismatch
};

GoldenResult checkGolden(const std::string& path, const nlohmann::json& metrics, double defaultTolerance);

[[nodiscard]] bool goldenUpdateRequested();
} // namespace fd::testkit
