namespace fd::ui
{
TapFeed::TapFeed(Device& device, Song song)
    : device_(device), tap_(device.analysis().notes()), song_(std::move(song)),
      followParam_(device.params().indexOf("follow"))
{
}

int TapFeed::follow() const
{
    return followParam_ ? device_.params().getInt(*followParam_) : 0;
}

void TapFeed::drain() const
{
    if (tap_ == nullptr)
        return;
    NoteTap::Mark m;
    while (tap_->pop(m))
    {
        using T = NoteTap::Mark::Type;
        const int slot = m.key + (m.out ? 128 : 0);
        const auto close = [&](int s, double at)
        {
            if (const auto it = open_.find(s); it != open_.end())
            {
                notes_.push_back({it->second.start, std::max(at, it->second.start), s % 128,
                                  it->second.velocity, s >= 128});
                open_.erase(it);
            }
        };
        switch (m.type)
        {
        case T::NoteOn:
            close(slot, m.beat); // played again over itself
            open_[slot] = {m.beat, m.velocity};
            break;
        case T::NoteOff:
            close(slot, m.beat);
            break;
        case T::AllOff:
            while (!open_.empty())
                close(open_.begin()->first, m.beat);
            break;
        case T::Chord:
            if (!followed_.empty() && std::abs(followed_.back().start - m.beat) < 1e-9)
                followed_.pop_back();
            followed_.push_back({m.beat, m.root, m.notes, m.fallback});
            break;
        }
    }
}

juce::String TapFeed::chordName(int root, std::uint16_t notes, const music::Key* key)
{
    // Its pitch classes from the root up: read in root position.
    std::vector<int> pitches;
    for (int i = 0; i < 12; ++i)
        if ((notes >> ((root + i) % 12)) & 1u)
            pitches.push_back(48 + root + i);
    if (const auto chord = music::nameChord(pitches))
        return juce::String::fromUTF8(music::chordName(*chord, key).c_str());
    return juce::String::fromUTF8(music::pitchClassName(key, root).c_str());
}

std::optional<music::Key> TapFeed::key() const
{
    const int root = tap_->keyRoot();
    for (const auto& s : music::scales())
        if (music::rotated(s.intervals, root) == tap_->keyNotes())
            return music::Key{root, std::string(s.id)};
    return std::nullopt;
}

juce::String TapFeed::keyName() const
{
    if (const auto k = key())
        return juce::String::fromUTF8(music::keyName(*k).c_str());
    return juce::String::fromUTF8(music::pitchClassName(nullptr, tap_->keyRoot()).c_str());
}

NoteFeed::View TapFeed::view(double before, double after) const
{
    drain();
    View v;
    if (tap_ == nullptr)
        return v;
    v.now = tap_->now();
    v.beatsPerBar = tap_->beatsPerBar();
    // The playhead went back (a jump, a loop): what was kept is another time's.
    if (v.now < last_ - 1.0)
    {
        notes_.clear();
        open_.clear();
        if (followed_.size() > 1)
            followed_.erase(followed_.begin(), followed_.end() - 1);
    }
    last_ = v.now;
    const double kept = v.now - keptBars * v.beatsPerBar;
    std::erase_if(notes_, [kept](const Note& n) { return n.end < kept; });
    while (followed_.size() > 1 && followed_[1].start < kept)
        followed_.erase(followed_.begin());

    const double from = v.now - before, to = v.now + after;
    for (const auto& n : notes_)
        if (n.end > from && n.start < to)
            v.notes.push_back(n);
    for (const auto& [slot, o] : open_) // still sounding: up to now
        v.notes.push_back({o.start, std::max(o.start, v.now), slot % 128, o.velocity, slot >= 128});

    // The key it follows names the chords.
    const auto followedKey = key();
    const auto* k = followedKey ? &*followedKey : nullptr;

    // The chords: the song's, past and ahead, while it follows the song; else those it followed.
    const auto song = song_.view ? song_.view() : nullptr;
    const int follows = follow();
    if (song != nullptr && follows == 0)
    {
        for (const auto& c : song->chords)
            if (c.end > from && c.start < to && c.notes != 0)
                v.chords.push_back({c.start, c.end, chordName(c.root, c.notes, k)});
        if (const auto* s = song->sectionAt(v.now); s != nullptr && s->end <= to)
            v.sectionEnd = s->end;
    }
    else
        for (std::size_t i = 0; i < followed_.size(); ++i)
        {
            const auto& f = followed_[i];
            const double end = i + 1 < followed_.size() ? followed_[i + 1].start : to; // the last goes on
            if (!f.fallback && f.notes != 0 && end > from && f.start < to)
                v.chords.push_back({f.start, end, chordName(f.root, f.notes, k)});
        }

    // What it follows, and why there's no chord now.
    static const std::array<juce::String, 4> names{"Song", "Track", "Played", "Own"};
    v.following = names[static_cast<std::size_t>(std::clamp(follows, 0, 3))];
    if (follows == 1 && song_.listened)
        v.following = song_.listened();
    const auto* now = followed_.empty() ? nullptr : &followed_.back();
    if (follows == 0 && !tap_->hasSong()) // a plugin host: no song to follow
    {
        v.following = "Own key";
        v.fallback = "No song here: " + keyName();
    }
    else if (follows == 3 && now != nullptr)
        v.fallback = "Own chord: " + chordName(now->root, now->notes, k);
    else if (now != nullptr && now->fallback)
        v.fallback = "No chords here: the key's " + chordName(now->root, now->notes, k);
    return v;
}
} // namespace fd::ui
