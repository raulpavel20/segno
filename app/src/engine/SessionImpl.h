#pragma once

#include "engine/Session.h"

#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

struct Session::Impl final : private juce::ValueTree::Listener, private juce::AsyncUpdater
{
    Impl(Session& owner, te::Engine& engine);
    ~Impl() override;

    void rebuildSnapshot();
    [[nodiscard]] te::AudioTrack* track(std::uint64_t id) const;
    [[nodiscard]] te::Clip* clip(std::uint64_t id) const;
    void beginEdit() { edit->getUndoManager().beginNewTransaction(); }

    Session& owner;
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<const model::Snapshot> snapshot;
    std::uint64_t version = 0;

private:
    void changed(juce::ValueTree& tree);
    void handleAsyncUpdate() override { rebuildSnapshot(); }
    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier& id) override
    {
        if (id != te::IDs::lastSignificantChange) // tracktion's own bookkeeping
            changed(t);
    }
    void valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree&) override { changed(t); }
    void valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree&, int) override { changed(t); }
    void valueTreeChildOrderChanged(juce::ValueTree& t, int, int) override { changed(t); }
};
} // namespace fd::engine
