#include "core/RunImport.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <regex>

namespace cartesio::plant {

namespace {

std::string upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Testo di un TEXT o MTEXT, senza codici di formattazione; vuoto per gli altri oggetti.
std::string textOf(const Entity& e, Vec2* center = nullptr, double* height = nullptr)
{
    std::string s;
    if (e.type() == EntityType::Text) {
        const auto& t = static_cast<const TextEntity&>(e);
        s = t.text;
        if (height)
            *height = t.height;
    } else if (e.type() == EntityType::MText) {
        const auto& t = static_cast<const MTextEntity&>(e);
        s = mtextPlainText(t.text);
        if (height)
            *height = t.height;
    } else {
        return {};
    }
    if (center) {
        const BBox b = e.bounds();
        *center = b.isValid() ? (b.min + b.max) * 0.5 : Vec2{};
    }
    return s;
}

bool isPathEntity(const Entity& e)
{
    return e.type() == EntityType::Line || e.type() == EntityType::Polyline || e.type() == EntityType::Arc;
}

// Punti della linea (archi in 8 corde) e i suoi due estremi.
std::vector<Vec2> pointsOf(const Entity& e)
{
    std::vector<Vec2> pts;
    switch (e.type()) {
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        pts = {l.start, l.end};
        break;
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        for (const PolyVertex& v : pl.vertices)
            pts.push_back(v.point);
        if (pl.closed && !pts.empty())
            pts.push_back(pts.front());
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        for (int k = 0; k <= 8; ++k)
            pts.push_back(a.pointAt(a.startAngle + a.sweep() * k / 8.0));
        break;
    }
    default: break;
    }
    return pts;
}

double lengthOf(const std::vector<Vec2>& pts)
{
    double l = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i)
        l += distance(pts[i - 1], pts[i]);
    return l;
}

double distanceToPolyline(Vec2 p, const std::vector<Vec2>& pts)
{
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const Vec2 a = pts[i - 1], d = pts[i] - pts[i - 1];
        const double len2 = d.x * d.x + d.y * d.y;
        const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, distance(p, a + d * t));
    }
    if (pts.size() == 1)
        best = distance(p, pts.front());
    return best;
}

} // namespace

std::optional<RunSpec> parseRunText(std::string_view text)
{
    std::string s(text);
    if (s.find('\\') != std::string::npos || s.find('{') != std::string::npos)
        s = mtextPlainText(s);
    const std::string u = upper(s);
    RunSpec spec;
    if (u.find("PASSERELL") != std::string::npos)
        spec.kind = RunKind::Tray;
    else if (u.find("CANAL") != std::string::npos)
        spec.kind = RunKind::Duct;
    else if (u.find("TUB") != std::string::npos)
        spec.kind = RunKind::Tube;
    else
        return std::nullopt;
    static const std::regex size(R"((\d{2,4})\s*[X\*]\s*(\d{2,4}))");
    static const std::regex diameter(R"((?:%%C|\bD|\bDIAM\.?\s*)\s*(\d{2,3})\b)");
    std::smatch m;
    // "×" (UTF-8) come "x"; "Ø" e "ø" come il codice del diametro "%%C".
    std::string u2 = u;
    for (std::size_t p; (p = u2.find("\xC3\x97")) != std::string::npos;)
        u2.replace(p, 2, "X");
    for (const char* sign : {"\xC3\x98", "\xC3\xB8"})
        for (std::size_t p; (p = u2.find(sign)) != std::string::npos;)
            u2.replace(p, 2, "%%C");
    if (spec.kind != RunKind::Tube && std::regex_search(u2, m, size)) {
        double a = std::stod(m[1]), b = std::stod(m[2]);
        spec.width = std::max(a, b);
        spec.height = std::min(a, b);
    } else if (spec.kind == RunKind::Tube && std::regex_search(u2, m, diameter)) {
        spec.tube = std::stod(m[1]);
    } else if (spec.kind != RunKind::Tube) {
        return std::nullopt;   // canale o passerella senza misura: non serve
    }
    spec.text = s;
    return spec;
}

PathStyle styleOf(const Document& doc, const Entity& e)
{
    PathStyle st;
    std::string lt = upper(e.linetype());
    if (lt.empty() || lt == "BYLAYER" || lt == "DALAYER") {
        const auto& layers = doc.layers();
        lt = e.layer() >= 0 && e.layer() < static_cast<int>(layers.size()) ? upper(layers[e.layer()].linetype) : "CONTINUOUS";
    } else if (lt == "BYBLOCK" || lt == "DABLOCCO") {
        lt = "CONTINUOUS";
    }
    st.linetype = lt.empty() ? "CONTINUOUS" : lt;
    if (e.type() == EntityType::Polyline) {
        double w = 0.0;
        for (const PolyVertex& v : static_cast<const PolylineEntity&>(e).vertices)
            w = std::max({w, v.startWidth, v.endWidth});
        st.width = std::round(w * 100.0) / 100.0;
    }
    return st;
}

std::vector<StyleGroup> pathStyles(const Document& doc, const std::vector<int>& layers)
{
    std::map<PathStyle, StyleGroup> groups;
    for (const auto& e : doc.entities()) {
        if (e->space() != 0 || !isPathEntity(*e) || !e->plantRun().empty())
            continue;
        if (std::find(layers.begin(), layers.end(), e->layer()) == layers.end())
            continue;
        const PathStyle st = styleOf(doc, *e);
        StyleGroup& g = groups[st];
        g.style = st;
        g.ids.push_back(e->id());
        g.length += lengthOf(pointsOf(*e));
    }
    std::vector<StyleGroup> out;
    for (auto& [st, g] : groups)
        out.push_back(std::move(g));
    std::sort(out.begin(), out.end(), [](const StyleGroup& a, const StyleGroup& b) { return a.length > b.length; });
    return out;
}

std::vector<LegendItem> readRunLegend(const Document& doc)
{
    std::vector<LegendItem> out;
    for (const auto& e : doc.entities()) {
        if (e->space() != 0)
            continue;
        Vec2 center;
        double h = 0.0;
        const std::string text = textOf(*e, &center, &h);
        if (text.empty() || h <= 0.0)
            continue;
        const auto spec = parseRunText(text);
        if (!spec)
            continue;
        const BBox tb = e->bounds();
        if (!tb.isValid())
            continue;
        // Campione: una linea orizzontale sulla stessa riga, che finisce poco prima del testo.
        const double rowY = (tb.min.y + tb.max.y) * 0.5;
        const Entity* best = nullptr;
        double bestScore = std::numeric_limits<double>::max();
        for (const auto& c : doc.entities()) {
            if (c->space() != 0 || !isPathEntity(*c) || !c->plantRun().empty())
                continue;
            const BBox b = c->bounds();
            if (!b.isValid())
                continue;
            const double cy = (b.min.y + b.max.y) * 0.5;
            const double len = b.max.x - b.min.x;
            if (b.max.y - b.min.y > h || std::abs(cy - rowY) > h * 0.75 || len < h * 2.0)
                continue;
            const double gap = tb.min.x - b.max.x;
            if (gap < -h || gap > h * 15.0)
                continue;
            const double score = std::abs(cy - rowY) * 4.0 + std::max(0.0, gap);
            if (score < bestScore) {
                bestScore = score;
                best = c.get();
            }
        }
        if (best)
            out.push_back({styleOf(doc, *best), *spec, e->id()});
    }
    return out;
}

std::vector<RunChain> chainRuns(const Document& doc, const std::vector<EntityId>& ids, double tolerance,
                                double labelDistance, const std::vector<LegendItem>& legend)
{
    struct Item {
        EntityId id;
        PathStyle style;
        std::vector<Vec2> pts;
        int ends[2] = {-1, -1};   // nodi degli estremi
    };
    std::vector<Item> items;
    for (EntityId id : ids)
        if (const Entity* e = doc.find(id); e && isPathEntity(*e)) {
            Item it{id, styleOf(doc, *e), pointsOf(*e)};
            if (it.pts.size() >= 2)
                items.push_back(std::move(it));
        }
    // Nodi agli estremi, uniti se vicini.
    std::vector<Vec2> nodes;
    auto node = [&](Vec2 p) {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
            if (distance(nodes[i], p) <= tolerance)
                return i;
        nodes.push_back(p);
        return static_cast<int>(nodes.size()) - 1;
    };
    std::vector<std::vector<int>> at;   // nodo -> estremi (indice oggetto)
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        Item& it = items[i];
        const bool closed = distance(it.pts.front(), it.pts.back()) <= tolerance;
        it.ends[0] = node(it.pts.front());
        it.ends[1] = closed ? it.ends[0] : node(it.pts.back());
        at.resize(nodes.size());
        at[it.ends[0]].push_back(i);
        if (!closed)
            at[it.ends[1]].push_back(i);
    }
    // Un nodo è anche una derivazione se cade sull'interno di un'altra linea (T): lì non si unisce.
    std::vector<bool> junction(nodes.size(), false);
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n) {
        if (at[n].size() >= 3) {
            junction[n] = true;
            continue;
        }
        for (const Item& it : items)
            if (it.ends[0] != n && it.ends[1] != n && distanceToPolyline(nodes[n], it.pts) <= tolerance) {
                junction[n] = true;
                break;
            }
    }
    // Rami: si uniscono due linee dello stesso stile che si toccano in un nodo con solo loro due.
    std::vector<int> parent(items.size());
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        parent[i] = i;
    std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n)
        if (!junction[n] && at[n].size() == 2 && items[at[n][0]].style == items[at[n][1]].style)
            parent[find(at[n][0])] = find(at[n][1]);
    std::map<int, RunChain> chains;
    std::map<int, std::vector<Vec2>> chainPts;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        RunChain& c = chains[find(i)];
        c.style = items[i].style;
        c.ids.push_back(items[i].id);
        c.length += lengthOf(items[i].pts);
    }
    std::vector<RunChain> out;
    std::vector<std::vector<const Item*>> members;
    for (auto& [root, c] : chains) {
        out.push_back(c);
        members.emplace_back();
        for (const Item& it : items)
            if (find(static_cast<int>(&it - items.data())) == root)
                members.back().push_back(&it);
    }
    // Scritte con tipo e misura: al ramo più vicino, se entro labelDistance.
    struct Best { double d = std::numeric_limits<double>::max(); std::optional<RunSpec> spec; };
    std::vector<Best> best(out.size());
    for (const auto& e : doc.entities()) {
        if (e->space() != 0)
            continue;
        if (std::any_of(legend.begin(), legend.end(), [&e](const LegendItem& l) { return l.text == e->id(); }))
            continue;
        Vec2 center;
        const std::string text = textOf(*e, &center);
        if (text.empty())
            continue;
        const auto spec = parseRunText(text);
        if (!spec)
            continue;
        int bi = -1;
        double bd = labelDistance;
        for (std::size_t c = 0; c < out.size(); ++c)
            for (const Item* it : members[c])
                if (const double d = distanceToPolyline(center, it->pts); d <= bd) {
                    bd = d;
                    bi = static_cast<int>(c);
                }
        if (bi >= 0 && bd < best[bi].d) {
            best[bi].d = bd;
            best[bi].spec = spec;
        }
    }
    for (std::size_t c = 0; c < out.size(); ++c)
        out[c].label = best[c].spec;
    return out;
}

std::vector<int> suggestRunLayers(const Document& doc)
{
    static const std::regex name(R"(CANAL|PASSER|TUB|CAVID|VIE.?CAVI|CABLE|TRAY|CONDUIT)", std::regex::icase);
    std::vector<int> out;
    const auto& layers = doc.layers();
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (!std::regex_search(layers[i].name, name))
            continue;
        const bool hasPaths = std::any_of(doc.entities().begin(), doc.entities().end(),
                                          [i](const auto& e) { return e->layer() == i && e->space() == 0 && isPathEntity(*e); });
        if (hasPaths)
            out.push_back(i);
    }
    return out;
}

} // namespace cartesio::plant
