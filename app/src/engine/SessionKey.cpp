// The song's key (M3 P3, ADR 0056, docs/design/key-and-scale.md): stored in the edit's pitch
// sequence (SongKey.h), one undo step per change, or one for a Key popover's whole choice.

#include "engine/SessionImpl.h"
#include "engine/SongKey.h"
#include "model/SongKey.h"

namespace fd::engine
{
bool Session::setKey(const std::optional<music::Key>& key)
{
    if (key && music::scaleOf(*key) == nullptr)
        return false;
    auto& impl = *impl_;
    std::optional<music::Key> normalised = key;
    if (normalised)
        normalised->root = ((normalised->root % 12) + 12) % 12;
    if (songKeyOf(*impl.edit) == normalised)
        return true;

    const auto name = juce::String(model::keyStepName(normalised));
    auto& um = impl.edit->getUndoManager();
    if (impl.keyChoices > 0 && impl.keyStepOpen && impl.groupDepth == 0)
        um.setCurrentTransactionName(name); // the popover's step goes on, named after the last key
    else
    {
        impl.beginEdit(name);
        impl.keyStepOpen = impl.keyChoices > 0;
    }
    writeSongKey(*impl.edit, normalised);
    return true;
}

Session::KeyChoice::KeyChoice(Session& session) : session_(session)
{
    auto& impl = session_.impl();
    if (impl.keyChoices++ == 0)
    {
        impl.keyBefore = songKeyOf(*impl.edit);
        impl.keyStepOpen = false;
        impl.keyInhibitor = std::make_unique<te::Edit::UndoTransactionInhibitor>(*impl.edit);
    }
}

Session::KeyChoice::~KeyChoice()
{
    auto& impl = session_.impl();
    if (--impl.keyChoices > 0)
        return;
    // Back where it began: no step at all.
    if (impl.keyStepOpen && songKeyOf(*impl.edit) == impl.keyBefore)
        impl.edit->getUndoManager().undoCurrentTransactionOnly();
    impl.keyStepOpen = false;
    impl.keyInhibitor.reset();
}

std::vector<music::KeyCandidate> Session::detectKeys(const std::vector<std::uint64_t>& clips, int count) const
{
    const auto s = snapshot();
    const auto notes = model::notesForKeyDetection(*s, clips);
    return music::detectKeys(notes, count);
}
} // namespace fd::engine
