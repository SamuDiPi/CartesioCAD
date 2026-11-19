#include "core/Compare.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace cartesio {

namespace {

std::string layerName(const Document& doc, const Entity& e)
{
    const int li = e.layer();
    return li >= 0 && li < static_cast<int>(doc.layers().size()) ? doc.layers()[static_cast<std::size_t>(li)].name : std::string();
}

} // namespace

std::string compareSignature(const Document& doc, const Entity& e, double tolerance)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto q = [tolerance](double v) { return static_cast<long long>(std::llround(v / tolerance)); };
    out << static_cast<int>(e.type()) << '|' << layerName(doc, e) << '|' << e.color() << '|' << e.trueColor() << '|'
        << e.linetype() << '|' << e.lineweight() << '|';
    // Geometria: i punti delle primitive (estremi, centri, raggi), arrotondati.
    // Blocchi, quote, direttrici e testi si riconoscono dai loro dati (scomporre i blocchi costerebbe troppo).
    std::vector<Prim> prims;
    const EntityType t = e.type();
    if (t != EntityType::Insert && t != EntityType::Dimension && t != EntityType::Leader && t != EntityType::Text
        && t != EntityType::MText && t != EntityType::Table)
        e.decompose(prims);
    for (const Prim& p : prims) {
        if (p.kind == Prim::Kind::Segment)
            out << 'S' << q(p.a.x) << ',' << q(p.a.y) << ',' << q(p.b.x) << ',' << q(p.b.y) << ';';
        else
            out << 'A' << q(p.center.x) << ',' << q(p.center.y) << ',' << q(p.radius) << ',' << std::llround(p.start * 1e4) << ','
                << std::llround(p.sweep * 1e4) << ';';
    }
    switch (e.type()) {
    case EntityType::Text: {
        const auto& t = static_cast<const TextEntity&>(e);
        out << t.text << '|' << q(t.position.x) << ',' << q(t.position.y) << ',' << q(t.height) << ',' << std::llround(t.rotation * 1e4);
        break;
    }
    case EntityType::MText: {
        const auto& t = static_cast<const MTextEntity&>(e);
        out << t.text << '|' << q(t.position.x) << ',' << q(t.position.y) << ',' << q(t.height) << ',' << q(t.width);
        break;
    }
    case EntityType::Insert: {
        const auto& in = static_cast<const InsertEntity&>(e);
        out << in.blockName << '|' << q(in.position.x) << ',' << q(in.position.y) << ',' << std::llround(in.scaleX * 1e4) << ','
            << std::llround(in.scaleY * 1e4) << ',' << std::llround(in.rotation * 1e4);
        for (const Attribute& a : in.attributes)
            out << '|' << a.tag << '=' << a.text.text;
        break;
    }
    case EntityType::Dimension: {
        const auto& d = static_cast<const DimensionEntity&>(e);
        out << d.displayText() << '|' << q(d.p1.x) << ',' << q(d.p1.y) << ',' << q(d.p2.x) << ',' << q(d.p2.y) << ','
            << q(d.dimLine.x) << ',' << q(d.dimLine.y);
        break;
    }
    case EntityType::Leader:
        out << static_cast<const LeaderEntity&>(e).text;
        break;
    case EntityType::Hatch:
        out << static_cast<const HatchEntity&>(e).pattern;
        break;
    default:
        break;
    }
    // Oggetti senza primitive (punti, immagini...): l'ingombro.
    if (prims.empty()) {
        const BBox b = e.bounds();
        if (b.isValid())
            out << "|B" << q(b.min.x) << ',' << q(b.min.y) << ',' << q(b.max.x) << ',' << q(b.max.y);
    }
    return out.str();
}

DrawingDiff compareDrawings(const Document& here, const Document& there, int spaceHere, int spaceThere)
{
    DrawingDiff diff;
    BBox extent = here.extents(spaceHere);
    extent.expand(there.extents(spaceThere));
    const double size = extent.isValid() ? std::max(extent.width(), extent.height()) : 1000.0;
    const double tolerance = std::max(size * 1e-7, 1e-6);

    // Firme dell'altro disegno, con quante volte compaiono.
    std::unordered_map<std::string, std::vector<const Entity*>> theirs;
    for (const auto& e : there.entities())
        if (e->space() == spaceThere)
            theirs[compareSignature(there, *e, tolerance)].push_back(e.get());
    std::vector<const Entity*> unmatched;
    for (const auto& e : here.entities()) {
        if (e->space() != spaceHere)
            continue;
        auto it = theirs.find(compareSignature(here, *e, tolerance));
        if (it != theirs.end() && !it->second.empty()) {
            it->second.pop_back();
            ++diff.same;
            continue;
        }
        unmatched.push_back(e.get());
    }
    // Secondo giro, tollerante: fra gli spaiati, stesso tipo, layer e testo/blocco con ingombri che coincidono entro
    // un centomillesimo del disegno sono lo stesso oggetto (spline, polilinee e blocchi cambiano di poco i loro dati
    // passando da un formato all'altro).
    const auto looseKey = [](const Document& doc, const Entity& e) {
        std::string key = std::to_string(static_cast<int>(e.type())) + '|' + layerName(doc, e);
        if (e.type() == EntityType::Insert)
            key += '|' + static_cast<const InsertEntity&>(e).blockName;
        else if (e.type() == EntityType::Text)
            key += '|' + static_cast<const TextEntity&>(e).text;
        else if (e.type() == EntityType::MText)
            key += '|' + static_cast<const MTextEntity&>(e).text;
        return key;
    };
    const double loose = std::max(size * 1e-5, 1e-6);
    std::unordered_map<std::string, std::vector<const Entity*>> theirsLoose;
    for (auto& [signature, rest] : theirs)
        for (const Entity* e : rest)
            theirsLoose[looseKey(there, *e)].push_back(e);
    std::vector<BBox> boxes;
    for (const Entity* e : unmatched) {
        auto it = theirsLoose.find(looseKey(here, *e));
        const BBox b = e->bounds();
        bool found = false;
        if (it != theirsLoose.end() && b.isValid()) {
            auto& candidates = it->second;
            for (std::size_t k = 0; k < candidates.size(); ++k) {
                const BBox c = candidates[k]->bounds();
                if (c.isValid() && std::abs(c.min.x - b.min.x) <= loose && std::abs(c.min.y - b.min.y) <= loose
                    && std::abs(c.max.x - b.max.x) <= loose && std::abs(c.max.y - b.max.y) <= loose) {
                    candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(k));
                    found = true;
                    break;
                }
            }
        }
        if (found) {
            ++diff.same;
            continue;
        }
        diff.onlyHere.push_back(e->id());
        if (b.isValid())
            boxes.push_back(b);
    }
    for (auto& [key, rest] : theirsLoose)
        for (const Entity* e : rest) {
            diff.onlyThere.push_back(std::shared_ptr<const Entity>(e->clone()));
            if (const BBox b = e->bounds(); b.isValid())
                boxes.push_back(b);
        }

    // Zone: ingombri allargati del 2% del disegno, uniti finché si toccano.
    const double margin = size * 0.02;
    std::vector<BBox> regions;
    for (BBox b : boxes) {
        b.expand(b.min - Vec2{margin, margin});
        b.expand(b.max + Vec2{margin, margin});
        regions.push_back(b);
    }
    const auto touch = [](const BBox& a, const BBox& b) {
        return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y;
    };
    // Unione per celle di una griglia (tante differenze: niente confronti tutti con tutti), poi a coppie.
    bool merged = true;
    for (int pass = 0; merged && pass < 50; ++pass) {
        merged = false;
        std::vector<BBox> next;
        for (const BBox& b : regions) {
            bool joined = false;
            for (BBox& n : next)
                if (touch(n, b)) {
                    n.expand(b);
                    joined = true;
                    merged = true;
                    break;
                }
            if (!joined)
                next.push_back(b);
            if (next.size() > 4000)   // troppe zone: si tengono così
                break;
        }
        regions = std::move(next);
    }
    std::sort(regions.begin(), regions.end(), [](const BBox& a, const BBox& b) {
        if (std::abs(a.max.y - b.max.y) > 1e-9)
            return a.max.y > b.max.y;   // dall'alto
        return a.min.x < b.min.x;       // da sinistra
    });
    diff.regions = std::move(regions);
    return diff;
}

} // namespace cartesio
