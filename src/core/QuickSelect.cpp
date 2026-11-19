#include "core/QuickSelect.h"

#include <cctype>
#include <charconv>
#include <unordered_set>

namespace cartesio {

namespace {

std::string upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::optional<double> number(const std::string& s)
{
    std::string t = s;
    for (char& c : t)
        if (c == ',')
            c = '.';
    double v = 0.0;
    const auto* begin = t.data();
    const auto* end = t.data() + t.size();
    while (begin < end && *begin == ' ')
        ++begin;
    const auto [ptr, ec] = std::from_chars(begin, end, v);
    if (ec != std::errc() || ptr == begin)
        return std::nullopt;
    return v;
}

std::string colorName(int c)
{
    if (c == kColorByLayer)
        return "DaLayer";
    if (c == kColorByBlock)
        return "DaBlocco";
    return std::to_string(c);
}

} // namespace

bool wildcardMatches(const std::string& pattern, const std::string& text)
{
    const std::string p = upper(pattern), t = upper(text);
    std::size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
            ++pi;
            ++ti;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*')
        ++pi;
    return pi == p.size();
}

std::string entityTypeName(EntityType type)
{
    switch (type) {
    case EntityType::Line: return "Linea";
    case EntityType::Circle: return "Cerchio";
    case EntityType::Arc: return "Arco";
    case EntityType::Polyline: return "Polilinea";
    case EntityType::Text: return "Testo";
    case EntityType::MText: return "Testo multilinea";
    case EntityType::Insert: return "Blocco";
    case EntityType::Hatch: return "Tratteggio";
    case EntityType::Dimension: return "Quota";
    case EntityType::Leader: return "Direttrice";
    case EntityType::Ellipse: return "Ellisse";
    case EntityType::Spline: return "Spline";
    case EntityType::Point: return "Punto";
    case EntityType::Solid: return "Solido 2D";
    case EntityType::Viewport: return "Finestra";
    case EntityType::Image: return "Immagine";
    case EntityType::Wipeout: return "Copertura";
    case EntityType::Table: return "Tabella";
    case EntityType::AttDef: return "Definizione di attributo";
    default: return "Altro";
    }
}

std::optional<std::string> propertyValue(const Document& doc, const Entity& e, const std::string& property)
{
    const auto& layers = doc.layers();
    if (property == "color")
        return e.trueColor() >= 0 ? std::string("RGB") : colorName(e.color());
    if (property == "layer") {
        const int li = e.layer();
        return li >= 0 && li < static_cast<int>(layers.size()) ? layers[static_cast<std::size_t>(li)].name : std::string("0");
    }
    if (property == "linetype")
        return e.linetype().empty() ? std::string("ByLayer") : e.linetype();
    if (property == "lineweight") {
        const int lw = e.lineweight();
        if (lw == kLineweightByLayer)
            return std::string("DaLayer");
        if (lw == kLineweightByBlock)
            return std::string("DaBlocco");
        if (lw < 0)
            return std::string("Predefinito");
        return std::to_string(lw / 100.0).substr(0, 4);   // mm
    }
    if (property == "block") {
        if (e.type() != EntityType::Insert)
            return std::nullopt;
        return static_cast<const InsertEntity&>(e).effectiveName();
    }
    if (property == "text") {
        if (e.type() == EntityType::Text)
            return static_cast<const TextEntity&>(e).text;
        if (e.type() == EntityType::MText)
            return static_cast<const MTextEntity&>(e).text;
        return std::nullopt;
    }
    if (property == "height") {
        if (e.type() == EntityType::Text)
            return std::to_string(static_cast<const TextEntity&>(e).height);
        if (e.type() == EntityType::MText)
            return std::to_string(static_cast<const MTextEntity&>(e).height);
        return std::nullopt;
    }
    if (property.rfind("attribute:", 0) == 0) {
        if (e.type() != EntityType::Insert)
            return std::nullopt;
        const std::string tag = upper(property.substr(10));
        for (const Attribute& a : static_cast<const InsertEntity&>(e).attributes)
            if (upper(a.tag) == tag)
                return a.text.text;
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<EntityId> quickSelect(const Document& doc, const QuickSelectQuery& q)
{
    const std::unordered_set<EntityId> within(q.within.begin(), q.within.end());
    std::vector<EntityId> out;
    for (const auto& e : doc.entities()) {
        if (!within.empty()) {
            if (!within.count(e->id()))
                continue;
        } else if ((e->space() < 0 ? 0 : e->space()) != q.space) {
            continue;
        }
        if (!doc.isSelectable(*e))
            continue;
        bool match = !q.type || e->type() == *q.type;
        if (match && !q.property.empty()) {
            const auto value = propertyValue(doc, *e, q.property);
            if (!value) {
                match = false;
            } else {
                switch (q.op) {
                case QuickSelectQuery::Op::Equal: {
                    const auto a = number(*value), b = number(q.value);
                    match = a && b && q.property != "layer" && q.property != "block" && q.property != "text"
                                ? std::abs(*a - *b) < 1e-9
                                : upper(*value) == upper(q.value);
                    break;
                }
                case QuickSelectQuery::Op::NotEqual: {
                    const auto a = number(*value), b = number(q.value);
                    match = a && b && q.property != "layer" && q.property != "block" && q.property != "text"
                                ? std::abs(*a - *b) >= 1e-9
                                : upper(*value) != upper(q.value);
                    break;
                }
                case QuickSelectQuery::Op::Greater:
                case QuickSelectQuery::Op::Less: {
                    const auto a = number(*value), b = number(q.value);
                    match = a && b && (q.op == QuickSelectQuery::Op::Greater ? *a > *b : *a < *b);
                    break;
                }
                case QuickSelectQuery::Op::Wildcard:
                    match = wildcardMatches(q.value, *value);
                    break;
                }
            }
        }
        if (match != q.exclude)
            out.push_back(e->id());
    }
    return out;
}

} // namespace cartesio
