#include "fd/device/Realtime.h"

namespace fd
{
#if defined(FD_RTSAN)
// The nonblocking attribute makes RealtimeSanitizer treat everything called from here as
// real-time context. Calls through the function pointer are unknown to the static
// effect analysis, which is fine: the runtime check is what we want.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
__attribute__((noinline)) void runRealtime(void (*fn)(void*), void* ctx) noexcept [[clang::nonblocking]]
{
    fn(ctx);
}
#pragma clang diagnostic pop
#else
void runRealtime(void (*fn)(void*), void* ctx) noexcept
{
    fn(ctx);
}
#endif
} // namespace fd
