#include "fd/device/AssetHandoff.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

using fd::AssetHandoff;

namespace
{
/// Counts live instances so tests can see when data is freed.
struct Tracked
{
    static inline std::atomic<int> alive{0};
    explicit Tracked(int v) : value(v) { ++alive; }
    ~Tracked() { --alive; }
    int value;
};
} // namespace

TEST_CASE("asset handoff delivers the latest published data", "[fast]")
{
    {
        AssetHandoff<Tracked> h;
        CHECK_FALSE(h.takeNew().isSet());

        h.publish(std::make_shared<Tracked>(1));
        h.publish(std::make_shared<Tracked>(2)); // replaces 1 before the audio side took it
        CHECK(Tracked::alive == 1);

        auto ref = h.takeNew();
        REQUIRE(ref);
        CHECK(ref->value == 2);
        CHECK_FALSE(h.takeNew().isSet()); // nothing newer

        h.publish(nullptr); // clearing is a publish too
        auto cleared = h.takeNew();
        CHECK(cleared.isSet());
        CHECK_FALSE(cleared);
        h.retire(cleared);
        CHECK(Tracked::alive == 1); // still held by the audio side
        h.retire(ref);
        CHECK(Tracked::alive == 1); // retired, not freed by the audio side
        h.collect();
        CHECK(Tracked::alive == 0); // freed on the loader side
    }
    CHECK(Tracked::alive == 0);
}

TEST_CASE("asset handoff bounds what the audio side holds", "[fast]")
{
    AssetHandoff<Tracked> h;
    std::vector<AssetHandoff<Tracked>::Ref> held;
    for (size_t i = 0; i < AssetHandoff<Tracked>::maxHeld; ++i)
    {
        h.publish(std::make_shared<Tracked>(static_cast<int>(i)));
        held.push_back(h.takeNew());
        REQUIRE(held.back());
    }
    h.publish(std::make_shared<Tracked>(99));
    CHECK_FALSE(h.takeNew().isSet()); // all slots held: waits
    h.retire(held.front());
    auto next = h.takeNew();
    REQUIRE(next);
    CHECK(next->value == 99);
}

TEST_CASE("asset handoff destructor frees everything", "[fast]")
{
    {
        AssetHandoff<Tracked> h;
        h.publish(std::make_shared<Tracked>(1));
        auto a = h.takeNew();
        h.publish(std::make_shared<Tracked>(2)); // pending
        (void)a;                                  // still held at destruction
    }
    CHECK(Tracked::alive == 0);
}

TEST_CASE("asset handoff survives a loader and an audio thread racing", "[fast]")
{
    {
        AssetHandoff<Tracked> h;
        std::atomic<bool> done{false};
        std::atomic<long> checksum{0};
        std::thread audio([&] {
            AssetHandoff<Tracked>::Ref current;
            while (!done.load())
            {
                if (auto fresh = h.takeNew(); fresh.isSet())
                {
                    h.retire(current);
                    current = fresh;
                }
                if (current)
                    checksum += current->value; // the data must still be alive
            }
            h.retire(current);
        });
        for (int i = 0; i < 20000; ++i)
            h.publish(std::make_shared<Tracked>(i));
        done = true;
        audio.join();
        h.collect();
        CHECK(Tracked::alive == 1); // only latest() remains
    }
    CHECK(Tracked::alive == 0);
}
