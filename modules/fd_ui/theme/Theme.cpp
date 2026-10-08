namespace fd::ui
{
namespace
{
juce::Colour hex(juce::uint32 rgb)
{
    return juce::Colour(0xff000000u | rgb);
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
    return t;
}

const Theme* gCurrent = nullptr;
} // namespace

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
