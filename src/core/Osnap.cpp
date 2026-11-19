#include "core/Osnap.h"

#include "core/Prim.h"

#include <algorithm>
#include <cmath>

namespace cartesio {

int snapBit(SnapKind kind)
{
    switch (kind) {
    case SnapKind::Endpoint: return Osnap::Endpoint;
    case SnapKind::Midpoint: return Osnap::Midpoint;
    case SnapKind::Center: return Osnap::Center;
    case SnapKind::Quadrant: return Osnap::Quadrant;
    case SnapKind::Node: return Osnap::Node;
    case SnapKind::Insertion: return Osnap::Insertion;
    case SnapKind::Intersection: return Osnap::Intersection;
    case SnapKind::Perpendicular: return Osnap::Perpendicular;
    case SnapKind::Tangent: return Osnap::Tangent;
    case SnapKind::Nearest: return Osnap::Nearest;
    }
    return 0;
}

namespace {

// Gli snap geometrici (intersezione, perpendicolare...) lavorano sulle linee vere degli oggetti:
// testi, tratteggi e finestre non hanno linee su cui agganciarsi.
bool hasSnapGeometry(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Text:
    case EntityType::MText:
    case EntityType::Hatch:
    case EntityType::AttDef:
    case EntityType::Viewport:
        return false;
    default:
        return true;
    }
}

Vec2 nearestOnPrim(const Prim& p, Vec2 c)
{
    if (p.kind == Prim::Kind::Segment) {
        const Vec2 d = p.b - p.a;
        const double len2 = d.dot(d);
        if (len2 <= 0.0)
            return p.a;
        const double t = std::clamp((c - p.a).dot(d) / len2, 0.0, 1.0);
        return p.a + d * t;
    }
    if (distance(c, p.center) > 0.0) {
        const double angle = angleOf(c - p.center);
        if (p.containsAngle(angle))
            return p.pointAt(angle);
    }
    return distance(c, p.a) <= distance(c, p.b) ? p.a : p.b;
}

// Piedi delle perpendicolari da `base` (sul tratto, non sul suo prolungamento).
void perpendicularFeet(const Prim& p, Vec2 base, std::vector<Vec2>& out)
{
    if (p.kind == Prim::Kind::Segment) {
        const Vec2 d = p.b - p.a;
        const double len2 = d.dot(d);
        if (len2 <= 0.0)
            return;
        const double t = (base - p.a).dot(d) / len2;
        if (t >= -1e-9 && t <= 1.0 + 1e-9)
            out.push_back(p.a + d * t);
        return;
    }
    if (distance(base, p.center) <= 1e-12)
        return;
    const double angle = angleOf(base - p.center);
    for (double a : {angle, angle + kPi})
        if (p.containsAngle(a))
            out.push_back(p.pointAt(a));
}

// Punti di tangenza da `base` su un arco o un cerchio.
void tangentPoints(const Prim& p, Vec2 base, std::vector<Vec2>& out)
{
    if (p.kind != Prim::Kind::Arc)
        return;
    const double d = distance(base, p.center);
    if (d <= p.radius * (1.0 + 1e-9))
        return;
    const double phi = angleOf(base - p.center);
    const double alpha = std::acos(p.radius / d);
    for (double a : {phi + alpha, phi - alpha})
        if (p.containsAngle(a))
            out.push_back(p.pointAt(a));
}

} // namespace

std::optional<SnapPoint> findSnap(const std::vector<const Entity*>& nearby, Vec2 cursor, double radius, int mode,
                                  std::optional<Vec2> base)
{
    std::optional<SnapPoint> best;
    double bestDistance = radius;
    // A parità di distanza vince il primo trovato: punti finali e centri prima delle intersezioni.
    const auto consider = [&](Vec2 p, SnapKind kind) {
        const double d = distance(p, cursor);
        if (d < bestDistance || (!best && d <= bestDistance)) {
            bestDistance = d;
            best = SnapPoint{p, kind};
        }
    };

    std::vector<SnapPoint> points;
    for (const Entity* e : nearby) {
        points.clear();
        e->snapPointsNear(cursor, radius, points);
        for (const SnapPoint& sp : points)
            if (mode & snapBit(sp.kind))
                consider(sp.position, sp.kind);
    }

    constexpr int geometric = Osnap::Intersection | Osnap::Perpendicular | Osnap::Tangent | Osnap::Nearest;
    if ((mode & geometric) == 0)
        return best;

    // Solo i tratti che passano vicino al cursore.
    struct Near {
        Prim prim;
        std::size_t owner;
    };
    std::vector<Near> prims;
    for (std::size_t i = 0; i < nearby.size(); ++i) {
        if (!hasSnapGeometry(*nearby[i]))
            continue;
        std::vector<Prim> near;
        nearby[i]->primsNear(cursor, radius, near);
        for (const Prim& p : near)
            prims.push_back({p, i});
    }

    if (mode & Osnap::Intersection) {
        for (std::size_t i = 0; i < prims.size(); ++i)
            for (std::size_t j = i + 1; j < prims.size(); ++j)
                if (prims[i].owner != prims[j].owner)
                    for (Vec2 p : primIntersections(prims[i].prim, prims[j].prim))
                        consider(p, SnapKind::Intersection);
    }
    if (base) {
        std::vector<Vec2> points;
        for (const Near& n : prims) {
            points.clear();
            if (mode & Osnap::Perpendicular) {
                perpendicularFeet(n.prim, *base, points);
                for (Vec2 p : points)
                    if (distance(p, *base) > 1e-9)
                        consider(p, SnapKind::Perpendicular);
            }
            points.clear();
            if (mode & Osnap::Tangent) {
                tangentPoints(n.prim, *base, points);
                for (Vec2 p : points)
                    consider(p, SnapKind::Tangent);
            }
        }
    }
    if (best || !(mode & Osnap::Nearest))
        return best;

    // Vicino: il punto dell'oggetto più vicino al cursore, solo se non c'è altro.
    double nearest = radius;
    for (const Near& n : prims) {
        const Vec2 p = nearestOnPrim(n.prim, cursor);
        const double d = distance(p, cursor);
        if (d <= nearest) {
            nearest = d;
            best = SnapPoint{p, SnapKind::Nearest};
        }
    }
    return best;
}

} // namespace cartesio
