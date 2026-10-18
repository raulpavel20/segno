// Groups in the shell (ADR 0072, docs/design/groups.md §5, §6.4, §11, §12): Group tracks, Ungroup
// tracks and Remove from group, Ctrl+G and Ctrl+Shift+G shared with racks by what was used last;
// folding in the view shown; the group's colour given to its tracks; Add track in a group; the
// toasts saying what went.

#include "model/Groups.h"
#include "ui/Shell.h"

#include <algorithm>

namespace fd::app
{
namespace
{
/// "Drums", "Drums and Bass", "Drums, Bass and Keys".
juce::String namesText(const std::vector<std::string>& names)
{
    juce::String out;
    for (size_t i = 0; i < names.size(); ++i)
        out << (i == 0                  ? ""
                : i + 1 == names.size() ? " and "
                                        : ", ")
            << juce::String::fromUTF8(names[i].c_str());
    return out;
}
} // namespace

juce::String Shell::emptiedText(const std::vector<std::string>& names)
{
    return namesText(names) +
           (names.size() == 1 ? " went too: nothing was left in it" : " went too: nothing was left in them");
}

std::optional<model::TrackInfo> Shell::selectedGroup() const
{
    const auto c = commandChannel();
    const auto* t = c ? session_.snapshot()->channel(*c) : nullptr;
    return t != nullptr && t->isGroup() ? std::optional(*t) : std::nullopt;
}

std::set<std::uint64_t> Shell::shownFolds() const
{
    return mixerShown_ ? mixer_.foldedGroups() : arrangement_.foldedGroups();
}

void Shell::setShownFolds(std::set<std::uint64_t> folded)
{
    if (mixerShown_)
        mixer_.setFoldedGroups(std::move(folded));
    else
        arrangement_.setFoldedGroups(std::move(folded));
}

void Shell::registerGroupActions()
{
    auto& s = session_;
    // A group a command leaves empty goes in its step (§4): said by the command when it says
    // something itself, else on its own once the command is done (a dragged header or strip).
    s.onGroupsEmptied = [this](const std::vector<std::string>& names)
    {
        emptied_.insert(emptied_.end(), names.begin(), names.end());
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<Shell>(this)]
            {
                if (safe != nullptr && !safe->emptied_.empty())
                    safe->showToast(emptiedText(std::exchange(safe->emptied_, {})), "Undo",
                                    [safe]
                                    {
                                        if (safe != nullptr)
                                            safe->session_.undo();
                                    });
            });
    };
    const auto chosen = [this]
    {
        const auto c = commandChannel();
        return c ? chosenWith(*c) : std::vector<std::uint64_t>{};
    };
    // Tracks and groups only: returns and the master are never in a group (§7.4).
    const auto groupable = [this, chosen]
    {
        const auto ids = chosen();
        const auto snapshot = session_.snapshot();
        return !ids.empty() && std::all_of(ids.begin(), ids.end(),
                                           [&](std::uint64_t id)
                                           {
                                               const auto* t = snapshot->channel(id);
                                               return t != nullptr && (t->holdsClips() || t->isGroup());
                                           });
    };
    const auto recording = [this] { return session_.isRecording(); };
    const auto whyNotRecording = [this]
    { return session_.isRecording() ? juce::String("Stop recording first") : juce::String(); };

    // Group tracks (§5.1): selected, unfolded (a new group is in no view's folds).
    actions_.add({.id = "track.group",
                  .name = "Group tracks",
                  .category = "Track",
                  .perform =
                      [this, chosen]
                  {
                      const auto made = session_.groupTracks(chosen());
                      if (made == 0)
                          return;
                      session_.updateSnapshotNow();
                      lastArea_ = Area::Arrangement;
                      arrangement_.selectTrack(made);
                  },
                  .isEnabled = [groupable, recording] { return groupable() && !recording(); },
                  .whyNot =
                      [this, groupable, whyNotRecording]
                  {
                      if (!commandChannel())
                          return juce::String("Select tracks to group first");
                      return !groupable()
                                 ? juce::String("Return tracks stay among the returns: they can't be grouped")
                                 : whyNotRecording();
                  },
                  .keysFrom = "module.groupIntoRack"});
    // Ungroup tracks (§5.2): what it held takes its place, chosen together; the toast says what
    // went with it.
    actions_.add({.id = "track.ungroup",
                  .name = "Ungroup tracks",
                  .category = "Track",
                  .perform =
                      [this]
                  {
                      const auto g = selectedGroup();
                      if (!g)
                          return;
                      const auto freed = session_.ungroupTracks(g->id);
                      if (freed.empty())
                          return;
                      session_.updateSnapshotNow();
                      lastArea_ = Area::Arrangement;
                      const std::set<std::uint64_t> together(freed.begin(), freed.end());
                      arrangement_.setSelection(
                          freed.size() > 1 ? model::Selection{.channel = freed.front(), .rowTracks = together}
                                           : model::Selection{.channel = freed.front()});
                      const auto name = juce::String::fromUTF8(g->name.c_str());
                      const auto modules = static_cast<int>(g->devices.size());
                      showToast("Ungrouped " + name +
                                    (modules == 0   ? juce::String()
                                     : modules == 1 ? juce::String(": its module is gone")
                                                    : ": its " + juce::String(modules) + " modules are gone"),
                                "Undo", [this] { session_.undo(); });
                  },
                  .isEnabled = [this, recording] { return selectedGroup().has_value() && !recording(); },
                  .whyNot = [this, whyNotRecording]
                  { return !selectedGroup() ? juce::String("Select a group first") : whyNotRecording(); },
                  .keysFrom = "module.ungroupRack"});
    // Remove from group (§5.3): each right after its group.
    const auto inAGroup = [this, chosen]
    {
        const auto ids = chosen();
        const auto snapshot = session_.snapshot();
        return std::any_of(ids.begin(), ids.end(),
                           [&](std::uint64_t id)
                           {
                               const auto* t = snapshot->channel(id);
                               return t != nullptr && t->group != 0;
                           });
    };
    actions_.add({.id = "track.removeFromGroup",
                  .name = "Remove from group",
                  .category = "Track",
                  .perform = [this, chosen] { session_.removeFromGroup(chosen()); },
                  .isEnabled = [inAGroup, recording] { return inAGroup() && !recording(); }});
    // Folding (§6.4), in the view shown: the selected group (a toggle), or every group.
    actions_.add({.id = "track.fold",
                  .name = "Fold group",
                  .category = "Track",
                  .keys = {juce::KeyPress('u', juce::ModifierKeys::altModifier, 0)},
                  .perform =
                      [this]
                  {
                      if (const auto g = selectedGroup())
                          setShownFolds(model::toggledFold(*session_.snapshot(), shownFolds(), g->id, false));
                  },
                  .isEnabled = [this] { return selectedGroup().has_value(); },
                  .isToggled =
                      [this]
                  {
                      const auto g = selectedGroup();
                      return g && shownFolds().contains(g->id);
                  },
                  .whyNot = [] { return juce::String("Select a group first"); }});
    const auto anyGroup = [this]
    {
        const auto& tracks = session_.snapshot()->tracks;
        return std::any_of(tracks.begin(), tracks.end(), [](const auto& t) { return t.isGroup(); });
    };
    const auto foldAll = [this](bool fold)
    {
        std::set<std::uint64_t> folded;
        if (fold)
            for (const auto& t : session_.snapshot()->tracks)
                if (t.isGroup())
                    folded.insert(t.id);
        setShownFolds(std::move(folded));
    };
    actions_.add({.id = "view.foldGroups",
                  .name = "Fold all groups",
                  .category = "View",
                  .perform = [foldAll] { foldAll(true); },
                  .isEnabled = anyGroup});
    actions_.add({.id = "view.unfoldGroups",
                  .name = "Unfold all groups",
                  .category = "View",
                  .perform = [foldAll] { foldAll(false); },
                  .isEnabled = anyGroup});
    // Give tracks the group's colour (§4): once, an action, not a link.
    actions_.add({.id = "track.colourMembers",
                  .name = "Give tracks the group's colour",
                  .category = "Track",
                  .perform =
                      [this]
                  {
                      if (const auto g = selectedGroup())
                          session_.colourMembers(g->id);
                  },
                  .isEnabled = [this] { return selectedGroup().has_value(); }});

    // Bounce to new track with a group selected, no clips (§11): its whole sound before its fader
    // onto a new track after it, the group muted; the new track then selected.
    const auto groupBounce = [this]
    { return !arrangement_.hasClipSelection() && selectedGroup().has_value(); };
    actions_.amend("clip.bounceToTrack",
                   [this, groupBounce](Action& a)
                   {
                       a.perform = [this, groupBounce, before = a.perform]
                       {
                           if (!groupBounce())
                           {
                               before();
                               return;
                           }
                           session_.bounceGroup(selectedGroup()->id,
                                                [this](const engine::Session::BounceResult& r)
                                                {
                                                    if (r.cancelled)
                                                        return;
                                                    if (!r.error.empty())
                                                    {
                                                        showWarning(juce::String::fromUTF8(r.error.c_str()));
                                                        return;
                                                    }
                                                    session_.updateSnapshotNow();
                                                    if (!r.tracks.empty())
                                                        arrangement_.selectTrack(r.tracks.front());
                                                });
                       };
                       a.isEnabled = [this, groupBounce, before = a.isEnabled]
                       {
                           return groupBounce() ? session_.groupBounceRefusal(selectedGroup()->id).empty()
                                                : before == nullptr || before();
                       };
                       a.whyNot = [this, groupBounce, before = a.whyNot]
                       {
                           return groupBounce()
                                      ? juce::String::fromUTF8(
                                            session_.groupBounceRefusal(selectedGroup()->id).c_str())
                                  : before ? before()
                                           : juce::String();
                       };
                   });

    // Ctrl+G and Ctrl+Shift+G (§12): the chain used last, its modules; else the tracks.
    actions_.amend("module.groupIntoRack",
                   [this](Action& a)
                   {
                       a.keyGoesTo = [this]
                       { return lastArea_ == Area::Detail ? std::string() : std::string("track.group"); };
                   });
    actions_.amend("module.ungroupRack",
                   [this](Action& a)
                   {
                       a.keyGoesTo = [this]
                       { return lastArea_ == Area::Detail ? std::string() : std::string("track.ungroup"); };
                   });
    // Add track (§11): with a group or one of its tracks selected, at that group's end.
    actions_.amend("track.add",
                   [this](Action& a)
                   {
                       a.perform = [this]
                       {
                           std::uint64_t in = 0;
                           if (const auto c = commandChannel())
                               if (const auto* t = session_.snapshot()->channel(*c))
                                   in = t->isGroup() ? t->id : t->group;
                           session_.addTrack({}, in);
                       };
                   });
}

void Shell::headerMenu(juce::PopupMenu& menu, std::uint64_t channel)
{
    // A track's or return's own items (tracks-and-clips.md §10), a group's (groups.md §12).
    const auto* c = session_.snapshot()->channel(channel);
    const auto add = [&](const char* id) { actions_.addMenuItem(menu, id); };
    if (c != nullptr && c->isGroup())
    {
        add("edit.rename");
        add("track.fold");
        add("track.group");
        add("track.ungroup");
        if (c->group != 0)
            add("track.removeFromGroup");
        add("track.duplicate");
        add("track.delete");
        menu.addSubMenu("Colour", trackColourMenu());
        add("track.colourMembers");
        add("clip.bounceToTrack");
        return;
    }
    add("edit.rename");
    add("track.duplicate");
    add("track.delete");
    if (c != nullptr && c->holdsClips())
    {
        add("track.group");
        if (c->group != 0)
            add("track.removeFromGroup");
    }
    menu.addSubMenu("Colour", trackColourMenu());
    if (const auto harmony = trackHarmonyMenu(); harmony.getNumItems() > 0) // a track's, not a return's
        menu.addSubMenu("Harmony", harmony);
}
} // namespace fd::app
