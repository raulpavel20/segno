// The Spectrum's panel with sound flowing (devices/spectrum/SPEC.md): Freeze and the reference go
// through the host binding, and the curves, labels, legend and readout are drawn for review
// (FD_DUMP_DIR).
#include "Spectrum.h"
#include "SpectrumPanel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/audiofile/AudioFile.h>
#include <fd/device/Registry.h>
#include <fd/testkit/Signals.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>

using fd::testkit::AudioBuffer;
using S = fd::devices::Spectrum;

namespace
{
constexpr double sr = 48000.0;

/// The DAW's sources, with a track chosen to compare with.
struct Compare final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Kick"}; }
    int selected() const override { return choice; }
    void select(int index) override { choice = index; }
    int choice = 1;
};

void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

/// What the panel's refresh tick does on screen (it rests offscreen): every control refreshed.
void refresh(juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (auto* control = dynamic_cast<fd::ui::ParamControl*>(c))
            control->refresh();
        refresh(*c);
    }
}

/// Plays `in` (and `key` into the sidechain) through the device a quarter second at a time,
/// letting the panel take each part as it would on screen.
void play(fd::Device& device, juce::Component& panel, const AudioBuffer& in, const AudioBuffer* key)
{
    constexpr int block = 512, part = 12000;
    AudioBuffer out(2, block);
    for (int start = 0; start < in.numSamples(); start += part)
    {
        for (int at = start; at < std::min(start + part, in.numSamples()); at += block)
        {
            const int n =
                std::min(block, std::min(start + part, in.numSamples()) - at); // never into the next part
            const float* inPtrs[] = {in.channel(0) + at, in.channel(1) + at};
            float* outPtrs[] = {out.channel(0), out.channel(1)};
            const float* keyPtrs[] = {key != nullptr ? key->channel(0) + at : nullptr,
                                      key != nullptr ? key->channel(1) + at : nullptr};
            fd::ProcessContext ctx;
            ctx.input = {const_cast<float* const*>(inPtrs), 2};
            ctx.output = {outPtrs, 2};
            if (key != nullptr)
                ctx.sidechain = {const_cast<float* const*>(keyPtrs), 2};
            ctx.numSamples = n;
            device.process(ctx);
        }
        pump(20);
        refresh(panel);
    }
}

/// `b` added into `a`.
void mixInto(AudioBuffer& a, const AudioBuffer& b)
{
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < std::min(a.numSamples(), b.numSamples()); ++i)
            a.at(c, i) += b.at(c, i % b.numSamples());
}

/// The first descendant of `root` that `match` accepts.
template <typename T, typename Match> T* find(juce::Component& root, Match match)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(c); t != nullptr && match(*t))
            return t;
        if (auto* deeper = find<T>(*c, match))
            return deeper;
    }
    return nullptr;
}

void dump(const juce::String& name, const juce::Image& image)
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        const auto file = juce::File(dir).getChildFile(name);
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}
} // namespace

TEST_CASE("spectrum panel: sound compared with a track, frozen, with a reference, drawn for review", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* factory = fd::findDevice("spectrum");
    REQUIRE(factory != nullptr);
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "spectrum")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);

    auto device = factory->create();
    device->prepare({sr, 512});
    const auto& spectrum = dynamic_cast<const S&>(*device);
    fd::ui::LocalParamBinding binding(*device);
    Compare compare;
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), &compare};
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    auto* display = panel->findSection("spectrum")->placements().front().component;
    REQUIRE(display != nullptr);

    // A bass (55 Hz and its harmonics) over a pink bed; the kick: a 60 Hz thump.
    auto mix = fd::testkit::pinkNoise(static_cast<int>(sr * 2), 9, 0.2f);
    for (const double hz : {55.0, 110.0, 165.0})
        mixInto(mix, fd::testkit::sine(hz, sr, mix.numSamples(), hz == 55.0 ? 0.3f : 0.1f));
    mixInto(mix, fd::testkit::sine(2600.0, sr, mix.numSamples(), 0.05f)); // a resonance to find
    auto kick = fd::testkit::sine(60.0, sr, mix.numSamples(), 0.4f);
    mixInto(kick, fd::testkit::pinkNoise(mix.numSamples(), 4, 0.02f));
    play(*device, *panel, mix, &kick);

    // Freeze keeps the curve, through the binding.
    auto* freeze = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Freeze"; });
    REQUIRE(freeze != nullptr);
    freeze->triggerClick();
    pump(100);
    refresh(*panel);
    REQUIRE(spectrum.snapshot());
    CHECK(spectrum.snapshot()->channel == 0);

    // A reference dropped on the display: read off the message thread, then kept.
    const auto path = std::filesystem::temp_directory_path() / "fd-spectrum-reference.wav";
    auto bright = fd::testkit::whiteNoise(static_cast<int>(sr * 4), 3, 0.05f);
    mixInto(bright, fd::testkit::pinkNoise(bright.numSamples(), 8, 0.2f));
    std::vector<std::vector<float>> channels{{bright.channel(0), bright.channel(0) + bright.numSamples()},
                                             {bright.channel(1), bright.channel(1) + bright.numSamples()}};
    REQUIRE(fd::audiofile::writeWav(path.string(), channels, sr));
    auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(display);
    REQUIRE(target != nullptr);
    CHECK(target->isInterestedInFileDrag({juce::String(path.string())}));
    CHECK_FALSE(target->isInterestedInFileDrag({"notes.txt"}));
    target->filesDropped({juce::String(path.string())}, 0, 0);
    for (int i = 0; i < 100 && !spectrum.reference(); ++i)
    {
        pump(50);
        refresh(*panel);
    }
    REQUIRE(spectrum.reference());
    CHECK(spectrum.reference()->name == "fd-spectrum-reference.wav");
    std::filesystem::remove(path);

    // A little more, with everything shown, and the side overlay on.
    binding.setPlain(S::Side, 1.0f);
    play(*device, *panel, mix, &kick);
    dump("spectrum-panel@1x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f));
    dump("spectrum-panel@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    const auto displayArea = panel->getLocalArea(display, display->getLocalBounds());
    dump("spectrum-display@2x.png", panel->createComponentSnapshot(displayArea, true, 2.0f));

    // The levels under the pointer.
    const juce::Point<float> at(static_cast<float>(display->getWidth()) * 0.3f,
                                static_cast<float>(display->getHeight()) * 0.5f);
    display->mouseMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, display, display, juce::Time::getCurrentTime(), at,
                                        juce::Time::getCurrentTime(), 0, false));
    dump("spectrum-hover@2x.png", panel->createComponentSnapshot(displayArea, true, 2.0f));

    // Clear takes each away again.
    for (auto* clear = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Clear"; });
         clear != nullptr; clear = nullptr)
        clear->triggerClick();
    pump(100);
    refresh(*panel);
    CHECK_FALSE(spectrum.snapshot());
}

namespace
{
/// Plays `in` through the device as play() does, the song playing or not.
void playAs(fd::Device& device, juce::Component& panel, const AudioBuffer& in, const AudioBuffer* key,
            bool playing)
{
    constexpr int block = 512, part = 12000;
    AudioBuffer out(2, block);
    for (int start = 0; start < in.numSamples(); start += part)
    {
        for (int at = start; at < std::min(start + part, in.numSamples()); at += block)
        {
            const int n =
                std::min(block, std::min(start + part, in.numSamples()) - at); // never into the next part
            const float* inPtrs[] = {in.channel(0) + at, in.channel(1) + at};
            float* outPtrs[] = {out.channel(0), out.channel(1)};
            const float* keyPtrs[] = {key != nullptr ? key->channel(0) + at : nullptr,
                                      key != nullptr ? key->channel(1) + at : nullptr};
            fd::ProcessContext ctx;
            ctx.input = {const_cast<float* const*>(inPtrs), 2};
            ctx.output = {outPtrs, 2};
            if (key != nullptr)
                ctx.sidechain = {const_cast<float* const*>(keyPtrs), 2};
            ctx.numSamples = n;
            ctx.transport.playing = playing;
            device.process(ctx);
        }
        pump(20);
        refresh(panel);
    }
}

/// A sine sweeping from `from` to `to` Hz (exponentially) over `seconds`.
AudioBuffer sweep(double from, double to, double seconds, float amplitude)
{
    const int n = static_cast<int>(sr * seconds);
    AudioBuffer out(2, n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double hz = from * std::pow(to / from, static_cast<double>(i) / n);
        phase += 2.0 * 3.14159265358979323846 * hz / sr;
        out.at(0, i) = out.at(1, i) = amplitude * static_cast<float>(std::sin(phase));
    }
    return out;
}
} // namespace

TEST_CASE("spectrum panel: the spectrogram follows a sweep, holds when nothing plays, marks collisions in "
          "time; drawn for review (SPEC.md, stage 2)",
          "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // Segno's look, as the app and the plugins have it (its fonts size the header's settings).
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    struct Restore
    {
        ~Restore() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    } restore;
    const auto* factory = fd::findDevice("spectrum");
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "spectrum")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);
    auto device = factory->create();
    device->prepare({sr, 512});
    fd::ui::LocalParamBinding binding(*device);
    Compare compare;
    compare.choice = 0; // nothing compared yet
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), &compare};
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    // On screen, so its refresh tick runs (header settings shown, what doesn't apply dimmed).
    panel->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    panel->setVisible(true);
    auto* display = panel->findSection("spectrum")->placements().front().component;
    auto* view = dynamic_cast<fd::devices::SpectrogramView*>(display);
    REQUIRE(view != nullptr);
    CHECK_FALSE(view->spectrogramShown());

    // The view switched in the header; Length shows beside it only now.
    auto* length = find<fd::ui::ParamComboBox>(*panel, [](auto& c) { return c.getText() == "5 s"; });
    REQUIRE(length != nullptr);
    CHECK_FALSE(length->isVisible());
    binding.setPlain(S::View, 1.0f);
    binding.setPlain(S::Slope, 0.0f); // untilted: a sine reads its own level
    refresh(*panel);
    pump(100);
    refresh(*panel);
    CHECK(view->spectrogramShown());
    CHECK(length->isVisible());
    CHECK(length->getWidth() > 40); // its widest choice and its chevron
    const auto& history = view->history();
    REQUIRE(history.width() == display->getWidth());
    // A row a pixel, above the frequency labels' strip.
    REQUIRE(history.rows() == display->getHeight() - fd::ui::Theme::current().deviceGrid.displayTextHeight);
    CHECK(history.secondsPerRow() == Catch::Approx(5.0 / history.rows()));
    // What belongs to the curve recedes: Peak hold, among others.
    auto* hold = find<fd::ui::ParamComboBox>(*panel, [](auto& c) { return c.getText() == "3 s"; });
    REQUIRE(hold != nullptr);
    CHECK(hold->getAlpha() < 1.0f);

    // A sweep from 100 Hz to 10 kHz over two seconds, the song playing: the brightest point of
    // each row rises with it, the newest rows the highest.
    play(*device, *panel, AudioBuffer(2, 4800), nullptr); // a moment of silence, not playing: nothing kept
    CHECK(history.filled() == 0);
    playAs(*device, *panel, sweep(100.0, 10000.0, 2.0, 0.5f), nullptr, true);
    REQUIRE(history.filled() > 20);
    const auto brightest = [&](int age)
    {
        const auto row = history.levels(age);
        return static_cast<int>(std::max_element(row.begin(), row.end()) - row.begin());
    };
    int rising = 0, rows = 0;
    for (int age = 2; age + 1 < history.filled() - 2; ++age, ++rows)
        rising += brightest(age) >= brightest(age + 1) ? 1 : 0;
    INFO(rising << " of " << rows << " rows step up towards the newest");
    CHECK(rising == rows);
    // A steady tone reads its own level (−10.5 dBFS), within the window's loss.
    playAs(*device, *panel, fd::testkit::sine(1000.0, sr, 24000, 0.3f), nullptr, true);
    const auto steady = history.levels(1);
    CHECK(*std::max_element(steady.begin(), steady.end()) == Catch::Approx(-10.5f).margin(1.5f));

    // Stopped and silent: once the sound has left the window, it holds what it showed.
    play(*device, *panel, AudioBuffer(2, 24000), nullptr);
    const auto held = history.version();
    play(*device, *panel, AudioBuffer(2, 24000), nullptr);
    CHECK(history.version() == held);
    // Sound coming in moves it again, playing or not (someone playing live).
    playAs(*device, *panel, fd::testkit::sine(440.0, sr, 4800, 0.3f), nullptr, false);
    CHECK(history.version() != held);

    // Under the pointer: the frequency and note, how long ago, the level.
    const auto at440 = static_cast<float>(display->getWidth()) *
                       static_cast<float>(std::log(440.0 / 20.0) / std::log(1000.0));
    const auto readout = view->spectrogramReadout(at440, 0.0f);
    INFO(readout);
    CHECK(readout.startsWith(juce::String::fromUTF8("44")));
    CHECK(readout.contains("A3"));
    CHECK(readout.contains("0.0 s ago"));
    CHECK(readout.endsWith(" dB"));

    // Compared with a kick at 60 Hz: where both are loud at once, marked; the rest not.
    compare.choice = 1;
    auto bass = fd::testkit::sine(60.0, sr, static_cast<int>(sr), 0.4f);
    auto kick = fd::testkit::sine(60.0, sr, static_cast<int>(sr), 0.4f);
    const auto x60 = static_cast<size_t>(display->getWidth() * std::log(60.0 / 20.0) / std::log(1000.0));
    // The bass alone, the kick silent: nothing collides.
    AudioBuffer silentKick(2, static_cast<int>(sr));
    playAs(*device, *panel, bass, &silentKick, true);
    CHECK(history.collisions(1)[x60] == 0);
    // Both at once: marked there, and nowhere else.
    playAs(*device, *panel, bass, &kick, true);
    CHECK(history.collisions(1)[x60] == 1);
    CHECK(history.collisions(1)[static_cast<size_t>(display->getWidth()) - 10] == 0);

    // Drawn for review: the sweep, then a compared kick, at 100 % and 200 %.
    playAs(*device, *panel, sweep(80.0, 12000.0, 3.0, 0.4f), nullptr, true);
    auto mix = fd::testkit::pinkNoise(static_cast<int>(sr * 2), 9, 0.1f);
    mixInto(mix, fd::testkit::sine(55.0, sr, mix.numSamples(), 0.3f));
    AudioBuffer hits(2, mix.numSamples());
    for (int i = 0; i < mix.numSamples(); ++i)
        if (i % 24000 < 2400) // a hit every half second
            hits.at(0, i) = hits.at(1, i) =
                0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 60.0 * i / sr)) *
                (1.0f - static_cast<float>(i % 24000) / 2400.0f);
    mixInto(mix, hits);
    playAs(*device, *panel, mix, &kick, true);
    dump("spectrogram-panel@1x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f));
    dump("spectrogram-panel@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    const auto displayArea = panel->getLocalArea(display, display->getLocalBounds());
    dump("spectrogram-display@2x.png", panel->createComponentSnapshot(displayArea, true, 2.0f));
    const juce::Point<float> at(at440, static_cast<float>(display->getHeight()) * 0.4f);
    display->mouseMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, display, display, juce::Time::getCurrentTime(), at,
                                        juce::Time::getCurrentTime(), 0, false));
    dump("spectrogram-hover@2x.png", panel->createComponentSnapshot(displayArea, true, 2.0f));

    // At its heaviest (16 384 points, 20 s) a frame stays within budget.
    binding.setPlain(S::Resolution, 3.0f);
    binding.setPlain(S::Length, 3.0f);
    refresh(*panel);
    playAs(*device, *panel, mix, nullptr, true);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 10; ++i)
    {
        display->repaint();
        (void)panel->createComponentSnapshot(displayArea, true, 1.0f);
    }
    const double perFrame = (juce::Time::getMillisecondCounterHiRes() - start) / 10.0;
    INFO("a frame: " << perFrame << " ms (Debug)");
    CHECK(perFrame < 50.0); // a Debug build; a Release one is several times faster
    panel->removeFromDesktop();
}

TEST_CASE("eq panel: its spectrum through the shared analyser, drawn for review", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* factory = fd::findDevice("eq");
    REQUIRE(factory != nullptr);
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "eq")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);
    auto device = factory->create();
    device->prepare({sr, 512});
    CHECK(device->analysis().sampleRate() == sr);
    fd::ui::LocalParamBinding binding(*device);
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), nullptr};
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);

    auto mix = fd::testkit::pinkNoise(static_cast<int>(sr), 2, 0.2f);
    mixInto(mix, fd::testkit::sine(220.0, sr, mix.numSamples(), 0.2f));
    auto* display = panel->findSection("curve")->placements().front().component;
    REQUIRE(display != nullptr);
    play(*device, *panel, mix, nullptr);
    dump("eq-display@2x.png",
         panel->createComponentSnapshot(panel->getLocalArea(display, display->getLocalBounds()), true, 2.0f));
}
