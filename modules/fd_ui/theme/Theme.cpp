namespace fd::ui
{
namespace
{
juce::Colour hex(juce::uint32 rgb)
{
    return juce::Colour(0xff000000u | rgb);
}

/// Shared by all candidates: deep neutral surfaces, text, meters and clip colours.
Theme base()
{
    Theme t;
    t.surface0 = hex(0x060708);
    t.surface1 = hex(0x0B0C0E);
    t.surface2 = hex(0x111215);
    t.surface3 = hex(0x1C1E22);
    t.line = hex(0x202226);
    t.textPrimary = hex(0xE4E6E9);
    t.textSecondary = hex(0x9A9EA6);
    t.textTertiary = hex(0x5D6168);
    t.accentInstrument = hex(0xE9B44C);
    t.accentMidi = hex(0xA88BEB);
    t.accentModulation = hex(0x4FD1A5);
    t.record = hex(0xF0505A);
    t.meterNormal = hex(0x45C97A);
    t.meterHot = hex(0xE8C547);
    t.meterClip = hex(0xF0505A);
    t.clipColours = {hex(0xD9824A), hex(0xD4B04A), hex(0x7DBA5A), hex(0x46B3A3),
                     hex(0x4F8FD6), hex(0x8E74D6), hex(0xCC6699), hex(0xC95454)};
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
    t.description = "Slightly cool greys, blue values and active states";
    t.surface0 = hex(0x060709);
    t.surface1 = hex(0x0A0C0F);
    t.surface2 = hex(0x101317);
    t.surface3 = hex(0x1A1E24);
    t.line = hex(0x1F242B);
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
