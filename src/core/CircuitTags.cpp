#include "core/CircuitTags.h"

#include "core/Geometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <regex>
#include <set>

namespace cartesio {

namespace {

std::string upper(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Il layer comincia con uno dei prefissi (separati da ";"); nessun prefisso = tutti.
bool layerMatches(const Document& doc, int layer, const std::string& prefixes)
{
    if (prefixes.empty())
        return true;
    if (layer < 0 || layer >= static_cast<int>(doc.layers().size()))
        return false;
    const std::string name = upper(doc.layers()[static_cast<std::size_t>(layer)].name);
    std::size_t start = 0;
    while (start <= prefixes.size()) {
        const std::size_t end = std::min(prefixes.find(';', start), prefixes.size());
        std::string p = upper(prefixes.substr(start, end - start));
        p.erase(0, p.find_first_not_of(' '));
        p.erase(p.find_last_not_of(' ') + 1);
        if (!p.empty() && name.rfind(p, 0) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

std::string plainText(const Entity& e)
{
    if (e.type() == EntityType::Text)
        return static_cast<const TextEntity&>(e).displayText();
    if (e.type() == EntityType::MText) {
        std::string out;
        for (const std::string& line : static_cast<const MTextEntity&>(e).lines()) {
            if (!out.empty())
                out += ' ';
            out += line;
        }
        return out;
    }
    return {};
}

double textHeight(const Entity& e)
{
    if (e.type() == EntityType::Text)
        return static_cast<const TextEntity&>(e).height;
    if (e.type() == EntityType::MText)
        return static_cast<const MTextEntity&>(e).height;
    return 0.0;
}

Vec2 centerOf(const Entity& e, Vec2 fallback)
{
    const BBox b = e.bounds();
    return b.isValid() ? b.center() : fallback;
}

// Centro della sola geometria di un blocco inserito (l'ingombro dell'inserimento comprende anche il punto di
// inserimento, che nei blocchi esportati dai programmi BIM sta nell'origine del progetto).
Vec2 symbolCenter(const InsertEntity& in)
{
    if (!in.block)
        return in.position;
    BBox b;
    const Affine t = in.affine();
    for (const auto& child : in.block->entities) {
        const BBox cb = child->bounds();
        if (!cb.isValid())
            continue;
        for (Vec2 c : {cb.min, Vec2{cb.max.x, cb.min.y}, cb.max, Vec2{cb.min.x, cb.max.y}})
            b.expand(t.apply(c));
    }
    return b.isValid() ? b.center() : in.position;
}

bool unused(const Circuit& c)
{
    const std::string d = upper(c.description);
    return c.number <= 0 || d.rfind("RISERVA", 0) == 0 || upper(c.cable).find("NESSUNA LINEA") != std::string::npos;
}

} // namespace

std::vector<CircuitTag> findCircuitTags(const Document& doc, const CircuitScanOptions& options)
{
    static const std::regex code(R"(([A-Za-z0-9][A-Za-z0-9._/-]*)\s+[Cc]\s*-?\s*\d+)");
    std::vector<CircuitTag> tags;
    for (const auto& e : doc.entities()) {
        if ((e->type() != EntityType::Text && e->type() != EntityType::MText)
            || doc.resolveSpace(e->space()) != options.space || !layerMatches(doc, e->layer(), options.tagLayers))
            continue;
        const std::string text = plainText(*e);
        for (auto it = std::sregex_iterator(text.begin(), text.end(), code); it != std::sregex_iterator(); ++it)
            if (const auto normal = normalizeCircuitCode(it->str()))
                tags.push_back(CircuitTag{e->id(), *normal, centerOf(*e, {}), textHeight(*e), e->layer()});
    }
    return tags;
}

std::vector<CircuitPoint> findCircuitPoints(const Document& doc, const CircuitScanOptions& options)
{
    // Lettere di identificazione: testi brevi sui loro layer.
    struct Letter {
        std::string text;
        Vec2 position;
    };
    std::vector<Letter> letters;
    if (!options.letterLayers.empty())
        for (const auto& e : doc.entities())
            if ((e->type() == EntityType::Text || e->type() == EntityType::MText)
                && doc.resolveSpace(e->space()) == options.space && layerMatches(doc, e->layer(), options.letterLayers)) {
                std::string t = plainText(*e);
                t.erase(0, t.find_first_not_of(' '));
                t.erase(t.find_last_not_of(' ') + 1);
                if (!t.empty() && t.size() <= 4)
                    letters.push_back(Letter{t, centerOf(*e, {})});
            }

    std::vector<CircuitPoint> points;
    for (const auto& e : doc.entities()) {
        if (e->type() != EntityType::Insert || doc.resolveSpace(e->space()) != options.space)
            continue;
        // I layer delle lettere cominciano spesso come quelli dei punti (E-ELEC-FIXT-IDEN): si escludono.
        if (!layerMatches(doc, e->layer(), options.pointLayers)
            || (!options.letterLayers.empty() && layerMatches(doc, e->layer(), options.letterLayers)))
            continue;
        const auto& in = static_cast<const InsertEntity&>(*e);
        CircuitPoint p;
        p.id = e->id();
        p.family = in.blockName.substr(0, in.blockName.find(" - "));
        p.position = symbolCenter(in);
        points.push_back(std::move(p));
    }
    // Ogni lettera va solo al punto più vicino (accanto a un simbolo ci possono essere altri blocchi);
    // un punto con più lettere vicine tiene la più vicina.
    std::vector<double> letterDistance(points.size(), options.letterRadius);
    for (const Letter& l : letters) {
        std::size_t best = points.size();
        double bestD = options.letterRadius;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (const double d = distance(l.position, points[i].position); d <= bestD) {
                bestD = d;
                best = i;
            }
        if (best < points.size() && (points[best].letter.empty() || bestD < letterDistance[best])) {
            points[best].letter = l.text;
            letterDistance[best] = bestD;
        }
    }
    return points;
}

CircuitCheck checkCircuits(const Document& doc, const std::vector<Circuit>& circuits, const CircuitScanOptions& options)
{
    CircuitCheck check;
    std::set<std::string> known;
    for (const Circuit& c : circuits)
        known.insert(c.code);
    const std::vector<CircuitTag> tags = findCircuitTags(doc, options);
    std::set<std::string> tagged, boards;
    for (const CircuitTag& t : tags) {
        if (known.count(t.code)) {
            ++check.matchedTags;
            tagged.insert(t.code);
            boards.insert(t.code.substr(0, t.code.rfind(" C-")));
        } else {
            check.unknownTags.push_back(t);
        }
    }
    for (const Circuit& c : circuits)
        if (boards.count(c.board) && !tagged.count(c.code) && !unused(c))
            check.circuitsWithoutTags.push_back(c.code);
    for (const CircuitPoint& p : findCircuitPoints(doc, options)) {
        const bool near = std::any_of(tags.begin(), tags.end(), [&](const CircuitTag& t) {
            return distance(t.position, p.position) <= options.tagRadius;
        });
        if (!near)
            check.pointsWithoutTags.push_back(p);
    }
    return check;
}

double usualTagHeight(const std::vector<CircuitTag>& tags, double fallback)
{
    std::map<long long, int> counts;
    for (const CircuitTag& t : tags)
        if (t.height > 0.0)
            ++counts[std::llround(t.height * 100.0)];
    if (counts.empty())
        return fallback;
    const auto best = std::max_element(counts.begin(), counts.end(),
                                       [](const auto& a, const auto& b) { return a.second < b.second; });
    return best->first / 100.0;
}

std::vector<std::unique_ptr<Entity>> makeCircuitTags(Document& doc, const std::vector<CircuitPoint>& points,
                                                     const std::string& code, const CircuitTagStyle& style)
{
    int layer = doc.findLayer(style.layer);
    if (layer < 0) {
        Layer l;
        l.name = style.layer;
        l.color = style.color;
        layer = doc.addLayer(l);
    }
    std::vector<std::unique_ptr<Entity>> out;
    for (const CircuitPoint& p : points) {
        auto text = std::make_unique<TextEntity>(p.position + Vec2{style.offset, -style.height / 2.0}, style.height, code);
        text->setLayer(layer);
        text->setColor(kColorByLayer);
        out.push_back(std::move(text));
    }
    return out;
}

} // namespace cartesio

namespace cartesio {

std::vector<CircuitPoint> pointsFromObjects(const Document& doc, const std::vector<EntityId>& ids)
{
    std::vector<CircuitPoint> points;
    for (EntityId id : ids) {
        const Entity* e = doc.find(id);
        if (!e)
            continue;
        CircuitPoint p;
        p.id = id;
        if (e->type() == EntityType::Insert) {
            const auto& in = static_cast<const InsertEntity&>(*e);
            p.family = in.blockName.substr(0, in.blockName.find(" - "));
            p.position = symbolCenter(in);
        } else {
            p.position = centerOf(*e, {});
        }
        points.push_back(std::move(p));
    }
    return points;
}

} // namespace cartesio
