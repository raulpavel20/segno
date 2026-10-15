// The plugin scan in a child process (M3 C5; daw-shell.md, "Scanning"): a plugin that crashes
// when it's looked at takes only the scan's child down. It's skipped and listed, the scan goes
// on to find the rest, and this process (the DAW) carries on. The child is this test binary run
// again (Main.cpp), as the app's own binary is.
#include "engine/EngineBehaviour.h"
#include "engine/PluginScan.h"

#include <catch2/catch_test_macros.hpp>

#if FD_HAVE_TEST_PATHS
#include "fd_test_paths.h"
#endif

using fd::engine::PluginScan;

#if defined(FD_CRASHING_VST3_PATH) && FD_HAVE_TEST_PATHS
TEST_CASE("plugin scan: a plugin that crashes the scan is skipped and listed; the scan goes on in the "
          "background and finds the rest (C5)",
          "[integration][plugins]")
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-plugin-scan-tests", nullptr,
                             fd::engine::makeEngineBehaviour({.scanPluginsOutOfProcess = true})};
    auto& list = engine.getPluginManager().knownPluginList;
    list.clear();
    list.clearBlacklistedFiles();
    REQUIRE(engine.getPluginManager().usesSeparateProcessForScanning());

    // A folder holding the crashing plugin and our Utility, which comes after it.
    const auto folder = juce::File::createTempFile("fd-plugin-scan");
    REQUIRE(folder.createDirectory());
    const juce::File crashing(FD_CRASHING_VST3_PATH), utility(FD_UTILITY_VST3_PATH);
    REQUIRE(crashing.isDirectory());
    REQUIRE(utility.isDirectory());
    REQUIRE(crashing.createSymbolicLink(folder.getChildFile(crashing.getFileName()), true));
    REQUIRE(utility.createSymbolicLink(folder.getChildFile(utility.getFileName()), true));

    std::vector<juce::String> seen;
    std::optional<PluginScan::Result> result;
    {
        PluginScan scan(
            engine, {.folders = {folder.getFullPathName().toStdString()}, .systemFolders = false},
            [&](const juce::String& file) { seen.push_back(juce::File(file).getFileName()); },
            [&](PluginScan::Result r) { result = r; });
        // The message thread stays free while it scans.
        const auto start = juce::Time::getMillisecondCounter();
        while (!result && juce::Time::getMillisecondCounter() - start < 60000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(scan.isDone());
    }
    REQUIRE(result.has_value());
    CHECK(result->skipped == 1);
    CHECK(result->found >= 1);
    CHECK(std::find(seen.begin(), seen.end(), crashing.getFileName()) != seen.end());
    // The crashing plugin is on the skipped list; the Utility was found after it.
    const auto skipped = list.getBlacklistedFiles();
    CHECK(skipped.size() == 1);
    CHECK(skipped[0].contains(crashing.getFileName()));
    CHECK(std::any_of(list.getTypes().begin(), list.getTypes().end(),
                      [](const juce::PluginDescription& d) { return d.name.contains("Utility"); }));

    // Again: the skipped one isn't looked at, and nothing new is skipped.
    result.reset();
    seen.clear();
    {
        PluginScan scan(
            engine, {.folders = {folder.getFullPathName().toStdString()}, .systemFolders = false},
            [&](const juce::String& file) { seen.push_back(juce::File(file).getFileName()); },
            [&](PluginScan::Result r) { result = r; });
        const auto start = juce::Time::getMillisecondCounter();
        while (!result && juce::Time::getMillisecondCounter() - start < 60000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    REQUIRE(result.has_value());
    CHECK(result->skipped == 0);
    folder.deleteRecursively(false);
    list.clear();
    list.clearBlacklistedFiles();
}
#endif
