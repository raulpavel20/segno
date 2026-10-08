#include <FdUiAssets.h>

namespace fd::ui::fonts
{
juce::Typeface::Ptr typeface(Weight weight)
{
    static juce::Typeface::Ptr base = juce::Typeface::createSystemTypefaceFor(fd_assets::InterVariable_ttf,
                                                                             static_cast<size_t>(fd_assets::InterVariable_ttfSize));
    static std::map<int, juce::Typeface::Ptr> cache;

    const int w = static_cast<int>(weight);
    if (auto it = cache.find(w); it != cache.end())
        return it->second;

    juce::Typeface::Ptr tf = base;
    if (base != nullptr)
    {
        const juce::FontVariableSetting setting{"wght", static_cast<float>(w)};
        if (auto variant = base->cloneWithVariableSettings(juce::Span<const juce::FontVariableSetting>(&setting, 1)))
            tf = variant;
    }
    cache[w] = tf;
    return tf;
}

juce::Font font(float sizePx, Weight weight, bool tabular)
{
    auto options = juce::FontOptions(typeface(weight)).withPointHeight(sizePx);
    if (tabular)
        options = options.withFeatureEnabled("tnum");
    return juce::Font(options);
}
} // namespace fd::ui::fonts
