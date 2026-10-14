#pragma once

// A Library of the test's own (ADR 0055): the factory tables written as the Library holds them,
// beside its library.json, in a folder the test owns. CTest gives every test none
// (SEGNO_LIBRARY), so one is there only where a test puts it.

#include "TableFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

inline juce::File writeTestLibrary(const juce::File& folder)
{
    REQUIRE(folder.getChildFile("Wavetables").createDirectory());
    REQUIRE(
        folder.getChildFile("library.json").replaceWithText(R"({"segnoLibrary": 1, "version": "1.0.0"})"));
    std::string error;
    REQUIRE(fd::devices::wavetable::writeFactoryTableFiles(
        folder.getChildFile("Wavetables").getFullPathName().toStdString(), &error));
    return folder;
}
