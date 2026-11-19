#include "core/Prim.h"

#include <algorithm>
#include <cmath>

namespace cartesio {

namespace {

constexpr double kEps = 1e-9;

bool inside(const BBox& r, Vec2 p)
{
    return p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y;
}

std::vector<Vec2> segmentSegment(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const Vec2 r = b - a;
    const Vec2 s = d - c;
    const double den = cross(r, s);
    if (std::abs(den) <= 1e-12 * r.length() * s.length())
        return {};
    const double t = cross(c - a, s) / den;
    const double u = cross(c - a, r) / den;
    if (t < -kEps || t > 1.0 + kEps || u < -kEps || u > 1.0 + kEps)
        return {};
    return {a + r * t};
}

std::vector<Vec2> segmentCircle(Vec2 a, Vec2 b, Vec2 center, double radius)
{
    const Vec2 d = b - a;
    const double qa = d.dot(d);
    if (qa == 0.0)
        return {};
    const Vec2 f = a - center;
    const double qb = 2.0 * f.dot(d);
    const double qc = f.dot(f) - radius * radius;
    const double disc = qb * qb - 4.0 * qa * qc;
    if (disc < -kEps * qa)
        return {};
    const double sq = std::sqrt(std::max(disc, 0.0));
    std::vector<Vec2> result;
    for (const double t : {(-qb - sq) / (2.0 * qa), (-qb + sq) / (2.0 * qa)}) {
        if (t < -kEps || t > 1.0 + kEps)
            continue;
        const Vec2 p = a + d * t;
        if (result.empty() || distance(result.front(), p) > kEps)
            result.push_back(p);
    }
    return result;
}

std::vector<Vec2> circleCircle(Vec2 c1, double r1, Vec2 c2, double r2)
{
    const double d = distance(c1, c2);
    if (d < kEps || d > r1 + r2 + kEps || d < std::abs(r1 - r2) - kEps)
        return {};
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h2 = r1 * r1 - a * a;
    const double h = h2 > 0.0 ? std::sqrt(h2) : 0.0;
    const Vec2 dir = (c2 - c1) / d;
    const Vec2 mid = c1 + dir * a;
    const Vec2 perp{-dir.y, dir.x};
    if (h < kEps)
        return {mid};
    return {mid + perp * h, mid - perp * h};
}

} // namespace

// ----------------------------------------------------------------------- Prim

Prim Prim::segment(Vec2 a, Vec2 b)
{
    Prim p;
    p.kind = Kind::Segment;
    p.a = a;
    p.b = b;
    return p;
}

Prim Prim::arc(Vec2 center, double radius, double start, double sweep)
{
    Prim p;
    p.kind = Kind::Arc;
    p.center = center;
    p.radius = radius;
    p.start = start;
    p.sweep = sweep;
    p.a = p.pointAt(start);
    p.b = p.pointAt(start + sweep);
    return p;
}

Prim Prim::bulge(Vec2 a, Vec2 b, double bulge)
{
    const Vec2 chord = b - a;
    const double c = chord.length();
    if (std::abs(bulge) < 1e-12 || c < 1e-15)
        return segment(a, b);

    // Distanza del centro dal punto medio della corda: c * (1 - b²) / (4b), verso sinistra.
    const Vec2 mid = (a + b) / 2.0;
    const Vec2 left{-chord.y / c, chord.x / c};
    const Vec2 center = mid + left * (c * (1.0 - bulge * bulge) / (4.0 * bulge));
    const double radius = distance(center, a);
    const double theta = 4.0 * std::atan(std::abs(bulge));

    Prim p = bulge > 0.0 ? arc(center, radius, angleOf(a - center), theta)
                         : arc(center, radius, angleOf(b - center), theta);
    p.a = a;   // estremi nell'ordine della polilinea
    p.b = b;
    return p;
}

bool Prim::containsAngle(double angle) const
{
    if (sweep >= 2.0 * kPi - 1e-12)
        return true;
    constexpr double eps = 1e-9;
    const double u = normalizeAngle(angle - start);
    return u <= sweep + eps || u >= 2.0 * kPi - eps;
}

// --------------------------------------------------------------------- Affine

Affine Affine::translation(Vec2 t)
{
    Affine a;
    a.dx = t.x;
    a.dy = t.y;
    return a;
}

Affine Affine::insert(Vec2 base, Vec2 position, double scaleX, double scaleY, double rotation)
{
    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    Affine a;
    a.m11 = c * scaleX;
    a.m12 = s * scaleX;
    a.m21 = -s * scaleY;
    a.m22 = c * scaleY;
    const Vec2 shifted = a.applyVector(base);
    a.dx = position.x - shifted.x;
    a.dy = position.y - shifted.y;
    return a;
}

Affine Affine::operator*(const Affine& o) const
{
    Affine r;
    r.m11 = m11 * o.m11 + m21 * o.m12;
    r.m12 = m12 * o.m11 + m22 * o.m12;
    r.m21 = m11 * o.m21 + m21 * o.m22;
    r.m22 = m12 * o.m21 + m22 * o.m22;
    const Vec2 t = apply({o.dx, o.dy});
    r.dx = t.x;
    r.dy = t.y;
    return r;
}

bool Affine::isSimilarity() const
{
    const double lx = std::hypot(m11, m12);
    const double ly = std::hypot(m21, m22);
    const double dot = m11 * m21 + m12 * m22;
    return std::abs(lx - ly) <= 1e-9 * std::max(lx, ly) && std::abs(dot) <= 1e-9 * lx * ly;
}

double Affine::uniformScale() const
{
    return std::hypot(m11, m12);
}

void transformPrims(std::vector<Prim>& prims, const Affine& t)
{
    std::vector<Prim> out;
    out.reserve(prims.size());
    const bool similar = t.isSimilarity();
    const bool mirrored = t.determinant() < 0.0;

    for (const Prim& p : prims) {
        if (p.kind == Prim::Kind::Segment) {
            out.push_back(Prim::segment(t.apply(p.a), t.apply(p.b)));
            continue;
        }
        if (similar) {
            const double rot = angleOf(t.applyVector({1.0, 0.0}));
            Prim q;
            if (!mirrored) {
                q = Prim::arc(t.apply(p.center), p.radius * t.uniformScale(), p.start + rot, p.sweep);
            } else {
                // La specchiatura inverte il verso: l'arco va percorso dall'altro estremo.
                const double endAngle = angleOf(t.applyVector(Vec2::polar(1.0, p.start + p.sweep)));
                q = Prim::arc(t.apply(p.center), p.radius * t.uniformScale(), endAngle, p.sweep);
            }
            q.a = t.apply(p.a);
            q.b = t.apply(p.b);
            out.push_back(q);
            continue;
        }
        // Scala non uniforme: l'arco diventa un'ellisse, approssimata con segmenti.
        const int n = std::max(8, static_cast<int>(std::ceil(p.sweep / (kPi / 32))));
        std::vector<Vec2> pts;
        for (int i = 0; i <= n; ++i)
            pts.push_back(t.apply(p.pointAt(p.start + p.sweep * i / n)));
        appendPolyline(out, pts);
    }
    prims = std::move(out);
}

// -------------------------------------------------------------------- misure

double primDistance(const Prim& p, Vec2 point)
{
    if (p.kind == Prim::Kind::Segment) {
        const Vec2 d = p.b - p.a;
        const double len2 = d.dot(d);
        if (len2 == 0.0)
            return distance(point, p.a);
        const double t = std::clamp((point - p.a).dot(d) / len2, 0.0, 1.0);
        return distance(point, p.a + d * t);
    }
    if (p.containsAngle(angleOf(point - p.center)))
        return std::abs(distance(point, p.center) - p.radius);
    return std::min(distance(point, p.pointAt(p.start)), distance(point, p.pointAt(p.start + p.sweep)));
}

void primBounds(const Prim& p, BBox& box)
{
    if (p.kind == Prim::Kind::Segment) {
        box.expand(p.a);
        box.expand(p.b);
        return;
    }
    box.expand(p.pointAt(p.start));
    box.expand(p.pointAt(p.start + p.sweep));
    for (int k = 0; k < 4; ++k) {
        const double a = k * kPi / 2.0;
        if (p.containsAngle(a))
            box.expand(p.pointAt(a));
    }
}

std::vector<Vec2> primIntersections(const Prim& p, const Prim& q)
{
    std::vector<Vec2> pts;
    if (p.kind == Prim::Kind::Segment && q.kind == Prim::Kind::Segment)
        return segmentSegment(p.a, p.b, q.a, q.b);
    if (p.kind == Prim::Kind::Segment)
        pts = segmentCircle(p.a, p.b, q.center, q.radius);
    else if (q.kind == Prim::Kind::Segment)
        pts = segmentCircle(q.a, q.b, p.center, p.radius);
    else
        pts = circleCircle(p.center, p.radius, q.center, q.radius);

    std::erase_if(pts, [&](Vec2 x) {
        return (p.kind == Prim::Kind::Arc && !p.containsAngle(angleOf(x - p.center)))
            || (q.kind == Prim::Kind::Arc && !q.containsAngle(angleOf(x - q.center)));
    });
    return pts;
}

bool primTouchesRect(const Prim& p, const BBox& rect)
{
    if (inside(rect, p.a) || inside(rect, p.b))
        return true;
    const Vec2 corners[] = {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
    for (int i = 0; i < 4; ++i)
        if (!primIntersections(p, Prim::segment(corners[i], corners[(i + 1) % 4])).empty())
            return true;
    return false;
}

void appendPolyline(std::vector<Prim>& out, const std::vector<Vec2>& points, bool closed)
{
    for (std::size_t i = 1; i < points.size(); ++i)
        out.push_back(Prim::segment(points[i - 1], points[i]));
    if (closed && points.size() > 2)
        out.push_back(Prim::segment(points.back(), points.front()));
}

} // namespace cartesio

namespace cartesio {

Affine Affine::rotation(Vec2 center, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    Affine a;
    a.m11 = c;
    a.m12 = s;
    a.m21 = -s;
    a.m22 = c;
    const Vec2 moved = a.applyVector(center);
    a.dx = center.x - moved.x;
    a.dy = center.y - moved.y;
    return a;
}

Affine Affine::scaling(Vec2 center, double factor)
{
    Affine a;
    a.m11 = factor;
    a.m22 = factor;
    a.dx = center.x * (1.0 - factor);
    a.dy = center.y * (1.0 - factor);
    return a;
}

Affine Affine::mirror(Vec2 p, Vec2 q)
{
    const Vec2 d = q - p;
    const double len2 = d.dot(d);
    Affine a;
    if (len2 == 0.0)
        return a;
    // Riflessione: R = 2·u·uᵀ - I, con u direzione della retta.
    const double ux = d.x * d.x / len2;
    const double uy = d.y * d.y / len2;
    const double uxy = d.x * d.y / len2;
    a.m11 = 2.0 * ux - 1.0;
    a.m12 = 2.0 * uxy;
    a.m21 = 2.0 * uxy;
    a.m22 = 2.0 * uy - 1.0;
    const Vec2 moved = a.applyVector(p);
    a.dx = p.x - moved.x;
    a.dy = p.y - moved.y;
    return a;
}

} // namespace cartesio
