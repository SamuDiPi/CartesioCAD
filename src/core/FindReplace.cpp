#include "core/FindReplace.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace cartesio {

namespace {

// Minuscole per il confronto (ASCII; le lettere accentate UTF-8 restano come sono, va bene per le sigle).
std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isWordChar(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

int spaceOfEntity(const Entity& e)
{
    return e.space() < 0 ? 0 : e.space();
}

} // namespace

std::vector<std::size_t> findInString(const std::string& text, const std::string& query, const FindOptions& options)
{
    std::vector<std::size_t> out;
    if (query.empty())
        return out;
    const std::string hay = options.matchCase ? text : lower(text);
    const std::string needle = options.matchCase ? query : lower(query);
    for (std::size_t pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + 1)) {
        if (options.wholeWord) {
            const bool before = pos > 0 && isWordChar(hay[pos - 1]);
            const bool after = pos + needle.size() < hay.size() && isWordChar(hay[pos + needle.size()]);
            if (before || after)
                continue;
        }
        out.push_back(pos);
    }
    return out;
}

std::vector<FindMatch> findText(const Document& doc, const std::string& query, const FindOptions& options)
{
    std::vector<FindMatch> out;
    if (query.empty())
        return out;
    const std::unordered_set<EntityId> only(options.only.begin(), options.only.end());
    const auto consider = [&](const Entity& e, int attribute, const std::string& kind, const std::string& text, BBox box) {
        if (findInString(text, query, options).empty())
            return;
        out.push_back({e.id(), attribute, spaceOfEntity(e), kind, text, box});
    };
    for (const auto& e : doc.entities()) {
        if (options.space >= 0 && spaceOfEntity(*e) != options.space)
            continue;
        if (!only.empty() && !only.count(e->id()))
            continue;
        switch (e->type()) {
        case EntityType::Text:
            if (options.texts)
                consider(*e, -1, "Testo", static_cast<const TextEntity&>(*e).text, e->bounds());
            break;
        case EntityType::MText:
            if (options.texts)
                consider(*e, -1, "Testo multilinea", static_cast<const MTextEntity&>(*e).text, e->bounds());
            break;
        case EntityType::Insert:
            if (options.attributes) {
                const auto& insert = static_cast<const InsertEntity&>(*e);
                for (std::size_t i = 0; i < insert.attributes.size(); ++i)
                    consider(*e, static_cast<int>(i), "Attributo " + insert.attributes[i].tag,
                             insert.attributes[i].text.text, insert.attributes[i].text.bounds());
            }
            break;
        case EntityType::Dimension:
            if (options.dimensions)
                consider(*e, -1, "Quota", static_cast<const DimensionEntity&>(*e).textOverride, e->bounds());
            break;
        case EntityType::Leader:
            if (options.dimensions)
                consider(*e, -1, "Direttrice", static_cast<const LeaderEntity&>(*e).text, e->bounds());
            break;
        default:
            break;
        }
    }
    return out;
}

int replaceText(Document& doc, const std::vector<FindMatch>& matches, const std::string& query,
                const std::string& replacement, const FindOptions& options)
{
    // Una copia per oggetto (un inserimento può avere più attributi trovati).
    std::vector<std::unique_ptr<Entity>> copies;
    std::vector<EntityId> ids;
    int count = 0;
    const auto copyOf = [&](EntityId id) -> Entity* {
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == id)
                return copies[i].get();
        const Entity* e = doc.find(id);
        if (!e)
            return nullptr;
        ids.push_back(id);
        copies.push_back(e->clone());
        return copies.back().get();
    };
    const auto replaceIn = [&](std::string& text) {
        const auto positions = findInString(text, query, options);
        // Dalla fine, così le posizioni precedenti restano valide.
        for (auto it = positions.rbegin(); it != positions.rend(); ++it) {
            text.replace(*it, query.size(), replacement);
            ++count;
        }
    };
    for (const FindMatch& m : matches) {
        Entity* e = copyOf(m.id);
        if (!e)
            continue;
        switch (e->type()) {
        case EntityType::Text: replaceIn(static_cast<TextEntity&>(*e).text); break;
        case EntityType::MText: replaceIn(static_cast<MTextEntity&>(*e).text); break;
        case EntityType::Insert: {
            auto& insert = static_cast<InsertEntity&>(*e);
            if (m.attribute >= 0 && m.attribute < static_cast<int>(insert.attributes.size()))
                replaceIn(insert.attributes[static_cast<std::size_t>(m.attribute)].text.text);
            break;
        }
        case EntityType::Dimension: replaceIn(static_cast<DimensionEntity&>(*e).textOverride); break;
        case EntityType::Leader: replaceIn(static_cast<LeaderEntity&>(*e).text); break;
        default: break;
        }
    }
    if (count > 0)
        doc.commit(std::move(copies), ids);
    return count;
}

} // namespace cartesio
