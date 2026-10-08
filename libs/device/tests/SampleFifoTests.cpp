#include "fd/device/SampleFifo.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace fd;

TEST_CASE("sample FIFO preserves order and drops when full", "[fast]")
{
    SampleFifo f;
    f.prepare(6); // rounds to 8
    CHECK(f.capacity() == 8);
    const float in[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK(f.push(in, 10) == 8); // two dropped
    float out[16] = {};
    CHECK(f.pop(out, 3) == 3);
    CHECK(out[0] == 1.0f);
    CHECK(out[2] == 3.0f);
    CHECK(f.push(in, 2) == 2); // wraps
    CHECK(f.pop(out, 16) == 7);
    CHECK(out[4] == 8.0f);
    CHECK(out[5] == 1.0f);
    CHECK(f.available() == 0);
}

TEST_CASE("sample FIFO is consistent across threads", "[fast]")
{
    SampleFifo f;
    f.prepare(1024);
    constexpr int total = 200000;
    std::thread producer([&] {
        float next = 0.0f;
        while (next < total)
        {
            const float block[] = {next, next + 1, next + 2, next + 3};
            next += static_cast<float>(f.push(block, next + 4 <= total ? 4 : static_cast<size_t>(total - next)));
        }
    });
    float expected = 0.0f;
    std::vector<float> buf(256);
    while (expected < total)
    {
        const size_t n = f.pop(buf.data(), buf.size());
        for (size_t i = 0; i < n; ++i)
        {
            REQUIRE(buf[i] == expected);
            expected += 1.0f;
        }
    }
    producer.join();
}
