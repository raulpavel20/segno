// The song's sections in the shell (M3 step C3, ADR 0049, docs/design/arranger.md §8-§12): the
// Song menu's commands and the sections' right-click menu. Edit's Delete, Duplicate and Rename act
// on selected sections too (Shell.cpp).

#include "model/Sections.h"
#include "ui/Shell.h"

namespace fd::app
{
namespace
{
juce::KeyPress key(int code, int mods = 0)
{
    return juce::KeyPress(code, mods, 0);
}

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr double kNewSectionBars = 8.0; // a section's length where nothing says otherwise
} // namespace

bool Shell::sectionsActive() const
{
    return lastArea_ == Area::Arrangement && !mixerShown_ && arrangement_.hasSectionSelection();
}

void Shell::openKeyPopover()
{
    // The keys found from the selected clips, or from every one (key-and-scale.md §11): MIDI
    // clips' notes and audio clips' keys (audio-editor.md §8.2).
    std::vector<std::uint64_t> clips;
    const auto snapshot = session_.snapshot();
    for (const auto& track : snapshot->tracks)
        for (const auto& clip : track.clips)
            if (arrangement_.selectedClips().contains(clip.id))
                clips.push_back(clip.id);
    auto& field = transport_->keyField();
    // Under the bar, not over its edge, as the Assistants popover opens.
    const auto anchor =
        (field.isVisible() ? getLocalArea(transport_.get(), field.getBounds()) : transport_->getBounds())
            .withBottom(transport_->getBottom());
    keyPopover_.open(*this, anchor, clips);
}

void Shell::registerSongActions()
{
    // The song's key (ADR 0057, key-and-scale.md §13): its own settings first in the Song menu.
    actions_.add({.id = "song.key",
                  .name = juce::String::fromUTF8("Key\xE2\x80\xA6"), // it asks for more (menu-bar.md §5)
                  .category = "Song",
                  .perform = [this] { openKeyPopover(); }});
    actions_.add({.id = "song.noKey",
                  .name = "No key",
                  .category = "Song",
                  .perform = [this] { session_.setKey(std::nullopt); },
                  .isEnabled = [this] { return session_.snapshot()->keys.first().has_value(); }});

    auto arrangeShown = [this] { return !mixerShown_; };
    auto selected = [this] { return !mixerShown_ && arrangement_.hasSectionSelection(); };
    auto bars = [this](double n)
    {
        const auto& tempo = session_.snapshot()->tempo;
        return tempo.secondsForBeats(n * tempo.beatsPerBar());
    };
    auto span = [this]
    { return model::spanOf(session_.snapshot()->sections, arrangement_.selectedSections()); };

    // Add: the loop if it's on, else from the playhead to the next section (or 8 bars).
    actions_.add(
        {.id = "song.addSection",
         .name = "Add section",
         .category = "Song",
         .perform =
             [this, bars]
         {
             const auto snapshot = session_.snapshot();
             double from = session_.position(), to = 0.0;
             if (session_.isLooping())
                 std::tie(from, to) = session_.loopRange();
             else
                 to =
                     model::nextSectionStart(snapshot->sections, from).value_or(from + bars(kNewSectionBars));
             if (const auto id = session_.addSection(from, to))
             {
                 arrangement_.selectSections({id});
                 lastArea_ = Area::Arrangement;
             }
             else
                 showWarning(
                     "A section can't overlap another: move the playhead to unmarked time, or set the "
                     "loop there");
         },
         .isEnabled = arrangeShown});
    // Insert: an empty section after the selected ones, as long as the last of them.
    actions_.add({.id = "song.insertSection",
                  .name = "Insert empty section",
                  .category = "Song",
                  .perform =
                      [this, span]
                  {
                      const auto run = span();
                      const auto ids = arrangement_.selectedSections();
                      if (!run || ids.empty())
                          return;
                      const auto* last = session_.snapshot()->section(ids.back());
                      if (const auto id = session_.insertSection(
                              run->second, last != nullptr ? last->length() : run->second - run->first))
                          arrangement_.selectSections({id});
                  },
                  .isEnabled = selected});
    actions_.add({.id = "song.loopSection",
                  .name = "Loop section",
                  .category = "Song",
                  .perform =
                      [this, span]
                  {
                      if (const auto run = span())
                      {
                          session_.setLoopRange(run->first, run->second);
                          session_.setLooping(true);
                      }
                  },
                  .isEnabled = selected});
    actions_.add({.id = "song.previousSection",
                  .name = "Previous section",
                  .category = "Song",
                  .keys = {key(juce::KeyPress::leftKey, kAlt)},
                  .perform =
                      [this]
                  {
                      if (const auto at =
                              model::previousSectionStart(session_.snapshot()->sections, session_.position()))
                          session_.setPosition(*at);
                  },
                  .isEnabled =
                      [this]
                  {
                      return model::previousSectionStart(session_.snapshot()->sections, session_.position())
                          .has_value();
                  }});
    actions_.add(
        {.id = "song.nextSection",
         .name = "Next section",
         .category = "Song",
         .keys = {key(juce::KeyPress::rightKey, kAlt)},
         .perform =
             [this]
         {
             if (const auto at = model::nextSectionStart(session_.snapshot()->sections, session_.position()))
                 session_.setPosition(*at);
         },
         .isEnabled =
             [this]
         {
             return model::nextSectionStart(session_.snapshot()->sections, session_.position()).has_value();
         }});
    // Its clips: everything in its time, those across its edges too (then clip commands apply).
    // The selected sections' stretch as a time selection on every track and lane (time-selection.md
    // §6): what Select clips in section was a bridge to (arranger.md A11).
    actions_.add({.id = "song.selectSectionTime",
                  .name = "Select section's time",
                  .category = "Song",
                  .perform =
                      [this, span]
                  {
                      if (const auto run = span())
                      {
                          lastArea_ = Area::Arrangement;
                          arrangement_.selectTimeOnEveryRow({run->first, run->second});
                      }
                  },
                  .isEnabled = selected});
    actions_.add({.id = "song.duplicateSection",
                  .name = "Duplicate section",
                  .category = "Song",
                  .perform =
                      [this, span]
                  {
                      if (const auto run = span())
                          if (const auto copies =
                                  session_.copySections(arrangement_.selectedSections(), run->second);
                              !copies.empty())
                              arrangement_.selectSections({copies.begin(), copies.end()});
                  },
                  .isEnabled = selected});
    actions_.add({.id = "song.deleteSection",
                  .name = "Delete section",
                  .category = "Song",
                  .perform = [this] { session_.deleteSections(arrangement_.selectedSections()); },
                  .isEnabled = selected});
    actions_.add({.id = "song.removeSectionMark",
                  .name = "Remove section mark",
                  .category = "Song",
                  .perform = [this] { session_.deleteSections(arrangement_.selectedSections(), true); },
                  .isEnabled = selected});

    arrangement_.onSectionMenu = [this](juce::PopupMenu& menu, std::uint64_t section)
    { sectionMenu(menu, section); };
    arrangement_.onLoopSection = [this] { actions_.perform("song.loopSection"); };
    arrangement_.onSectionActivated = [this] { lastArea_ = Area::Arrangement; };
}

juce::PopupMenu Shell::sectionColourMenu()
{
    juce::PopupMenu menu;
    const auto snapshot = session_.snapshot();
    const auto ids = arrangement_.selectedSections();
    const auto* first = ids.empty() ? nullptr : snapshot->section(ids.front());
    if (first == nullptr || mixerShown_)
    {
        menu.addItem(juce::PopupMenu::Item("Select a section first").setEnabled(false));
        return menu;
    }
    ui::addColourGrid(menu, first->colour,
                      [this, ids](int c)
                      {
                          for (const auto id : ids)
                              session_.setSectionColour(id, c);
                      });
    return menu;
}

void Shell::sectionMenu(juce::PopupMenu& menu, std::uint64_t section)
{
    if (section == 0) // the empty song row's
    {
        actions_.addMenuItem(menu, "song.addSection");
        return;
    }
    // Its names: rename it in place, or pick a usual one (arranger.md §7).
    actions_.addMenuItem(menu, "edit.rename");
    juce::PopupMenu names;
    for (const auto name : model::kUsualSectionNames)
        names.addItem(juce::String(std::string(name)),
                      [this, section, n = std::string(name)] { session_.renameSection(section, n); });
    menu.addSubMenu("Name", names);
    menu.addSubMenu("Colour", sectionColourMenu());
    menu.addSeparator();
    actions_.addMenuItem(menu, "song.loopSection");
    actions_.addMenuItem(menu, "song.selectSectionTime");
    menu.addSeparator();
    actions_.addMenuItem(menu, "edit.duplicate");
    actions_.addMenuItem(menu, "song.insertSection");
    actions_.addMenuItem(menu, "edit.delete");
    actions_.addMenuItem(menu, "song.removeSectionMark");
    menu.addSeparator();
    actions_.addMenuItem(menu, "edit.cut");
    actions_.addMenuItem(menu, "edit.copy");
    actions_.addMenuItem(menu, "edit.paste");
}
} // namespace fd::app
