// The audio editor, stage P6a (ADR 0063, docs/design/audio-editor.md §4–§6, §10–§13): it opens
// on selection; a row per signal the clip plays and the level column; a range dragged across the
// waveform and the bar's commands on it; the range's gain handle; the gain line; the bar's Gain,
// Reverse, Normalise and Audio menu; the inspector's AUDIO section; the arrangement's gain
// handle; Edit's commands on the range; and the bar at standard and compact widths. Each drawn
// for review with FD_DUMP_DIR set.

#include "ShellTestKit.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/WavIO.h"
#include "model/AudioAnalysis.h"
#include "model/AudioSound.h"
#include "model/Editing.h"
#include "model/Selection.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <iostream>

using namespace fd::testing;
using Catch::Approx;
using A = fd::engine::Session::RangeAction;
using fd::model::ChannelMode;

namespace
{
/// A shell with a four-second drum-like loop (a decaying hit on every beat, left louder than right)
/// as an audio clip on the first track, selected, so the clip view shows it.
struct AudioFixture : ShellFixture
{
    juce::File root;
    std::uint64_t track = 0, clipId = 0;
    float peak = 0.0f; // the file's, over both channels

    /// `bits`: the file's sample format, 0 for 32-bit float.
    explicit AudioFixture(int width = 1800, int height = 1000, int channels = 2, int bits = 0)
    {
        shell->setSize(width, height);
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("fd-audio-editor-" + juce::Uuid().toString());
        REQUIRE(root.createDirectory());
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->updateSnapshotNow();
        track = session->snapshot()->tracks.front().id;
        fd::testkit::AudioBuffer beat(channels, 4 * 48000);
        std::uint32_t noise = 12345;
        for (int i = 0; i < beat.numSamples(); ++i)
        {
            noise = noise * 1664525u + 1013904223u;
            const float t = static_cast<float>(i) / 48000.0f, hit = std::fmod(t, 0.5f);
            const float x =
                std::exp(-hit / 0.08f) * (0.6f * std::sin(2.0f * 3.14159265f * 110.0f * t) +
                                          0.4f * (static_cast<float>(noise >> 8) / 8388608.0f - 1.0f));
            for (int c = 0; c < channels; ++c)
            {
                beat.at(c, i) = (c == 0 ? 0.6f : 0.3f) * x;
                peak = std::max(peak, std::abs(beat.at(c, i)));
            }
        }
        const auto file = root.getChildFile("Beat.wav");
        if (bits == 0)
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), beat, 48000.0));
        else
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate(48000.0)
                                                          .withNumChannels(channels)
                                                          .withBitsPerSample(bits));
            REQUIRE(writer != nullptr);
            juce::AudioBuffer<float> buffer(channels, beat.numSamples());
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < beat.numSamples(); ++i)
                    buffer.setSample(c, i, beat.at(c, i));
            REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, beat.numSamples()));
        }
        clipId = session->insertAudioFile(track, file, 0.0);
        REQUIRE(clipId != 0);
        settle();
        select(clipId);
    }

    ~AudioFixture()
    {
        shell.reset();
        session.reset();
        root.deleteRecursively();
    }

    /// The snapshot after a change, in the views (as the snapshot callback does).
    void settle()
    {
        session->updateSnapshotNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    /// Selects a clip, as a click on it does.
    void select(std::uint64_t id)
    {
        session->updateSnapshotNow();
        shell->arrangement().setSelection(fd::model::withClips(*session->snapshot(), {id}, track, track));
        shell->finishAnimations();
        view().waitForWaveform();
        settle();
    }

    [[nodiscard]] fd::model::ClipInfo clip(std::uint64_t id = 0)
    {
        session->updateSnapshotNow();
        const auto found = fd::model::findClip(*session->snapshot(), id == 0 ? clipId : id);
        REQUIRE(found);
        return *found->first;
    }

    [[nodiscard]] std::vector<fd::model::ClipInfo> clips()
    {
        session->updateSnapshotNow();
        for (const auto& t : session->snapshot()->tracks)
            if (t.id == track)
                return t.clips;
        return {};
    }

    fd::app::AudioClipView& view() { return shell->detail().audioClipView(); }

    /// A point of the waveform at `seconds` on the timeline, in the middle of a row.
    juce::Point<float> at(double seconds, int row = 0)
    {
        auto& v = view();
        return {static_cast<float>(v.waveformArea().getX() + v.timeline().xForTime(seconds)),
                static_cast<float>(v.rowArea(row).getCentreY())};
    }

    /// A range dragged across the waveform.
    void dragRange(double from, double to, int mods = 0)
    {
        drag(view(), at(from), at(to), mods);
        settle();
    }

    bool press(int code, int mods = 0)
    {
        return shell->keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0));
    }

    void dump(const char* name, juce::Component& c, juce::Rectangle<int> area = {})
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            shell->detail().frame(0.0); // the grid's step in the header, as a frame shows it
            dumpPng(dir, name,
                    c.createComponentSnapshot(area.isEmpty() ? c.getLocalBounds() : area, true, 2.0f));
        }
    }
};
} // namespace

TEST_CASE(
    "audio editor: one click on an audio clip opens it; a row per signal it plays; the level column zooms",
    "[integration]")
{
    AudioFixture f;
    auto& detail = f.shell->detail();
    auto& view = f.view();
    auto& arrangement = f.shell->arrangement();
    // Clicked in the arrangement, as a MIDI clip opens the piano roll (AE2).
    arrangement.setSelection({});
    f.settle();
    const auto& tl = arrangement.timeline();
    const juce::Point<float> body(
        static_cast<float>(fd::app::ArrangementView::headerWidth() + tl.xForTime(1.0)),
        static_cast<float>(fd::app::ArrangementView::defaultRowsTop()) + 40.0f);
    click(arrangement, body);
    CHECK(detail.mode() == fd::app::DetailPanel::Mode::Clip);
    REQUIRE(detail.showsAudioClip());
    f.shell->finishAnimations();
    view.waitForWaveform();
    f.settle();
    REQUIRE(view.waveformArea().getHeight() > 120);

    // A stereo file: two rows, left over right; one where it plays one signal (§6.3).
    CHECK(view.rowCount() == 2);
    CHECK(view.rowArea(0).getBottom() < view.rowArea(1).getY());
    CHECK(view.rowArea(1).getBottom() == view.waveformArea().getBottom());
    f.dump("audio-editor@2x.png", detail);
    for (const auto [mode, rows] :
         {std::pair{ChannelMode::Mono, 1}, std::pair{ChannelMode::Left, 1}, std::pair{ChannelMode::Right, 1},
          std::pair{ChannelMode::Swapped, 2}, std::pair{ChannelMode::Stereo, 2}})
    {
        f.session->setSound({f.clipId}, {.channels = mode}, "Channels");
        f.settle();
        INFO(fd::model::channelModeName(mode));
        CHECK(view.rowCount() == rows);
        CHECK(view.rowArea(0).getY() == view.waveformArea().getY());
    }

    // The level column, dragged up: the rows drawn taller; a double-click: full scale fills them.
    const auto level = view.levelArea().getCentre().toFloat();
    drag(view, level, level - juce::Point<float>(0.0f, 48.0f));
    CHECK(view.levelZoom() == Catch::Approx(2.0f).margin(0.01f));
    drag(view, level, level - juce::Point<float>(0.0f, 1000.0f));
    CHECK(view.levelZoom() == Catch::Approx(64.0f)); // at most
    f.dump("audio-editor-zoomed@2x.png", view);
    doubleClick(view, level);
    CHECK(view.levelZoom() == Catch::Approx(1.0f));
    CHECK(f.session->undoDescription() != "Gain"); // view state: nothing to undo
}

TEST_CASE("audio editor: a mono file shows one row and no Channels", "[integration]")
{
    AudioFixture f(1800, 1000, 1);
    REQUIRE(f.shell->detail().showsAudioClip());
    CHECK(f.view().rowCount() == 1);
    CHECK(f.view().rowArea(0) == f.view().waveformArea());
    CHECK_FALSE(f.shell->inspector().audioChannels().isVisible());
    // Channels in the Audio menu are for stereo files.
    CHECK_FALSE(f.shell->actions().isEnabled("audio.channels.mono"));
}

TEST_CASE(
    "audio editor: a range dragged across the waveform; the bar's commands act on it, one undo step each",
    "[integration]")
{
    AudioFixture f;
    auto& detail = f.shell->detail();
    auto& view = f.view();
    for (const auto action : {A::Split, A::Crop, A::Silence, A::Fade, A::Loop})
        CHECK_FALSE(detail.rangeButton(action).isVisible()); // only while there's a range

    // Snapped to the grid, within the clip; the range's commands on the bar.
    f.dragRange(0.97, 2.04);
    REQUIRE(view.range());
    CHECK(view.range()->first == Catch::Approx(1.0));
    CHECK(view.range()->second == Catch::Approx(2.0));
    for (const auto action : {A::Split, A::Crop, A::Silence, A::Fade, A::Loop})
    {
        CHECK(detail.rangeButton(action).isVisible());
        // A fade starts at the clip's start or ends at its end: in the middle, Fade says so.
        CHECK(detail.rangeButton(action).isEnabled() == (action != A::Fade));
    }
    CHECK(detail.rangeButton(A::Fade).getTooltip().startsWith("A fade starts at the clip's start"));
    f.dump("audio-editor-range@2x.png", detail);
    // Alt: free of the grid. Past the clip: up to its end.
    f.dragRange(0.97, 9.0, juce::ModifierKeys::altModifier);
    CHECK(view.range()->first == Catch::Approx(0.97).margin(0.01));
    CHECK(view.range()->second == Catch::Approx(4.0));
    // A click lets it go, and the commands with it.
    click(view, f.at(3.0));
    CHECK_FALSE(view.range());
    CHECK_FALSE(detail.rangeButton(A::Crop).isVisible());

    // Silence: the gain line falls to silence over the range.
    f.dragRange(1.0, 2.0);
    detail.rangeButton(A::Silence).onClick();
    CHECK(f.session->undoDescription() == "Silence");
    CHECK(fd::model::soundGainAt(f.clip().sound, 1.5) < 1e-3f);
    CHECK(fd::model::soundGainAt(f.clip().sound, 0.5) == Catch::Approx(1.0f).margin(1e-3));
    CHECK(fd::model::soundGainAt(f.clip().sound, 2.5) == Catch::Approx(1.0f).margin(1e-3));
    REQUIRE(view.range()); // kept for the next command
    f.dump("audio-editor-silenced@2x.png", detail);
    f.session->undo();
    f.settle();
    CHECK(f.clip().sound.line.empty());

    // Fade: a range from the clip's start becomes its fade-in.
    f.dragRange(0.0, 0.5);
    detail.rangeButton(A::Fade).onClick();
    CHECK(f.clip().fadeIn.length == Catch::Approx(0.5));
    f.session->undo();
    f.settle();

    // Loop: the clip loops the range.
    f.dragRange(1.0, 2.0);
    detail.rangeButton(A::Loop).onClick();
    CHECK(f.clip().loop.on());
    CHECK(f.clip().loop.start == Catch::Approx(1.0));
    CHECK(f.clip().loop.length == Catch::Approx(1.0));
    f.session->undo();
    f.settle();

    // Crop: only the range is left, and still shown.
    f.dragRange(1.0, 2.0);
    detail.rangeButton(A::Crop).onClick();
    CHECK(f.session->undoDescription() == "Crop");
    REQUIRE(f.clips().size() == 1);
    CHECK(f.clips()[0].start == Catch::Approx(1.0));
    CHECK(f.clips()[0].length == Catch::Approx(1.0));
    CHECK(f.clips()[0].offset == Catch::Approx(1.0));
    CHECK(detail.showsAudioClip());
    CHECK_FALSE(view.range());
    f.session->undo();
    f.settle();

    // Split: the range a clip of its own, selected and shown.
    f.select(f.clipId);
    f.dragRange(1.0, 2.0);
    detail.rangeButton(A::Split).onClick();
    f.settle();
    CHECK(f.session->undoDescription() == "Split");
    REQUIRE(f.clips().size() == 3);
    REQUIRE(view.clip());
    CHECK(view.clip()->start == Catch::Approx(1.0));
    CHECK(view.clip()->length == Catch::Approx(1.0));
    CHECK(f.shell->arrangement().selection().clips == std::set<std::uint64_t>{view.clip()->id});
}

TEST_CASE("audio editor: Edit's Select all, Split and Delete act on the range when the editor was used last",
          "[integration]")
{
    AudioFixture f;
    auto& view = f.view();
    click(view, f.at(0.5)); // the editor used last
    CHECK(f.press('a', juce::ModifierKeys::commandModifier));
    REQUIRE(view.range());
    CHECK(view.range()->first == Catch::Approx(0.0));
    CHECK(view.range()->second == Catch::Approx(4.0));

    // Delete: the range taken out, a gap where it was.
    f.dragRange(1.0, 2.0);
    CHECK(f.press(juce::KeyPress::deleteKey));
    CHECK(f.session->undoDescription() == "Delete");
    auto clips = f.clips();
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].end() == Catch::Approx(1.0));
    CHECK(clips[1].start == Catch::Approx(2.0));
    f.session->undo();
    f.settle();

    // Split (Ctrl+E): at the range's edges, not the playhead, and Edit says so.
    f.select(f.clipId);
    CHECK(f.shell->actions().label("edit.split") == "Split at playhead");
    f.dragRange(1.0, 2.0);
    CHECK(f.shell->actions().label("edit.split") == "Split at range");
    CHECK(f.press('e', juce::ModifierKeys::commandModifier));
    CHECK(f.clips().size() == 3);
}

TEST_CASE("audio editor: the range's gain handle; the gain line edited in its lane as automation is",
          "[integration]")
{
    AudioFixture f;
    auto& view = f.view();
    auto& detail = f.shell->detail();

    // Dragged up 30 px: the range 3 dB louder, its edges ramped, one undo step.
    f.dragRange(1.0, 2.0);
    const auto handle = view.rangeHandle();
    REQUIRE_FALSE(handle.isEmpty());
    CHECK(view.rowArea(0).toFloat().contains(handle));
    drag(view, handle.getCentre(), handle.getCentre() - juce::Point<float>(0.0f, 30.0f));
    f.settle();
    CHECK(f.session->undoDescription() == "Gain");
    CHECK(fd::model::soundGainAt(f.clip().sound, 1.5) ==
          Catch::Approx(std::pow(10.0f, 3.0f / 20.0f)).margin(0.01));
    CHECK(fd::model::soundGainAt(f.clip().sound, 0.5) == Catch::Approx(1.0f).margin(1e-3));
    f.dump("audio-editor-range-gain@2x.png", detail);
    f.session->undo();
    f.settle();
    REQUIRE(f.clip().sound.line.empty());

    // The lane: a double-click adds a point to the gain line; selected, Delete takes it out.
    const auto lane = view.laneArea();
    const juce::Point<float> spot(
        static_cast<float>(view.waveformArea().getX() + view.timeline().xForTime(2.0)),
        static_cast<float>(lane.getY() + lane.getHeight() / 4));
    doubleClick(view, spot);
    f.settle();
    REQUIRE(f.clip().sound.line.size() == 1);
    CHECK(f.clip().sound.line[0].time ==
          Catch::Approx(2.0).margin(0.01)); // the file's time: the clip starts at 0
    CHECK(fd::model::lineDbForValue(f.clip().sound.line[0].value) > 0.0); // above the 0 dB line
    CHECK(view.hasPointSelection());
    f.dump("audio-editor-gain-line@2x.png", detail);
    CHECK(f.press(juce::KeyPress::deleteKey));
    f.settle();
    CHECK(f.clip().sound.line.empty());
    CHECK_FALSE(view.hasPointSelection());
}

TEST_CASE("audio editor: the bar's Gain, Reverse, Normalise and Audio menu", "[integration]")
{
    AudioFixture f;
    auto& detail = f.shell->detail();
    for (auto* c : std::initializer_list<juce::Component*>{&detail.audioButton(), &detail.normaliseButton(),
                                                           &detail.gainField(), &detail.reverseButton()})
        CHECK(c->isVisible());
    CHECK_FALSE(detail.notesButton().isVisible()); // the piano roll's bar isn't

    // Gain, typed: the clip's gain, shown as it is.
    auto& gain = detail.gainField().field();
    gain.setValue(-6.0, juce::sendNotification);
    f.settle();
    CHECK(f.clip().sound.gainDb == Catch::Approx(-6.0f));
    CHECK(f.session->undoDescription() == "Gain");
    CHECK(gain.textFromValue(gain.getValue()) == juce::String::fromUTF8("\xE2\x88\x92") + "6.0 dB");
    CHECK(gain.textFromValue(-100.0) == juce::String::fromUTF8("\xE2\x88\x92\xE2\x88\x9E dB"));
    CHECK(gain.valueFromText(juce::String::fromUTF8("\xE2\x88\x92") + "3 dB") == Catch::Approx(-3.0));

    // Reverse: lit while the clip plays backwards.
    detail.reverseButton().onClick();
    f.settle();
    CHECK(f.clip().sound.reversed);
    CHECK(detail.reverseButton().getToggleState());
    f.view().waitForWaveform();
    f.dump("audio-editor-reversed@2x.png", detail);
    f.session->undo();
    f.settle();
    CHECK_FALSE(detail.reverseButton().getToggleState());

    // Normalise: the Tools pane at it, lit while the pane shows it; again, the pane closes.
    auto& normalise = detail.normaliseButton();
    auto& pane = detail.toolsPane();
    normalise.onClick();
    REQUIRE(pane.isVisible());
    CHECK(pane.showsAudio());
    CHECK(pane.chosen() == "normalise");
    CHECK(normalise.getToggleState());
    normalise.onClick();
    CHECK_FALSE(pane.isVisible());
    CHECK_FALSE(normalise.getToggleState());
    // Apply sets the gain so the peak meets the target, and lets the tool go.
    normalise.onClick();
    pane.form().setValue("peak", -1.0);
    f.dump("audio-editor-normalise@2x.png", detail);
    pane.apply();
    f.settle();
    CHECK(f.session->undoDescription() == "Normalise");
    CHECK(f.clip().sound.gainDb == Catch::Approx(-1.0f - 20.0f * std::log10(f.peak)).margin(0.05f));
    CHECK_FALSE(normalise.getToggleState());

    // Audio ⌄: the menu bar's Audio menu, built by the same function.
    const auto items = itemsOf(detail.audioMenu());
    for (const char* item :
         {"Gain \xE2\x96\xB8 Up 1 dB", "Normalise\xE2\x80\xA6", "Reverse", "Invert polarity",
          "Channels \xE2\x96\xB8 Mono", "Crop to range", "Fade to range", "Loop range"})
        CHECK(items.contains(juce::String::fromUTF8(item)));
    CHECK(invoke(detail.audioMenu(), "Invert polarity"));
    f.settle();
    CHECK(f.clip().sound.inverted);
    // The range's commands are dimmed without a range.
    CHECK_FALSE(invoke(detail.audioMenu(), "Crop to range"));
    CHECK(f.shell->actions().find("audio.crop")->whyNot().startsWith("Select a range"));
}

TEST_CASE("audio editor: the inspector's AUDIO section: gain, Reverse, Polarity and Channels",
          "[integration]")
{
    AudioFixture f(1800, 1000, 2, 24);
    CHECK(f.clip().fileBitDepth == 24);
    using S = fd::model::InspectorSection;
    auto& inspector = f.shell->inspector();
    const auto& sections = inspector.sections();
    const auto clip = std::find(sections.begin(), sections.end(), S::Clip);
    REQUIRE(clip != sections.end());
    REQUIRE(clip + 1 != sections.end());
    CHECK(*(clip + 1) == S::Audio); // after CLIP

    for (auto* c :
         std::initializer_list<juce::Component*>{&inspector.audioGain(), &inspector.audioReverse(),
                                                 &inspector.audioPolarity(), &inspector.audioChannels()})
        CHECK(c->isVisible());
    inspector.audioGain().setValue(4.5, juce::sendNotification);
    f.settle();
    CHECK(f.clip().sound.gainDb == Catch::Approx(4.5f));
    CHECK(f.shell->detail().gainField().field().getValue() == Catch::Approx(4.5)); // the bar shows it too
    inspector.audioPolarity().onClick();
    f.settle();
    CHECK(f.clip().sound.inverted);
    CHECK(inspector.audioPolarity().getToggleState());
    inspector.audioChannels().setSelectedId(static_cast<int>(ChannelMode::Swapped) + 1,
                                            juce::sendNotificationSync);
    f.settle();
    CHECK(f.clip().sound.channels == ChannelMode::Swapped);
    inspector.audioReverse().onClick();
    f.settle();
    CHECK(f.clip().sound.reversed);
    CHECK(f.clip().fileBitDepth == 24); // the person's file, not the reversed copy we play
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "audio-editor-inspector@2x.png",
                inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));
}

TEST_CASE("audio editor: the Tools pane's Quantise and Tempo (P6b)", "[integration][warp-editor]")
{
    AudioFixture f;
    f.session->impl().analyser.waitUntilIdle();
    REQUIRE(f.session->setWarp({f.clipId}, {.on = false})); // the loop warped when placed: off, to see Apply
    f.settle();
    auto& detail = f.shell->detail();
    auto& pane = detail.toolsPane();
    // The Tempo tool, by its button: what's found in the file, the loop's 2 bars at 120 BPM, the
    // first hit its downbeat; its beats drawn over the waveform.
    detail.tempoButton().onClick();
    REQUIRE(pane.isVisible());
    CHECK(pane.chosen() == "tempo");
    CHECK(detail.tempoButton().getToggleState());
    CHECK(pane.form().values().at("tempo") == Approx(120.0).margin(0.01));
    CHECK(pane.form().values().at("downbeat") == Approx(0.0).margin(0.005));
    f.dump("audio-tempo-tool@2x.png", detail);
    // ×2 beside it doubles it, as typed would.
    auto* tempoRow = pane.form().control("tempo");
    REQUIRE(tempoRow != nullptr);
    juce::Button* twice = nullptr;
    for (auto* c : tempoRow->getChildren())
        if (auto* b = dynamic_cast<juce::Button*>(c);
            b != nullptr && b->getButtonText() == juce::String::fromUTF8("\xC3\x97"
                                                                         "2"))
            twice = b;
    REQUIRE(twice != nullptr);
    twice->onClick();
    CHECK(pane.form().values().at("tempo") == Approx(240.0).margin(0.01));
    pane.form().show("tempo", 120.0);
    // The downbeat dragged on the waveform shows in the form.
    auto& v = f.view();
    const float y = static_cast<float>(v.rowArea(0).getCentreY());
    const auto x0 = static_cast<float>(v.waveformArea().getX() + v.timeline().xForTime(0.0)) + 1.0f;
    drag(v, {x0, y}, {static_cast<float>(v.waveformArea().getX() + v.timeline().xForTime(0.5)), y});
    CHECK(pane.form().values().at("downbeat") == Approx(0.5).margin(0.01));
    // Apply: the clip warps at the tempo and starts at its downbeat, one undo step.
    pane.apply();
    f.settle();
    auto c = f.clip();
    CHECK(c.warp.on);
    CHECK(c.warp.tempo == Approx(120.0));
    CHECK(c.offset == Approx(0.5).margin(0.01));
    CHECK(c.length == Approx(3.5).margin(0.01));
    CHECK(f.session->undoDescription() == "Tempo");
    f.session->undo();
    f.settle();
    CHECK_FALSE(f.clip().warp.on);
    CHECK(f.clip().offset == Approx(0.0));

    // Quantise's tool: its strength and swing, and the clip's sensitivity; Apply puts markers on
    // the transients, moved to the grid.
    detail.showTool("quantise");
    REQUIRE(pane.chosen() == "quantise");
    CHECK(pane.form().values().at("sensitivity") == Approx(50.0));
    pane.form().setValue("strength", 50.0);
    pane.form().setValue("sensitivity", 70.0);
    f.dump("audio-quantise-tool@2x.png", detail);
    pane.apply();
    f.settle();
    c = f.clip();
    CHECK(c.warp.on);
    CHECK(c.warp.sensitivity == Approx(0.7));
    CHECK_FALSE(c.warp.markers.empty());
    CHECK(f.session->undoDescription() == "Quantise");
}

TEST_CASE(
    "audio editor: the inspector's warping: Transpose and Fine, Warp and its mode, the tempo, the mode's "
    "setting and the transients' sensitivity (P6b)",
    "[integration][warp-editor]")
{
    AudioFixture f;
    REQUIRE(f.session->setWarp({f.clipId}, {.on = false})); // the loop warped when placed: off first
    f.settle();
    auto& inspector = f.shell->inspector();
    for (auto* c :
         std::initializer_list<juce::Component*>{&inspector.audioTranspose(), &inspector.audioFine(),
                                                 &inspector.audioWarp(), &inspector.audioSensitivity()})
        CHECK(c->isVisible());
    CHECK_FALSE(inspector.audioTempo().isVisible()); // only while it warps
    inspector.audioFine().setValue(-12.0, juce::sendNotificationSync);
    f.settle();
    CHECK(f.clip().pitch.fine == -12);
    CHECK(inspector.audioFine().text() == juce::String::fromUTF8("\xE2\x88\x92"
                                                                 "12 ct"));
    inspector.audioWarp().onClick();
    f.settle();
    REQUIRE(f.clip().warp.on);
    CHECK(inspector.audioTempo().isVisible());
    CHECK(inspector.audioTempo().text() == "120.00 BPM");
    inspector.audioMode().setSelectedId(static_cast<int>(fd::model::WarpMode::Beats) + 1,
                                        juce::sendNotificationSync);
    f.settle();
    CHECK(f.clip().warp.mode == fd::model::WarpMode::Beats);
    CHECK(inspector.audioTails().isVisible());
    CHECK_FALSE(inspector.audioFormants().isVisible());
    inspector.audioTails().setSelectedId(static_cast<int>(fd::model::WarpTails::Loop) + 1,
                                         juce::sendNotificationSync);
    f.settle();
    CHECK(f.clip().warp.tails == fd::model::WarpTails::Loop);
    inspector.audioSensitivity().setValue(80.0, juce::sendNotificationSync);
    f.settle();
    CHECK(f.clip().warp.sensitivity == Approx(0.8));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "audio-warp-inspector@2x.png",
                inspector.createComponentSnapshot(inspector.getLocalBounds(), true, 2.0f));
    // Tones: Formants instead of Tails; Repitch has no transposition.
    inspector.audioMode().setSelectedId(static_cast<int>(fd::model::WarpMode::Tones) + 1,
                                        juce::sendNotificationSync);
    f.settle();
    CHECK(inspector.audioFormants().isVisible());
    inspector.audioFormants().onClick();
    f.settle();
    CHECK(f.clip().warp.formants);
    inspector.audioMode().setSelectedId(static_cast<int>(fd::model::WarpMode::Repitch) + 1,
                                        juce::sendNotificationSync);
    f.settle();
    CHECK_FALSE(inspector.audioTranspose().isEnabled());
}

TEST_CASE(
    "arrangement: a warped clip is drawn as it plays, each piece between markers at its own speed (P6b)",
    "[integration][warp-editor]")
{
    AudioFixture f;
    // The hits every half second of the file, a marker pinning the third (1 s) to beat 3: the
    // first two pieces' hits spread out, the rest as before.
    REQUIRE(f.session->setWarp({f.clipId}, {.on = true, .tempo = 120.0}));
    REQUIRE(f.session->setWarpMarkers(f.clipId, {{1.0, 3.0}}));
    f.settle();
    // The clip keeps its place in beats; its file as it plays is half a second longer (its first
    // second plays as 1.5).
    CHECK(f.clip().length == Approx(4.0).margin(1e-6));
    CHECK(f.clip().sourceLength == Approx(4.5).margin(1e-6));
    auto& arrangement = f.shell->arrangement();
    arrangement.zoomToFit();
    f.shell->finishAnimations();
    f.settle();
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "arrangement-warped@2x.png",
                arrangement.createComponentSnapshot(juce::Rectangle<int>(0, 0, arrangement.getWidth(), 200),
                                                    true, 2.0f));
}

TEST_CASE(
    "arrangement: an audio clip's gain handle, dragged, sets its gain; a double-click puts it back at 0 dB",
    "[integration]")
{
    AudioFixture f;
    auto& arrangement = f.shell->arrangement();
    using K = fd::model::FadeHandle::Kind;
    const auto grip = arrangement.handleGrip(f.clipId, K::Gain);
    REQUIRE(grip);
    // In the middle of the clip, between its fade squares.
    const auto fadeIn = arrangement.handleGrip(f.clipId, K::FadeIn),
               fadeOut = arrangement.handleGrip(f.clipId, K::FadeOut);
    REQUIRE((fadeIn && fadeOut));
    CHECK(grip->x == Catch::Approx((fadeIn->x + fadeOut->x) * 0.5f).margin(2.0f));
    CHECK(grip->y == Catch::Approx(fadeIn->y));

    drag(arrangement, *grip, *grip - juce::Point<float>(0.0f, 40.0f));
    f.settle();
    CHECK(f.session->undoDescription() == "Gain");
    CHECK(f.clip().sound.gainDb == Catch::Approx(4.0f));
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        dumpPng(dir, "arrangement-gain-handle@2x.png",
                arrangement.createComponentSnapshot(juce::Rectangle<int>(0, 0, arrangement.getWidth(), 260),
                                                    true, 2.0f));
    click(arrangement, *grip, 2);
    f.settle();
    CHECK(f.clip().sound.gainDb == Catch::Approx(0.0f));

    // Zoomed out until the clip is too narrow for it: no handle.
    arrangement.zoomAt(0.0, 0.01);
    CHECK_FALSE(arrangement.handleGrip(f.clipId, K::Gain));
}

TEST_CASE("audio editor: the bar at standard and compact widths, nothing touching; Normalise, Reverse, then "
          "Gain give way",
          "[integration]")
{
    for (const int width : {1800, 1440, 1280, 1024, 820, 700, 600})
    {
        AudioFixture f(width, 900);
        auto& detail = f.shell->detail();
        auto& header = detail.header();
        f.dragRange(1.0, 2.0); // the range's commands too
        INFO("width " << width << ", header " << header.getWidth());
        REQUIRE(detail.rangeButton(A::Split).isVisible());
        CHECK(crowded(header, fd::ui::Theme::current().shell.itemGap).isEmpty());
        for (auto* c : header.getChildren())
            if (c->isVisible() && !c->getBounds().isEmpty())
                CHECK(header.getLocalBounds().contains(c->getBounds()));
        const bool normalise = header.isDropped(detail.normaliseButton()),
                   reverse = header.isDropped(detail.reverseButton()),
                   gain = header.isDropped(detail.gainField());
        CHECK((!reverse || normalise));
        CHECK((!gain || reverse));
        CHECK_FALSE(header.isDropped(detail.audioButton())); // the whole menu, always there
        if (width >= 1440) // the loop warps when it's placed: its warping's controls too (P6b)
            CHECK_FALSE(normalise);
        f.dump(("audio-editor-header-" + juce::String(width) + "@2x.png").toRawUTF8(), detail,
               header.getBounds());
        if (width == 1024 || width == 600)
            f.dump(("audio-editor-" + juce::String(width) + "@2x.png").toRawUTF8(), detail);
    }
}

TEST_CASE("audio editor: a warped clip's bar: its mode, its tempo, ×2 and ÷2; what gives way, in order (P6b)",
          "[integration][warp-editor]")
{
    for (const int width : {1800, 1440, 1280, 1024, 820, 700, 600})
    {
        AudioFixture f(width, 900);
        REQUIRE(
            f.session->setWarp({f.clipId}, {.on = true, .mode = fd::model::WarpMode::Beats, .tempo = 92.0}));
        f.settle();
        auto& detail = f.shell->detail();
        auto& header = detail.header();
        INFO("width " << width << ", header " << header.getWidth());
        CHECK(detail.warpButton().getToggleState());
        CHECK(detail.warpModeButton().getButtonText() == "Beats");
        CHECK(detail.tempoField().field().text() == "92.00 BPM");
        CHECK(crowded(header, fd::ui::Theme::current().shell.itemGap).isEmpty());
        for (auto* c : header.getChildren())
            if (c->isVisible() && !c->getBounds().isEmpty())
                CHECK(header.getLocalBounds().contains(c->getBounds()));
        // The least used give way first (§4): each only after all before it.
        const std::vector<juce::Component*> order{
            &detail.normaliseButton(), &detail.doubleButton(),        &detail.tempoButton(),
            &detail.reverseButton(),   &detail.warpModeButton(),      &detail.transposeField(),
            &detail.gainField(),       &detail.audioQuantiseButton(), &detail.tempoField()};
        for (size_t i = 1; i < order.size(); ++i)
            if (header.isDropped(*order[i]))
                for (size_t j = 0; j < i; ++j)
                    CHECK(header.isDropped(*order[j]));
        CHECK(header.isDropped(detail.doubleButton()) == header.isDropped(detail.halveButton())); // a pair
        CHECK_FALSE(header.isDropped(detail.audioButton()));
        CHECK_FALSE(header.isDropped(detail.warpButton()));
        if (width >= 1440)
            for (auto* c : order)
                CHECK_FALSE(header.isDropped(*c));
        f.dump(("audio-warp-header-" + juce::String(width) + "@2x.png").toRawUTF8(), detail,
               header.getBounds());
    }
}

TEST_CASE("audio editor: the bar's Warp, mode, ×2 and ÷2, Transpose and Quantise act on the clip (P6b)",
          "[integration][warp-editor]")
{
    AudioFixture f;
    auto& detail = f.shell->detail();
    // A loop placed plays in time (§7.2): its tempo found (2 bars at 120 BPM), in the mode its
    // sound chooses (§7.3).
    {
        const auto placed = f.clip();
        CHECK(placed.warp.on);
        CHECK(placed.warp.tempo == Approx(120.0).margin(0.01));
        REQUIRE(placed.analysis != nullptr);
        CHECK(placed.warp.mode == (fd::model::startsInBeats(*placed.analysis) ? fd::model::WarpMode::Beats
                                                                              : fd::model::WarpMode::Tones));
        CHECK(placed.length == Approx(4.0).margin(1e-6)); // at the song's tempo, untouched
    }
    detail.warpButton().triggerClick();
    f.settle();
    CHECK_FALSE(f.clip().warp.on);
    detail.warpButton().triggerClick();
    f.settle();
    auto c = f.clip();
    REQUIRE(c.warp.on);
    CHECK(c.warp.tempo == Approx(120.0)); // the song's: it plays untouched
    CHECK(detail.warpModeButton().isVisible());
    detail.doubleButton().triggerClick();
    f.settle();
    CHECK(f.clip().warp.tempo == Approx(240.0));
    CHECK(f.clip().length == Approx(8.0)); // the same audio, twice as long in beats
    detail.halveButton().triggerClick();
    f.settle();
    CHECK(f.clip().warp.tempo == Approx(120.0));
    CHECK(f.shell->actions().perform("audio.warpMode.repitch"));
    f.settle();
    CHECK(f.clip().warp.mode == fd::model::WarpMode::Repitch);
    CHECK(detail.warpModeButton().getButtonText() == "Repitch");
    // Transpose typed; Ctrl+↑ (the Notes menu's key) goes to the audio clip shown.
    detail.transposeField().field().setValue(3.0, juce::sendNotificationSync);
    f.settle();
    CHECK(f.clip().pitch.transpose == 3);
    CHECK(detail.transposeField().field().text() == "+3 st");
    click(f.view(), f.at(0.1)); // the editor used last
    CHECK(f.press(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier));
    f.settle();
    CHECK(f.clip().pitch.transpose == 4);
    // Quantise: the transients to the grid through markers (here they're on it already).
    f.session->impl().analyser.waitUntilIdle();
    f.settle();
    CHECK(detail.audioQuantiseButton().isEnabled());
    detail.audioQuantiseButton().triggerClick();
    f.settle();
    c = f.clip();
    CHECK(c.warp.markers.size() == 7);
    for (const auto& m : c.warp.markers)
        CHECK(m.beat == Approx(std::round(m.beat * 4.0) / 4.0).margin(1e-3));
    // Warp off keeps its markers for when it's on again.
    detail.warpButton().triggerClick();
    f.settle();
    CHECK_FALSE(f.clip().warp.on);
    CHECK(f.clip().warp.markers.size() == 7);
    CHECK_FALSE(detail.warpModeButton().isVisible());
}

TEST_CASE("audio editor: the marker strip: transients, markers, and a transient dragged into a marker (P6b)",
          "[integration][warp-editor]")
{
    AudioFixture f;
    f.session->impl().analyser.waitUntilIdle();
    f.settle();
    auto& v = f.view();
    // A hit every half second (each beat at the song's 120 BPM): eight transients, each at its hit.
    const auto found = v.transients();
    REQUIRE(found.size() == 8);
    CHECK(found[2] == Approx(1.0).margin(0.003));
    // Without Warp there are no markers, only the ticks.
    CHECK_FALSE(v.markerX(0));
    f.dump("audio-warp-strip-off@2x.png", v);

    // A transient dragged becomes a marker and moves its audio, snapped; Warp goes on with it.
    const float y = static_cast<float>(v.markerStripArea().getCentreY());
    const float from = *v.transientX(2);
    drag(v, {from, y}, {from + static_cast<float>(0.25 * v.timeline().pixelsPerSecond()) + 2.0f, y});
    f.settle();
    auto c = f.clip();
    CHECK(c.warp.on);
    REQUIRE(c.warp.markers.size() == 1);
    CHECK(c.warp.markers[0].file == Approx(1.0).margin(0.003));
    CHECK(c.warp.markers[0].beat == Approx(2.5)); // 1.25 s at 120 BPM: on the grid
    CHECK(*v.markerX(0) == Approx(v.waveformArea().getX() + v.timeline().xForTime(1.25)).margin(1.0));
    CHECK(v.selectedMarkers() == std::set<size_t>{0});
    f.dump("audio-warp-strip@2x.png", v);

    // Shift + drag slides the audio under it: it stays at its beat and pins another moment.
    const float at = *v.markerX(0);
    drag(v, {at, y}, {at + static_cast<float>(0.1 * v.timeline().pixelsPerSecond()), y},
         juce::ModifierKeys::shiftModifier);
    f.settle();
    c = f.clip();
    REQUIRE(c.warp.markers.size() == 1);
    CHECK(c.warp.markers[0].beat == Approx(2.5));
    CHECK(c.warp.markers[0].file < 0.95);

    // Double-click on a marker removes it; double-click in the strip adds one on the transient there.
    doubleClick(v, {*v.markerX(0), y});
    f.settle();
    CHECK(f.clip().warp.markers.empty());
    doubleClick(v, {*v.transientX(5) + 1.0f, y});
    f.settle();
    c = f.clip();
    REQUIRE(c.warp.markers.size() == 1);
    CHECK(c.warp.markers[0].file == Approx(found[5]).margin(1e-6));

    // Markers at transients, then a box selects some and Delete... removes them (one step).
    REQUIRE(v.markersAtTransients());
    f.settle();
    CHECK(f.clip().warp.markers.size() == 7); // every transient but the one at the file's start
    drag(v, {static_cast<float>(v.waveformArea().getX() + v.timeline().xForTime(0.9)), y},
         {static_cast<float>(v.waveformArea().getX() + v.timeline().xForTime(2.1)), y});
    CHECK(v.selectedMarkers().size() == 3); // at 1.0, 1.5 and 2.0 s
    REQUIRE(v.removeMarkers());
    f.settle();
    CHECK(f.clip().warp.markers.size() == 4);
    f.session->undo();
    f.settle();
    CHECK(f.clip().warp.markers.size() == 7);
    f.dump("audio-warp-markers@2x.png", v);
}

// The editor with a 10-minute stereo file (audio-editor.md §19), zooming in and out, its gain line
// and fades drawn through: every column read from the peaks each frame. Hidden; it reports, and
// is read in a Release build (ArrangementBenchmark.cpp says how).
TEST_CASE("audio editor paints a 10-minute stereo file zooming, within the frame budget", "[.benchmark]")
{
    ShellFixture f;
    auto& s = *f.session;
    s.updateSnapshotNow();
    const auto track = s.snapshot()->tracks.front().id;
    juce::TemporaryFile wav(".wav");
    fd::testkit::AudioBuffer audio(2, 600 * 48000);
    for (int i = 0; i < audio.numSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            audio.at(c, i) =
                0.6f * std::sin(0.011f * static_cast<float>(i) * (1.0f + 0.3f * static_cast<float>(c))) *
                std::exp(-static_cast<float>(i % 12000) / 5000.0f);
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), audio, 48000.0));
    const auto id = s.insertAudioFile(track, wav.getFile(), 0.0);
    REQUIRE(id != 0);
    fd::model::AutomationPoints line;
    for (int k = 0; k < 64; ++k)
        line.push_back({.time = k * 9.0, .value = fd::model::lineValueForDb(k % 2 == 0 ? 0.0 : -6.0)});
    REQUIRE(s.setGainLine(id, line, "Gain line"));
    s.setFades({id}, fd::model::Fade{2.0, 0.3f}, fd::model::Fade{4.0, -0.3f});
    s.updateSnapshotNow();

    fd::app::AudioClipView view(s);
    view.setSize(1700, 420); // a 4K screen's clip view at 2x
    view.setClip(*fd::model::findClip(*s.snapshot(), id)->first, s.snapshot()->tempo);
    view.waitForWaveform();
    constexpr float scale = 2.0f;
    juce::Image image(juce::Image::ARGB, static_cast<int>(static_cast<float>(view.getWidth()) * scale),
                      static_cast<int>(static_cast<float>(view.getHeight()) * scale), true);
    std::vector<double> times;
    const juce::Point<float> centre(850.0f, 150.0f);
    for (int frame = 0; frame < 240; ++frame)
    {
        // Zoom in for 120 frames, then out: about 6 octaves each way.
        view.mouseMagnify(mouseEvent(view, centre, centre, false), frame < 120 ? 1.035f : 1.0f / 1.035f);
        const auto t0 = std::chrono::steady_clock::now();
        {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));
            view.paintEntireComponent(g, true);
        }
        times.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(times.begin(), times.end());
    double sum = 0.0;
    for (double t : times)
        sum += t;
    std::cout << "audio editor paint, 10 min stereo, " << image.getWidth() << "x" << image.getHeight()
              << ": avg " << sum / static_cast<double>(times.size()) << " ms, p95 "
              << times[times.size() * 95 / 100] << " ms" << std::endl;
    CHECK(times.back() > 0.0);
}
