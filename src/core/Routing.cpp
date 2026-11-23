#include "core/Routing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>

namespace cartesio::plant {

namespace {

struct Edge {
    int to;
    double length;
    int run;   // indice della tratta, -1 per i tratti di attacco
};

struct Graph {
    std::vector<Vec2> nodes;
    std::vector<std::vector<Edge>> adj;

    int addNode(Vec2 p)
    {
        nodes.push_back(p);
        adj.emplace_back();
        return static_cast<int>(nodes.size()) - 1;
    }
    void addEdge(int a, int b, double length, int run)
    {
        if (a == b)
            return;
        adj[a].push_back({b, length, run});
        adj[b].push_back({a, length, run});
    }
};

// Punto più vicino a p sul segmento ab; t in [0,1].
Vec2 closestOnSegment(Vec2 p, Vec2 a, Vec2 b, double* t)
{
    const Vec2 d = b - a;
    const double len2 = d.x * d.x + d.y * d.y;
    double s = len2 > 0.0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2 : 0.0;
    s = std::clamp(s, 0.0, 1.0);
    if (t)
        *t = s;
    return a + d * s;
}

// Rete delle tratte: nodi agli estremi dei tratti (uniti se vicini) e ai piedi delle derivazioni a T; ogni tratto è
// spezzato nei punti in cui lo toccano gli estremi di altri tratti.
struct Network {
    Graph g;
    std::vector<std::string> runs;
    struct Piece { int a, b, run; };   // pezzi dopo le suddivisioni (per attaccare partenza e arrivi)
    std::vector<Piece> pieces;
};

Network buildNetwork(const std::vector<RouteSegment>& segments, double tol)
{
    Network net;
    std::map<std::string, int> runIndex;
    for (const RouteSegment& s : segments)
        if (!runIndex.count(s.run)) {
            runIndex[s.run] = static_cast<int>(net.runs.size());
            net.runs.push_back(s.run);
        }
    // Nodi degli estremi, uniti per vicinanza (griglia di passo `tol`).
    std::map<std::pair<long long, long long>, std::vector<int>> grid;
    const double cell = std::max(tol, 1e-9);
    auto findOrAdd = [&](Vec2 p) {
        const long long cx = static_cast<long long>(std::floor(p.x / cell));
        const long long cy = static_cast<long long>(std::floor(p.y / cell));
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy) {
                auto it = grid.find({cx + dx, cy + dy});
                if (it == grid.end())
                    continue;
                for (int n : it->second)
                    if (distance(net.g.nodes[n], p) <= tol)
                        return n;
            }
        const int n = net.g.addNode(p);
        grid[{cx, cy}].push_back(n);
        return n;
    };
    struct Seg { int a, b, run; };
    std::vector<Seg> segs;
    for (const RouteSegment& s : segments) {
        if (distance(s.a, s.b) <= tol)
            continue;
        segs.push_back({findOrAdd(s.a), findOrAdd(s.b), runIndex[s.run]});
    }
    // Derivazioni a T: un estremo (nodo) che cade sull'interno di un altro tratto lo spezza.
    for (const Seg& s : segs) {
        const Vec2 a = net.g.nodes[s.a], b = net.g.nodes[s.b];
        std::vector<std::pair<double, int>> cuts{{0.0, s.a}, {1.0, s.b}};
        for (int n = 0; n < static_cast<int>(net.g.nodes.size()); ++n) {
            if (n == s.a || n == s.b)
                continue;
            double t = 0.0;
            const Vec2 q = closestOnSegment(net.g.nodes[n], a, b, &t);
            if (t > 0.0 && t < 1.0 && distance(q, net.g.nodes[n]) <= tol)
                cuts.push_back({t, n});
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t i = 1; i < cuts.size(); ++i) {
            const int u = cuts[i - 1].second, v = cuts[i].second;
            const double len = distance(net.g.nodes[u], net.g.nodes[v]);
            net.g.addEdge(u, v, len, s.run);
            net.pieces.push_back({u, v, s.run});
        }
    }
    return net;
}

// Attacca p alla rete nel punto più vicino (nuovo nodo sul pezzo, con il pezzo diviso in due), se entro maxAccess.
// Restituisce il nodo di p (collegato con un tratto di attacco), oppure -1.
int attach(Network& net, Vec2 p, double maxAccess)
{
    double best = std::numeric_limits<double>::max();
    int bestPiece = -1;
    double bestT = 0.0;
    Vec2 bestQ;
    for (int i = 0; i < static_cast<int>(net.pieces.size()); ++i) {
        const auto& pc = net.pieces[i];
        double t = 0.0;
        const Vec2 q = closestOnSegment(p, net.g.nodes[pc.a], net.g.nodes[pc.b], &t);
        const double d = distance(p, q);
        if (d < best) {
            best = d;
            bestPiece = i;
            bestT = t;
            bestQ = q;
        }
    }
    if (bestPiece < 0 || best > maxAccess)
        return -1;
    const auto pc = net.pieces[bestPiece];
    int foot;
    if (bestT <= 0.0)
        foot = pc.a;
    else if (bestT >= 1.0)
        foot = pc.b;
    else {
        // Nodo nuovo sul pezzo, che si divide in due (così due punti attaccati allo stesso pezzo sono collegati per
        // la via diretta). L'arco del pezzo intero resta: ha la stessa lunghezza dei due mezzi.
        foot = net.g.addNode(bestQ);
        net.g.addEdge(pc.a, foot, distance(net.g.nodes[pc.a], bestQ), pc.run);
        net.g.addEdge(foot, pc.b, distance(bestQ, net.g.nodes[pc.b]), pc.run);
        net.pieces[bestPiece] = {pc.a, foot, pc.run};
        net.pieces.push_back({foot, pc.b, pc.run});
    }
    const int node = net.g.addNode(p);
    net.g.addEdge(node, foot, best, -1);
    return node;
}

// Cammini minimi da `source` (Dijkstra): distanza e arco di arrivo di ogni nodo.
void shortestPaths(const Graph& g, int source, std::vector<double>& dist, std::vector<std::pair<int, int>>& prev)
{
    const double inf = std::numeric_limits<double>::infinity();
    dist.assign(g.nodes.size(), inf);
    prev.assign(g.nodes.size(), {-1, -1});   // nodo precedente, tratta dell'arco
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    dist[source] = 0.0;
    queue.push({0.0, source});
    while (!queue.empty()) {
        const auto [d, u] = queue.top();
        queue.pop();
        if (d > dist[u])
            continue;
        for (const Edge& e : g.adj[u])
            if (d + e.length < dist[e.to]) {
                dist[e.to] = d + e.length;
                prev[e.to] = {u, e.run};
                queue.push({dist[e.to], e.to});
            }
    }
}

} // namespace

std::vector<RouteResult> routeCircuits(const std::vector<RouteSegment>& segments, const std::vector<RouteRequest>& requests,
                                       const RouteOptions& options)
{
    std::vector<RouteResult> out;
    const Network base = buildNetwork(segments, options.joinTolerance);
    for (const RouteRequest& req : requests) {
        RouteResult r;
        r.circuit = req.circuit;
        Network net = base;   // gli attacchi si aggiungono a una copia
        const int source = attach(net, req.from, options.maxAccess);
        r.sourceOnNetwork = source >= 0;
        std::vector<int> targets;
        for (Vec2 p : req.to)
            targets.push_back(source >= 0 ? attach(net, p, options.maxAccess) : -1);
        if (source < 0) {
            r.unreached = static_cast<int>(req.to.size());
            r.unreachedPoints = req.to;
            out.push_back(std::move(r));
            continue;
        }
        std::vector<double> dist;
        std::vector<std::pair<int, int>> prev;
        shortestPaths(net.g, source, dist, prev);
        std::set<int> usedRuns;
        double farthest = 0.0;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const int t = targets[i];
            if (t < 0 || !std::isfinite(dist[t])) {
                ++r.unreached;
                r.unreachedPoints.push_back(req.to[i]);
                continue;
            }
            ++r.reached;
            farthest = std::max(farthest, dist[t]);
            for (int n = t; prev[n].first >= 0; n = prev[n].first)
                if (prev[n].second >= 0)
                    usedRuns.insert(prev[n].second);
        }
        for (int i : usedRuns)
            r.runs.push_back(net.runs[i]);
        std::sort(r.runs.begin(), r.runs.end());
        if (r.reached > 0)
            r.lengthM = farthest * options.unitsToM + options.extraPerCircuit;
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace cartesio::plant
