namespace fd::ui
{
namespace
{
juce::Colour hex(juce::uint32 rgb)
{
    return juce::Colour(0xff000000u | rgb);
}

/// Every role, from the base palette (docs/design/ui-foundations.md). Called last by each theme.
void deriveRoles(Theme& t)
{
    auto& r = t.roles;
    r.lane = t.surface0.interpolatedWith(t.surface1, 0.45f);
    r.laneSelected = t.surface1.interpolatedWith(t.surface2, 0.4f);
    r.subLane = t.surface0.interpolatedWith(t.surface1, 0.35f);
    r.strip = t.surface0.interpolatedWith(t.surface1, 0.5f);
    r.keyRowLight = t.surface1.interpolatedWith(t.surface2, 0.7f);
    r.keyRowDark = t.surface0.interpolatedWith(t.surface1, 0.3f);
    r.shade = t.surface0.withAlpha(0.55f);
    r.gridBar = t.surface3;
    r.gridBeat = t.surface3.withAlpha(0.55f);
    r.gridSub = t.surface3.withAlpha(0.28f);
    r.gridOctave = t.line;
    r.gridMinor = t.surface3.withAlpha(0.35f);
    r.selectionFill = t.active.withAlpha(0.10f);
    r.selectionStroke = t.active.withAlpha(0.6f);
    r.dropTarget = t.active.withAlpha(0.12f);
    r.textHighlight = t.active.withAlpha(0.35f);
    r.playhead = t.textPrimary;
    r.marker = t.textPrimary.withAlpha(0.8f);
    r.loopInactive = t.textTertiary.withAlpha(0.7f);
    r.onClip = t.surface0;
    r.keyWhite = t.textTertiary.interpolatedWith(t.textSecondary, 0.25f);
    r.keyBlack = t.surface0;
    r.keySeam = t.surface1.withAlpha(0.6f);
    r.curveFill = t.value.withAlpha(r.fillAlpha);
    r.selectionCurve = t.active.withAlpha(0.45f);
    r.spectrumFill = t.textTertiary.withAlpha(0.22f);
    r.spectrumLine = t.textTertiary.withAlpha(0.5f);
    r.spectrumPre = t.textTertiary.withAlpha(0.35f);
    r.outside = t.textTertiary.withAlpha(0.6f);
    r.loopTint = t.value.withAlpha(0.10f);
    r.focusRing = t.active.withAlpha(0.7f);
    r.hoverWash = t.surface3.withAlpha(0.35f);
    r.dropWell = t.surface1.brighter(0.08f);
    r.valueHot = t.value.brighter(0.15f);
    r.valueText = t.textPrimary.withAlpha(0.85f);
    r.warning = t.meterHot.withAlpha(0.8f);
    r.overlay = t.surface3.withAlpha(0.9f);
    r.shadow = juce::Colours::black.withAlpha(0.5f);
}

/// Shared by all candidates: deep neutral surfaces (no tint), text, meters and clip colours.
Theme base()
{
    Theme t;
    t.surface0 = hex(0x070707);
    t.surface1 = hex(0x0C0C0C);
    t.surface2 = hex(0x131313);
    t.surface3 = hex(0x1E1E1E);
    t.line = hex(0x232323);
    t.textPrimary = hex(0xE6E6E6);
    t.textSecondary = hex(0x9E9E9E);
    t.textTertiary = hex(0x616161);
    t.accentInstrument = hex(0xE9B44C);
    t.accentMidi = hex(0xA88BEB);
    t.accentModulation = hex(0x4FD1A5);
    t.record = hex(0xF0505A);
    t.meterNormal = hex(0x45C97A);
    t.meterHot = hex(0xE8C547);
    t.meterClip = hex(0xF0505A);
    // 16 hues at equal perceptual lightness and chroma (OKLCH L 0.70, C 0.115), so no clip
    // shouts louder than another on the dark canvas.
    t.clipColours = {hex(0xDD8273), hex(0xD78958), hex(0xC89344), hex(0xB19F42), hex(0x92AA54), hex(0x6FB171),
                     hex(0x4CB58C), hex(0x29B5A8), hex(0x1DB2C0), hex(0x3AADD4), hex(0x5CA5E1), hex(0x809BE6),
                     hex(0x9D92E1), hex(0xB689D2), hex(0xCA82BB), hex(0xD67FA2)};
    return t;
}

Theme makeCarbon()
{
    Theme t = base();
    t.name = "Carbon";
    t.description = "Neutral greys, light-grey values, amber for active states";
    t.value = hex(0xC9CDD3);
    t.active = hex(0xF0A33C);
    t.onActive = hex(0x1A1105);
    t.effectStart = hex(0xF0A33C);
    t.effectEnd = hex(0xF06A3C);
    deriveRoles(t);
    return t;
}

Theme makeSteel()
{
    Theme t = base();
    t.name = "Steel";
    t.description = "Neutral greys, blue values and active states";
    t.value = hex(0x7DB0FF);
    t.active = hex(0x4E95F5);
    t.onActive = hex(0x050D1A);
    t.effectStart = hex(0x4E95F5);
    t.effectEnd = hex(0x9A78F0);
    deriveRoles(t);
    return t;
}

Theme makeSignal()
{
    Theme t = base();
    t.name = "Signal";
    t.description = "Neutral greys, near-white values, lime for active states";
    t.value = hex(0xDDE0E4);
    t.active = hex(0xB5EA3F);
    t.onActive = hex(0x111604);
    t.effectStart = hex(0xB5EA3F);
    t.effectEnd = hex(0x3FD6C2);
    deriveRoles(t);
    return t;
}

const Theme* gCurrent = nullptr;
} // namespace

juce::Colour Theme::clipColour(int index) const
{
    return clipColours.isEmpty() ? textTertiary : clipColours[((index % clipColours.size()) + clipColours.size()) % clipColours.size()];
}

juce::Colour Theme::clipBody(juce::Colour clip, bool selected) const
{
    return clip.withBrightness(selected ? 0.42f : 0.32f);
}

juce::Colour Theme::clipContent(juce::Colour clip) const
{
    return clip.interpolatedWith(juce::Colours::white, 0.25f);
}

juce::Colour Theme::noteFill(juce::Colour clip, float velocity, bool selected) const
{
    const auto c = surface3.interpolatedWith(clip, 0.35f + 0.65f * velocity);
    return selected ? c.interpolatedWith(juce::Colours::white, 0.3f) : c;
}

juce::Colour Theme::lift(juce::Colour c, bool hovered, bool pressed) const
{
    return c.brighter(pressed ? roles.pressedLift : (hovered ? roles.hoverLift : 0.0f));
}

const juce::Array<Theme>& Theme::candidates()
{
    // Steel won the design gate (docs/design/design-language.md). The other two stay available
    // in the gallery for comparison only.
    static const juce::Array<Theme> themes{makeSteel(), makeCarbon(), makeSignal()};
    return themes;
}

const Theme& Theme::current() noexcept
{
    return gCurrent != nullptr ? *gCurrent : candidates().getReference(0);
}

void Theme::setCurrent(const Theme& theme) noexcept
{
    gCurrent = &theme;
}
} // namespace fd::ui
