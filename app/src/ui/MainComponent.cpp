#include "MainComponent.h"

namespace fd::app
{
namespace te = tracktion;

MainComponent::MainComponent(te::Engine& engine) : engine_(engine)
{
    edit_ = te::Edit::createSingleTrackEdit(engine_);

    auto* track = te::getAudioTracks(*edit_)[0];
    plugin_ = edit_->getPluginCache().createNewPlugin(engine::DevicePlugin::typeNameFor("utility"), {});
    track->pluginList.insertPlugin(plugin_, 0, nullptr);

    auto& device = dynamic_cast<engine::DevicePlugin&>(*plugin_);
    binding_ = std::make_unique<engine::DevicePluginBinding>(device);
    context_ = std::make_unique<ui::DeviceUiContext>(
        ui::DeviceUiContext{device.device().info(), *binding_, &device.device().analysis()});
    panel_ = ui::findPanel("utility")->create(*context_);
    panel_->onEnabledChange = [this](bool on) { plugin_->setEnabled(on); };
    addAndMakeVisible(*panel_);

    play_.onClick = [this] {
        auto& transport = edit_->getTransport();
        if (play_.getToggleState())
            transport.play(false);
        else
            transport.stop(false, false);
    };
    audioSettings_.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible(play_);
    addAndMakeVisible(audioSettings_);

    loadFile(createDemoLoop());
    setSize(900, 420);
}

MainComponent::~MainComponent()
{
    edit_->getTransport().stop(false, true);
    panel_.reset();
}

void MainComponent::loadFile(const juce::File& file)
{
    auto& transport = edit_->getTransport();
    transport.stop(false, false);

    auto* track = te::getAudioTracks(*edit_)[0];
    for (auto* clip : track->getClips())
        clip->removeFromParent();

    const te::AudioFile audioFile(engine_, file);
    if (!audioFile.isValid())
    {
        fileName_ = "Could not read " + file.getFileName();
        repaint();
        return;
    }
    auto clip = track->insertWaveClip(file.getFileNameWithoutExtension(), file,
                                      {{te::TimePosition(), te::TimeDuration::fromSeconds(audioFile.getLength())}, {}},
                                      false);
    transport.setLoopRange(clip->getEditTimeRange());
    transport.looping = true;
    transport.setPosition(te::TimePosition());
    fileName_ = file.getFileName();
    play_.setToggleState(false, juce::dontSendNotification); // never start making sound by itself
    repaint();
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(engine_.getDeviceManager().deviceManager, 0, 2, 2, 2,
                                                            false, false, true, false);
    selector->setSize(520, 420);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(selector);
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = ui::Theme::current().surface1;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

juce::File MainComponent::createDemoLoop()
{
    // A two-bar arpeggio at 120 BPM, slightly different per channel so Width and Mono are audible.
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-demo-loop.wav");
    constexpr double sr = 48000.0;
    constexpr int length = static_cast<int>(sr * 4.0);
    juce::AudioBuffer<float> buffer(2, length);
    buffer.clear();
    const double notes[] = {261.63, 329.63, 392.0, 523.25, 392.0, 329.63, 293.66, 246.94};
    const int step = length / 16;
    for (int n = 0; n < 16; ++n)
    {
        const double f = notes[n % 8];
        for (int i = 0; i < step * 2 && n * step + i < length; ++i)
        {
            const double t = i / sr;
            const float env = static_cast<float>(std::exp(-t * 6.0) * 0.18);
            buffer.addSample(0, n * step + i, env * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * f * t)));
            buffer.addSample(1, n * step + i,
                             env * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * f * 1.004 * t)));
        }
    }
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    if (out != nullptr)
        if (auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate(sr)
                                                       .withNumChannels(2)
                                                       .withBitsPerSample(24)))
            writer->writeFromAudioSampleBuffer(buffer, 0, length);
    return file;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;aif;aiff;flac;ogg");
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    loadFile(juce::File(files[0]));
}

void MainComponent::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);
    auto top = getLocalBounds().removeFromTop(48);
    g.setColour(t.surface1);
    g.fillRect(top);

    auto centre = getLocalBounds().withTrimmedTop(48).withTrimmedBottom(t.devicePanelHeight + 24);
    g.setColour(t.textSecondary);
    g.setFont(ui::fonts::font(t.textLg, ui::fonts::Weight::Medium));
    g.drawText(fileName_, centre.removeFromTop(centre.getHeight() / 2 + 10), juce::Justification::centredBottom);
    g.setColour(t.textTertiary);
    g.setFont(ui::fonts::font(t.textSm));
    g.drawText("Drop an audio file here to loop it through the device chain", centre.withTrimmedTop(8),
               juce::Justification::centredTop);
}

void MainComponent::resized()
{
    const auto& t = ui::Theme::current();
    auto area = getLocalBounds();
    auto top = area.removeFromTop(48).reduced(t.grid * 3, t.grid * 2 + 2);
    play_.setBounds(top.removeFromLeft(72));
    audioSettings_.setBounds(top.removeFromRight(130));

    auto chain = area.removeFromBottom(t.devicePanelHeight + 24).reduced(t.grid * 3);
    panel_->setBounds(chain.removeFromLeft(panel_->preferredWidth()));
}
} // namespace fd::app
