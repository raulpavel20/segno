// The Library (ADR 0055): found where it's installed, references into it resolved on each machine.
#include "fd/device/Library.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("library: references into it stored and resolved; files outside it kept as they are", "[fast]")
{
    const auto dir = fs::temp_directory_path() / "fd-library-tests";
    fs::remove_all(dir);
    fs::create_directories(dir / "Library" / "Samples" / "Strings");
    std::ofstream(dir / "Library" / "library.json") << R"({"segnoLibrary": 1, "version": "1.0.0"})";
    std::ofstream(dir / "Library" / "Samples" / "Strings" / "Cello.sfz") << "<region> sample=c.wav";

    // CTest points SEGNO_LIBRARY at a folder that isn't there: no Library, wherever one is installed.
    REQUIRE(std::getenv("SEGNO_LIBRARY") != nullptr);
    CHECK_FALSE(fs::exists(fd::library::root()));
    // The host's choice comes before the environment.
    fd::library::setRoot(dir / "Library");
    CHECK(fd::library::root() == dir / "Library");
    CHECK(fd::library::version() == "1.0.0");

    // A file in it is stored relative to it, in forward slashes, and comes back where it is.
    const auto cello = dir / "Library" / "Samples" / "Strings" / "Cello.sfz";
    const auto stored = fd::library::reference(cello);
    CHECK(stored == "library:Samples/Strings/Cello.sfz");
    CHECK(fd::library::isReference(stored));
    CHECK(fs::equivalent(fd::library::resolve(stored), cello));
    // Moved elsewhere, the same reference finds it there.
    fs::rename(dir / "Library", dir / "Moved");
    fd::library::setRoot(dir / "Moved");
    CHECK(fs::exists(fd::library::resolve(stored)));

    // Files outside it stay as they are.
    const auto mine = dir / "Mine.wav";
    CHECK(fd::library::reference(mine) == mine.string());
    CHECK(fd::library::resolve(mine.string()) == mine);
    CHECK_FALSE(fd::library::isReference(mine.string()));

    // A reference says what's missing.
    CHECK(
        fd::library::missingMessage("library:Samples/Strings/Viola.sfz").find("Samples/Strings/Viola.sfz") !=
        std::string::npos);

    // The standard places: yours before everyone's.
    const auto places = fd::library::standardPlaces();
    REQUIRE(places.size() >= 2);
    fd::library::setRoot({});
    fs::remove_all(dir);
}
