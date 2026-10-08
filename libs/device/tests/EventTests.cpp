#include "fd/device/Events.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd;

TEST_CASE("event list respects capacity", "[fast]")
{
    EventList list;
    list.reserve(2);
    CHECK(list.push({.sampleOffset = 0}));
    CHECK(list.push({.sampleOffset = 1}));
    CHECK_FALSE(list.push({.sampleOffset = 2}));
    CHECK(list.size() == 2);
    list.clear();
    CHECK(list.empty());
    CHECK(list.capacity() == 2);
}

TEST_CASE("sortByTime is stable", "[fast]")
{
    EventList list;
    list.reserve(8);
    list.push({.sampleOffset = 10, .key = 1});
    list.push({.sampleOffset = 0, .key = 2});
    list.push({.sampleOffset = 10, .key = 3});
    list.push({.sampleOffset = 5, .key = 4});
    list.sortByTime();
    REQUIRE(list.size() == 4);
    CHECK(list[0].key == 2);
    CHECK(list[1].key == 4);
    CHECK(list[2].key == 1);
    CHECK(list[3].key == 3);
}
