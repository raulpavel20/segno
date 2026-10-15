// The audio editor's commands in the shell (M3 step P6, ADR 0063, docs/design/audio-editor.md §13):
// the Audio menu, its button on the bar and the waveform's right-click menu. Edit's Split, Delete
// and Select all act on the editor's range and its gain line's points when it was used last
// (Shell.cpp).

#include "model/AudioSound.h"
#include "model/Editing.h"
#include "ui/Shell.h"

namespace fd::app
{
namespace
{
juce::String asks(const char* name)
{
    return juce::String(name) + juce::String::fromUTF8("\xE2\x80\xA6");
}

std::vector<std::uint64_t> idsOf(const std::vector<model::ClipInfo>& clips)
{
    std::vector<std::uint64_t> out;
    for (const auto& c : clips)
        out.push_back(c.id);
    return out;
}
} // namespace

std::vector<model::ClipInfo> Shell::audioTargets() const
{
    std::vector<model::ClipInfo> out;
    const auto& audio = const_cast<DetailPanel&>(detail_).audioClipView();
    if (audioActive() && audio.clip())
    {
        out.push_back(*audio.clip());
        return out;
    }
    const auto snapshot = session_.snapshot();
    for (auto id : arrangement_.selection().clips)
        if (const auto found = model::findClip(*snapshot, id);
            found && found->first->kind == model::ClipInfo::Kind::Audio)
            out.push_back(*found->first);
    return out;
}

void Shell::registerAudioActions()
{
    auto& audio = detail_.audioClipView();
    using A = engine::Session::RangeAction;
    // Clip commands act on the editor's clip, or the selected audio clips (§13).
    const auto hasClips = [this] { return !audioTargets().empty(); };
    const auto whyNoClips = [] { return juce::String("Select an audio clip first"); };
    // The range's commands need the editor's range (§5).
    const auto rangeAction = [this, &audio](std::string id, juce::String name, A action, double db = 0.0)
    {
        actions_.add({.id = std::move(id),
                      .name = std::move(name),
                      .category = "Audio",
                      .perform = [&audio, action, db] { audio.editRange(action, db); },
                      .isEnabled = [this, &audio, action]
                      { return detail_.showsAudioClip() && audio.rangeRefusal(action).empty(); },
                      .whyNot = [&audio, action]
                      { return juce::String::fromUTF8(audio.rangeRefusal(action).c_str()); }});
    };
    // Gain ▸ (§6.1, §5): the range's gain line when there's a range, else the clips' gain.
    const auto gainBy = [this, &audio](double db)
    {
        return [this, &audio, db]
        {
            if (audioActive() && audio.range())
                audio.editRange(A::Gain, db);
            else
                session_.setSound(idsOf(audioTargets()), {.gainBy = static_cast<float>(db)}, "Gain");
        };
    };
    actions_.add({.id = "audio.gainUp",
                  .name = "Up 1 dB",
                  .category = "Audio",
                  .perform = gainBy(1.0),
                  .isEnabled = hasClips,
                  .whyNot = whyNoClips});
    actions_.add({.id = "audio.gainDown",
                  .name = "Down 1 dB",
                  .category = "Audio",
                  .perform = gainBy(-1.0),
                  .isEnabled = hasClips,
                  .whyNot = whyNoClips});
    rangeAction("audio.silence", "Silence", A::Silence);
    // Normalise… (§6.4): the Tools pane at it, over the clip view's audio clip.
    actions_.add({.id = "audio.normalise",
                  .name = asks("Normalise"),
                  .category = "Audio",
                  .perform =
                      [this]
                  {
                      if (!detail_.showsAudioClip())
                      {
                          arrangeMode_ = DetailPanel::Mode::Clip;
                          applyDetailMode();
                          openPanel(detailPanel_);
                      }
                      if (detail_.showsAudioClip())
                          detail_.showTool("normalise");
                  },
                  .isEnabled = hasClips,
                  .whyNot = whyNoClips});
    // Reverse and polarity (§6.3): ticked as the first clip has them.
    const auto toggle = [this, hasClips, whyNoClips](std::string id, juce::String name,
                                                     bool model::AudioSound::* flag, const char* undo)
    {
        actions_.add({.id = std::move(id),
                      .name = std::move(name),
                      .category = "Audio",
                      .perform =
                          [this, flag, undo]
                      {
                          const auto clips = audioTargets();
                          if (clips.empty())
                              return;
                          const bool on = !(clips.front().sound.*flag);
                          engine::Session::SoundChange change;
                          (flag == &model::AudioSound::reversed ? change.reversed : change.inverted) = on;
                          session_.setSound(idsOf(clips), change, undo);
                      },
                      .isEnabled = hasClips,
                      .isToggled =
                          [this, flag]
                      {
                          const auto clips = audioTargets();
                          return !clips.empty() && clips.front().sound.*flag;
                      },
                      .whyNot = whyNoClips});
    };
    toggle("audio.reverse", "Reverse", &model::AudioSound::reversed, "Reverse");
    toggle("audio.polarity", "Invert polarity", &model::AudioSound::inverted, "Polarity");
    // Channels ▸ (§6.3): how a stereo file plays.
    const auto stereo = [this]
    {
        const auto clips = audioTargets();
        return std::any_of(clips.begin(), clips.end(), [](const auto& c) { return c.fileChannels >= 2; });
    };
    for (const auto mode : {model::ChannelMode::Stereo, model::ChannelMode::Left, model::ChannelMode::Right,
                            model::ChannelMode::Mono, model::ChannelMode::Swapped})
    {
        const auto name = model::channelModeName(mode);
        actions_.add({.id = "audio.channels." + juce::String(name).toLowerCase().toStdString(),
                      .name = juce::String(name),
                      .category = "Audio",
                      .perform = [this, mode]
                      { session_.setSound(idsOf(audioTargets()), {.channels = mode}, "Channels"); },
                      .isEnabled = stereo,
                      .isToggled =
                          [this, mode]
                      {
                          const auto clips = audioTargets();
                          return !clips.empty() && clips.front().sound.channels == mode;
                      },
                      .whyNot = []
                      { return juce::String("Channels are for stereo files: select a stereo audio clip"); }});
    }
    rangeAction("audio.crop", "Crop to range", A::Crop);
    rangeAction("audio.fadeRange", "Fade to range", A::Fade);
    rangeAction("audio.loopRange", "Loop range", A::Loop);

    // View > Waveform height ▸ (§4, §13): the level column's drag and double-click, by menu.
    const auto shown = [this] { return detail_.showsAudioClip(); };
    const auto whyNotShown = [] { return juce::String("Show an audio clip in the clip view first"); };
    actions_.add({.id = "view.waveformTaller",
                  .name = "Taller",
                  .category = "View",
                  .perform = [&audio] { audio.setLevelZoom(audio.levelZoom() * 2.0f); },
                  .isEnabled = shown,
                  .whyNot = whyNotShown});
    actions_.add({.id = "view.waveformShorter",
                  .name = "Shorter",
                  .category = "View",
                  .perform = [&audio] { audio.setLevelZoom(audio.levelZoom() * 0.5f); },
                  .isEnabled = shown,
                  .whyNot = whyNotShown});
    actions_.add({.id = "view.waveformReset",
                  .name = "Reset",
                  .category = "View",
                  .perform = [&audio] { audio.setLevelZoom(1.0f); },
                  .isEnabled = shown,
                  .whyNot = whyNotShown});

    // The bar's Audio button opens the menu bar's Audio menu: one builder (§13).
    detail_.audioMenu = [this]
    {
        const auto& menus = menus_->menus();
        for (std::size_t i = 0; i < menus.size(); ++i)
            if (menus[i].title == "Audio")
                return menus_->menu(static_cast<int>(i));
        return juce::PopupMenu();
    };
    audio.menu = [this] { return audioContextMenu(); };
    audio.onMessage = [this](const juce::String& m) { showWarning(m); };
    audio.onUsed = [this]
    {
        lastArea_ = Area::Clip;
        detail_.toolsPane().disarm(); // working on the clip lets go of a tool shown on it (§11)
    };
    // A range command that made a clip of the range, or took it away, selects what's left.
    audio.onSelectClips = [this](std::vector<std::uint64_t> clips)
    {
        session_.updateSnapshotNow();
        const auto channel = arrangement_.selection().channel;
        arrangement_.setSelection(
            model::withClips(*session_.snapshot(), {clips.begin(), clips.end()}, channel, channel));
    };
    audio.selectedClips = [this]
    {
        std::vector<std::uint64_t> out;
        const auto snapshot = session_.snapshot();
        for (auto id : arrangement_.selection().clips)
            if (const auto found = model::findClip(*snapshot, id);
                found && found->first->kind == model::ClipInfo::Kind::Audio)
                out.push_back(id);
        return out;
    };
}

juce::PopupMenu Shell::audioContextMenu()
{
    // The most used, on the waveform (§13): the range's, then the clip's; the rest in the Audio
    // menu, which opens from here too.
    juce::PopupMenu menu;
    actions_.addMenuItem(menu, "edit.split");
    actions_.addMenuItem(menu, "audio.crop");
    actions_.addMenuItem(menu, "edit.delete");
    actions_.addMenuItem(menu, "audio.silence");
    actions_.addMenuItem(menu, "audio.fadeRange");
    actions_.addMenuItem(menu, "audio.loopRange");
    menu.addSeparator();
    juce::PopupMenu gain;
    for (const char* id : {"audio.gainUp", "audio.gainDown"})
        actions_.addMenuItem(gain, id);
    menu.addSubMenu("Gain", gain);
    actions_.addMenuItem(menu, "audio.normalise");
    actions_.addMenuItem(menu, "audio.reverse");
    menu.addSeparator();
    menu.addSubMenu("Audio", detail_.audioMenu ? detail_.audioMenu() : juce::PopupMenu());
    menu.addSubMenu("Grid", detail_.pianoRoll().gridMenu()); // the clip view's (ADR 0047)
    return menu;
}
} // namespace fd::app
