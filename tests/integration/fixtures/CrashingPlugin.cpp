// A VST3 that crashes as soon as a host looks at it (the plugin scan's test, M3 C5): its module
// entry point aborts, so scanning it takes down whatever process loaded it. Only the scan's
// child process should ever load it.
#include <cstdlib>

extern "C"
{
    __attribute__((visibility("default"))) bool ModuleEntry(void*)
    {
        std::abort();
    }

    __attribute__((visibility("default"))) bool ModuleExit()
    {
        return true;
    }

    __attribute__((visibility("default"))) void* GetPluginFactory()
    {
        std::abort();
    }
}
