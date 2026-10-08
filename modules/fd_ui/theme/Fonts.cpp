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
    return juce::Font(juce::FontOptions(typeface(weight)).withPointHeight(sizePx));
}

juce::Font mono(float sizePx, Weight weight)
{
    return juce::Font(juce::FontOptions(monoTypeface(weight)).withPointHeight(sizePx));
}
} // namespace fd::ui::fonts
