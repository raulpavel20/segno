#include <FdUiAssets.h>

namespace fd::ui::fonts
{
namespace
{
/// `base` at `weight` on its 'wght' axis, cached per weight.
juce::Typeface::Ptr variant(const juce::Typeface::Ptr& base, std::map<int, juce::Typeface::Ptr>& cache, Weight weight)
{
    const int w = static_cast<int>(weight);
    if (auto it = cache.find(w); it != cache.end())
        return it->second;

    juce::Typeface::Ptr tf = base;
    if (base != nullptr)
    {
        const juce::FontVariableSetting setting{"wght", static_cast<float>(w)};
        if (auto v = base->cloneWithVariableSettings(juce::Span<const juce::FontVariableSetting>(&setting, 1)))
            tf = v;
    }
    cache[w] = tf;
    return tf;
}
} // namespace

juce::Typeface::Ptr typeface(Weight weight)
{
    static juce::Typeface::Ptr base = juce::Typeface::createSystemTypefaceFor(fd_assets::InterVariable_ttf,
                                                                             static_cast<size_t>(fd_assets::InterVariable_ttfSize));
    static std::map<int, juce::Typeface::Ptr> cache;
    return variant(base, cache, weight);
}

juce::Typeface::Ptr monoTypeface(Weight weight)
{
    static juce::Typeface::Ptr base = juce::Typeface::createSystemTypefaceFor(
        fd_assets::JetBrainsMonoVariable_ttf, static_cast<size_t>(fd_assets::JetBrainsMonoVariable_ttfSize));
    static std::map<int, juce::Typeface::Ptr> cache;
    return variant(base, cache, weight);
}

juce::Font font(float sizePx, Weight weight)
{
    // Inter has no music signs: ♯ and ♭ (a key's notes, key-and-scale.md §4) come from the bundled
    // mono, never from whatever the system has.
    static const auto mono = monoTypeface(Weight::Regular); // registered, so found by its name
    auto options = juce::FontOptions(typeface(weight)).withPointHeight(sizePx);
    return juce::Font(mono != nullptr ? options.withFallbacks({mono->getName()}) : options);
}

juce::Font mono(float sizePx, Weight weight)
{
    return juce::Font(juce::FontOptions(monoTypeface(weight)).withPointHeight(sizePx));
}
} // namespace fd::ui::fonts

namespace fd::ui
{
juce::Font font(TextStyle style)
{
    using fonts::Weight;
    const auto& t = Theme::current();
    switch (style)
    {
    case TextStyle::Title: return fonts::font(t.textSm, Weight::SemiBold);
    case TextStyle::Name: return fonts::font(t.textSm, Weight::Medium);
    case TextStyle::Body: return fonts::font(t.textSm);
    case TextStyle::Label: return fonts::font(t.textXs, Weight::Medium);
    case TextStyle::Caption: return fonts::font(t.textXs);
    case TextStyle::Control: return fonts::font(t.textXs, Weight::SemiBold);
    case TextStyle::Field: return fonts::font(t.textMd);
    case TextStyle::Section: return fonts::mono(t.textXs - 1.0f, Weight::SemiBold);
    case TextStyle::Value: return fonts::mono(t.textXs, Weight::Medium);
    case TextStyle::ValueSmall: return fonts::mono(t.knobValueSize);
    case TextStyle::ValueMedium: return fonts::mono(t.textMd, Weight::Medium);
    case TextStyle::ValueLarge: return fonts::mono(t.textLg, Weight::Medium);
    case TextStyle::Micro: return fonts::mono(t.textXs - 2.0f, Weight::SemiBold);
    case TextStyle::Wordmark: return fonts::mono(t.textLg, Weight::Bold).withExtraKerningFactor(0.16f);
    }
    return fonts::font(t.textSm);
}
} // namespace fd::ui
