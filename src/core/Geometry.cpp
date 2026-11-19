#include "core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace cartesio {

namespace {

constexpr double kEps = 1e-9;       // tolleranza lineare / parametrica
constexpr double kAngleEps = 1e-9;  // tolleranza angolare (radianti)

bool inside(const BBox& r, Vec2 p)
{
    return p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y;
}

// Elimina i duplicati da una lista di angoli, anche a cavallo di 0/2π.
std::vector<double> uniqueAngles(std::vector<double> angles)
{
    std::sort(angles.begin(), angles.end());
    std::vector<double> out;
    for (double a : angles)
        if (out.empty() || a - out.back() > kAngleEps)
            out.push_back(a);
    if (out.size() > 1 && out.front() + 2.0 * kPi - out.back() <= kAngleEps)
        out.pop_back();
    return out;
}

template <typename T>
std::unique_ptr<T> withPropsOf(std::unique_ptr<T> piece, const Entity& source)
{
    piece->setLayer(source.layer());
    piece->setColor(source.color());
    piece->setLinetype(source.linetype());
    piece->setLineweight(source.lineweight());
    piece->setLinetypeScale(source.linetypeScale());
    return piece;
}

// --- Polilinee: parametro globale s = indice del tratto + posizione locale (0..1).

struct PolyParam {
    const PolylineEntity& poly;
    std::vector<Prim> prims;

    explicit PolyParam(const PolylineEntity& p) : poly(p), prims(p.prims()) {}

    int segments() const { return static_cast<int>(prims.size()); }

    double bulgeOf(int i) const
    {
        return poly.vertices[static_cast<std::size_t>(i % static_cast<int>(poly.vertices.size()))].bulge;
    }

    // Posizione locale (0..1) di un punto sul tratto i, nel verso della polilinea.
    double local(int i, Vec2 x) const
    {
        const Prim& p = prims[static_cast<std::size_t>(i)];
        if (p.kind == Prim::Kind::Segment) {
            const Vec2 d = p.b - p.a;
            const double len2 = d.dot(d);
            return len2 > 0.0 ? std::clamp((x - p.a).dot(d) / len2, 0.0, 1.0) : 0.0;
        }
        const double f = std::clamp(normalizeAngle(angleOf(x - p.center) - p.start) / p.sweep, 0.0, 1.0);
        return bulgeOf(i) > 0.0 ? f : 1.0 - f;
    }

    // Parametro globale del punto, cercando il tratto più vicino.
    double param(Vec2 x) const
    {
        int best = 0;
        double bestDistance = 1e300;
        for (int i = 0; i < segments(); ++i) {
            const double d = primDistance(prims[static_cast<std::size_t>(i)], x);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best + local(best, x);
    }

    Vec2 pointAt(int i, double u) const
    {
        const Prim& p = prims[static_cast<std::size_t>(i % segments())];
        if (p.kind == Prim::Kind::Segment)
            return p.a + (p.b - p.a) * u;
        const double f = bulgeOf(i) > 0.0 ? u : 1.0 - u;
        return p.pointAt(p.start + p.sweep * f);
    }

    // Tratto di polilinea fra i parametri s0 < s1 (s1 può superare il numero di tratti se chiusa).
    std::unique_ptr<PolylineEntity> sub(double s0, double s1) const
    {
        int i0 = static_cast<int>(std::floor(s0));
        int i1 = static_cast<int>(std::floor(s1));
        double u0 = s0 - i0;
        double u1 = s1 - i1;
        if (u1 < kEps && i1 > i0) {
            --i1;
            u1 = 1.0;
        }
        auto result = std::make_unique<PolylineEntity>();
        for (int i = i0; i <= i1; ++i) {
            const double ua = i == i0 ? u0 : 0.0;
            const double ub = i == i1 ? u1 : 1.0;
            const double b = std::tan((ub - ua) * std::atan(bulgeOf(i)));
            result->vertices.push_back({pointAt(i, ua), b});
        }
        result->vertices.push_back({pointAt(i1, u1), 0.0});
        return result;
    }
};

} // namespace

std::vector<Vec2> intersections(const Entity& a, const Entity& b)
{
    const auto pa = a.prims();
    const auto pb = b.prims();
    std::vector<Vec2> points;
    for (const Prim& p : pa) {
        for (const Prim& q : pb) {
            for (Vec2 x : primIntersections(p, q)) {
                const bool duplicate = std::any_of(points.begin(), points.end(),
                                                   [&](Vec2 y) { return distance(x, y) <= kEps; });
                if (!duplicate)
                    points.push_back(x);
            }
        }
    }
    return points;
}

bool touchesRect(const Entity& e, const BBox& rect)
{
    return e.touchesRect(rect);
}

bool containsBox(const BBox& outer, const BBox& inner)
{
    return inner.isValid() && inside(outer, inner.min) && inside(outer, inner.max);
}

std::unique_ptr<Entity> offsetEntity(const Entity& e, double dist, Vec2 side)
{
    if (dist <= 0.0)
        return nullptr;

    std::unique_ptr<Entity> result = e.clone();
    result->setId(0);

    switch (e.type()) {
    case EntityType::Line: {
        auto& line = static_cast<LineEntity&>(*result);
        const Vec2 dir = line.end - line.start;
        const double len = dir.length();
        if (len < kEps)
            return nullptr;
        const Vec2 normal{-dir.y / len, dir.x / len};   // normale a sinistra
        const double sign = cross(dir, side - line.start) >= 0.0 ? 1.0 : -1.0;
        line.translate(normal * (dist * sign));
        return result;
    }
    case EntityType::Circle: {
        auto& c = static_cast<CircleEntity&>(*result);
        c.radius += distance(side, c.center) < c.radius ? -dist : dist;
        return c.radius > kEps ? std::move(result) : nullptr;
    }
    case EntityType::Arc: {
        auto& a = static_cast<ArcEntity&>(*result);
        a.radius += distance(side, a.center) < a.radius ? -dist : dist;
        return a.radius > kEps ? std::move(result) : nullptr;
    }
    case EntityType::Polyline: {
        // Offset di polilinee composte solo da tratti rettilinei: ogni lato viene spostato e
        // i lati consecutivi vengono raccordati nel loro punto di intersezione.
        auto& poly = static_cast<PolylineEntity&>(*result);
        const int n = static_cast<int>(poly.vertices.size());
        if (n < 2 || std::any_of(poly.vertices.begin(), poly.vertices.end(),
                                 [](const PolyVertex& v) { return std::abs(v.bulge) > 1e-12; }))
            return nullptr;
        // Lato su cui spostare: quello del punto rispetto al lato più vicino.
        const auto prims = poly.prims();
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < prims.size(); ++i)
            if (primDistance(prims[i], side) < primDistance(prims[nearest], side))
                nearest = i;
        const double sign = cross(prims[nearest].b - prims[nearest].a, side - prims[nearest].a) >= 0.0 ? 1.0 : -1.0;

        std::vector<std::pair<Vec2, Vec2>> lines;   // lati spostati
        for (const Prim& p : prims) {
            const Vec2 d = p.b - p.a;
            const double len = d.length();
            if (len < kEps)
                continue;
            const Vec2 shift = Vec2{-d.y / len, d.x / len} * (dist * sign);
            lines.emplace_back(p.a + shift, p.b + shift);
        }
        const auto meet = [](const std::pair<Vec2, Vec2>& l1, const std::pair<Vec2, Vec2>& l2) {
            const Vec2 r = l1.second - l1.first;
            const Vec2 s = l2.second - l2.first;
            const double den = cross(r, s);
            if (std::abs(den) < 1e-12)
                return l1.second;
            return l1.first + r * (cross(l2.first - l1.first, s) / den);
        };
        const int m = static_cast<int>(lines.size());
        std::vector<PolyVertex> vertices;
        for (int i = 0; i < m; ++i) {
            if (!poly.closed && i == 0) {
                vertices.push_back({lines[0].first, 0.0});
                continue;
            }
            const auto& prev = lines[static_cast<std::size_t>((i - 1 + m) % m)];
            vertices.push_back({meet(prev, lines[static_cast<std::size_t>(i)]), 0.0});
        }
        if (!poly.closed && m > 0)
            vertices.push_back({lines.back().second, 0.0});
        poly.vertices = std::move(vertices);
        return result;
    }
    default:
        return nullptr;
    }
}

std::optional<std::vector<std::unique_ptr<Entity>>> trimEntity(const Entity& target,
                                                              const std::vector<Vec2>& cuts,
                                                              Vec2 pick)
{
    std::vector<std::unique_ptr<Entity>> pieces;

    switch (target.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(target);
        const Vec2 d = line.end - line.start;
        const double len2 = d.dot(d);
        if (len2 == 0.0)
            return pieces;
        const auto param = [&](Vec2 p) { return (p - line.start).dot(d) / len2; };

        const double tp = param(pick);
        std::optional<double> before, after;
        for (Vec2 c : cuts) {
            const double t = param(c);
            if (t <= kEps || t >= 1.0 - kEps)
                continue; // i punti di taglio sugli estremi non contano
            if (t < tp && (!before || t > *before))
                before = t;
            if (t > tp && (!after || t < *after))
                after = t;
        }
        if (before)
            pieces.push_back(withPropsOf(std::make_unique<LineEntity>(line.start, line.start + d * *before), target));
        if (after)
            pieces.push_back(withPropsOf(std::make_unique<LineEntity>(line.start + d * *after, line.end), target));
        return pieces;
    }

    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(target);
        std::vector<double> angles;
        for (Vec2 c : cuts)
            angles.push_back(normalizeAngle(angleOf(c - circle.center)));
        angles = uniqueAngles(std::move(angles));
        if (angles.empty())
            return pieces;              // nessun taglio: si cancella il cerchio
        if (angles.size() == 1)
            return std::nullopt;        // un solo punto: non si può aprire il cerchio

        const double ap = normalizeAngle(angleOf(pick - circle.center));
        // Il tratto rimosso va dal taglio precedente (orario) al successivo (antiorario).
        double next = angles.front();
        for (double a : angles)
            if (a > ap) { next = a; break; }
        double prev = angles.back();
        for (auto it = angles.rbegin(); it != angles.rend(); ++it)
            if (*it < ap) { prev = *it; break; }

        pieces.push_back(withPropsOf(std::make_unique<ArcEntity>(circle.center, circle.radius, next, prev), target));
        return pieces;
    }

    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(target);
        const double sweep = arc.sweep();
        const auto param = [&](Vec2 p) { return normalizeAngle(angleOf(p - arc.center) - arc.startAngle); };

        double up = param(pick);
        if (up > sweep)
            up = (up - sweep < 2.0 * kPi - up) ? sweep : 0.0;

        std::optional<double> before, after;
        for (Vec2 c : cuts) {
            const double u = param(c);
            if (u <= kAngleEps || u >= sweep - kAngleEps)
                continue;
            if (u < up && (!before || u > *before))
                before = u;
            if (u > up && (!after || u < *after))
                after = u;
        }
        if (before)
            pieces.push_back(withPropsOf(std::make_unique<ArcEntity>(
                arc.center, arc.radius, arc.startAngle, arc.startAngle + *before), target));
        if (after)
            pieces.push_back(withPropsOf(std::make_unique<ArcEntity>(
                arc.center, arc.radius, arc.startAngle + *after, arc.endAngle), target));
        return pieces;
    }

    case EntityType::Polyline: {
        const auto& poly = static_cast<const PolylineEntity&>(target);
        const PolyParam pp(poly);
        const int segs = pp.segments();
        if (segs == 0)
            return pieces;

        std::vector<double> params;
        for (Vec2 c : cuts) {
            const double s = pp.param(c);
            if (!poly.closed && (s <= kEps || s >= segs - kEps))
                continue;
            if (std::none_of(params.begin(), params.end(), [&](double x) { return std::abs(x - s) <= 1e-9; }))
                params.push_back(s);
        }
        std::sort(params.begin(), params.end());
        const double sp = pp.param(pick);

        if (!poly.closed) {
            std::optional<double> before, after;
            for (double s : params) {
                if (s < sp)
                    before = s;
                if (s > sp && !after)
                    after = s;
            }
            if (before)
                pieces.push_back(withPropsOf(pp.sub(0.0, *before), target));
            if (after)
                pieces.push_back(withPropsOf(pp.sub(*after, segs), target));
            return pieces;
        }

        if (params.empty())
            return pieces;
        if (params.size() == 1)
            return std::nullopt;
        // Polilinea chiusa: resta il tratto dal taglio successivo a quello precedente (passando per l'inizio).
        double after = params.front() + segs;
        for (double s : params)
            if (s > sp) { after = s; break; }
        double before = params.back() - segs;
        for (auto it = params.rbegin(); it != params.rend(); ++it)
            if (*it < sp) { before = *it; break; }
        if (after > segs) {   // normalizzazione: il tratto parte nel primo giro
            after -= segs;
            before -= segs;
        }
        pieces.push_back(withPropsOf(pp.sub(after, before + segs), target));
        return pieces;
    }

    default:
        return std::nullopt;
    }
}

std::unique_ptr<ArcEntity> arcThroughPoints(Vec2 p1, Vec2 p2, Vec2 p3)
{
    const double d = 2.0 * (p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y));
    const double scale = std::max({distance(p1, p2), distance(p2, p3), distance(p1, p3)});
    if (scale < kEps || std::abs(d) < 1e-12 * scale * scale)
        return nullptr;

    const double s1 = p1.dot(p1), s2 = p2.dot(p2), s3 = p3.dot(p3);
    const Vec2 center{(s1 * (p2.y - p3.y) + s2 * (p3.y - p1.y) + s3 * (p1.y - p2.y)) / d,
                      (s1 * (p3.x - p2.x) + s2 * (p1.x - p3.x) + s3 * (p2.x - p1.x)) / d};
    const double radius = distance(center, p1);
    const double a1 = normalizeAngle(angleOf(p1 - center));
    const double a3 = normalizeAngle(angleOf(p3 - center));

    // Antiorario se p1 -> p2 -> p3 gira a sinistra, altrimenti si scambiano gli estremi.
    if (cross(p2 - p1, p3 - p1) > 0.0)
        return std::make_unique<ArcEntity>(center, radius, a1, a3);
    return std::make_unique<ArcEntity>(center, radius, a3, a1);
}

} // namespace cartesio

namespace cartesio {

std::unique_ptr<Entity> extendEntity(const Entity& target, const std::vector<const Entity*>& boundaries, Vec2 pick)
{
    constexpr double eps = 1e-9;
    std::vector<Prim> edges;
    for (const Entity* b : boundaries)
        b->decompose(edges);

    if (target.type() == EntityType::Line) {
        const auto& line = static_cast<const LineEntity&>(target);
        const bool atEnd = distance(pick, line.end) < distance(pick, line.start);
        const Vec2 tip = atEnd ? line.end : line.start;
        const Vec2 other = atEnd ? line.start : line.end;
        const double len = distance(tip, other);
        if (len < eps)
            return nullptr;
        const Vec2 dir = (tip - other) / len;
        // Raggio lungo quanto l'insieme degli oggetti di contorno.
        BBox all;
        all.expand(tip);
        for (const Prim& p : edges)
            primBounds(p, all);
        const double reach = (all.width() + all.height()) * 2.0 + len;
        const Prim ray = Prim::segment(tip, tip + dir * reach);
        std::optional<double> best;
        for (const Prim& p : edges)
            for (Vec2 x : primIntersections(ray, p)) {
                const double t = (x - tip).dot(dir);
                if (t > eps * (1.0 + len) && (!best || t < *best))
                    best = t;
            }
        if (!best)
            return nullptr;
        auto result = std::make_unique<LineEntity>(line);
        (atEnd ? result->end : result->start) = tip + dir * *best;
        return result;
    }

    if (target.type() == EntityType::Arc) {
        const auto& arc = static_cast<const ArcEntity&>(target);
        const bool atEnd = distance(pick, arc.endPoint()) < distance(pick, arc.startPoint());
        const Prim circle = Prim::arc(arc.center, arc.radius, 0.0, 2.0 * kPi);
        const double free = 2.0 * kPi - arc.sweep();   // spazio fra i due estremi
        std::optional<double> best;
        for (const Prim& p : edges)
            for (Vec2 x : primIntersections(circle, p)) {
                const double a = angleOf(x - arc.center);
                const double gap = atEnd ? normalizeAngle(a - arc.endAngle) : normalizeAngle(arc.startAngle - a);
                if (gap > eps && gap < free - eps && (!best || gap < *best))
                    best = gap;
            }
        if (!best)
            return nullptr;
        auto result = std::make_unique<ArcEntity>(arc);
        if (atEnd)
            result->endAngle += *best;
        else
            result->startAngle -= *best;
        return result;
    }
    return nullptr;
}

std::optional<FilletResult> filletLines(const LineEntity& a, Vec2 pickA, const LineEntity& b, Vec2 pickB, double radius)
{
    constexpr double eps = 1e-9;
    // Intersezione delle rette.
    const Vec2 r = a.end - a.start;
    const Vec2 s = b.end - b.start;
    const double den = cross(r, s);
    if (std::abs(den) < 1e-12 * r.length() * s.length())
        return std::nullopt;   // parallele
    const Vec2 p = a.start + r * (cross(b.start - a.start, s) / den);

    // Per ogni linea si tiene la parte dal lato del punto cliccato; l'estremo lontano resta fisso.
    const auto keep = [&](const LineEntity& line, Vec2 pick) {
        const Vec2 d = line.end - line.start;
        const double tp = (pick - line.start).dot(d) / d.dot(d);
        const double ti = (p - line.start).dot(d) / d.dot(d);
        return tp < ti ? line.start : line.end;
    };
    const Vec2 farA = keep(a, pickA);
    const Vec2 farB = keep(b, pickB);
    if (distance(farA, p) < eps || distance(farB, p) < eps)
        return std::nullopt;
    const Vec2 ua = (farA - p) / distance(farA, p);
    const Vec2 ub = (farB - p) / distance(farB, p);

    FilletResult result;
    result.first = std::make_unique<LineEntity>(a);
    result.second = std::make_unique<LineEntity>(b);
    if (radius <= 0.0) {
        result.first->start = farA;
        result.first->end = p;
        result.second->start = farB;
        result.second->end = p;
        return result;
    }

    const double theta = std::acos(std::clamp(ua.dot(ub), -1.0, 1.0));
    if (theta < 1e-9 || theta > kPi - 1e-9)
        return std::nullopt;
    const double tangent = radius / std::tan(theta / 2.0);
    if (tangent > distance(farA, p) + eps || tangent > distance(farB, p) + eps)
        return std::nullopt;   // raggio troppo grande per queste linee
    const Vec2 ta = p + ua * tangent;
    const Vec2 tb = p + ub * tangent;
    const Vec2 bisector = (ua + ub) / (ua + ub).length();
    const Vec2 center = p + bisector * (radius / std::sin(theta / 2.0));

    result.first->start = farA;
    result.first->end = ta;
    result.second->start = farB;
    result.second->end = tb;
    double a1 = angleOf(ta - center);
    double a2 = angleOf(tb - center);
    if (normalizeAngle(a2 - a1) > kPi)
        std::swap(a1, a2);
    result.arc = std::make_unique<ArcEntity>(center, radius, a1, a2);
    result.arc->copyStyleFrom(a);
    return result;
}

std::unique_ptr<Entity> gripEdit(const Entity& e, std::size_t index, Vec2 to)
{
    constexpr double eps = 1e-9;
    const auto points = e.gripPoints();
    if (e.type() == EntityType::MText && (index == 1 || index == 2)) {   // frecce della larghezza
        auto copy = e.clone();
        static_cast<MTextEntity&>(*copy).dragWidth(index == 1, to);
        return copy;
    }
    if (index >= points.size())
        return nullptr;
    const Vec2 from = points[index].position;
    std::unique_ptr<Entity> copy = e.clone();

    switch (e.type()) {
    case EntityType::Line: {
        auto& l = static_cast<LineEntity&>(*copy);
        if (index == 0)
            l.start = to;
        else if (index == 1)
            l.end = to;
        else
            l.translate(to - from);
        return copy;
    }
    case EntityType::Circle: {
        auto& c = static_cast<CircleEntity&>(*copy);
        if (index == 0)
            c.center = to;
        else if (distance(c.center, to) > eps)
            c.radius = distance(c.center, to);
        return copy;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        const Vec2 mid = a.pointAt(a.startAngle + a.sweep() / 2.0);
        std::unique_ptr<ArcEntity> result;
        if (index == 0)
            result = arcThroughPoints(to, mid, a.endPoint());
        else if (index == 1)
            result = arcThroughPoints(a.startPoint(), mid, to);
        else if (index == 2)
            result = arcThroughPoints(a.startPoint(), to, a.endPoint());
        if (index <= 2) {
            if (!result)
                return nullptr;
            result->setId(e.id());
            result->copyStyleFrom(e);
            result->setSpace(e.space());
            return result;
        }
        auto& c = static_cast<ArcEntity&>(*copy);
        if (index == 3)
            c.center = to;
        else if (distance(c.center, to) > eps)
            c.radius = distance(c.center, to);
        return copy;
    }
    case EntityType::Polyline: {
        auto& p = static_cast<PolylineEntity&>(*copy);
        const std::size_t n = p.vertices.size();
        if (index < n) {
            p.vertices[index].point = to;
        } else {   // punto medio di un tratto: si sposta il tratto intero
            const std::size_t k = index - n;
            p.vertices[k].point = p.vertices[k].point + (to - from);
            p.vertices[(k + 1) % n].point = p.vertices[(k + 1) % n].point + (to - from);
        }
        return copy;
    }
    case EntityType::Solid:
        static_cast<SolidEntity&>(*copy).corners[index] = to;
        return copy;
    case EntityType::Dimension: {
        auto& d = static_cast<DimensionEntity&>(*copy);
        if (points[index].kind == SnapKind::Insertion) {   // il testo: dove lo si lascia
            d.textMoved = true;
            d.textPos = to;
        } else if (d.kind == DimensionEntity::Linear || d.kind == DimensionEntity::Aligned) {
            if (index == 0)
                d.p1 = to;
            else if (index == 1)
                d.p2 = to;
            else
                d.dimLine = to;   // estremo della linea di quota: la linea si sposta
        } else if (d.kind == DimensionEntity::Angular || d.kind == DimensionEntity::Angular3) {
            if (index == 0)
                d.p1 = to;
            else if (index == 1)
                d.p2 = to;
            else if (index == 2)
                d.curvePoint = to;
            else
                d.dimLine = to;
        } else if (d.kind == DimensionEntity::Radius) {   // punto sul cerchio: si gira attorno al centro
            const double r = distance(d.dimLine, d.curvePoint);
            if (distance(to, d.dimLine) < eps)
                return nullptr;
            d.curvePoint = d.dimLine + (to - d.dimLine) / distance(to, d.dimLine) * r;
        } else if (d.kind == DimensionEntity::Diameter) {
            const Vec2 c = (d.dimLine + d.curvePoint) / 2.0;
            const double r = distance(d.dimLine, d.curvePoint) / 2.0;
            if (distance(to, c) < eps)
                return nullptr;
            const Vec2 u = (to - c) / distance(to, c);
            d.curvePoint = c + u * r;
            d.dimLine = c - u * r;
        } else if (d.kind == DimensionEntity::Ordinate) {
            (index == 0 ? d.p1 : d.p2) = to;
        }
        d.block.reset();   // la geometria si ricalcola
        d.blockName.clear();
        return copy;
    }
    case EntityType::Leader: {
        // Frecce, gomito e approdo: si sposta il punto della maniglia; l'ultimo (l'approdo) porta con sé il testo e
        // il tratto orizzontale.
        auto& l = static_cast<LeaderEntity&>(*copy);
        const std::size_t n = l.points.size();
        if (index < n) {
            if (index + 1 == n && n >= 3 && l.junction().x != l.points.back().x) {
                const Vec2 d = to - l.points[index];
                l.points[index] = l.points[index] + d;
                l.points[index - 1] = l.points[index - 1] + d;   // il tratto d'approdo resta orizzontale
            } else {
                l.points[index] = to;
            }
        } else if (index - n < l.extraTips.size()) {
            l.extraTips[index - n] = to;
        }
        return copy;
    }
    case EntityType::Viewport: {
        auto& v = static_cast<ViewportEntity&>(*copy);
        if (index >= 4) {
            v.center = to;
            return copy;
        }
        // Angolo trascinato: l'angolo opposto resta fermo, la scala non cambia.
        const double scale = v.scale();
        const Vec2 opposite = points[(index + 2) % 4].position;
        v.center = (opposite + to) / 2.0;
        v.width = std::abs(to.x - opposite.x);
        v.height = std::abs(to.y - opposite.y);
        if (v.width < eps || v.height < eps)
            return nullptr;
        v.setScale(scale);
        return copy;
    }
    default:
        copy->translate(to - from);
        return copy;
    }
}

void keepTextReadable(Entity& e)
{
    const auto fix = [](TextEntity& t) {
        const double r = normalizeAngle(t.rotation);
        if (std::cos(r) < -1e-9 || (std::abs(std::cos(r)) <= 1e-9 && std::sin(r) < 0.0))
            t.rotation = r + kPi;
    };
    switch (e.type()) {
    case EntityType::Text:
        fix(static_cast<TextEntity&>(e));
        break;
    case EntityType::AttDef:
        fix(static_cast<AttDefEntity&>(e).text);
        break;
    case EntityType::MText: {
        auto& m = static_cast<MTextEntity&>(e);
        if (std::cos(normalizeAngle(m.rotation)) < -1e-9)
            m.rotation = normalizeAngle(m.rotation) + kPi;
        break;
    }
    case EntityType::Insert:
        for (Attribute& a : static_cast<InsertEntity&>(e).attributes)
            fix(a.text);
        break;
    default:
        break;
    }
}

} // namespace cartesio

namespace cartesio {

std::vector<Vec2> closedOutline(const Entity& e, bool force)
{
    bool closed = force;
    switch (e.type()) {
    case EntityType::Circle:
        closed = true;
        break;
    case EntityType::Polyline:
        closed = closed || static_cast<const PolylineEntity&>(e).closed;
        break;
    case EntityType::Spline:
        closed = closed || static_cast<const SplineEntity&>(e).closed;
        break;
    case EntityType::Ellipse: {
        const auto& el = static_cast<const EllipseEntity&>(e);
        closed = closed || std::abs(el.endParam - el.startParam) >= 2.0 * kPi - 1e-9;
        break;
    }
    default:
        break;
    }
    if (!closed)
        return {};

    std::vector<Vec2> points;
    for (const Prim& p : e.prims()) {
        if (p.kind == Prim::Kind::Segment) {
            points.push_back(p.a);
            continue;
        }
        // Archi: un punto ogni 5° circa.
        const int steps = std::max(4, static_cast<int>(std::ceil(p.sweep / degToRad(5.0))));
        for (int i = 0; i < steps; ++i)
            points.push_back(p.pointAt(p.start + p.sweep * i / steps));
    }
    // Niente punti doppi consecutivi.
    std::vector<Vec2> clean;
    for (Vec2 p : points)
        if (clean.empty() || distance(clean.back(), p) > 1e-9)
            clean.push_back(p);
    if (clean.size() > 1 && distance(clean.front(), clean.back()) <= 1e-9)
        clean.pop_back();
    if (clean.size() < 3)
        return {};
    return clean;
}

} // namespace cartesio

namespace cartesio {

// ------------------------------------------------------------------- CHAMFER

std::optional<ChamferResult> chamferLines(const LineEntity& a, Vec2 pickA, const LineEntity& b, Vec2 pickB, double d1,
                                          double d2)
{
    constexpr double eps = 1e-9;
    const Vec2 r = a.end - a.start;
    const Vec2 s = b.end - b.start;
    const double den = cross(r, s);
    if (std::abs(den) < 1e-12 * r.length() * s.length())
        return std::nullopt;   // parallele
    const Vec2 p = a.start + r * (cross(b.start - a.start, s) / den);

    const auto keep = [&](const LineEntity& line, Vec2 pick) {
        const Vec2 d = line.end - line.start;
        const double tp = (pick - line.start).dot(d) / d.dot(d);
        const double ti = (p - line.start).dot(d) / d.dot(d);
        return tp < ti ? line.start : line.end;
    };
    const Vec2 farA = keep(a, pickA);
    const Vec2 farB = keep(b, pickB);
    const double la = distance(farA, p);
    const double lb = distance(farB, p);
    if (la < eps || lb < eps || d1 > la + eps || d2 > lb + eps)
        return std::nullopt;   // distanze troppo grandi per queste linee
    const Vec2 ta = p + (farA - p) / la * d1;
    const Vec2 tb = p + (farB - p) / lb * d2;

    ChamferResult result;
    result.first = std::make_unique<LineEntity>(a);
    result.second = std::make_unique<LineEntity>(b);
    result.first->start = farA;
    result.first->end = ta;
    result.second->start = farB;
    result.second->end = tb;
    if (distance(ta, tb) > eps) {
        result.chamfer = std::make_unique<LineEntity>(ta, tb);
        result.chamfer->copyStyleFrom(a);
    }
    return result;
}

// --------------------------------------------------------------------- BREAK

std::optional<std::vector<std::unique_ptr<Entity>>> breakEntity(const Entity& e, Vec2 p1, Vec2 p2)
{
    constexpr double eps = 1e-9;
    std::vector<std::unique_ptr<Entity>> pieces;
    const bool split = distance(p1, p2) < eps;

    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        const Vec2 d = line.end - line.start;
        const double len2 = d.dot(d);
        if (len2 == 0.0)
            return std::nullopt;
        double t1 = std::clamp((p1 - line.start).dot(d) / len2, 0.0, 1.0);
        double t2 = std::clamp((p2 - line.start).dot(d) / len2, 0.0, 1.0);
        if (split)
            t2 = t1;
        if (t1 > t2)
            std::swap(t1, t2);
        if (t1 > eps)
            pieces.push_back(withPropsOf(std::make_unique<LineEntity>(line.start, line.start + d * t1), e));
        if (t2 < 1.0 - eps)
            pieces.push_back(withPropsOf(std::make_unique<LineEntity>(line.start + d * t2, line.end), e));
        return pieces;
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        const double sweep = arc.sweep();
        const auto param = [&](Vec2 p) {
            double u = normalizeAngle(angleOf(p - arc.center) - arc.startAngle);
            if (u > sweep)   // fuori dall'arco: l'estremo più vicino
                u = (u - sweep < 2.0 * kPi - u) ? sweep : 0.0;
            return u;
        };
        double u1 = param(p1);
        double u2 = split ? u1 : param(p2);
        if (u1 > u2)
            std::swap(u1, u2);
        if (u1 > kAngleEps)
            pieces.push_back(withPropsOf(
                std::make_unique<ArcEntity>(arc.center, arc.radius, arc.startAngle, arc.startAngle + u1), e));
        if (u2 < sweep - kAngleEps)
            pieces.push_back(withPropsOf(
                std::make_unique<ArcEntity>(arc.center, arc.radius, arc.startAngle + u2, arc.endAngle), e));
        return pieces;
    }
    case EntityType::Circle: {
        if (split)
            return std::nullopt;   // un cerchio non si apre in un solo punto
        const auto& c = static_cast<const CircleEntity&>(e);
        // Si toglie il tratto antiorario da p1 a p2: resta l'arco da p2 a p1.
        const double a1 = angleOf(p1 - c.center);
        const double a2 = angleOf(p2 - c.center);
        if (std::abs(normalizeAngle(a2 - a1)) < kAngleEps)
            return std::nullopt;
        pieces.push_back(withPropsOf(std::make_unique<ArcEntity>(c.center, c.radius, a2, a1), e));
        return pieces;
    }
    case EntityType::Polyline: {
        const auto& poly = static_cast<const PolylineEntity&>(e);
        const PolyParam pp(poly);
        const double n = pp.segments();
        if (n < 1)
            return std::nullopt;
        double s1 = pp.param(p1);
        double s2 = split ? s1 : pp.param(p2);
        const auto add = [&](double a, double b) {
            if (b - a > 1e-9) {
                auto piece = pp.sub(a, b);
                piece->closed = false;
                pieces.push_back(withPropsOf(std::move(piece), e));
            }
        };
        if (poly.closed) {
            // Polilinea chiusa: si toglie il tratto da p1 a p2 nel verso della polilinea.
            if (split)
                add(s1, s1 + n);
            else if (s1 < s2)
                add(s2, s1 + n);
            else
                add(s2, s1);
            return pieces;
        }
        if (s1 > s2)
            std::swap(s1, s2);
        add(0.0, s1);
        add(s2, n);
        return pieces;
    }
    default:
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------- JOIN

namespace {

// Pezzo da concatenare: vertici con il bulge del tratto che segue.
struct Chain {
    std::vector<PolyVertex> vertices;
    const Entity* source = nullptr;

    Vec2 front() const { return vertices.front().point; }
    Vec2 back() const { return vertices.back().point; }

    void reverse()
    {
        std::vector<PolyVertex> r;
        for (std::size_t i = vertices.size(); i-- > 0;) {
            const double bulge = i > 0 ? -vertices[i - 1].bulge : 0.0;
            r.push_back({vertices[i].point, bulge});
        }
        vertices = std::move(r);
    }
};

std::optional<Chain> chainOf(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        return Chain{{{l.start, 0.0}, {l.end, 0.0}}, &e};
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        return Chain{{{a.startPoint(), std::tan(a.sweep() / 4.0)}, {a.endPoint(), 0.0}}, &e};
    }
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        if (p.closed || p.vertices.size() < 2)
            return std::nullopt;
        Chain c{p.vertices, &e};
        c.vertices.back().bulge = 0.0;
        return c;
    }
    default:
        return std::nullopt;
    }
}

} // namespace

std::vector<std::unique_ptr<Entity>> joinEntities(const std::vector<const Entity*>& entities, double tol,
                                                  std::vector<EntityId>& consumed)
{
    std::vector<std::unique_ptr<Entity>> result;
    std::vector<bool> used(entities.size(), false);

    // 1. Linee sulla stessa retta che si toccano o si sovrappongono: una linea sola.
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (used[i] || entities[i]->type() != EntityType::Line)
            continue;
        const auto& base = static_cast<const LineEntity&>(*entities[i]);
        const Vec2 d = base.end - base.start;
        const double len = d.length();
        if (len < tol)
            continue;
        const Vec2 u = d / len;
        double t0 = 0.0, t1 = len;
        std::vector<std::size_t> group{i};
        for (bool grew = true; grew;) {
            grew = false;
            for (std::size_t j = 0; j < entities.size(); ++j) {
                if (used[j] || entities[j]->type() != EntityType::Line
                    || std::find(group.begin(), group.end(), j) != group.end())
                    continue;
                const auto& l = static_cast<const LineEntity&>(*entities[j]);
                if (std::abs(cross(u, l.start - base.start)) > tol || std::abs(cross(u, l.end - base.start)) > tol)
                    continue;
                double a = (l.start - base.start).dot(u);
                double b = (l.end - base.start).dot(u);
                if (a > b)
                    std::swap(a, b);
                if (b < t0 - tol || a > t1 + tol)
                    continue;   // stessa retta ma staccate
                t0 = std::min(t0, a);
                t1 = std::max(t1, b);
                group.push_back(j);
                grew = true;
            }
        }
        if (group.size() < 2)
            continue;
        for (std::size_t j : group) {
            used[j] = true;
            consumed.push_back(entities[j]->id());
        }
        auto line = std::make_unique<LineEntity>(base.start + u * t0, base.start + u * t1);
        line->copyStyleFrom(base);
        result.push_back(std::move(line));
    }

    // 2. Archi dello stesso cerchio che si toccano: un arco (o un cerchio se si chiude).
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (used[i] || entities[i]->type() != EntityType::Arc)
            continue;
        const auto& base = static_cast<const ArcEntity&>(*entities[i]);
        double start = base.startAngle;
        double sweep = base.sweep();
        std::vector<std::size_t> group{i};
        for (bool grew = true; grew && sweep < 2.0 * kPi - kAngleEps;) {
            grew = false;
            for (std::size_t j = 0; j < entities.size(); ++j) {
                if (used[j] || entities[j]->type() != EntityType::Arc
                    || std::find(group.begin(), group.end(), j) != group.end())
                    continue;
                const auto& a = static_cast<const ArcEntity&>(*entities[j]);
                if (distance(a.center, base.center) > tol || std::abs(a.radius - base.radius) > tol)
                    continue;
                const double angTol = tol / base.radius;
                const double s = normalizeAngle(a.startAngle - start);   // inizio dell'altro rispetto al nostro
                if (s <= sweep + angTol) {
                    // L'altro comincia dentro (o alla fine) del nostro: si allunga in avanti.
                    sweep = std::max(sweep, s + a.sweep());
                } else if (normalizeAngle(start - a.startAngle) <= a.sweep() + angTol) {
                    // L'altro arriva fino all'inizio del nostro: si allunga all'indietro.
                    const double back = normalizeAngle(start - a.startAngle);
                    start = a.startAngle;
                    sweep = std::max(sweep + back, a.sweep());
                } else {
                    continue;
                }
                group.push_back(j);
                grew = true;
            }
        }
        if (group.size() < 2)
            continue;
        for (std::size_t j : group) {
            used[j] = true;
            consumed.push_back(entities[j]->id());
        }
        std::unique_ptr<Entity> joined;
        if (sweep >= 2.0 * kPi - kAngleEps)
            joined = std::make_unique<CircleEntity>(base.center, base.radius);
        else
            joined = std::make_unique<ArcEntity>(base.center, base.radius, start, start + sweep);
        joined->copyStyleFrom(base);
        result.push_back(std::move(joined));
    }

    // 3. Catene di linee, archi e polilinee aperte che si toccano agli estremi: una polilinea.
    std::vector<Chain> pieces;
    std::vector<std::size_t> pieceIndex;
    for (std::size_t i = 0; i < entities.size(); ++i)
        if (!used[i])
            if (auto c = chainOf(*entities[i])) {
                pieces.push_back(std::move(*c));
                pieceIndex.push_back(i);
            }
    std::vector<bool> taken(pieces.size(), false);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (taken[i])
            continue;
        taken[i] = true;
        Chain chain = pieces[i];
        std::vector<std::size_t> members{i};
        for (bool grew = true; grew;) {
            grew = false;
            for (std::size_t j = 0; j < pieces.size(); ++j) {
                if (taken[j])
                    continue;
                Chain c = pieces[j];
                if (distance(chain.back(), c.back()) <= tol || distance(chain.front(), c.front()) <= tol)
                    c.reverse();
                if (distance(chain.back(), c.front()) <= tol) {
                    chain.vertices.back().bulge = c.vertices.front().bulge;
                    chain.vertices.insert(chain.vertices.end(), c.vertices.begin() + 1, c.vertices.end());
                } else if (distance(c.back(), chain.front()) <= tol) {
                    c.vertices.back().bulge = chain.vertices.front().bulge;
                    c.vertices.insert(c.vertices.end(), chain.vertices.begin() + 1, chain.vertices.end());
                    chain.vertices = std::move(c.vertices);
                } else {
                    continue;
                }
                taken[j] = true;
                members.push_back(j);
                grew = true;
            }
        }
        if (members.size() < 2)
            continue;
        bool closed = false;
        if (chain.vertices.size() > 2 && distance(chain.front(), chain.back()) <= tol) {
            // L'ultimo vertice coincide con il primo: il tratto di chiusura è già nel penultimo.
            chain.vertices.pop_back();
            closed = true;
        }
        for (std::size_t m : members)
            consumed.push_back(entities[pieceIndex[m]]->id());
        auto poly = std::make_unique<PolylineEntity>(chain.vertices, closed);
        poly->copyStyleFrom(*pieces[i].source);
        result.push_back(std::move(poly));
    }
    return result;
}

// -------------------------------------------------------------------- STRETCH

std::unique_ptr<Entity> stretchEntity(const Entity& e, const BBox& window, Vec2 delta)
{
    const auto in = [&window](Vec2 p) { return inside(window, p); };
    auto copy = e.clone();
    // Oggetto interamente dentro la finestra: si sposta.
    if (containsBox(window, e.bounds())) {
        copy->translate(delta);
        return copy;
    }
    bool changed = false;
    const auto move = [&](Vec2& p) {
        if (in(p)) {
            p = p + delta;
            changed = true;
        }
    };
    switch (copy->type()) {
    case EntityType::Line: {
        auto& l = static_cast<LineEntity&>(*copy);
        move(l.start);
        move(l.end);
        break;
    }
    case EntityType::Polyline:
        for (PolyVertex& v : static_cast<PolylineEntity&>(*copy).vertices)
            move(v.point);
        break;
    case EntityType::Solid:
        for (Vec2& c : static_cast<SolidEntity&>(*copy).corners)
            move(c);
        break;
    case EntityType::Spline: {
        auto& s = static_cast<SplineEntity&>(*copy);
        for (Vec2& c : s.control)
            move(c);
        for (Vec2& f : s.fit)
            move(f);
        break;
    }
    case EntityType::Hatch:
        for (auto& loop : static_cast<HatchEntity&>(*copy).loops)
            for (PolyVertex& v : loop)
                move(v.point);
        break;
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        const bool s = in(arc.startPoint());
        const bool t = in(arc.endPoint());
        if (!s && !t)
            break;
        // Un estremo dentro: arco per i nuovi estremi e per il punto medio spostato di metà.
        const Vec2 mid = arc.pointAt(arc.startAngle + arc.sweep() / 2.0);
        auto stretched = arcThroughPoints(s ? arc.startPoint() + delta : arc.startPoint(),
                                          (s && t) ? mid + delta : mid + delta / 2.0,
                                          t ? arc.endPoint() + delta : arc.endPoint());
        if (!stretched)
            return nullptr;
        stretched->setId(e.id());
        stretched->copyStyleFrom(e);
        stretched->setSpace(e.space());
        return stretched;
    }
    case EntityType::Dimension: {
        auto& d = static_cast<DimensionEntity&>(*copy);
        move(d.p1);
        move(d.p2);
        move(d.dimLine);
        move(d.textPos);
        move(d.curvePoint);
        if (changed) {   // la geometria si ricalcola dai punti di definizione
            d.block.reset();
            d.blockName.clear();
        }
        break;
    }
    case EntityType::Circle:
        move(static_cast<CircleEntity&>(*copy).center);
        break;
    default: {
        // Testi, blocchi, punti, ellissi: si spostano se il punto di inserimento è nella finestra.
        const auto points = e.snapPoints();
        if (!points.empty() && in(points.front().position)) {
            copy->translate(delta);
            return copy;
        }
        break;
    }
    }
    if (!changed)
        return nullptr;
    return copy;
}


// ------------------------------------------------------- maniglie multifunzione

std::vector<GripAction> gripActions(const Entity& e, std::size_t index)
{
    switch (e.type()) {
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        const std::size_t n = p.vertices.size();
        if (index < n) {
            std::vector<GripAction> a{GripAction::Stretch, GripAction::AddVertex};
            if (n > 2)
                a.push_back(GripAction::RemoveVertex);
            return a;
        }
        const std::size_t k = index - n;
        if (k >= n)
            return {};
        const bool arc = std::abs(p.vertices[k].bulge) > 1e-12;
        return {GripAction::Stretch, GripAction::AddVertex, arc ? GripAction::ToLine : GripAction::ToArc};
    }
    case EntityType::Line:
    case EntityType::Arc:
        if (index <= 1)
            return {GripAction::Stretch, GripAction::Lengthen};
        return {};
    default:
        return {};
    }
}

bool gripActionNeedsPoint(GripAction action)
{
    return action != GripAction::RemoveVertex && action != GripAction::ToLine;
}

std::unique_ptr<Entity> gripApply(const Entity& e, std::size_t index, GripAction action, Vec2 to)
{
    if (action == GripAction::Stretch)
        return gripEdit(e, index, to);
    std::unique_ptr<Entity> copy = e.clone();
    if (e.type() == EntityType::Polyline) {
        auto& p = static_cast<PolylineEntity&>(*copy);
        auto& v = p.vertices;
        const std::size_t n = v.size();
        const bool vertex = index < n;
        const std::size_t k = vertex ? index : index - n;   // vertice, oppure tratto che parte dal vertice k
        if (k >= n)
            return nullptr;
        switch (action) {
        case GripAction::AddVertex: {
            if (!vertex && !p.closed && k + 1 >= n)
                return nullptr;
            PolyVertex added = v[k];
            added.point = to;
            added.bulge = 0.0;
            v[k].bulge = 0.0;   // i due tratti nuovi sono diritti
            v.insert(v.begin() + static_cast<std::ptrdiff_t>(k + 1), added);
            return copy;
        }
        case GripAction::RemoveVertex:
            if (!vertex || n <= 2)
                return nullptr;
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(k));
            return copy;
        case GripAction::ToLine:
            if (vertex)
                return nullptr;
            v[k].bulge = 0.0;
            return copy;
        case GripAction::ToArc: {
            if (vertex)
                return nullptr;
            const Vec2 a = v[k].point, b = v[(k + 1) % n].point;
            // Arco per a, `to`, b: l'angolo in `to` è π − θ/2 (θ = ampiezza), bulge = tan(θ/4).
            const Vec2 ta = a - to, tb = b - to;
            const double la = ta.length(), lb = tb.length();
            const double side = cross(b - a, to - a);
            if (la < 1e-12 || lb < 1e-12 || std::abs(side) < 1e-12 * std::max(1.0, distance(a, b) * distance(a, b)))
                return nullptr;
            const double inscribed = std::acos(std::clamp(ta.dot(tb) / (la * lb), -1.0, 1.0));
            const double theta = 2.0 * (kPi - inscribed);
            const double bulge = std::tan(theta / 4.0);
            v[k].bulge = side < 0.0 ? bulge : -bulge;   // a destra della corda = antiorario
            return copy;
        }
        default:
            return nullptr;
        }
    }
    if (action != GripAction::Lengthen || index > 1)
        return nullptr;
    if (e.type() == EntityType::Line) {
        auto& l = static_cast<LineEntity&>(*copy);
        const Vec2 fixed = index == 0 ? l.end : l.start;
        const Vec2 moving = index == 0 ? l.start : l.end;
        const Vec2 dir = moving - fixed;
        const double len = dir.length();
        if (len < 1e-12)
            return nullptr;
        const double along = (to - fixed).dot(dir / len);
        if (along <= 1e-9)
            return nullptr;
        (index == 0 ? l.start : l.end) = fixed + dir / len * along;
        return copy;
    }
    if (e.type() == EntityType::Arc) {
        auto& a = static_cast<ArcEntity&>(*copy);
        if (distance(to, a.center) < 1e-12)
            return nullptr;
        (index == 0 ? a.startAngle : a.endAngle) = angleOf(to - a.center);
        return copy;
    }
    return nullptr;
}


// ------------------------------------------------------------------- spline

namespace {

int findSpan(int n, int p, double u, const std::vector<double>& U)
{
    if (u >= U[static_cast<std::size_t>(n + 1)])
        return n;
    int low = p, high = n + 1, mid = (low + high) / 2;
    while (u < U[static_cast<std::size_t>(mid)] || u >= U[static_cast<std::size_t>(mid + 1)]) {
        if (u < U[static_cast<std::size_t>(mid)])
            high = mid;
        else
            low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

std::vector<double> basisFunctions(int span, double u, int p, const std::vector<double>& U)
{
    std::vector<double> N(static_cast<std::size_t>(p) + 1, 0.0), left(N.size()), right(N.size());
    N[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[static_cast<std::size_t>(j)] = u - U[static_cast<std::size_t>(span + 1 - j)];
        right[static_cast<std::size_t>(j)] = U[static_cast<std::size_t>(span + j)] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double den = right[static_cast<std::size_t>(r + 1)] + left[static_cast<std::size_t>(j - r)];
            const double temp = den != 0.0 ? N[static_cast<std::size_t>(r)] / den : 0.0;
            N[static_cast<std::size_t>(r)] = saved + right[static_cast<std::size_t>(r + 1)] * temp;
            saved = left[static_cast<std::size_t>(j - r)] * temp;
        }
        N[static_cast<std::size_t>(j)] = saved;
    }
    return N;
}

} // namespace

bool interpolateSpline(SplineEntity& s)
{
    std::vector<Vec2> q = s.fit;
    if (s.closed && q.size() >= 3 && distance(q.front(), q.back()) > 1e-12)
        q.push_back(q.front());   // chiusa: si torna al primo punto
    const int count = static_cast<int>(q.size());
    if (count < 2)
        return false;
    const int p = std::min(3, count - 1);
    const int n = count - 1;
    // Parametri con la lunghezza delle corde (The NURBS Book, A9.1).
    std::vector<double> u(static_cast<std::size_t>(count), 0.0);
    double total = 0.0;
    for (int k = 1; k < count; ++k)
        total += distance(q[static_cast<std::size_t>(k)], q[static_cast<std::size_t>(k - 1)]);
    if (total <= 1e-12)
        return false;
    for (int k = 1; k < count; ++k)
        u[static_cast<std::size_t>(k)] = u[static_cast<std::size_t>(k - 1)]
                                         + distance(q[static_cast<std::size_t>(k)], q[static_cast<std::size_t>(k - 1)]) / total;
    u.back() = 1.0;
    std::vector<double> U(static_cast<std::size_t>(n + p + 2), 0.0);
    for (int j = 0; j <= p; ++j)
        U[static_cast<std::size_t>(n + p + 1 - j)] = 1.0;
    for (int j = 1; j <= n - p; ++j) {
        double sum = 0.0;
        for (int i = j; i < j + p; ++i)
            sum += u[static_cast<std::size_t>(i)];
        U[static_cast<std::size_t>(j + p)] = sum / p;
    }
    // Sistema A·P = Q (matrice piena: pochi punti), eliminazione di Gauss con pivot.
    std::vector<std::vector<double>> A(static_cast<std::size_t>(count), std::vector<double>(static_cast<std::size_t>(count) + 2, 0.0));
    for (int k = 0; k < count; ++k) {
        const int span = findSpan(n, p, u[static_cast<std::size_t>(k)], U);
        const auto N = basisFunctions(span, u[static_cast<std::size_t>(k)], p, U);
        for (int j = 0; j <= p; ++j)
            A[static_cast<std::size_t>(k)][static_cast<std::size_t>(span - p + j)] = N[static_cast<std::size_t>(j)];
        A[static_cast<std::size_t>(k)][static_cast<std::size_t>(count)] = q[static_cast<std::size_t>(k)].x;
        A[static_cast<std::size_t>(k)][static_cast<std::size_t>(count) + 1] = q[static_cast<std::size_t>(k)].y;
    }
    for (int c = 0; c < count; ++c) {
        int pivot = c;
        for (int r = c + 1; r < count; ++r)
            if (std::abs(A[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]) > std::abs(A[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(c)]))
                pivot = r;
        if (std::abs(A[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(c)]) < 1e-14)
            return false;
        std::swap(A[static_cast<std::size_t>(c)], A[static_cast<std::size_t>(pivot)]);
        for (int r = 0; r < count; ++r) {
            if (r == c)
                continue;
            const double f = A[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] / A[static_cast<std::size_t>(c)][static_cast<std::size_t>(c)];
            if (f == 0.0)
                continue;
            for (int k = c; k < count + 2; ++k)
                A[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] -= f * A[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)];
        }
    }
    s.degree = p;
    s.control.clear();
    for (int c = 0; c < count; ++c) {
        const double d = A[static_cast<std::size_t>(c)][static_cast<std::size_t>(c)];
        s.control.push_back({A[static_cast<std::size_t>(c)][static_cast<std::size_t>(count)] / d,
                             A[static_cast<std::size_t>(c)][static_cast<std::size_t>(count) + 1] / d});
    }
    s.knots = U;
    s.weights.clear();
    return true;
}

} // namespace cartesio
