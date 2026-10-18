#pragma once

namespace fd::ui
{
/// A MIDI effect's note display, fed from its NoteTap (docs/design/midi-effects.md §12), the same in
/// every host: what came in and went out and the chord it followed, kept for the last 32 bars. A
/// host with a song adds what's ahead: the song's chords coming, and where the section ends.
/// Message thread only.
class TapFeed final : public NoteFeed
{
public:
    struct Song
    {
        /// The song the module was last handed: its chords, past and ahead, and its sections.
        std::function<std::shared_ptr<const SongView>()> view;
        /// The name of the track it hears (Follow ▸ Track).
        std::function<juce::String()> listened;
    };

    /// `device` a MIDI effect (its analysis bus has a note tap); it outlives the feed.
    explicit TapFeed(Device& device, Song song = {});

    [[nodiscard]] View view(double before, double after) const override;

    /// How long it remembers, in bars.
    static constexpr double keptBars = 32.0;

private:
    struct Open
    {
        double start = 0.0;
        float velocity = 0.0f;
    };
    struct Followed
    {
        double start = 0.0;
        int root = -1;
        std::uint16_t notes = 0;
        bool fallback = false;
    };

    void drain() const;
    [[nodiscard]] int follow() const;
    /// The key it follows, as a scale of the catalogue (none for a set no scale has).
    [[nodiscard]] std::optional<music::Key> key() const;
    [[nodiscard]] juce::String keyName() const;
    [[nodiscard]] static juce::String chordName(int root, std::uint16_t notes, const music::Key* key);

    Device& device_;
    NoteTap* tap_;
    Song song_;
    std::optional<std::size_t> followParam_;
    mutable std::vector<Note> notes_;
    mutable std::map<int, Open> open_; // by key, the notes it played after 128
    mutable std::vector<Followed> followed_;
    mutable double last_ = 0.0;
};
} // namespace fd::ui
