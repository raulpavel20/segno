// The time selection's commands in the shell (ADR 0073, docs/design/time-selection.md §8, §9,
// §12, §17): its own actions, and the existing ones (Delete, Duplicate, the clipboard, Split,
// Select all, Loop, Zoom to fit, Crossfade, Mute, New MIDI clip, Reverse, the arrows) amended to
// act on it while it's what was used last. Their keys stay; their labels say what they do.

#include "model/Groups.h"
#include "ui/Shell.h"

#include <algorithm>
#include <cmath>

namespace fd::app
{
namespace
{
constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

juce::KeyPress key(int code, int mods = 0)
{
    return {code, mods, 0};
}
} // namespace

bool Shell::timeActive() const
{
    return (lastArea_ == Area::Arrangement || lastArea_ == Area::Lane) && !mixerShown_ &&
           arrangement_.hasTimeSelection();
}

void Shell::timeCommand(engine::Session::StretchCommand command)
{
    using C = engine::Session::StretchCommand;
    const auto stretch = arrangement_.timeSelection();
    if (const auto why = session_.stretchRefusal(stretch, command); !why.empty())
    {
        showWarning(juce::String::fromUTF8(why.c_str()));
        return;
    }
    const auto result = session_.editStretch(stretch, command);
    if (!result.changed)
        return;
    session_.updateSnapshotNow();
    lastArea_ = Area::Arrangement;
    // The selection follows the result (§8): Split and Crop hand back the clips inside; Duplicate
    // moves onto the copy; the rest keep the stretch.
    if (command == C::Split || command == C::Crop)
    {
        const auto& clips = result.clips;
        arrangement_.setSelection(model::withClips(*session_.snapshot(), {clips.begin(), clips.end()},
                                                   std::nullopt, arrangement_.selection().channel));
    }
    else if (command == C::Duplicate)
        arrangement_.moveTimeTo(result.span);
}

void Shell::timeMenu(juce::PopupMenu& menu)
{
    for (const auto* id : {"edit.cut", "edit.copy", "edit.paste"})
        actions_.addMenuItem(menu, id);
    menu.addSeparator();
    for (const auto* id : {"edit.delete", "edit.duplicate", "edit.split", "clip.crop"})
        actions_.addMenuItem(menu, id);
    menu.addSeparator();
    for (const auto* id : {"clip.crossfade", "clip.mute", "audio.reverse", "clip.newMidi"})
        actions_.addMenuItem(menu, id);
    menu.addSeparator();
    for (const auto* id : {"clip.bounce", "clip.bounceToTrack"})
        actions_.addMenuItem(menu, id);
    menu.addSeparator();
    for (const auto* id : {"transport.loop", "view.zoomFit"})
        actions_.addMenuItem(menu, id);
    menu.addSeparator();
    for (const auto* id : {"song.insertTime", "song.deleteTime", "song.duplicateTime"})
        actions_.addMenuItem(menu, id);
}

void Shell::registerTimeActions()
{
    using C = engine::Session::StretchCommand;
    arrangement_.onTimeMenu = [this](juce::PopupMenu& menu) { timeMenu(menu); };
    const auto active = [this] { return timeActive(); };
    const auto noTime = [] { return juce::String("Select a stretch of time first: drag across the tracks"); };

    // ---- the existing actions, on a time selection (§8, §12) ------------------------------------
    // Each keeps its key: it acts on the time selection while that was used last, else as before.
    const auto onTime = [this](const char* id, std::function<void()> command, const char* label = nullptr)
    {
        actions_.amend(id,
                       [this, act = std::move(command), label](Action& a)
                       {
                           a.perform = [this, act, before = std::move(a.perform)]
                           {
                               if (timeActive())
                                   act();
                               else if (before)
                                   before();
                           };
                           a.isEnabled = [this, before = std::move(a.isEnabled)]
                           { return timeActive() || !before || before(); };
                           if (label != nullptr)
                               a.label = [this, label, before = std::move(a.label), name = a.name]
                               {
                                   return timeActive() ? juce::String(label) : before ? before() : name;
                               };
                       });
    };
    onTime("edit.delete", [this] { timeCommand(C::Delete); });
    onTime("edit.duplicate", [this] { timeCommand(C::Duplicate); });
    onTime("edit.split", [this] { timeCommand(C::Split); }, "Split at selection");
    onTime("edit.selectAll", [this] { arrangement_.widenTimeToAllRows(); });
    onTime("clip.crossfade", [this] { timeCommand(C::Fade); }, "Fade to selection");
    onTime("clip.mute", [this] { timeCommand(C::Mute); });
    onTime("clip.newMidi", [this] { timeCommand(C::NewMidiClip); });
    onTime("audio.reverse", [this] { timeCommand(C::Reverse); });
    onTime("view.zoomFit", [this] { arrangement_.zoomToTime(); }, "Zoom to selection");
    onTime(
        "transport.loop",
        [this]
        {
            // The brace takes its stretch and looping turns on (§12); punching follows the loop.
            const auto span = *arrangement_.selection().time;
            session_.setLoopRange(span.start, span.end);
            session_.setLooping(true);
        },
        "Loop selection");
    onTime(
        "song.addSection",
        [this]
        {
            // The selection's stretch, before the loop and the playhead (§13).
            const auto span = *arrangement_.selection().time;
            if (const auto id = session_.addSection(span.start, span.end))
                arrangement_.selectSections({id});
            else
                showWarning("A section can't overlap another: select unmarked time");
        },
        "Add section from selection");
    onTime("notes.later", [this] { arrangement_.nudgeTime(1); });
    onTime("notes.earlier", [this] { arrangement_.nudgeTime(-1); });

    // The clipboard: a stretch on its rows (§8), or a piece of the whole song (§9).
    const auto copyStretch = [this]
    {
        heldStretchFrom_ = arrangement_.timeSelection();
        heldStretch_ = session_.copyStretch(heldStretchFrom_);
        held_ = heldStretch_.empty() ? held_ : Held::Stretch;
        return !heldStretch_.empty();
    };
    onTime("edit.copy", [copyStretch] { copyStretch(); });
    onTime("edit.cut",
           [this, copyStretch]
           {
               if (!copyStretch())
                   return;
               const engine::Session::EditGroup group(session_, "Cut");
               session_.editStretch(arrangement_.timeSelection(), C::Delete);
           });
    actions_.amend(
        "edit.paste",
        [this](Action& a)
        {
            a.perform = [this, before = std::move(a.perform)]
            {
                if (held_ == Held::Time)
                    actions_.perform("song.pasteTime");
                else if (held_ != Held::Stretch)
                    before();
                else if (mixerShown_)
                    showWarning("A stretch pastes into the arrangement");
                else
                {
                    // At the playhead, from the time selection's topmost track (else the
                    // selected track or a selected group's first, else the first), its rows
                    // keeping their places. Rows are tracks that hold clips (groups.md §11).
                    const auto snapshot = session_.snapshot();
                    const auto& sel = arrangement_.selection();
                    const auto holders = model::clipTracks(*snapshot);
                    const auto at = [&](size_t i) -> const model::TrackInfo&
                    { return snapshot->tracks[static_cast<size_t>(holders[i])]; };
                    std::optional<size_t> first;
                    for (size_t i = 0; i < holders.size() && !first; ++i)
                        if (sel.hasTime()
                                ? sel.rowTracks.contains(at(i).id)
                                : sel.channel == at(i).id ||
                                      (sel.channel && model::isInGroup(*snapshot, at(i), *sel.channel)))
                            first = i;
                    if (!first && !holders.empty())
                        first = 0;
                    std::optional<size_t> top;
                    std::vector<size_t> rows;
                    for (size_t i = 0; i < holders.size(); ++i)
                        if (heldStretchFrom_.tracks.contains(at(i).id))
                        {
                            top = top ? top : i;
                            rows.push_back(i - *top);
                        }
                    const auto target = first ? at(*first).id : 0;
                    const auto result = session_.pasteStretch(heldStretch_, session_.position(), target);
                    if (!result.changed)
                        return;
                    session_.updateSnapshotNow();
                    const auto after = session_.snapshot();
                    const auto afterHolders = model::clipTracks(*after);
                    std::set<std::uint64_t> tracks;
                    for (const auto row : rows)
                        if (first && *first + row < afterHolders.size())
                            tracks.insert(after->tracks[static_cast<size_t>(afterHolders[*first + row])].id);
                    lastArea_ = Area::Arrangement;
                    arrangement_.setSelection(model::withTime(*session_.snapshot(), result.span, tracks,
                                                              heldStretchFrom_.lanes, target));
                    if (result.rowsLeftOut > 0)
                        showWarning(juce::String(result.rowsLeftOut) +
                                    (result.rowsLeftOut == 1 ? " row" : " rows") +
                                    " had no track below: left out");
                }
            };
        });
    // Bounce in place and to a new track (§13): each track's stretch, offline, cut at its edges as it
    // lands; the result lands as one undo step and is selected.
    const auto stretchTracks = [this]
    {
        std::vector<std::uint64_t> out;
        const auto& rows = arrangement_.selection().rowTracks;
        for (const auto& t : session_.snapshot()->tracks)
            if (rows.contains(t.id))
                out.push_back(t.id);
        return out;
    };
    for (const auto& [id, mode] : {std::pair{"clip.bounce", model::BounceMode::InPlace},
                                   std::pair{"clip.bounceToTrack", model::BounceMode::NewTrack}})
        actions_.amend(
            id,
            [this, mode, stretchTracks](Action& a)
            {
                const auto why = [this, mode, stretchTracks]
                {
                    const auto span = *arrangement_.selection().time;
                    return session_.stretchBounceRefusal(stretchTracks(), span.start, span.end, mode);
                };
                a.perform = [this, mode, stretchTracks, before = std::move(a.perform)]
                {
                    if (!timeActive())
                    {
                        before();
                        return;
                    }
                    const auto span = *arrangement_.selection().time;
                    session_.bounceStretch(stretchTracks(), span.start, span.end, mode,
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
                                               arrangement_.setSelection(model::withClips(
                                                   *session_.snapshot(), {r.clips.begin(), r.clips.end()},
                                                   std::nullopt, std::nullopt));
                                           });
                };
                a.isEnabled = [this, why, before = std::move(a.isEnabled)]
                { return timeActive() ? why().empty() : !before || before(); };
                a.whyNot = [this, why, before = std::move(a.whyNot)]
                {
                    return timeActive() ? juce::String::fromUTF8(why().c_str())
                           : before     ? before()
                                        : juce::String();
                };
            });

    // Cancel bounces' key, Escape, also clears a selection when nothing bounces (Select none).
    actions_.add(
        {.id = "edit.selectNone",
         .name = "Select none",
         .category = "Edit",
         .perform = [this] { arrangement_.setSelection({.channel = arrangement_.selection().channel}); },
         .isEnabled =
             [this]
         {
             const auto& sel = arrangement_.selection();
             return !mixerShown_ && (sel.hasTime() || !sel.clips.empty() || !sel.points.empty() ||
                                     !sel.sections.empty() || !sel.takes.empty());
         },
         .keysFrom = "clip.cancelBounces"});
    actions_.amend("clip.cancelBounces",
                   [this](Action& a)
                   {
                       a.perform = [this, before = std::move(a.perform)]
                       {
                           if (!session_.bouncing().empty())
                               before();
                           else
                               actions_.perform("edit.selectNone");
                       };
                       a.isEnabled = [this, before = std::move(a.isEnabled)]
                       { return (before && before()) || actions_.isEnabled("edit.selectNone"); };
                   });

    // ---- its own (§6, §8, §9) ---------------------------------------------------------------------
    actions_.add({.id = "edit.selectLoop",
                  .name = "Select loop",
                  .category = "Edit",
                  .perform =
                      [this]
                  {
                      const auto [a, b] = session_.loopRange();
                      lastArea_ = Area::Arrangement;
                      arrangement_.selectTimeOnEveryRow({a, b});
                  },
                  .isEnabled =
                      [this]
                  {
                      const auto [a, b] = session_.loopRange();
                      return !mixerShown_ && b > a;
                  },
                  .whyNot = [] { return juce::String("Draw a loop brace first"); }});
    actions_.add({.id = "edit.timeEndLater",
                  .name = "Selection end later",
                  .category = "Edit",
                  .keys = {key(juce::KeyPress::rightKey, kShift)},
                  .perform = [this] { arrangement_.extendTime(1); },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add({.id = "edit.timeEndEarlier",
                  .name = "Selection end earlier",
                  .category = "Edit",
                  .keys = {key(juce::KeyPress::leftKey, kShift)},
                  .perform = [this] { arrangement_.extendTime(-1); },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add({.id = "clip.crop",
                  .name = "Crop to selection",
                  .category = "Clip",
                  .keys = {key('j', kCmd | kShift)},
                  .perform = [this] { timeCommand(C::Crop); },
                  .isEnabled = active,
                  .whyNot = noTime});

    // Song-wide (§9): every track and lane, and the section marks.
    const auto span = [this] { return *arrangement_.selection().time; };
    actions_.add({.id = "song.insertTime",
                  .name = "Insert time",
                  .category = "Song",
                  .keys = {key('i', kCmd)},
                  .perform = [this, span] { session_.insertTime(span().start, span().length()); },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add({.id = "song.deleteTime",
                  .name = "Delete time",
                  .category = "Song",
                  .keys = {key(juce::KeyPress::deleteKey, kCmd | kShift)},
                  .perform =
                      [this, span]
                  {
                      if (session_.deleteTime(span().start, span().end))
                          arrangement_.setSelection({.channel = arrangement_.selection().channel});
                  },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add({.id = "song.duplicateTime",
                  .name = "Duplicate time",
                  .category = "Song",
                  .keys = {key('d', kCmd | kShift)},
                  .perform =
                      [this, span]
                  {
                      const auto s = span();
                      if (session_.duplicateTime(s.start, s.end))
                          arrangement_.moveTimeTo({s.end, s.end + s.length()});
                  },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add({.id = "song.copyTime",
                  .name = "Copy time",
                  .category = "Song",
                  .keys = {key('c', kCmd | kShift)},
                  .perform =
                      [this, span]
                  {
                      heldTime_ = session_.copyTime(span().start, span().end);
                      held_ = heldTime_.empty() ? held_ : Held::Time;
                  },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add({.id = "song.cutTime",
                  .name = "Cut time",
                  .category = "Song",
                  .perform =
                      [this, span]
                  {
                      const auto s = span();
                      heldTime_ = session_.copyTime(s.start, s.end);
                      if (heldTime_.empty())
                          return;
                      held_ = Held::Time;
                      {
                          const engine::Session::EditGroup group(session_, "Cut time");
                          session_.deleteTime(s.start, s.end);
                      }
                      arrangement_.setSelection({.channel = arrangement_.selection().channel});
                  },
                  .isEnabled = active,
                  .whyNot = noTime});
    actions_.add(
        {.id = "song.pasteTime",
         .name = "Paste time",
         .category = "Song",
         .keys = {key('v', kCmd | kShift)},
         .perform =
             [this]
         {
             if (mixerShown_)
             {
                 showWarning("Time pastes into the arrangement");
                 return;
             }
             session_.pasteTime(heldTime_, session_.position());
         },
         .isEnabled = [this] { return held_ == Held::Time && !heldTime_.empty(); },
         .whyNot = [] { return juce::String::fromUTF8("Copy time first (Song \xE2\x96\xB8 Copy time)"); }});
    registerClipTimeActions();
}

bool Shell::clipTimeActive() const
{
    return notesActive() && detail_.pianoRoll().timeSelection().has_value();
}

void Shell::registerClipTimeActions()
{
    using T = model::ClipTimeCommand;
    using A = engine::Session::RangeAction;
    auto& roll = detail_.pianoRoll();
    const auto active = [this] { return clipTimeActive(); };
    const auto noTime = []
    { return juce::String("Select a stretch of time in the clip first: drag across the notes"); };

    // Insert, Delete and Duplicate time inside the clip: the Song menu's keys (§17).
    for (const auto& [notesId, name, which, song] :
         {std::tuple{"notes.insertTime", "Insert time", T::Insert, "song.insertTime"},
          std::tuple{"notes.deleteTime", "Delete time", T::Delete, "song.deleteTime"},
          std::tuple{"notes.duplicateTime", "Duplicate time", T::Duplicate, "song.duplicateTime"}})
    {
        actions_.add({.id = notesId,
                      .name = name,
                      .category = "Notes",
                      .perform =
                          [this, &roll, command = which]
                      {
                          const auto span = *roll.timeSelection();
                          if (!roll.clip() ||
                              !session_.editClipTime(roll.clip()->id, command, span.start, span.end))
                              return;
                          // Inserted time stays selected; deleted time is gone; a duplicate's copy is
                          // selected.
                          if (command == T::Delete)
                              roll.setTimeSelection(std::nullopt);
                          else if (command == T::Duplicate)
                              roll.setTimeSelection(model::TimeSpan{span.end, span.end + span.length()});
                      },
                      .isEnabled = active,
                      .whyNot = noTime,
                      .keysFrom = song});
        actions_.amend(song,
                       [this, inClip = std::string(notesId)](Action& a)
                       {
                           a.perform = [this, inClip, before = std::move(a.perform)]
                           {
                               if (clipTimeActive())
                                   actions_.perform(inClip);
                               else if (before)
                                   before();
                           };
                           a.isEnabled = [this, before = std::move(a.isEnabled)]
                           { return clipTimeActive() || !before || before(); };
                       });
    }

    // The clip's edges, or its loop, take the stretch (§14.1).
    for (const auto& [notesId, name, which] :
         {std::tuple{"notes.cropClip", "Crop clip to selection", A::Crop},
          std::tuple{"notes.loopClip", "Clip loop to selection", A::Loop}})
    {
        const auto why = [this, &roll, action = which]
        {
            if (!clipTimeActive() || !roll.clip())
                return std::string();
            const auto span = *roll.timeSelection();
            return session_.rangeRefusal(roll.clip()->id, span.start, span.end, action);
        };
        actions_.add({.id = notesId,
                      .name = name,
                      .category = "Notes",
                      .perform =
                          [this, &roll, action = which]
                      {
                          const auto span = *roll.timeSelection();
                          const auto r = session_.editRange(roll.clip()->id, span.start, span.end, action);
                          if (!r.why.empty())
                          {
                              showWarning(juce::String::fromUTF8(r.why.c_str()));
                              return;
                          }
                          // Cropped, the clip is what's left of it: selected, so the clip view shows it.
                          if (action == A::Crop && !r.clips.empty())
                          {
                              session_.updateSnapshotNow();
                              const auto channel = arrangement_.selection().channel;
                              arrangement_.setSelection(model::withClips(
                                  *session_.snapshot(), {r.clips.front()}, channel, channel));
                              roll.setTimeSelection(std::nullopt);
                          }
                      },
                      .isEnabled = [active, why] { return active() && why().empty(); },
                      .whyNot = [active, noTime, why]
                      { return active() ? juce::String::fromUTF8(why().c_str()) : noTime(); }});
    }

    // Ctrl+D right after the stretch, Ctrl+E at its edges, Escape clearing the clip view's own (§14.1).
    actions_.amend("edit.duplicate",
                   [this, &roll](Action& a)
                   {
                       a.perform = [this, &roll, before = std::move(a.perform)]
                       {
                           if (clipTimeActive())
                               roll.duplicateSelection();
                           else if (before)
                               before();
                       };
                       a.isEnabled = [this, before = std::move(a.isEnabled)]
                       { return clipTimeActive() || !before || before(); };
                   });
    actions_.amend(
        "edit.split",
        [this, &roll](Action& a)
        {
            a.perform = [this, &roll, before = std::move(a.perform)]
            {
                if (clipTimeActive())
                    roll.splitAtSelection();
                else if (before)
                    before();
            };
            a.label = [this, before = std::move(a.label), name = a.name]
            {
                return clipTimeActive() ? juce::String("Split at selection") : before ? before() : name;
            };
        });
    actions_.amend("edit.selectNone",
                   [this, &roll](Action& a)
                   {
                       a.perform = [this, &roll, before = std::move(a.perform)]
                       {
                           if (notesActive())
                           {
                               roll.setSelection({});
                               roll.setTimeSelection(std::nullopt);
                           }
                           else if (audioActive())
                               detail_.audioClipView().setRange(std::nullopt);
                           else if (before)
                               before();
                       };
                       a.isEnabled = [this, &roll, before = std::move(a.isEnabled)]
                       {
                           if (notesActive())
                               return roll.hasSelection() || roll.timeSelection().has_value();
                           if (audioActive())
                               return detail_.audioClipView().range().has_value();
                           return !before || before();
                       };
                   });
}
} // namespace fd::app
