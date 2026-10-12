// The song's sections (M3 step C3, ADR 0049, docs/design/arranger.md): stored as the edit's
// arranger track; moving, copying, inserting and deleting them take their music along, planned by
// model::Sections and carried out with the clip and lane commands in one undo step.

#include "engine/SessionImpl.h"
#include "model/Sections.h"

#include <set>

namespace fd::engine
{
namespace
{
constexpr double kEpsilon = 1e-6;

te::ArrangerTrack& arrangerOf(te::Edit& edit)
{
    edit.ensureArrangerTrack();
    return *edit.getArrangerTrack();
}

te::Clip* sectionClip(te::Edit& edit, std::uint64_t id)
{
    for (auto* c : arrangerOf(edit).getClips())
        if (c->itemID.getRawID() == id)
            return c;
    return nullptr;
}

te::TimeRange rangeOf(double start, double end)
{
    return {te::TimePosition::fromSeconds(start), te::TimePosition::fromSeconds(end)};
}

/// "Chorus", or "2 sections".
std::string describe(const model::Snapshot& snapshot, const std::vector<std::uint64_t>& ids)
{
    std::vector<const model::SectionInfo*> found;
    for (const auto id : ids)
        if (const auto* s = snapshot.section(id))
            found.push_back(s);
    if (found.size() == 1)
        return found[0]->name;
    return std::to_string(found.size()) + " sections";
}
} // namespace

std::uint64_t Session::addSection(double start, double end, const std::string& name,
                                  std::optional<int> colour)
{
    updateSnapshotNow(); // plans read the project as it is now
    const auto s = snapshot();
    if (!model::sectionFits(s->sections, start, end))
        return 0;
    const auto trimmed = juce::String(name).trim();
    impl_->beginEdit("Add section");
    auto* clip =
        arrangerOf(*impl_->edit)
            .insertNewClip(te::TrackItem::Type::arranger,
                           trimmed.isNotEmpty() ? trimmed : juce::String(model::newSectionName(s->sections)),
                           rangeOf(start, end), nullptr);
    if (clip == nullptr)
        return 0;
    clip->state.setProperty(Impl::colourPropertyId,
                            colour.value_or(model::newSectionColour(s->sections, Impl::paletteSize)),
                            &impl_->edit->getUndoManager());
    return clip->itemID.getRawID();
}

bool Session::renameSection(std::uint64_t section, const std::string& name)
{
    const auto trimmed = juce::String(name).trim();
    auto* clip = sectionClip(*impl_->edit, section);
    if (clip == nullptr || trimmed.isEmpty() || clip->getName() == trimmed)
        return false;
    impl_->beginEdit("Rename section");
    clip->setName(trimmed);
    return true;
}

bool Session::setSectionColour(std::uint64_t section, int colour)
{
    auto* clip = sectionClip(*impl_->edit, section);
    if (clip == nullptr || colour < 0 || colour >= Impl::paletteSize ||
        static_cast<int>(clip->state.getProperty(Impl::colourPropertyId, -1)) == colour)
        return false;
    impl_->beginEdit("Section colour");
    clip->state.setProperty(Impl::colourPropertyId, colour, &impl_->edit->getUndoManager());
    return true;
}

bool Session::setSectionMarks(std::uint64_t section, double start, double end)
{
    updateSnapshotNow(); // plans read the project as it is now
    auto* clip = sectionClip(*impl_->edit, section);
    if (clip == nullptr || !model::sectionFits(snapshot()->sections, start, end, section))
        return false;
    impl_->beginEdit("Move section mark");
    clip->setPosition({rangeOf(start, end), te::TimeDuration()});
    return true;
}

// ---- with the music ---------------------------------------------------------------------------

namespace
{
/// Carries a plan out (arranger.md §9): clips split at its cuts, then deleted, moved and copied,
/// lanes' points replaced, and the sections placed, all inside the caller's EditGroup.
struct PlanRun
{
    Session& session;
    te::Edit& edit;

    model::PlanResult run(const model::Plan& plan, const std::optional<model::SectionInfo>& fresh)
    {
        // Clips crossing a cut are split there first, so each stretch holds whole clips.
        for (const double t : model::cutPoints(plan))
        {
            std::vector<std::uint64_t> crossing;
            for (const auto& track : session.snapshot()->tracks)
                for (const auto& c : track.clips)
                    if (c.start < t - kEpsilon && c.end() > t + kEpsilon)
                        crossing.push_back(c.id);
            if (!crossing.empty())
            {
                session.splitClips(crossing, t);
                session.updateSnapshotNow();
            }
        }
        const auto s = session.snapshot();
        model::SongContent content;
        content.sections = s->sections;
        for (const auto& track : s->tracks)
            for (const auto& c : track.clips)
                content.clips.push_back({.id = c.id, .track = track.id, .start = c.start, .end = c.end()});
        for (const auto& lane : s->automationLanes)
            content.lanes.push_back({.id = lane.id, .points = lane.points});
        auto result = model::applyPlan(content, plan, fresh);

        // Where each clip is now, to place it or its copy.
        const auto clipOf =
            [&s](std::uint64_t id) -> std::pair<const model::TrackInfo*, const model::ClipInfo*>
        {
            for (const auto& track : s->tracks)
                for (const auto& c : track.clips)
                    if (c.id == id)
                        return {&track, &c};
            return {nullptr, nullptr};
        };
        const auto placed = [&](const model::PlanResult::Place& p)
        {
            const auto [track, clip] = clipOf(p.clip);
            return model::ClipEdit{.clip = p.clip,
                                   .track = track != nullptr ? track->id : 0,
                                   .start = p.start,
                                   .length = clip != nullptr ? clip->length : 0.0,
                                   .offset = clip != nullptr ? clip->offset : 0.0};
        };
        // What goes first, then what moves (all at once: none lands on another), then the copies
        // into the room made for them.
        if (!result.deletes.empty())
            session.deleteClips(result.deletes);
        std::vector<model::ClipEdit> moves, copies;
        for (const auto& m : result.moves)
            moves.push_back(placed(m));
        for (const auto& c : result.copies)
            copies.push_back(placed(c));
        if (!moves.empty())
            session.applyClipEdits(moves);
        if (!copies.empty())
            session.duplicateClips(copies);
        for (const auto& lane : result.lanes)
            session.setLanePoints(lane.id, lane.points);

        // The sections: placed, made or gone.
        auto& arranger = arrangerOf(edit);
        auto* um = &edit.getUndoManager();
        std::set<std::uint64_t> kept;
        for (auto& section : result.sections)
        {
            if (section.id != 0)
            {
                kept.insert(section.id);
                if (auto* clip = sectionClip(edit, section.id))
                    clip->setPosition({rangeOf(section.start, section.end), te::TimeDuration()});
                continue;
            }
            if (auto* clip = arranger.insertNewClip(te::TrackItem::Type::arranger, juce::String(section.name),
                                                    rangeOf(section.start, section.end), nullptr))
            {
                clip->state.setProperty(Session::Impl::colourPropertyId, section.colour, um);
                section.id = clip->itemID.getRawID();
            }
        }
        for (const auto& old : content.sections)
            if (!kept.contains(old.id))
                if (auto* clip = sectionClip(edit, old.id))
                    clip->removeFromParent();
        return result;
    }
};
} // namespace

bool Session::moveSections(const std::vector<std::uint64_t>& sections, double to)
{
    updateSnapshotNow(); // plans read the project as it is now
    const auto s = snapshot();
    const auto span = model::spanOf(s->sections, sections);
    if (!span)
        return false;
    const auto plan = model::planMove(span->first, span->second, to);
    if (plan.empty())
        return false;
    EditGroup group(*this, "Move section " + describe(*s, sections));
    PlanRun{*this, *impl_->edit}.run(plan, std::nullopt);
    return true;
}

std::vector<std::uint64_t> Session::copySections(const std::vector<std::uint64_t>& sections, double to)
{
    updateSnapshotNow(); // plans read the project as it is now
    const auto s = snapshot();
    const auto span = model::spanOf(s->sections, sections);
    if (!span)
        return {};
    const auto plan = model::planCopy(span->first, span->second, to);
    if (plan.empty())
        return {};
    EditGroup group(*this, "Copy section " + describe(*s, sections));
    const auto before = s->sections;
    const auto result = PlanRun{*this, *impl_->edit}.run(plan, std::nullopt);
    std::vector<std::uint64_t> copies;
    for (const auto& section : result.sections)
        if (std::none_of(before.begin(), before.end(), [&](const auto& b) { return b.id == section.id; }))
            copies.push_back(section.id);
    return copies;
}

bool Session::deleteSections(const std::vector<std::uint64_t>& sections, bool keepMusic)
{
    updateSnapshotNow(); // plans read the project as it is now
    const auto s = snapshot();
    std::vector<std::uint64_t> found;
    for (const auto id : sections)
        if (s->section(id) != nullptr)
            found.push_back(id);
    if (found.empty())
        return false;
    if (keepMusic)
    {
        impl_->beginEdit(found.size() > 1 ? "Remove section marks" : "Remove section mark");
        for (const auto id : found)
            if (auto* clip = sectionClip(*impl_->edit, id))
                clip->removeFromParent();
        return true;
    }
    const auto span = model::spanOf(s->sections, found);
    EditGroup group(*this, "Delete section " + describe(*s, found));
    // The marks of every section in the span go with its music (a run's gaps included).
    PlanRun{*this, *impl_->edit}.run(model::planDelete(span->first, span->second), std::nullopt);
    return true;
}

std::uint64_t Session::insertSection(double at, double length, const std::string& name)
{
    updateSnapshotNow(); // plans read the project as it is now
    const auto s = snapshot();
    const auto plan = model::planInsert(at, length);
    const bool insideOne = std::any_of(s->sections.begin(), s->sections.end(), [at](const auto& x)
                                       { return x.start < at - kEpsilon && x.end > at + kEpsilon; });
    if (plan.empty() || insideOne)
        return 0; // inside a section: it would cut it in two
    const auto trimmed = juce::String(name).trim();
    model::SectionInfo fresh{.name = trimmed.isNotEmpty() ? trimmed.toStdString()
                                                          : model::newSectionName(s->sections),
                             .colour = model::newSectionColour(s->sections, Impl::paletteSize)};
    EditGroup group(*this, "Insert section");
    const auto result = PlanRun{*this, *impl_->edit}.run(plan, fresh);
    for (const auto& section : result.sections)
        if (std::abs(section.start - at) <= kEpsilon && section.name == fresh.name)
            return section.id;
    return 0;
}
} // namespace fd::engine
