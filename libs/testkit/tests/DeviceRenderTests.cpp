#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace fd;
using namespace fd::testkit;

namespace
{
constexpr std::array<ParamSpec, 0> kNoParams{};
constexpr DeviceInfo kInfo{.id = "probe", .name = "Probe", .params = kNoParams};

/// Pass-through device that can misbehave on purpose.
class ProbeDevice final : public Device
{
public:
    explicit ProbeDevice(bool allocate) : Device(kInfo), allocate_(allocate) {}
    void prepare(const ProcessSpec&) override { blocks.clear(); blocks.reserve(100000); }
    void reset() noexcept override {}
    void process(ProcessContext& ctx) noexcept override
    {
        blocks.push_back(ctx.numSamples); // capacity reserved in prepare: no allocation
        if (allocate_)
        {
            std::vector<float> scratch(64); // forbidden on the audio thread
            scratch[0] = 1.0f;
            sink += scratch[0];
        }
        for (int c = 0; c < ctx.output.numChannels; ++c)
            for (int i = 0; i < ctx.numSamples; ++i)
                ctx.output.channel(c)[i] = ctx.input.channel(c)[i];
    }
    std::vector<int> blocks;
    float sink = 0.0f;

private:
    bool allocate_;
};
} // namespace

TEST_CASE("render passes audio through and follows the block pattern", "[fast]")
{
    ProbeDevice d(false);
    const auto in = whiteNoise(1000, 1);
    const auto out = render(d, in, {.blockPattern = {300, 100}});
    CHECK(bitIdentical(in, out));
    CHECK(d.blocks == std::vector<int>{300, 100, 300, 100, 200});
}

TEST_CASE("allocation tracking catches allocations inside process()", "[fast]")
{
    if (!allocationTrackingEnabled())
        SKIP("allocation tracking is disabled in sanitizer builds");
    ProbeDevice good(false);
    auto before = allocationsDuringProcess();
    render(good, whiteNoise(4096, 1));
    CHECK(allocationsDuringProcess() == before);

    ProbeDevice bad(true);
    before = allocationsDuringProcess();
    render(bad, whiteNoise(4096, 1), {.blockPattern = {512}});
    CHECK(allocationsDuringProcess() - before == 8);
}
