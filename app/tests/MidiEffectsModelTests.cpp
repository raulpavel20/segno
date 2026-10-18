#include "model/MidiEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fd::model;

TEST_CASE("MIDI effects: placed before the first module that plays notes", "[fast]")
{
    using R = ChainRole;
    const std::vector<R> chain{R::MidiEffect, R::Player, R::Other, R::Other};
    CHECK(placeInChain(chain, -1, R::MidiEffect) == 1); // added at the end: before the instrument
    CHECK(placeInChain(chain, 3, R::MidiEffect) == 1);  // dropped after it: just before it
    CHECK(placeInChain(chain, 0, R::MidiEffect) == 0);  // before it already: where it's asked
    CHECK(placeInChain(chain, 1, R::MidiEffect) == 1);
    CHECK(placeInChain(chain, 3, R::Other) == 3); // an audio effect: anywhere
    CHECK(placeInChain(chain, -1, R::Other) == -1);
    const std::vector<R> noInstrument{R::Other, R::Other};
    CHECK(placeInChain(noInstrument, -1, R::MidiEffect) == -1); // none: at the end
    CHECK(placeInChain(noInstrument, 1, R::MidiEffect) == 1);
    const std::vector<R> twoPlayers{R::Other, R::Player, R::Player};
    CHECK(placeInChain(twoPlayers, 2, R::MidiEffect) == 1); // the first one that plays
}
