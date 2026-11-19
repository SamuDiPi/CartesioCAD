#include "core/HatchBoundary.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace cartesio {

double signedArea(const std::vector<Vec2>& polygon)
{
    double a = 0.0;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        a += cross(polygon[j], polygon[i]);
    return a / 2.0;
}

bool pointInPolygon(const std::vector<Vec2>& polygon, Vec2 p)
{
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[i];
        const Vec2 b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

namespace {

struct Segment {
    Vec2 a;
    Vec2 b;
    std::vector<double> cuts;   // parametri (0..1) dei punti di taglio
};

bool usableForBoundary(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Text:
    case EntityType::MText:
    case EntityType::Hatch:
    case EntityType::AttDef:
    case EntityType::Viewport:
    case EntityType::XLine:   // infinite: falserebbero le tolleranze
    case EntityType::Point:
    case EntityType::Dimension:
        return false;
    default:
        return true;
    }
}

// Archi approssimati con corde (al massimo 5° l'una).
void collectSegments(const std::vector<const Entity*>& entities, std::vector<Segment>& out)
{
    for (const Entity* e : entities) {
        if (!usableForBoundary(*e))
            continue;
        for (const Prim& p : e->prims()) {
            if (p.kind == Prim::Kind::Segment) {
                if (distance(p.a, p.b) > 0.0)
                    out.push_back({p.a, p.b, {}});
                continue;
            }
            const int steps = std::max(2, static_cast<int>(std::ceil(p.sweep / degToRad(5.0))));
            Vec2 prev = p.pointAt(p.start);
            for (int i = 1; i <= steps; ++i) {
                const Vec2 next = p.pointAt(p.start + p.sweep * i / steps);
                out.push_back({prev, next, {}});
                prev = next;
            }
        }
    }
}

// Taglia i segmenti in tutte le intersezioni (anche estremo su segmento e sovrapposizioni).
void cutAtIntersections(std::vector<Segment>& segs, double eps)
{
    std::vector<std::size_t> order(segs.size());
    for (std::size_t i = 0; i < segs.size(); ++i)
        order[i] = i;
    const auto minX = [&](std::size_t i) { return std::min(segs[i].a.x, segs[i].b.x); };
    const auto maxX = [&](std::size_t i) { return std::max(segs[i].a.x, segs[i].b.x); };
    std::sort(order.begin(), order.end(), [&](std::size_t i, std::size_t j) { return minX(i) < minX(j); });

    // Parametro di q sul segmento s, se q ci sta sopra.
    const auto paramOn = [eps](const Segment& s, Vec2 q) -> std::optional<double> {
        const Vec2 d = s.b - s.a;
        const double len2 = d.dot(d);
        const double t = (q - s.a).dot(d) / len2;
        if (t < -1e-12 || t > 1.0 + 1e-12)
            return std::nullopt;
        if (distance(s.a + d * t, q) > eps)
            return std::nullopt;
        return std::clamp(t, 0.0, 1.0);
    };

    std::vector<std::size_t> active;
    for (std::size_t oi = 0; oi < order.size(); ++oi) {
        const std::size_t i = order[oi];
        const double x0 = minX(i);
        std::erase_if(active, [&](std::size_t j) { return maxX(j) < x0 - eps; });
        Segment& s = segs[i];
        const double sy0 = std::min(s.a.y, s.b.y);
        const double sy1 = std::max(s.a.y, s.b.y);
        for (std::size_t j : active) {
            Segment& t = segs[j];
            if (std::max(t.a.y, t.b.y) < sy0 - eps || std::min(t.a.y, t.b.y) > sy1 + eps)
                continue;
            const Vec2 r = s.b - s.a;
            const Vec2 q = t.b - t.a;
            const double denom = cross(r, q);
            if (std::abs(denom) > 1e-12 * r.length() * q.length()) {
                const double u = cross(t.a - s.a, q) / denom;
                const double v = cross(t.a - s.a, r) / denom;
                const double tolU = eps / r.length();
                const double tolV = eps / q.length();
                if (u >= -tolU && u <= 1.0 + tolU && v >= -tolV && v <= 1.0 + tolV) {
                    s.cuts.push_back(std::clamp(u, 0.0, 1.0));
                    t.cuts.push_back(std::clamp(v, 0.0, 1.0));
                }
                continue;
            }
            // Paralleli: se sono sulla stessa retta, ognuno si taglia agli estremi dell'altro.
            for (Vec2 end : {t.a, t.b})
                if (const auto u = paramOn(s, end))
                    s.cuts.push_back(*u);
            for (Vec2 end : {s.a, s.b})
                if (const auto v = paramOn(t, end))
                    t.cuts.push_back(*v);
        }
        active.push_back(i);
    }
}

struct Graph {
    std::vector<Vec2> nodes;
    std::vector<std::vector<int>> adjacency;   // vicini, ordinati per angolo
};

Graph buildGraph(std::vector<Segment>& segs, double eps)
{
    Graph g;
    // Nodi uniti entro eps con una griglia.
    std::unordered_map<long long, std::vector<int>> grid;
    const double cell = eps * 4.0;
    const auto key = [](long long x, long long y) { return x * 73856093LL ^ y * 19349663LL; };
    const auto nodeOf = [&](Vec2 p) {
        const auto cx = static_cast<long long>(std::floor(p.x / cell));
        const auto cy = static_cast<long long>(std::floor(p.y / cell));
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy) {
                const auto it = grid.find(key(cx + dx, cy + dy));
                if (it == grid.end())
                    continue;
                for (int n : it->second)
                    if (distance(g.nodes[static_cast<std::size_t>(n)], p) <= eps)
                        return n;
            }
        const int n = static_cast<int>(g.nodes.size());
        g.nodes.push_back(p);
        grid[key(cx, cy)].push_back(n);
        return n;
    };

    std::vector<std::pair<int, int>> edges;
    for (Segment& s : segs) {
        s.cuts.push_back(0.0);
        s.cuts.push_back(1.0);
        std::sort(s.cuts.begin(), s.cuts.end());
        int prev = -1;
        for (double c : s.cuts) {
            const int n = nodeOf(s.a + (s.b - s.a) * c);
            if (prev >= 0 && n != prev)
                edges.emplace_back(std::min(prev, n), std::max(prev, n));
            prev = n;
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    g.adjacency.resize(g.nodes.size());
    for (const auto& [a, b] : edges) {
        g.adjacency[static_cast<std::size_t>(a)].push_back(b);
        g.adjacency[static_cast<std::size_t>(b)].push_back(a);
    }
    // Rami morti (nodi con un solo collegamento): non possono delimitare una regione.
    std::vector<int> queue;
    for (std::size_t n = 0; n < g.nodes.size(); ++n)
        if (g.adjacency[n].size() == 1)
            queue.push_back(static_cast<int>(n));
    while (!queue.empty()) {
        const int n = queue.back();
        queue.pop_back();
        auto& adj = g.adjacency[static_cast<std::size_t>(n)];
        if (adj.size() != 1)
            continue;
        const int m = adj.front();
        adj.clear();
        auto& other = g.adjacency[static_cast<std::size_t>(m)];
        std::erase(other, n);
        if (other.size() == 1)
            queue.push_back(m);
    }
    for (std::size_t n = 0; n < g.nodes.size(); ++n) {
        const Vec2 c = g.nodes[n];
        std::sort(g.adjacency[n].begin(), g.adjacency[n].end(), [&](int a, int b) {
            return angleOf(g.nodes[static_cast<std::size_t>(a)] - c) < angleOf(g.nodes[static_cast<std::size_t>(b)] - c);
        });
    }
    return g;
}

// Tutte le facce del grafo: percorrendo ogni lato con la regione a sinistra.
std::vector<std::vector<Vec2>> faces(const Graph& g)
{
    std::vector<std::vector<Vec2>> result;
    std::vector<std::vector<bool>> used(g.nodes.size());
    for (std::size_t n = 0; n < g.nodes.size(); ++n)
        used[n].assign(g.adjacency[n].size(), false);

    for (std::size_t start = 0; start < g.nodes.size(); ++start) {
        for (std::size_t k = 0; k < g.adjacency[start].size(); ++k) {
            if (used[start][k])
                continue;
            std::vector<Vec2> face;
            std::size_t u = start;
            std::size_t idx = k;
            const std::size_t limit = 4 * g.nodes.size() + 8;
            for (std::size_t steps = 0; steps < limit && !used[u][idx]; ++steps) {
                used[u][idx] = true;
                face.push_back(g.nodes[u]);
                const auto v = static_cast<std::size_t>(g.adjacency[u][idx]);
                // Al nodo v si prende il lato subito in senso orario rispetto a quello da cui si arriva.
                const auto& adj = g.adjacency[v];
                const auto back = static_cast<std::size_t>(
                    std::find(adj.begin(), adj.end(), static_cast<int>(u)) - adj.begin());
                idx = (back + adj.size() - 1) % adj.size();
                u = v;
            }
            if (face.size() >= 3)
                result.push_back(std::move(face));
        }
    }
    return result;
}

} // namespace

std::vector<std::vector<Vec2>> findHatchBoundary(const std::vector<const Entity*>& entities, Vec2 point)
{
    std::vector<Segment> segs;
    collectSegments(entities, segs);
    if (segs.empty())
        return {};
    BBox box;
    for (const Segment& s : segs) {
        box.expand(s.a);
        box.expand(s.b);
    }
    const double eps = std::max(1e-9, 1e-7 * std::hypot(box.width(), box.height()));
    cutAtIntersections(segs, eps);
    const Graph graph = buildGraph(segs, eps);
    const auto all = faces(graph);

    // Contorno esterno: la regione chiusa più piccola (antioraria) che contiene il punto.
    const std::vector<Vec2>* outer = nullptr;
    double outerArea = 0.0;
    for (const auto& f : all) {
        const double a = signedArea(f);
        if (a <= 0.0 || !pointInPolygon(f, point))
            continue;
        if (!outer || a < outerArea) {
            outer = &f;
            outerArea = a;
        }
    }
    if (!outer)
        return {};

    std::vector<std::vector<Vec2>> loops{*outer};
    // Isole: il contorno esterno (orario) di ogni gruppo di oggetti che sta dentro la regione.
    for (const auto& f : all) {
        const double a = signedArea(f);
        if (a >= 0.0 || -a >= outerArea * (1.0 - 1e-9))
            continue;
        if (!pointInPolygon(*outer, f.front()) || pointInPolygon(f, point))
            continue;
        // Il vertice potrebbe stare sul contorno esterno: si controlla anche il centro di un lato.
        const Vec2 mid = (f[0] + f[1]) / 2.0;
        if (!pointInPolygon(*outer, mid))
            continue;
        loops.push_back(f);
    }
    return loops;
}

} // namespace cartesio
