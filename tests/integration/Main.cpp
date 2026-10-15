// The integration tests' main: Catch2's session, unless tracktion's plugin scan launched this
// binary as its child process (PluginScanTests.cpp, M3 C5). The child serves the scan's requests
// until the scan lets it go, as the app does.
#include "engine/PluginScan.h"

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));
    if (args.size() == 1 && args[0].startsWith("--PluginScan:")) // tracktion's PluginScanHelpers
    {
        juce::ScopedJuceInitialiser_GUI init;
        if (fd::engine::PluginScan::serveAsChild(args.joinIntoString(" ")))
        {
            juce::MessageManager::getInstance()->runDispatchLoop();
            return 0;
        }
        return 1;
    }
    return Catch::Session().run(argc, argv);
}
