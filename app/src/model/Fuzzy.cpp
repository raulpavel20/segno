#include "model/Fuzzy.h"

#include <algorithm>
#include <cctype>

namespace fd::model
{
namespace
{
char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool isWordStart(std::string_view text, size_t i)
{
    return i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1]));
}
} // namespace

std::optional<int> fuzzyScore(std::string_view query, std::string_view text)
{
    if (query.empty())
        return 0;
    int score = 0;
    size_t pos = 0;
    size_t last = std::string_view::npos;
    for (char qc : query)
    {
        if (qc == ' ')
            continue; // spaces in the query only separate words
        const char q = lower(qc);
        // Prefer the next word start that matches, then any next match.
        size_t found = std::string_view::npos;
        for (size_t i = pos; i < text.size(); ++i)
            if (lower(text[i]) == q && isWordStart(text, i))
            {
                found = i;
                break;
            }
        size_t any = pos;
        while (any < text.size() && lower(text[any]) != q)
            ++any;
        if (any >= text.size())
            return std::nullopt;
        // Take the word start only if nothing consecutive is available.
        const bool consecutive = last != std::string_view::npos && any == last + 1;
        const size_t i = (!consecutive && found != std::string_view::npos) ? found : any;

        score += 1;
        if (isWordStart(text, i))
            score += 6;
        if (last != std::string_view::npos && i == last + 1)
            score += 4;
        if (last != std::string_view::npos)
            score -= static_cast<int>(std::min<size_t>(i - last - 1, 5)); // small gap penalty
        last = i;
        pos = i + 1;
    }
    if (text.size() >= query.size() &&
        std::equal(query.begin(), query.end(), text.begin(), [](char a, char b) { return lower(a) == lower(b); }))
        score += 10; // a prefix of the whole text
    return score;
}

std::vector<size_t> rankPalette(std::string_view query, const std::vector<PaletteItem>& items,
                                const std::vector<std::string>& recent)
{
    auto recency = [&](const std::string& id) {
        const auto it = std::find(recent.begin(), recent.end(), id);
        return it == recent.end() ? recent.size() : static_cast<size_t>(it - recent.begin());
    };
    struct Ranked
    {
        size_t index;
        int score;
        size_t recency;
    };
    std::vector<Ranked> ranked;
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto score = fuzzyScore(query, items[i].name);
        // Matching the category as well ("transport play") counts, slightly lower.
        if (const auto withCategory = fuzzyScore(query, items[i].category + " " + items[i].name))
            score = std::max(score.value_or(*withCategory - 2), *withCategory - 2);
        if (score)
            ranked.push_back({i, *score, recency(items[i].id)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [&](const Ranked& a, const Ranked& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.recency != b.recency)
            return a.recency < b.recency;
        return items[a.index].name < items[b.index].name;
    });
    std::vector<size_t> out;
    for (const auto& r : ranked)
        out.push_back(r.index);
    return out;
}
} // namespace fd::model
