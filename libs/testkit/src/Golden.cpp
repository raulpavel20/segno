#include "fd/testkit/Golden.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fd::testkit
{
bool goldenUpdateRequested()
{
    const char* v = std::getenv("FD_UPDATE_GOLDENS");
    return v != nullptr && std::string(v) == "1";
}

GoldenResult checkGolden(const std::string& path, const nlohmann::json& metrics, double defaultTolerance)
{
    namespace fs = std::filesystem;
    nlohmann::json golden = nlohmann::json::object();
    const bool exists = fs::exists(path);
    if (exists)
    {
        std::ifstream in(path);
        golden = nlohmann::json::parse(in, nullptr, /*allow_exceptions*/ false);
        if (golden.is_discarded() || !golden.is_object())
            return {false, "golden file is not a JSON object: " + path};
    }

    if (goldenUpdateRequested())
    {
        nlohmann::json updated = nlohmann::json::object();
        for (const auto& [name, value] : metrics.items())
        {
            const double tol = golden.contains(name) ? golden[name].value("tol", defaultTolerance) : defaultTolerance;
            updated[name] = {{"value", value.get<double>()}, {"tol", tol}};
        }
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream out(path);
        out << updated.dump(2) << "\n";
        return {true, "golden updated: " + path};
    }

    if (!exists)
        return {false, "golden file missing: " + path + " (run with FD_UPDATE_GOLDENS=1 to create it)"};

    std::ostringstream report;
    bool passed = true;
    for (const auto& [name, value] : metrics.items())
    {
        if (!golden.contains(name))
        {
            passed = false;
            report << "  new metric not in golden: " << name << "\n";
            continue;
        }
        const double expected = golden[name].at("value").get<double>();
        const double tol = golden[name].value("tol", defaultTolerance);
        const double actual = value.get<double>();
        if (!(std::abs(actual - expected) <= tol))
        {
            passed = false;
            report << "  " << name << ": expected " << expected << " +/- " << tol << ", got " << actual << "\n";
        }
    }
    for (const auto& [name, unused] : golden.items())
    {
        (void)unused;
        if (!metrics.contains(name))
        {
            passed = false;
            report << "  metric missing from results: " << name << "\n";
        }
    }
    return {passed, passed ? std::string("ok") : "golden mismatch (" + path + "):\n" + report.str()};
}
} // namespace fd::testkit
