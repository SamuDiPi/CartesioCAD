#include "core/DocCompare.h"

#include "core/QuickSelect.h"

#include <cctype>
#include <map>
#include <sstream>

namespace cartesio {

namespace {

std::string spaceName(const Document& doc, int space)
{
    if (space <= 0)
        return "Modello";
    if (const Layout* l = doc.findLayout(space))
        return "layout " + l->name;
    return "layout ?";
}

std::string layerName(const Document& doc, int layer)
{
    if (layer >= 0 && layer < static_cast<int>(doc.layers().size()))
        return doc.layers()[static_cast<std::size_t>(layer)].name;
    return "?";
}

// I nomi dei tipi di linea non distinguono maiuscole e minuscole (CONTINUOUS = Continuous).
std::string upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string layerProps(const Layer& l)
{
    std::ostringstream out;
    out << "colore " << l.color << (l.visible ? "" : ", spento") << (l.frozen ? ", congelato" : "")
        << (l.locked ? ", bloccato" : "") << ", tipo linea " << upper(l.linetype) << ", spessore " << l.lineweight
        << (l.plot ? "" : ", non stampabile");
    return out.str();
}


void addText(DocSummary& s, const std::string& text)
{
    if (!text.empty())
        ++s.texts[text];
}

} // namespace

DocSummary summarizeDocument(const Document& doc)
{
    DocSummary s;
    for (const auto& e : doc.entities()) {
        ++s.total;
        ++s.objects[entityTypeName(e->type()) + " | " + spaceName(doc, e->space()) + " | " + layerName(doc, e->layer())];
        switch (e->type()) {
        case EntityType::Text:
            addText(s, static_cast<const TextEntity&>(*e).text);
            break;
        case EntityType::MText:
            addText(s, static_cast<const MTextEntity&>(*e).text);
            break;
        case EntityType::Insert:
            for (const auto& a : static_cast<const InsertEntity&>(*e).attributes)
                addText(s, a.tag + "=" + a.text.text);
            break;
        default:
            break;
        }
    }
    for (const Layer& l : doc.layers())
        s.layers[l.name] = layerProps(l);
    for (const auto& b : doc.blocks())
        if (!b->name.empty() && b->name[0] != '*')   // gli anonimi (quote, blocchi dinamici) cambiano nome
            s.blocks[b->name] = static_cast<long long>(b->entities.size());
    for (const Layout& l : doc.layouts())
        s.layouts[l.name] = 1;
    return s;
}

namespace {

template <typename Map>
void compareCounts(const Map& a, const Map& b, const std::string& prefix, std::vector<DocDifference>& out)
{
    for (const auto& [key, n] : a) {
        const auto it = b.find(key);
        const long long m = it == b.end() ? 0 : it->second;
        if (m != n)
            out.push_back({prefix + key, n, m, {}});
    }
    for (const auto& [key, m] : b)
        if (!a.count(key))
            out.push_back({prefix + key, 0, m, {}});
}

} // namespace

DocComparison compareDocuments(const Document& before, const Document& after)
{
    return compareSummaries(summarizeDocument(before), summarizeDocument(after));
}

DocComparison compareSummaries(const DocSummary& a, const DocSummary& b)
{
    DocComparison c;
    c.entitiesBefore = a.total;
    c.entitiesAfter = b.total;
    compareCounts(a.layouts, b.layouts, "Layout ", c.differences);
    for (const auto& [name, props] : a.layers) {
        const auto it = b.layers.find(name);
        if (it == b.layers.end())
            c.differences.push_back({"Layer " + name, 1, 0, "mancante"});
        else if (it->second != props)
            c.differences.push_back({"Layer " + name, 1, 1, props + "  ->  " + it->second});
    }
    for (const auto& [name, props] : b.layers)
        if (!a.layers.count(name))
            c.differences.push_back({"Layer " + name, 0, 1, "in più"});
    compareCounts(a.blocks, b.blocks, "Blocco ", c.differences);
    compareCounts(a.objects, b.objects, "Oggetti ", c.differences);
    compareCounts(a.texts, b.texts, "Testo ", c.differences);
    return c;
}

std::string DocComparison::report(std::size_t maxLines) const
{
    std::ostringstream out;
    out << "Oggetti: " << entitiesBefore << " prima, " << entitiesAfter << " dopo. ";
    if (differences.empty()) {
        out << "Nessuna differenza.";
        return out.str();
    }
    out << differences.size() << " differenze:";
    for (std::size_t i = 0; i < differences.size() && i < maxLines; ++i) {
        const DocDifference& d = differences[i];
        out << "\n  " << d.what << ": " << d.before << " -> " << d.after;
        if (!d.detail.empty())
            out << " (" << d.detail << ")";
    }
    if (differences.size() > maxLines)
        out << "\n  ... e altre " << differences.size() - maxLines << ".";
    return out.str();
}

} // namespace cartesio
