#include "core/Entity.h"

#include <atomic>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>

namespace cartesio {

std::uint64_t Entity::UniqueSerial::next()
{
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}


namespace {

// Distanza da un quadrilatero (0 se il punto è dentro).
double quadDistance(const std::array<Vec2, 4>& q, Vec2 p)
{
    bool inside = false;
    for (int i = 0, j = 3; i < 4; j = i++) {
        if (((q[i].y > p.y) != (q[j].y > p.y))
            && (p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x))
            inside = !inside;
    }
    if (inside)
        return 0.0;
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 4; ++i)
        best = std::min(best, primDistance(Prim::segment(q[i], q[(i + 1) % 4]), p));
    return best;
}

bool quadTouchesRect(const std::array<Vec2, 4>& q, const BBox& rect)
{
    for (int i = 0; i < 4; ++i)
        if (primTouchesRect(Prim::segment(q[i], q[(i + 1) % 4]), rect))
            return true;
    // Rettangolo interamente dentro il quadrilatero.
    return quadDistance(q, rect.center()) == 0.0;
}

BBox quadBounds(const std::array<Vec2, 4>& q)
{
    BBox b;
    for (Vec2 p : q)
        b.expand(p);
    return b;
}

std::size_t utf8Length(const std::string& s)
{
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            ++n;
    return n;
}

void appendUtf8(std::string& out, unsigned code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// Codici speciali nei testi: %%d, %%c, %%p, %%u, %%o, %%%.
std::string decodePercentCodes(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && s[i + 1] == '%') {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            if (c == 'd') { out += "°"; i += 2; continue; }
            if (c == 'c') { out += "Ø"; i += 2; continue; }
            if (c == 'p') { out += "±"; i += 2; continue; }
            if (c == 'u' || c == 'o') { i += 2; continue; }
            if (c == '%') { out += '%'; i += 2; continue; }
            // %%nnn: il carattere con quel codice (es. %%176 = °).
            if (std::isdigit(static_cast<unsigned char>(s[i + 2]))) {
                std::size_t j = i + 2;
                unsigned code = 0;
                while (j < s.size() && j < i + 5 && std::isdigit(static_cast<unsigned char>(s[j])))
                    code = code * 10 + static_cast<unsigned>(s[j++] - '0');
                if (code > 0 && code < 256) {
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    i = j - 1;
                    continue;
                }
            }
        }
        out += s[i];
    }
    return out;
}

constexpr double kCharWidth = 0.7;   // larghezza media di un carattere / altezza (Arial)

double estimateWidth(const std::string& s, double height, double widthFactor = 1.0)
{
    return static_cast<double>(utf8Length(s)) * kCharWidth * height * widthFactor;
}

struct DepthGuard {
    static int& depth()
    {
        static thread_local int d = 0;
        return d;
    }
    DepthGuard() { ++depth(); }
    ~DepthGuard() { --depth(); }
    bool tooDeep() const { return depth() > 16; }
};

double rotationOf(const Affine& t)
{
    return angleOf(t.applyVector({1.0, 0.0}));
}

double scaleOf(const Affine& t)
{
    return std::sqrt(std::abs(t.determinant()));
}

} // namespace

// ----------------------------------------------------------------------- base

std::vector<Prim> Entity::prims() const
{
    std::vector<Prim> out;
    decompose(out);
    return out;
}

BBox Entity::bounds() const
{
    BBox b;
    for (const Prim& p : prims())
        primBounds(p, b);
    return b;
}

double Entity::distanceTo(Vec2 point) const
{
    double best = std::numeric_limits<double>::infinity();
    for (const Prim& p : prims())
        best = std::min(best, primDistance(p, point));
    return best;
}

void Entity::primsNear(Vec2 p, double radius, std::vector<Prim>& out) const
{
    for (const Prim& prim : prims())
        if (primDistance(prim, p) <= radius)
            out.push_back(prim);
}

void Entity::snapPointsNear(Vec2 p, double radius, std::vector<SnapPoint>& out) const
{
    for (const SnapPoint& sp : snapPoints())
        if (distance(sp.position, p) <= radius)
            out.push_back(sp);
}

bool Entity::touchesRect(const BBox& rect) const
{
    for (const Prim& p : prims())
        if (primTouchesRect(p, rect))
            return true;
    return false;
}

// ----------------------------------------------------------------------- linea

void LineEntity::decompose(std::vector<Prim>& out) const
{
    out.push_back(Prim::segment(start, end));
}

std::vector<SnapPoint> LineEntity::snapPoints() const
{
    return {{start, SnapKind::Endpoint}, {end, SnapKind::Endpoint}, {(start + end) / 2.0, SnapKind::Midpoint}};
}

void LineEntity::transform(const Affine& t)
{
    start = t.apply(start);
    end = t.apply(end);
}

// ---------------------------------------------------------------------- cerchio

void CircleEntity::decompose(std::vector<Prim>& out) const
{
    out.push_back(Prim::arc(center, radius, 0.0, 2.0 * kPi));
}

std::vector<SnapPoint> CircleEntity::snapPoints() const
{
    return {
        {center, SnapKind::Center},
        {center + Vec2{radius, 0.0}, SnapKind::Quadrant},
        {center + Vec2{0.0, radius}, SnapKind::Quadrant},
        {center - Vec2{radius, 0.0}, SnapKind::Quadrant},
        {center - Vec2{0.0, radius}, SnapKind::Quadrant},
    };
}

void CircleEntity::transform(const Affine& t)
{
    center = t.apply(center);
    radius *= scaleOf(t);
}

// ------------------------------------------------------------------------ arco

double ArcEntity::sweep() const
{
    const double s = normalizeAngle(endAngle - startAngle);
    return s <= 0.0 ? 2.0 * kPi : s;
}

bool ArcEntity::containsAngle(double angle) const
{
    constexpr double eps = 1e-9;
    const double u = normalizeAngle(angle - startAngle);
    return u <= sweep() + eps || u >= 2.0 * kPi - eps;
}

void ArcEntity::decompose(std::vector<Prim>& out) const
{
    out.push_back(Prim::arc(center, radius, startAngle, sweep()));
}

std::vector<SnapPoint> ArcEntity::snapPoints() const
{
    std::vector<SnapPoint> points{
        {startPoint(), SnapKind::Endpoint},
        {endPoint(), SnapKind::Endpoint},
        {pointAt(startAngle + sweep() / 2.0), SnapKind::Midpoint},
        {center, SnapKind::Center},
    };
    for (int k = 0; k < 4; ++k) {
        const double a = k * kPi / 2.0;
        if (containsAngle(a))
            points.push_back({pointAt(a), SnapKind::Quadrant});
    }
    return points;
}

void ArcEntity::transform(const Affine& t)
{
    std::vector<Prim> p{Prim::arc(center, radius, startAngle, sweep())};
    if (t.isSimilarity()) {
        transformPrims(p, t);
        center = p[0].center;
        radius = p[0].radius;
        startAngle = p[0].start;
        endAngle = p[0].start + p[0].sweep;
    } else {
        center = t.apply(center);
        radius *= scaleOf(t);
        startAngle += rotationOf(t);
        endAngle += rotationOf(t);
    }
}

// ------------------------------------------------------------------------ punto

BBox PointEntity::bounds() const
{
    BBox b;
    b.expand(position);
    return b;
}

bool PointEntity::touchesRect(const BBox& r) const
{
    return position.x >= r.min.x && position.x <= r.max.x && position.y >= r.min.y && position.y <= r.max.y;
}

// -------------------------------------------------------------------- polilinea

int PolylineEntity::segmentCount() const
{
    const int n = static_cast<int>(vertices.size());
    if (n < 2)
        return 0;
    return closed ? n : n - 1;
}

void PolylineEntity::decompose(std::vector<Prim>& out) const
{
    const int n = static_cast<int>(vertices.size());
    for (int i = 0; i < segmentCount(); ++i) {
        const PolyVertex& a = vertices[static_cast<std::size_t>(i)];
        const PolyVertex& b = vertices[static_cast<std::size_t>((i + 1) % n)];
        out.push_back(Prim::bulge(a.point, b.point, a.bulge));
    }
}

std::vector<SnapPoint> PolylineEntity::snapPoints() const
{
    std::vector<SnapPoint> points;
    for (const PolyVertex& v : vertices)
        points.push_back({v.point, SnapKind::Endpoint});
    for (const Prim& p : prims()) {
        const Vec2 mid = p.kind == Prim::Kind::Segment ? (p.a + p.b) / 2.0 : p.pointAt(p.start + p.sweep / 2.0);
        points.push_back({mid, SnapKind::Midpoint});
    }
    return points;
}

void PolylineEntity::transform(const Affine& t)
{
    const bool mirrored = t.determinant() < 0.0;
    const double scale = std::sqrt(std::abs(t.determinant()));   // le larghezze seguono la scala
    for (PolyVertex& v : vertices) {
        v.point = t.apply(v.point);
        if (mirrored)
            v.bulge = -v.bulge;
        v.startWidth *= scale;
        v.endWidth *= scale;
    }
}

bool PolylineEntity::hasWidth() const
{
    return maxWidth() > 0.0;
}

double PolylineEntity::maxWidth() const
{
    double w = 0.0;
    for (const PolyVertex& v : vertices)
        w = std::max({w, v.startWidth, v.endWidth});
    return w;
}

std::vector<std::unique_ptr<PolylineEntity>> PolylineEntity::thinParts() const
{
    std::vector<std::unique_ptr<PolylineEntity>> parts;
    const int n = static_cast<int>(vertices.size());
    const int segments = segmentCount();
    const auto thin = [this](int i) {
        const PolyVertex& v = vertices[static_cast<std::size_t>(i)];
        return v.startWidth <= 0.0 && v.endWidth <= 0.0;
    };
    int i = 0;
    while (i < segments) {
        if (!thin(i)) {
            ++i;
            continue;
        }
        const int first = i;
        while (i < segments && thin(i))
            ++i;
        std::unique_ptr<Entity> copy = clone();
        auto part = std::unique_ptr<PolylineEntity>(static_cast<PolylineEntity*>(copy.release()));
        part->closed = false;
        part->vertices.clear();
        for (int k = first; k <= i; ++k) {
            PolyVertex v = vertices[static_cast<std::size_t>(k % n)];
            v.startWidth = v.endWidth = 0.0;
            if (k == i)
                v.bulge = 0.0;
            part->vertices.push_back(v);
        }
        parts.push_back(std::move(part));
    }
    return parts;
}

void PolylineEntity::setWidth(double width)
{
    for (PolyVertex& v : vertices)
        v.startWidth = v.endWidth = std::max(0.0, width);
}

BBox PolylineEntity::bounds() const
{
    BBox box = Entity::bounds();
    const double h = maxWidth() / 2.0;
    if (h > 0.0 && box.isValid()) {
        box.min = box.min - Vec2{h, h};
        box.max = box.max + Vec2{h, h};
    }
    return box;
}

double PolylineEntity::distanceTo(Vec2 p) const
{
    return std::max(0.0, Entity::distanceTo(p) - maxWidth() / 2.0);
}

bool PolylineEntity::touchesRect(const BBox& rect) const
{
    const double h = maxWidth() / 2.0;
    if (h <= 0.0)
        return Entity::touchesRect(rect);
    BBox grown = rect;
    grown.min = grown.min - Vec2{h, h};
    grown.max = grown.max + Vec2{h, h};
    return Entity::touchesRect(grown);
}

std::vector<std::vector<Vec2>> PolylineEntity::widthPolygons() const
{
    std::vector<std::vector<Vec2>> out;
    const int n = static_cast<int>(vertices.size());
    const int segments = segmentCount();
    struct Side {
        Vec2 point, dir;
        double width = 0.0;
    };
    std::vector<Side> starts(static_cast<std::size_t>(segments)), ends(static_cast<std::size_t>(segments));
    std::vector<bool> wide(static_cast<std::size_t>(segments), false);
    const auto left = [](Vec2 d) { return Vec2{-d.y, d.x}; };
    const auto unit = [](Vec2 d) {
        const double len = std::hypot(d.x, d.y);
        return len > 0.0 ? Vec2{d.x / len, d.y / len} : Vec2{1.0, 0.0};
    };
    const auto cross2 = [](Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; };
    for (int i = 0; i < segments; ++i) {
        const PolyVertex& a = vertices[static_cast<std::size_t>(i)];
        const PolyVertex& b = vertices[static_cast<std::size_t>((i + 1) % n)];
        if (a.startWidth <= 0.0 && a.endWidth <= 0.0)
            continue;
        if (distance(a.point, b.point) < 1e-12)
            continue;
        // Punti del centro dal primo al secondo vertice.
        std::vector<Vec2> pts;
        const Prim pr = Prim::bulge(a.point, b.point, a.bulge);
        if (pr.kind == Prim::Kind::Segment) {
            pts = {a.point, b.point};
        } else {
            const int steps = std::clamp(static_cast<int>(std::ceil(pr.sweep / (kPi / 36.0))), 4, 144);
            for (int k = 0; k <= steps; ++k)
                pts.push_back(pr.pointAt(pr.start + pr.sweep * k / steps));
            if (distance(pts.front(), a.point) > distance(pts.back(), a.point))
                std::reverse(pts.begin(), pts.end());
            pts.front() = a.point;
            pts.back() = b.point;
        }
        const std::size_t m = pts.size();
        std::vector<Vec2> l, r;
        Vec2 firstDir, lastDir;
        for (std::size_t k = 0; k < m; ++k) {
            const Vec2 d = unit(pts[std::min(k + 1, m - 1)] - pts[k == 0 ? 0 : k - 1]);
            const double t = m > 1 ? static_cast<double>(k) / static_cast<double>(m - 1) : 0.0;
            const double half = (a.startWidth + (a.endWidth - a.startWidth) * t) / 2.0;
            l.push_back(pts[k] + left(d) * half);
            r.push_back(pts[k] - left(d) * half);
            if (k == 0)
                firstDir = d;
            lastDir = d;
        }
        std::vector<Vec2> poly = l;
        poly.insert(poly.end(), r.rbegin(), r.rend());
        out.push_back(std::move(poly));
        wide[static_cast<std::size_t>(i)] = true;
        starts[static_cast<std::size_t>(i)] = {a.point, firstDir, a.startWidth};
        ends[static_cast<std::size_t>(i)] = {b.point, lastDir, a.endWidth};
    }
    // Raccordi agli spigoli: dalla parte esterna della curva si chiude il cuneo fino all'incrocio dei bordi.
    for (int i = 0; i < segments; ++i) {
        const int next = i + 1 < segments ? i + 1 : (closed ? 0 : -1);
        if (next < 0 || next == i || !wide[static_cast<std::size_t>(i)] || !wide[static_cast<std::size_t>(next)])
            continue;
        const Side& e = ends[static_cast<std::size_t>(i)];
        const Side& s = starts[static_cast<std::size_t>(next)];
        if (e.width <= 0.0 || s.width <= 0.0)
            continue;
        const double turn = cross2(e.dir, s.dir);
        if (std::abs(turn) < 1e-9)
            continue;
        const double side = turn > 0.0 ? -1.0 : 1.0;   // girando a sinistra l'esterno è a destra
        const Vec2 v = e.point;
        const Vec2 pa = v + left(e.dir) * (side * e.width / 2.0);
        const Vec2 pb = v + left(s.dir) * (side * s.width / 2.0);
        // Incrocio dei bordi: pa + e.dir * t = pb + s.dir * u.
        const double det = cross2(e.dir, s.dir);
        bool mitered = false;
        if (std::abs(det) > 1e-12) {
            const double t = cross2(pb - pa, s.dir) / det;
            const Vec2 m = pa + e.dir * t;
            if (t >= 0.0 && distance(m, v) <= 2.0 * std::max(e.width, s.width)) {
                out.push_back({v, pa, m, pb});
                mitered = true;
            }
        }
        if (!mitered)
            out.push_back({v, pa, pb});
    }
    return out;
}

double PolylineEntity::length() const
{
    double len = 0.0;
    for (const Prim& p : prims())
        len += p.kind == Prim::Kind::Segment ? distance(p.a, p.b) : p.radius * p.sweep;
    return len;
}

double PolylineEntity::area() const
{
    if (!closed || vertices.size() < 3)
        return 0.0;
    double twice = 0.0;
    const std::size_t n = vertices.size();
    for (std::size_t i = 0; i < n; ++i)
        twice += cross(vertices[i].point, vertices[(i + 1) % n].point);
    double a = twice / 2.0;
    // Contributo dei tratti ad arco: segmento circolare, con il segno della curvatura.
    for (std::size_t i = 0; i < n; ++i) {
        const double b = vertices[i].bulge;
        if (std::abs(b) < 1e-12)
            continue;
        const Prim p = Prim::bulge(vertices[i].point, vertices[(i + 1) % n].point, b);
        const double segment = p.radius * p.radius / 2.0 * (p.sweep - std::sin(p.sweep));
        a += b > 0.0 ? segment : -segment;
    }
    return std::abs(a);
}

// ---------------------------------------------------------------------- ellisse

double EllipseEntity::paramSweep() const
{
    const double s = normalizeAngle(endParam - startParam);
    return s <= 1e-12 ? 2.0 * kPi : s;
}

bool EllipseEntity::isFull() const
{
    return paramSweep() >= 2.0 * kPi - 1e-9;
}

void EllipseEntity::decompose(std::vector<Prim>& out) const
{
    const double sweep = paramSweep();
    const int n = std::max(8, static_cast<int>(std::ceil(96.0 * sweep / (2.0 * kPi))));
    std::vector<Vec2> pts;
    pts.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i)
        pts.push_back(pointAt(startParam + sweep * i / n));
    appendPolyline(out, pts);
}

std::vector<SnapPoint> EllipseEntity::snapPoints() const
{
    std::vector<SnapPoint> points{{center, SnapKind::Center}};
    const double sweep = paramSweep();
    for (int k = 0; k < 4; ++k) {
        const double t = k * kPi / 2.0;
        if (normalizeAngle(t - startParam) <= sweep + 1e-9)
            points.push_back({pointAt(t), SnapKind::Quadrant});
    }
    if (!isFull()) {
        points.push_back({pointAt(startParam), SnapKind::Endpoint});
        points.push_back({pointAt(startParam + sweep), SnapKind::Endpoint});
    }
    return points;
}

void EllipseEntity::transform(const Affine& t)
{
    const Vec2 newMajor = t.applyVector(majorAxis);
    const Vec2 newMinor = t.applyVector(minorAxis());
    center = t.apply(center);
    if (newMajor.length() > 0.0)
        ratio = newMinor.length() / newMajor.length();
    majorAxis = newMajor;
    if (t.determinant() < 0.0) {
        const double s = startParam;
        startParam = -endParam;
        endParam = -s;
    }
}

// ----------------------------------------------------------------------- spline

std::vector<Vec2> SplineEntity::evaluate() const
{
    const int n = static_cast<int>(control.size());
    const int p = degree;
    const bool valid = p >= 1 && n >= p + 1 && static_cast<int>(knots.size()) == n + p + 1;

    if (valid) {
        const bool rational = static_cast<int>(weights.size()) == n;
        const double t0 = knots[static_cast<std::size_t>(p)];
        const double t1 = knots[static_cast<std::size_t>(n)];
        const int samples = std::max(32, n * 10);
        std::vector<Vec2> pts;
        pts.reserve(static_cast<std::size_t>(samples) + 1);

        struct H { double x, y, w; };
        for (int i = 0; i <= samples; ++i) {
            const double t = t0 + (t1 - t0) * i / samples;
            // Intervallo dei nodi che contiene t.
            int s = p;
            while (s < n - 1 && t >= knots[static_cast<std::size_t>(s + 1)])
                ++s;
            // Algoritmo di de Boor in coordinate omogenee.
            std::vector<H> d(static_cast<std::size_t>(p) + 1);
            for (int j = 0; j <= p; ++j) {
                const auto idx = static_cast<std::size_t>(j + s - p);
                const double w = rational ? weights[idx] : 1.0;
                d[static_cast<std::size_t>(j)] = {control[idx].x * w, control[idx].y * w, w};
            }
            for (int r = 1; r <= p; ++r) {
                for (int j = p; j >= r; --j) {
                    const double left = knots[static_cast<std::size_t>(j + s - p)];
                    const double right = knots[static_cast<std::size_t>(j + 1 + s - r)];
                    const double alpha = right > left ? (t - left) / (right - left) : 0.0;
                    H& cur = d[static_cast<std::size_t>(j)];
                    const H& prev = d[static_cast<std::size_t>(j - 1)];
                    cur = {(1 - alpha) * prev.x + alpha * cur.x, (1 - alpha) * prev.y + alpha * cur.y,
                           (1 - alpha) * prev.w + alpha * cur.w};
                }
            }
            const H& r = d[static_cast<std::size_t>(p)];
            pts.push_back(r.w != 0.0 ? Vec2{r.x / r.w, r.y / r.w} : Vec2{r.x, r.y});
        }
        return pts;
    }

    // Solo punti di interpolazione: curva di Catmull-Rom che passa per tutti i punti.
    if (fit.size() >= 2) {
        std::vector<Vec2> pts;
        const std::size_t m = fit.size();
        for (std::size_t i = 0; i + 1 < m; ++i) {
            const Vec2 p0 = i > 0 ? fit[i - 1] : fit[i];
            const Vec2 p1 = fit[i];
            const Vec2 p2 = fit[i + 1];
            const Vec2 p3 = i + 2 < m ? fit[i + 2] : fit[i + 1];
            for (int k = 0; k < 12; ++k) {
                const double t = k / 12.0;
                const double t2 = t * t, t3 = t2 * t;
                pts.push_back((p1 * 2.0 + (p2 - p0) * t + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2
                               + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5);
            }
        }
        pts.push_back(fit.back());
        return pts;
    }
    return control;
}

void SplineEntity::decompose(std::vector<Prim>& out) const
{
    appendPolyline(out, evaluate(), false);
}

std::vector<SnapPoint> SplineEntity::snapPoints() const
{
    const auto pts = evaluate();
    if (pts.empty() || closed)
        return {};
    return {{pts.front(), SnapKind::Endpoint}, {pts.back(), SnapKind::Endpoint}};
}

void SplineEntity::transform(const Affine& t)
{
    for (Vec2& p : control)
        p = t.apply(p);
    for (Vec2& p : fit)
        p = t.apply(p);
}

// ------------------------------------------------------------------------ testo

std::string TextEntity::displayText() const
{
    return decodePercentCodes(text);
}

double TextEntity::estimatedWidth() const
{
    return estimateWidth(displayText(), height, widthFactor);
}

double TextEntity::effectiveRotation() const
{
    if ((hAlign == Aligned || hAlign == Fit) && distance(position, alignPoint) > 0.0)
        return angleOf(alignPoint - position);
    return rotation;
}

Vec2 TextEntity::origin() const
{
    if ((hAlign == Left && vAlign == Baseline) || hAlign == Aligned || hAlign == Fit)
        return position;
    const double rot = effectiveRotation();
    const Vec2 dir = Vec2::polar(1.0, rot);
    const Vec2 up = Vec2::polar(1.0, rot + kPi / 2.0);
    const double w = estimatedWidth();
    double dx = 0.0;
    if (hAlign == Center || hAlign == Middle)
        dx = w / 2.0;
    else if (hAlign == Right)
        dx = w;
    double dy = 0.0;   // spostamento della linea di base rispetto al punto di riferimento
    if (hAlign == Middle || vAlign == VMiddle)
        dy = -height / 2.0;
    else if (vAlign == Top)
        dy = -height;
    else if (vAlign == Bottom)
        dy = height * 0.25;
    return alignPoint - dir * dx + up * dy;
}

std::array<Vec2, 4> TextEntity::box() const
{
    const double rot = effectiveRotation();
    const Vec2 dir = Vec2::polar(1.0, rot);
    const Vec2 up = Vec2::polar(1.0, rot + kPi / 2.0);
    const double w = (hAlign == Aligned || hAlign == Fit) ? distance(position, alignPoint) : estimatedWidth();
    const Vec2 o = origin();
    const Vec2 bottom = o - up * (height * 0.25);
    return {bottom, bottom + dir * w, o + dir * w + up * height, o + up * height};
}

std::vector<SnapPoint> TextEntity::snapPoints() const
{
    const bool simple = hAlign == Left && vAlign == Baseline;
    return {{simple ? position : alignPoint, SnapKind::Insertion}};
}

void TextEntity::transform(const Affine& t)
{
    position = t.apply(position);
    alignPoint = t.apply(alignPoint);
    rotation += rotationOf(t);
    height *= scaleOf(t);
}

BBox TextEntity::bounds() const { return quadBounds(box()); }
double TextEntity::distanceTo(Vec2 p) const { return quadDistance(box(), p); }
bool TextEntity::touchesRect(const BBox& rect) const { return quadTouchesRect(box(), rect); }

// ---------------------------------------------------------------- testo multilinea

std::string mtextPlainText(const std::string& s)
{
    std::string plain;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{' || c == '}')
            continue;
        if (c != '\\' || i + 1 >= s.size()) {
            plain += c;
            continue;
        }
        const char code = s[++i];
        switch (code) {
        case 'P':
        case 'N':
            plain += '\n';
            break;
        case '~':
            plain += ' ';
            break;
        case '\\':
        case '{':
        case '}':
            plain += code;
            break;
        case 'S': {   // frazioni impilate: \S1/2;  \S1^2;  \S1#2;
            const auto end = s.find(';', i + 1);
            std::string stack = s.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
            std::replace(stack.begin(), stack.end(), '^', '/');
            std::replace(stack.begin(), stack.end(), '#', '/');
            plain += stack;
            i = end == std::string::npos ? s.size() : end;
            break;
        }
        case 'U':     // \U+00B0
            if (i + 5 < s.size() && s[i + 1] == '+') {
                try {
                    appendUtf8(plain, static_cast<unsigned>(std::stoul(s.substr(i + 2, 4), nullptr, 16)));
                } catch (...) {
                }
                i += 5;
            }
            break;
        case 'L': case 'l': case 'O': case 'o': case 'K': case 'k':
            break;
        default: {    // \f..; \H..; \C..; \T..; \Q..; \W..; \A..; \p..;
            const auto end = s.find(';', i);
            i = end == std::string::npos ? s.size() : end;
            break;
        }
        }
    }
    return plain;
}

std::string mtextWithPlainText(const std::string& original, const std::string& plain)
{
    // Formattazione iniziale: codici "\X...;" all'inizio, eventualmente dentro un gruppo {} che chiude in fondo.
    const std::string& s = original;
    std::size_t i = 0;
    bool group = false;
    if (!s.empty() && s[0] == '{') {
        // Il gruppo deve chiudersi proprio all'ultimo carattere.
        int depth = 0;
        std::size_t close = std::string::npos;
        for (std::size_t k = 0; k < s.size(); ++k) {
            if (s[k] == '\\') {
                ++k;
                continue;
            }
            if (s[k] == '{')
                ++depth;
            else if (s[k] == '}' && --depth == 0) {
                close = k;
                break;
            }
        }
        group = close == s.size() - 1;
        if (group)
            i = 1;
    }
    const std::size_t codesStart = i;
    static const std::string kCodes = "fFHWQTACcp";
    while (i + 1 < s.size() && s[i] == '\\' && kCodes.find(s[i + 1]) != std::string::npos) {
        const auto end = s.find(';', i);
        if (end == std::string::npos)
            break;
        i = end + 1;
    }
    const std::string codes = s.substr(codesStart, i - codesStart);
    std::string body;
    for (char c : plain) {
        if (c == '\\' || c == '{' || c == '}') {
            body += '\\';
            body += c;
        } else if (c == '\n') {
            body += "\\P";
        } else if (c != '\r') {
            body += c;
        }
    }
    if (codes.empty())
        return body;
    return group ? "{" + codes + body + "}" : codes + body;
}

std::vector<std::string> MTextEntity::lines() const
{
    // 1. Rimozione dei codici di formattazione.
    const std::string plain = decodePercentCodes(mtextPlainText(text));

    // 2. Divisione in paragrafi e a capo automatico sulla larghezza.
    std::vector<std::string> result;
    std::istringstream paragraphs(plain);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        if (width <= 0.0 || estimateWidth(paragraph, height) <= width) {
            result.push_back(paragraph);
            continue;
        }
        std::istringstream words(paragraph);
        std::string word, line;
        while (words >> word) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && estimateWidth(candidate, height) > width) {
                result.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        result.push_back(line);
    }
    if (result.empty())
        result.emplace_back();
    return result;
}

std::vector<SnapPoint> MTextEntity::gripPoints() const
{
    const auto b = box();   // in basso a sinistra, in basso a destra, in alto a destra, in alto a sinistra
    return {{position, SnapKind::Insertion}, {(b[1] + b[2]) / 2.0, SnapKind::Node}, {(b[0] + b[3]) / 2.0, SnapKind::Node}};
}

void MTextEntity::dragWidth(bool right, Vec2 to)
{
    const auto b = box();
    const Vec2 dir = Vec2::polar(1.0, rotation);
    const double oldWidth = (b[1] - b[0]).dot(dir);
    const Vec2 left = b[0], rightEdge = b[1];
    double w = right ? (to - left).dot(dir) : (rightEdge - to).dot(dir);
    w = std::max(w, height);   // almeno un carattere
    const int col = (attachment - 1) % 3;   // 0 sinistra, 1 centro, 2 destra
    // Il punto di inserimento sta a col/2 della larghezza dal lato sinistro.
    const Vec2 leftNew = right ? left : rightEdge - dir * w;
    position = position + (leftNew - left) + dir * (col / 2.0 * (w - oldWidth));
    width = w;
}

std::array<Vec2, 4> MTextEntity::box() const
{
    const auto ls = lines();
    double w = width;
    if (w <= 0.0)
        for (const auto& l : ls)
            w = std::max(w, estimateWidth(l, height));
    std::size_t rows = ls.size();
    if (columns > 1 && width > 0.0) {   // colonne: più larghe, meno alte
        rows = (ls.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns);
        w = columns * width + (columns - 1) * gutter();
    }
    const double h = height + (static_cast<double>(std::max<std::size_t>(rows, 1)) - 1.0) * lineHeight();
    const int col = (attachment - 1) % 3;
    const int row = (attachment - 1) / 3;
    const double left = -col * w / 2.0;
    const double top = row == 0 ? 0.0 : (row == 1 ? h / 2.0 : h);
    const Vec2 dir = Vec2::polar(1.0, rotation);
    const Vec2 up = Vec2::polar(1.0, rotation + kPi / 2.0);
    const auto at = [&](double x, double y) { return position + dir * x + up * y; };
    return {at(left, top - h - height * 0.25), at(left + w, top - h - height * 0.25), at(left + w, top), at(left, top)};
}

void MTextEntity::transform(const Affine& t)
{
    position = t.apply(position);
    rotation += rotationOf(t);
    height *= scaleOf(t);
    width *= scaleOf(t);
}

BBox MTextEntity::bounds() const { return quadBounds(box()); }
double MTextEntity::distanceTo(Vec2 p) const { return quadDistance(box(), p); }
bool MTextEntity::touchesRect(const BBox& rect) const { return quadTouchesRect(box(), rect); }

// ------------------------------------------------------------------------ solido

void SolidEntity::decompose(std::vector<Prim>& out) const
{
    std::vector<Vec2> pts{corners[0], corners[1], corners[2]};
    if (distance(corners[3], corners[2]) > 1e-12)
        pts.push_back(corners[3]);
    appendPolyline(out, pts, true);
}

std::vector<SnapPoint> SolidEntity::snapPoints() const
{
    std::vector<SnapPoint> points;
    for (Vec2 c : corners)
        points.push_back({c, SnapKind::Endpoint});
    return points;
}

void SolidEntity::transform(const Affine& t)
{
    for (Vec2& c : corners)
        c = t.apply(c);
}

double SolidEntity::distanceTo(Vec2 p) const
{
    return quadDistance(corners, p);
}

// --------------------------------------------------------------------- tratteggio

void HatchEntity::decompose(std::vector<Prim>& out) const
{
    for (const auto& loop : loops) {
        const std::size_t n = loop.size();
        for (std::size_t i = 0; i < n && n >= 2; ++i)
            out.push_back(Prim::bulge(loop[i].point, loop[(i + 1) % n].point, loop[i].bulge));
    }
}

std::vector<std::vector<Vec2>> HatchEntity::tessellatedLoops() const
{
    std::vector<std::vector<Vec2>> result;
    for (const auto& loop : loops) {
        std::vector<Vec2> pts;
        const std::size_t n = loop.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Prim p = Prim::bulge(loop[i].point, loop[(i + 1) % n].point, loop[i].bulge);
            pts.push_back(loop[i].point);
            if (p.kind == Prim::Kind::Arc) {
                const int steps = std::max(4, static_cast<int>(std::ceil(p.sweep / (kPi / 24))));
                const bool reversed = loop[i].bulge < 0.0;
                for (int k = 1; k < steps; ++k) {
                    const double f = static_cast<double>(k) / steps;
                    pts.push_back(p.pointAt(reversed ? p.start + p.sweep * (1.0 - f) : p.start + p.sweep * f));
                }
            }
        }
        if (pts.size() >= 3)
            result.push_back(std::move(pts));
    }
    return result;
}

bool HatchEntity::contains(Vec2 p) const
{
    bool inside = false;
    for (const auto& poly : tessellatedLoops()) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            if (((poly[i].y > p.y) != (poly[j].y > p.y))
                && (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
                inside = !inside;
        }
    }
    return inside;
}

double HatchEntity::distanceTo(Vec2 p) const
{
    return contains(p) ? 0.0 : Entity::distanceTo(p);
}

void HatchEntity::transform(const Affine& t)
{
    const bool mirrored = t.determinant() < 0.0;
    for (auto& loop : loops)
        for (PolyVertex& v : loop) {
            v.point = t.apply(v.point);
            if (mirrored)
                v.bulge = -v.bulge;
        }
    patternScale *= scaleOf(t);
    patternAngle += rotationOf(t);
    const double s = scaleOf(t);
    for (HatchPatternLine& line : patternLines) {
        line.base = t.apply(line.base);
        line.offset = t.applyVector(line.offset);
        line.angle = angleOf(t.applyVector(Vec2::polar(1.0, line.angle)));
        for (double& d : line.dashes)
            d *= s;
    }
}

// --------------------------------------------------------------------- inserimento

Affine InsertEntity::affine() const
{
    return Affine::insert(block ? block->base : Vec2{}, position, scaleX, scaleY, rotation);
}

std::vector<Vec2> InsertEntity::clipPolygon() const
{
    if (clip.size() == 2) {
        const Vec2 a = clip[0], b = clip[1];
        return {{std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::min(a.y, b.y)},
                {std::max(a.x, b.x), std::max(a.y, b.y)}, {std::min(a.x, b.x), std::max(a.y, b.y)}};
    }
    return clip;
}

void InsertEntity::decompose(std::vector<Prim>& out) const
{
    if (!block)
        return;
    DepthGuard guard;
    if (guard.tooDeep())
        return;
    const Affine t = affine();
    for (const auto& child : block->entities) {
        std::vector<Prim> local;
        child->decompose(local);
        transformPrims(local, t);
        out.insert(out.end(), local.begin(), local.end());
    }
}

namespace {

// Ingombro nel disegno di un riquadro del blocco (i quattro angoli trasformati).
BBox transformedBox(const BBox& b, const Affine& t)
{
    BBox out;
    if (!b.isValid())
        return out;
    out.expand(t.apply(b.min));
    out.expand(t.apply(b.max));
    out.expand(t.apply({b.min.x, b.max.y}));
    out.expand(t.apply({b.max.x, b.min.y}));
    return out;
}

double boxDistance(const BBox& b, Vec2 p)
{
    const double dx = std::max({b.min.x - p.x, 0.0, p.x - b.max.x});
    const double dy = std::max({b.min.y - p.y, 0.0, p.y - b.max.y});
    return std::hypot(dx, dy);
}

// Affine inversa (nullopt se degenere).
std::optional<Affine> inverseOf(const Affine& t)
{
    const double det = t.determinant();
    if (std::abs(det) < 1e-300)
        return std::nullopt;
    Affine inv;
    inv.m11 = t.m22 / det;
    inv.m12 = -t.m12 / det;
    inv.m21 = -t.m21 / det;
    inv.m22 = t.m11 / det;
    inv.dx = -(inv.m11 * t.dx + inv.m21 * t.dy);
    inv.dy = -(inv.m12 * t.dx + inv.m22 * t.dy);
    return inv;
}

} // namespace

double InsertEntity::distanceTo(Vec2 p) const
{
    double best = std::numeric_limits<double>::infinity();
    if (!block)
        return best;
    DepthGuard guard;
    if (guard.tooDeep())
        return best;
    const Affine t = affine();
    const auto& boxes = block->childBounds();
    const bool similar = t.isSimilarity();
    const double scale = similar ? t.uniformScale() : 0.0;
    const auto inv = similar ? inverseOf(t) : std::nullopt;
    // Prima i figli più vicini: il migliore trovato scarta subito tutti gli altri.
    std::vector<std::pair<double, std::size_t>> order;
    order.reserve(block->entities.size());
    for (std::size_t i = 0; i < block->entities.size(); ++i) {
        const BBox world = transformedBox(i < boxes.size() ? boxes[i] : BBox{}, t);
        if (world.isValid())
            order.emplace_back(boxDistance(world, p), i);
    }
    std::sort(order.begin(), order.end());
    for (const auto& [lower, i] : order) {
        if (lower >= best)
            break;
        const Entity& child = *block->entities[i];
        if (inv && scale > 0.0) {
            best = std::min(best, child.distanceTo(inv->apply(p)) * scale);
        } else {
            std::vector<Prim> local;
            child.decompose(local);
            transformPrims(local, t);
            for (const Prim& prim : local)
                best = std::min(best, primDistance(prim, p));
        }
    }
    return best;
}

bool InsertEntity::touchesRect(const BBox& rect) const
{
    if (!block)
        return false;
    DepthGuard guard;
    if (guard.tooDeep())
        return false;
    const Affine t = affine();
    const auto& boxes = block->childBounds();
    for (std::size_t i = 0; i < block->entities.size(); ++i) {
        const BBox world = transformedBox(i < boxes.size() ? boxes[i] : BBox{}, t);
        if (!world.isValid() || world.max.x < rect.min.x || world.min.x > rect.max.x || world.max.y < rect.min.y
            || world.min.y > rect.max.y)
            continue;
        std::vector<Prim> local;
        block->entities[i]->decompose(local);
        transformPrims(local, t);
        for (const Prim& prim : local)
            if (primTouchesRect(prim, rect))
                return true;
    }
    return false;
}

void InsertEntity::primsNear(Vec2 p, double radius, std::vector<Prim>& out) const
{
    if (!block)
        return;
    DepthGuard guard;
    if (guard.tooDeep())
        return;
    const Affine t = affine();
    const auto& boxes = block->childBounds();
    for (std::size_t i = 0; i < block->entities.size(); ++i) {
        const BBox world = transformedBox(i < boxes.size() ? boxes[i] : BBox{}, t);
        if (!world.isValid() || boxDistance(world, p) > radius)
            continue;
        std::vector<Prim> local;
        block->entities[i]->decompose(local);
        transformPrims(local, t);
        for (const Prim& prim : local)
            if (primDistance(prim, p) <= radius)
                out.push_back(prim);
    }
}

void InsertEntity::snapPointsNear(Vec2 p, double radius, std::vector<SnapPoint>& out) const
{
    if (distance(position, p) <= radius)
        out.push_back({position, SnapKind::Insertion});
    if (!block)
        return;
    DepthGuard guard;
    if (guard.tooDeep())
        return;
    const Affine t = affine();
    const auto& boxes = block->childBounds();
    for (std::size_t i = 0; i < block->entities.size(); ++i) {
        const BBox world = transformedBox(i < boxes.size() ? boxes[i] : BBox{}, t);
        if (!world.isValid() || boxDistance(world, p) > radius)
            continue;
        // Punti del figlio nel blocco, portati nel disegno.
        for (const SnapPoint& sp : block->entities[i]->snapPoints()) {
            const Vec2 w = t.apply(sp.position);
            if (distance(w, p) <= radius)
                out.push_back({w, sp.kind});
        }
    }
}

std::vector<SnapPoint> InsertEntity::snapPoints() const
{
    std::vector<SnapPoint> points{{position, SnapKind::Insertion}};
    if (!block)
        return points;
    DepthGuard guard;
    if (guard.tooDeep())
        return points;
    const Affine t = affine();
    for (const auto& child : block->entities)
        for (const SnapPoint& sp : child->snapPoints())
            points.push_back({t.apply(sp.position), sp.kind});
    return points;
}

BBox InsertEntity::bounds() const
{
    BBox box = fullBounds();
    if (!clipped() || !box.isValid())
        return box;
    // Con il ritaglio: l'ingombro non va oltre il contorno.
    BBox frame;
    const Affine a = affine();
    for (Vec2 p : clipPolygon())
        frame.expand(a.apply(p));
    BBox out;
    out.min = {std::max(box.min.x, frame.min.x), std::max(box.min.y, frame.min.y)};
    out.max = {std::min(box.max.x, frame.max.x), std::min(box.max.y, frame.max.y)};
    return out.min.x <= out.max.x && out.min.y <= out.max.y ? out : frame;
}

BBox InsertEntity::fullBounds() const
{
    // Il punto di inserimento non fa parte dell'ingombro: la geometria del blocco può stare lontana dalla sua
    // origine (blocchi scalati e ruotati: l'ingombro arrivava a centinaia di metri dal disegno e ZOOM Estensioni
    // lo rimpiccioliva). Conta solo se il blocco non disegna niente.
    BBox b;
    for (const Attribute& a : attributes)
        if ((a.flags & AttInvisible) == 0)
            b.expand(a.text.bounds());
    DepthGuard guard;
    if (block && !guard.tooDeep()) {
        const Affine t = affine();
        for (const auto& child : block->entities) {
            const BBox cb = child->bounds();
            if (!cb.isValid())
                continue;
            for (Vec2 c : {cb.min, Vec2{cb.max.x, cb.min.y}, cb.max, Vec2{cb.min.x, cb.max.y}})
                b.expand(t.apply(c));
        }
    }
    if (!b.isValid())
        b.expand(position);
    return b;
}

void InsertEntity::transform(const Affine& t)
{
    for (Attribute& a : attributes)
        a.text.transform(t);
    const Affine combined = t * affine();
    position = t.apply(position);
    const Vec2 col1 = combined.applyVector({1.0, 0.0});
    const Vec2 col2 = combined.applyVector({0.0, 1.0});
    rotation = angleOf(col1);
    scaleX = col1.length();
    const Vec2 perpDir = Vec2::polar(1.0, rotation + kPi / 2.0);
    scaleY = col2.dot(perpDir);
}

// ------------------------------------------------------------------------- quota

double DimensionEntity::measurement() const
{
    switch (kind) {
    case Linear:
        return std::abs((p2 - p1).dot(Vec2::polar(1.0, rotation)));
    case Aligned:
        return distance(p1, p2);
    case Radius:
    case Diameter:
        return distance(dimLine, curvePoint);
    case Angular:
    case Angular3:
        return angularSweep() * 180.0 / kPi;   // gradi
    case Ordinate:
        return std::abs(ordinateX ? p1.x - dimLine.x : p1.y - dimLine.y);
    default:
        return 0.0;
    }
}

double DimensionEntity::angularSweep(double* startAngle) const
{
    // Dal primo lato al secondo in senso antiorario; se il punto dell'arco cade fuori, l'altro arco.
    const double a1 = angleOf(p1 - curvePoint), a2 = angleOf(p2 - curvePoint);
    const double sweep = normalizeAngle(a2 - a1);
    const double at = normalizeAngle(angleOf(dimLine - curvePoint) - a1);
    if (at <= sweep) {
        if (startAngle)
            *startAngle = a1;
        return sweep;
    }
    if (startAngle)
        *startAngle = a2;
    return 2.0 * kPi - sweep;
}

std::string DimensionEntity::displayText() const
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(style.decimals);
    const bool angular = kind == Angular || kind == Angular3;
    out << measurement() * (angular ? 1.0 : style.linearFactor);
    std::string value = out.str();
    // Zeri finali soppressi (DIMZIN 8).
    if (value.find('.') != std::string::npos) {
        while (!value.empty() && value.back() == '0')
            value.pop_back();
        if (!value.empty() && value.back() == '.')
            value.pop_back();
    }
    if (kind == Radius)
        value = "R" + value;
    else if (kind == Diameter)
        value = "Ø" + value;
    else if (angular)
        value += "°";
    // Prefisso e suffisso dello stile (DIMPOST).
    if (!style.post.empty()) {
        std::string post = style.post;
        if (const auto pos = post.find("<>"); pos != std::string::npos)
            value = post.replace(pos, 2, value);
        else
            value += post;
    }

    if (textOverride.empty())
        return value;
    std::string result = decodePercentCodes(textOverride);
    if (const auto pos = result.find("<>"); pos != std::string::npos)
        result.replace(pos, 2, value);
    return result;
}

std::vector<std::unique_ptr<Entity>> DimensionEntity::geometry() const
{
    std::vector<std::unique_ptr<Entity>> out;

    if (block) {
        for (const auto& child : block->entities) {
            auto copy = child->clone();
            copy->transform(blockTransform);
            out.push_back(std::move(copy));
        }
        return out;
    }

    const double s = style.scale > 0.0 ? style.scale : 1.0;
    const double textHeight = style.textHeight * s;
    const double gap = style.textGap * s;
    const double a = style.arrowSize * s;
    const auto arrow = [&](Vec2 tip, Vec2 towards) {
        const Vec2 d = towards - tip;
        const double len = d.length();
        if (len <= 0.0)
            return;
        const Vec2 u = d / len;
        if (style.tickSize > 0.0) {
            // Trattino obliquo a 45° (architettonico).
            const Vec2 t = Vec2::polar(style.tickSize * s, angleOf(u) + kPi / 4.0);
            out.push_back(std::make_unique<LineEntity>(tip - t, tip + t));
            return;
        }
        const Vec2 n{-u.y, u.x};
        const Vec2 base = tip + u * a;
        out.push_back(std::make_unique<SolidEntity>(
            std::array<Vec2, 4>{tip, base + n * (a / 6.0), base - n * (a / 6.0), base - n * (a / 6.0)}));
    };
    // Testo centrato sopra il tratto a -> b, sempre leggibile; spostato dall'utente: centrato in textPos.
    const auto label = [&](Vec2 a1, Vec2 b1) {
        double rot = angleOf(b1 - a1);
        if (rot > kPi / 2.0 + 1e-9)
            rot -= kPi;
        else if (rot <= -kPi / 2.0 + 1e-9)
            rot += kPi;
        const Vec2 up = Vec2::polar(1.0, rot + kPi / 2.0);
        if (textRotation)
            rot = *textRotation;
        auto text = std::make_unique<TextEntity>((a1 + b1) / 2.0 + up * gap, textHeight, displayText(), rot);
        if (textMoved) {
            text->hAlign = TextEntity::Middle;
            text->vAlign = TextEntity::Baseline;
            text->position = textPos;
        } else {
            text->hAlign = TextEntity::Center;
            text->vAlign = TextEntity::Bottom;
        }
        text->alignPoint = text->position;
        out.push_back(std::move(text));
    };

    switch (kind) {
    case Linear:
    case Aligned: {
        const auto [q1, q2] = dimensionLineEnds();
        for (const auto& [from, to] : {std::pair{p1, q1}, std::pair{p2, q2}}) {
            const double len = distance(from, to);
            if (len <= 0.0)
                continue;
            const Vec2 u = (to - from) / len;
            out.push_back(std::make_unique<LineEntity>(from + u * (style.extOffset * s), to + u * (style.extExtension * s)));
        }
        out.push_back(std::make_unique<LineEntity>(q1, q2));
        arrow(q1, q2);
        arrow(q2, q1);
        label(q1, q2);
        break;
    }
    case Radius:
        out.push_back(std::make_unique<LineEntity>(dimLine, curvePoint));
        arrow(curvePoint, dimLine);
        label(dimLine, curvePoint);
        break;
    case Diameter:
        out.push_back(std::make_unique<LineEntity>(dimLine, curvePoint));
        arrow(dimLine, curvePoint);
        arrow(curvePoint, dimLine);
        label(dimLine, curvePoint);
        break;
    case Angular:
    case Angular3: {
        // Arco di quota attorno al vertice, linee di estensione dai lati, frecce tangenti e testo a metà arco.
        const Vec2 v = curvePoint;
        const double r = distance(v, dimLine);
        if (r <= 0.0)
            break;
        double start = 0.0;
        const double sweep = angularSweep(&start);
        const double end = start + sweep;
        out.push_back(std::make_unique<ArcEntity>(v, r, start, end));
        for (const auto& [side, angle] : {std::pair{p1, angleOf(p1 - v)}, std::pair{p2, angleOf(p2 - v)}}) {
            const double len = distance(v, side);
            const Vec2 u = Vec2::polar(1.0, angle);
            if (len < r - 1e-9)   // il lato non arriva all'arco: linea di estensione
                out.push_back(std::make_unique<LineEntity>(side + u * (style.extOffset * s), v + u * (r + style.extExtension * s)));
        }
        const Vec2 q1 = v + Vec2::polar(r, start), q2 = v + Vec2::polar(r, end);
        arrow(q1, q1 + Vec2::polar(a, start + kPi / 2.0));
        arrow(q2, q2 + Vec2::polar(a, end - kPi / 2.0));
        const double mid = start + sweep / 2.0;
        const Vec2 m = v + Vec2::polar(r, mid);
        const Vec2 t = Vec2::polar(textHeight, mid + kPi / 2.0);
        label(m - t, m + t);
        break;
    }
    case Ordinate: {
        // Direttrice dal punto quotato (con il distacco) alla fine, con una piega; il valore dopo la fine.
        const double sign = ordinateX ? (p2.y >= p1.y ? 1.0 : -1.0) : (p2.x >= p1.x ? 1.0 : -1.0);
        const double jog = 2.0 * textHeight;
        std::vector<Vec2> pts;
        if (ordinateX) {
            pts = {p1 + Vec2{0.0, sign * style.extOffset * s}, {p1.x, p2.y - sign * jog}, {p2.x, p2.y - sign * jog * 0.5}, p2};
        } else {
            pts = {p1 + Vec2{sign * style.extOffset * s, 0.0}, {p2.x - sign * jog, p1.y}, {p2.x - sign * jog * 0.5, p2.y}, p2};
        }
        for (std::size_t i = 1; i < pts.size(); ++i)
            if (distance(pts[i - 1], pts[i]) > 1e-12)
                out.push_back(std::make_unique<LineEntity>(pts[i - 1], pts[i]));
        auto text = std::make_unique<TextEntity>(p2, textHeight, displayText(), ordinateX ? kPi / 2.0 : 0.0);
        text->hAlign = sign > 0 ? TextEntity::Left : TextEntity::Right;
        text->vAlign = TextEntity::VMiddle;
        text->position = text->alignPoint = ordinateX ? p2 + Vec2{0.0, sign * gap} : p2 + Vec2{sign * gap, 0.0};
        out.push_back(std::move(text));
        break;
    }
    default:
        break;
    }
    return out;
}

void DimensionEntity::decompose(std::vector<Prim>& out) const
{
    DepthGuard guard;
    if (guard.tooDeep())
        return;
    for (const auto& e : geometry())
        if (e->type() != EntityType::Text && e->type() != EntityType::MText)
            e->decompose(out);
}

// Punti delle maniglie: origini, estremi della linea di quota (o punto sull'arco), e per ultimo il testo
// (SnapKind::Insertion), che si trascina dove si vuole.
std::vector<SnapPoint> DimensionEntity::snapPoints() const
{
    DepthGuard guard;
    if (guard.tooDeep())
        return {};
    const SnapPoint text{textMidpoint(), SnapKind::Insertion};
    if (kind == Linear || kind == Aligned) {
        const auto [q1, q2] = dimensionLineEnds();
        return {{p1, SnapKind::Node}, {p2, SnapKind::Node}, {q1, SnapKind::Node}, {q2, SnapKind::Node}, text};
    }
    if (kind == Angular || kind == Angular3)
        return {{p1, SnapKind::Node}, {p2, SnapKind::Node}, {curvePoint, SnapKind::Node}, {dimLine, SnapKind::Node}, text};
    if (kind == Ordinate)
        return {{p1, SnapKind::Node}, {p2, SnapKind::Node}};
    if (kind == Radius || kind == Diameter)
        return {{curvePoint, SnapKind::Node}, text};
    return {};
}

std::pair<Vec2, Vec2> DimensionEntity::dimensionLineEnds() const
{
    const Vec2 dir = kind == Aligned && distance(p1, p2) > 0.0 ? (p2 - p1) / distance(p1, p2) : Vec2::polar(1.0, rotation);
    const Vec2 n{-dir.y, dir.x};
    // Linee di estensione oblique (DIMEDIT Obliqua): dall'origine nella direzione obliqua fino alla linea di quota.
    const Vec2 o = Vec2::polar(1.0, oblique);
    const double den = cross(o, dir);
    const auto hit = [&](Vec2 p) {
        if (std::abs(oblique) < 1e-12 || std::abs(den) < 1e-9)
            return p + n * (dimLine - p).dot(n);
        return p + o * (cross(dimLine - p, dir) / den);
    };
    return {hit(p1), hit(p2)};
}

Vec2 DimensionEntity::textMidpoint() const
{
    for (const auto& e : geometry())
        if (e->type() == EntityType::Text || e->type() == EntityType::MText) {
            const BBox b = e->bounds();
            if (b.isValid())
                return (b.min + b.max) / 2.0;
        }
    return textPos;
}

double DimensionEntity::distanceTo(Vec2 p) const
{
    double best = std::numeric_limits<double>::infinity();
    DepthGuard guard;
    if (guard.tooDeep())
        return best;
    for (const auto& e : geometry())
        best = std::min(best, e->distanceTo(p));
    return best;
}

bool DimensionEntity::touchesRect(const BBox& rect) const
{
    DepthGuard guard;
    if (guard.tooDeep())
        return false;
    for (const auto& e : geometry())
        if (e->touchesRect(rect))
            return true;
    return false;
}

BBox DimensionEntity::bounds() const
{
    BBox b;
    DepthGuard guard;
    if (guard.tooDeep())
        return b;
    for (const auto& e : geometry())
        b.expand(e->bounds());
    return b;
}

void DimensionEntity::transform(const Affine& t)
{
    p1 = t.apply(p1);
    p2 = t.apply(p2);
    dimLine = t.apply(dimLine);
    textPos = t.apply(textPos);
    curvePoint = t.apply(curvePoint);
    rotation += rotationOf(t);
    blockTransform = t * blockTransform;
}

} // namespace cartesio

namespace cartesio {

// -------------------------------------------------------------- finestra di layout

BBox ViewportEntity::paperRect() const
{
    BBox b;
    b.expand(center - Vec2{width / 2.0, height / 2.0});
    b.expand(center + Vec2{width / 2.0, height / 2.0});
    return b;
}

BBox ViewportEntity::modelRect() const
{
    const double k = scale();
    BBox b;
    b.expand(viewCenter - Vec2{width / 2.0, height / 2.0} / k);
    b.expand(viewCenter + Vec2{width / 2.0, height / 2.0} / k);
    return b;
}

Affine ViewportEntity::modelToPaper() const
{
    const double k = scale();
    Affine a;
    a.m11 = k;
    a.m22 = k;
    a.dx = center.x - viewCenter.x * k;
    a.dy = center.y - viewCenter.y * k;
    return a;
}

std::vector<Vec2> ViewportEntity::outline() const
{
    if (clip.size() >= 3)
        return clip;
    const BBox r = paperRect();
    return {r.min, {r.max.x, r.min.y}, r.max, {r.min.x, r.max.y}};
}

void ViewportEntity::setClip(std::vector<Vec2> polygon)
{
    if (polygon.size() < 3) {
        clip.clear();
        return;
    }
    BBox b;
    for (Vec2 p : polygon)
        b.expand(p);
    if (b.width() <= 0.0 || b.height() <= 0.0)
        return;
    // Il modello resta dov'era sul foglio: si sposta il centro della vista insieme al riquadro.
    const double k = scale();
    viewCenter = viewCenter + (b.center() - center) / k;
    center = b.center();
    width = b.width();
    height = b.height();
    viewHeight = height / k;
    clip = std::move(polygon);
}

bool ViewportEntity::containsPaperPoint(Vec2 p) const
{
    const BBox r = paperRect();
    if (p.x < r.min.x || p.x > r.max.x || p.y < r.min.y || p.y > r.max.y)
        return false;
    if (clip.size() < 3)
        return true;
    // Regola pari/dispari.
    bool inside = false;
    for (std::size_t i = 0, j = clip.size() - 1; i < clip.size(); j = i++) {
        const Vec2 a = clip[i];
        const Vec2 b = clip[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

bool ViewportEntity::isLayerFrozen(int layer) const
{
    return std::find(frozenLayers.begin(), frozenLayers.end(), layer) != frozenLayers.end();
}

void ViewportEntity::decompose(std::vector<Prim>& out) const
{
    appendPolyline(out, outline(), true);
}

std::vector<SnapPoint> ViewportEntity::snapPoints() const
{
    std::vector<SnapPoint> points;
    for (Vec2 p : outline())
        points.push_back({p, SnapKind::Endpoint});
    points.push_back({center, SnapKind::Center});
    return points;
}

void ViewportEntity::transform(const Affine& t)
{
    // Si spostano e si scalano il riquadro sul foglio; la vista del modello resta la stessa.
    const double s = std::sqrt(std::abs(t.determinant()));
    center = t.apply(center);
    width *= s;
    height *= s;
    for (Vec2& p : clip)
        p = t.apply(p);
}

} // namespace cartesio

namespace cartesio {

// ------------------------------------------------------------------- direttrice

std::unique_ptr<MTextEntity> LeaderEntity::annotation() const
{
    if (text.empty() || points.empty())
        return nullptr;
    const Vec2 last = points.back();
    // Il testo sta dalla parte verso cui va l'ultimo tratto, centrato in altezza sull'ultimo punto.
    const bool right = points.size() < 2 || points.back().x >= points[points.size() - 2].x;
    const double gap = landingGap >= 0.0 ? landingGap : textHeight * 0.5;
    auto m = std::make_unique<MTextEntity>(last + Vec2{right ? gap : -gap, 0.0}, textHeight, text);
    m->attachment = right ? 4 : 6;   // centro-sinistra / centro-destra
    m->style = textStyle.empty() ? std::string("Standard") : textStyle;
    m->copyStyleFrom(*this);
    return m;
}

Vec2 LeaderEntity::junction() const
{
    if (points.empty())
        return {};
    if (points.size() >= 3) {
        const Vec2 last = points.back(), prev = points[points.size() - 2];
        if (std::abs(last.y - prev.y) < 1e-9 * (1.0 + std::abs(last.y)))
            return prev;   // tratto d'approdo orizzontale: si attacca al suo inizio
    }
    return points.back();
}

std::vector<std::unique_ptr<Entity>> LeaderEntity::geometry() const
{
    std::vector<std::unique_ptr<Entity>> out;
    for (std::size_t i = 1; i < points.size(); ++i)
        out.push_back(std::make_unique<LineEntity>(points[i - 1], points[i]));
    const Vec2 join = junction();
    for (Vec2 tip : extraTips)
        out.push_back(std::make_unique<LineEntity>(tip, join));
    const auto arrow = [&](Vec2 tip, Vec2 toward) {
        const Vec2 d = toward - tip;
        if (arrowSize <= 0.0 || d.length() <= 0.0)
            return;
        const Vec2 u = d / d.length();
        const Vec2 n{-u.y, u.x};
        const Vec2 base = tip + u * arrowSize;
        out.push_back(std::make_unique<SolidEntity>(std::array<Vec2, 4>{
            tip, base + n * (arrowSize / 6.0), base - n * (arrowSize / 6.0), base - n * (arrowSize / 6.0)}));
    };
    if (points.size() >= 2)
        arrow(points[0], points[1]);
    for (Vec2 tip : extraTips)
        arrow(tip, join);
    if (auto m = annotation())
        out.push_back(std::move(m));
    for (auto& e : out)
        e->copyStyleFrom(*this);
    return out;
}

void LeaderEntity::decompose(std::vector<Prim>& out) const
{
    for (std::size_t i = 1; i < points.size(); ++i)
        out.push_back(Prim::segment(points[i - 1], points[i]));
    const Vec2 join = junction();
    for (Vec2 tip : extraTips)
        out.push_back(Prim::segment(tip, join));
}

std::vector<SnapPoint> LeaderEntity::snapPoints() const
{
    std::vector<SnapPoint> result;
    for (Vec2 p : points)
        result.push_back({p, SnapKind::Endpoint});
    for (Vec2 p : extraTips)   // dopo i punti della direttrice principale (le maniglie contano su quest'ordine)
        result.push_back({p, SnapKind::Endpoint});
    return result;
}

void LeaderEntity::transform(const Affine& t)
{
    for (Vec2& p : points)
        p = t.apply(p);
    for (Vec2& p : extraTips)
        p = t.apply(p);
    const double s = std::sqrt(std::abs(t.determinant()));
    textHeight *= s;
    arrowSize *= s;
}

double LeaderEntity::distanceTo(Vec2 p) const
{
    double d = Entity::distanceTo(p);
    if (auto m = annotation())
        d = std::min(d, m->distanceTo(p));
    return d;
}

bool LeaderEntity::touchesRect(const BBox& rect) const
{
    if (Entity::touchesRect(rect))
        return true;
    const auto m = annotation();
    return m && m->touchesRect(rect);
}

BBox LeaderEntity::bounds() const
{
    BBox b;
    for (Vec2 p : points)
        b.expand(p);
    if (auto m = annotation())
        b.expand(m->bounds());
    return b;
}

} // namespace cartesio

namespace cartesio {

// ------------------------------------------------------------ motivi di tratteggio

namespace {
struct PatDef {
    const char* name;
    // angolo (gradi), origine x y, passo lungo la linea e perpendicolare, tratti (in mm)
    std::vector<std::vector<double>> lines;
};

// Motivi di CartesioCAD: definiti qui in millimetri tondi su un modulo di 3 mm (4,24264 = 3·√2, lo scarto fra
// due linee a 45° distanti 3 mm). I nomi sono quelli d'uso comune per i simboli di sezione, così i disegni che li
// citano per nome li ritrovano; i disegni letti da file portano comunque la loro definizione.
const std::vector<PatDef>& patternLibrary()
{
    static const std::vector<PatDef> library = {
        {"ANSI31", {{45, 0, 0, 0, 3}}},
        {"ANSI32", {{45, 0, 0, 0, 9}, {45, 4.24264, 0, 0, 9}}},
        {"ANSI33", {{45, 0, 0, 0, 6}, {45, 4.24264, 0, 0, 6, 3, -1.5}}},
        {"ANSI34", {{45, 0, 0, 0, 18}, {45, 4.24264, 0, 0, 18}, {45, 8.48528, 0, 0, 18}, {45, 12.72792, 0, 0, 18}}},
        {"ANSI35", {{45, 0, 0, 0, 6}, {45, 4.24264, 0, 0, 6, 8, -1.5, 0, -1.5}}},
        {"ANSI36", {{45, 0, 0, 5.5, 3, 8, -1.5, 0, -1.5}}},
        {"ANSI37", {{45, 0, 0, 0, 3}, {135, 0, 0, 0, 3}}},
        {"ANSI38", {{45, 0, 0, 0, 3}, {135, 0, 0, 6, 3, 8, -4}}},
        {"LINE", {{0, 0, 0, 0, 3}}},
        {"NET", {{0, 0, 0, 0, 3}, {90, 0, 0, 0, 3}}},
        {"NET3", {{0, 0, 0, 0, 3}, {60, 0, 0, 0, 3}, {120, 0, 0, 0, 3}}},
        {"DASH", {{0, 0, 0, 3, 3, 3, -3}}},
        {"DOTS", {{0, 0, 0, 0.75, 1.5, 0, -1.5}}},
        {"SQUARE", {{0, 0, 0, 0, 3, 3, -3}, {90, 0, 0, 0, 3, 3, -3}}},
        // Mattoni: corsi alti 6, giunti verticali sfalsati di 6 a corsi alterni.
        {"BRICK", {{0, 0, 0, 0, 6}, {90, 0, 0, 0, 12, 6, -6}, {90, 6, 6, 0, 12, 6, -6}}},
        // Crocette da 2 mm su una maglia di 6.
        {"CROSS", {{0, 0, 0, 0, 6, 2, -4}, {90, 1, -1, 0, 6, 2, -4}}},
        // Terreno: gruppi di tre tratti orizzontali e tre verticali che si alternano.
        {"EARTH", {{0, 0, 0, 6, 6, 6, -6}, {0, 0, 2, 6, 6, 6, -6}, {0, 0, 4, 6, 6, 6, -6},
                   {90, 1, 5, 6, 6, 6, -6}, {90, 3, 5, 6, 6, 6, -6}, {90, 5, 5, 6, 6, 6, -6}}},
    };
    return library;
}
} // namespace

const std::vector<std::string>& standardHatchPatternNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n{"SOLID"};
        for (const PatDef& p : patternLibrary())
            n.push_back(p.name);
        return n;
    }();
    return names;
}

bool HatchEntity::setPattern(const std::string& name, double scale, double angle)
{
    if (scale <= 0.0)
        return false;
    const auto& names = standardHatchPatternNames();
    const bool standard = std::find(names.begin(), names.end(), name) != names.end();
    if (name == "SOLID") {
        solid = true;
        pattern = "SOLID";
        patternLines.clear();
    } else if (standard) {
        solid = false;
        pattern = name;
        patternType = 1;
        patternLines = standardHatchPattern(name, scale, angle);
    } else if (name == pattern && !solid) {
        // Motivo del file: le sue linee seguono il cambio di scala e di angolo.
        const double ratio = scale / (patternScale > 0.0 ? patternScale : 1.0);
        const double turn = angle - patternAngle;
        const double c = std::cos(turn), s = std::sin(turn);
        const auto rotate = [c, s, ratio](Vec2 v) { return Vec2{(v.x * c - v.y * s) * ratio, (v.x * s + v.y * c) * ratio}; };
        for (HatchPatternLine& line : patternLines) {
            line.angle += turn;
            line.base = rotate(line.base);
            line.offset = rotate(line.offset);
            for (double& d : line.dashes)
                d *= ratio;
        }
    } else {
        return false;
    }
    gradient.clear();
    patternScale = scale;
    patternAngle = angle;
    return true;
}

std::vector<HatchPatternLine> standardHatchPattern(const std::string& name, double scale, double angle)
{
    std::vector<HatchPatternLine> result;
    if (scale <= 0.0)
        scale = 1.0;
    for (const PatDef& p : patternLibrary()) {
        if (name != p.name)
            continue;
        for (const auto& d : p.lines) {
            HatchPatternLine line;
            const double a = degToRad(d[0]);
            line.angle = a + angle;
            const Vec2 base{d[1], d[2]};
            line.base = Vec2::polar(base.length() * scale, angleOf(base) + angle);
            // Il passo è nel sistema della linea (x lungo la linea, y perpendicolare).
            const Vec2 local{d[3], d[4]};
            line.offset = Vec2::polar(local.length() * scale, angleOf(local) + line.angle);
            for (std::size_t i = 5; i < d.size(); ++i)
                line.dashes.push_back(d[i] * scale);
            result.push_back(std::move(line));
        }
    }
    return result;
}

} // namespace cartesio

namespace cartesio {

// ---------------------------------------------------------------- XLINE / RAY

XLineEntity::XLineEntity(Vec2 b, Vec2 dir, bool isRay) : base(b), ray(isRay)
{
    const double len = dir.length();
    direction = len > 0.0 ? dir / len : Vec2{1.0, 0.0};
}

void XLineEntity::decompose(std::vector<Prim>& out) const
{
    out.push_back(Prim::segment(ray ? base : base - direction * kReach, base + direction * kReach));
}

void XLineEntity::transform(const Affine& t)
{
    base = t.apply(base);
    const Vec2 d = t.applyVector(direction);
    if (d.length() > 0.0)
        direction = d / d.length();
}

} // namespace cartesio

namespace cartesio {

// ----------------------------------------------------------------- WIPEOUT

void WipeoutEntity::decompose(std::vector<Prim>& out) const
{
    if (outline.size() >= 2)
        appendPolyline(out, outline, true);
}

std::vector<SnapPoint> WipeoutEntity::snapPoints() const
{
    std::vector<SnapPoint> points;
    for (Vec2 p : outline)
        points.push_back({p, SnapKind::Endpoint});
    return points;
}

void WipeoutEntity::transform(const Affine& t)
{
    for (Vec2& p : outline)
        p = t.apply(p);
}

bool WipeoutEntity::contains(Vec2 p) const
{
    bool inside = false;
    for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++) {
        const Vec2 a = outline[i];
        const Vec2 b = outline[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

double WipeoutEntity::distanceTo(Vec2 p) const
{
    return Entity::distanceTo(p);   // si seleziona sul bordo
}

// -------------------------------------------------------------------- IMAGE

std::array<Vec2, 4> ImageEntity::corners() const
{
    const Vec2 du = u * width, dv = v * height;
    return {insertion, insertion + du, insertion + du + dv, insertion + dv};
}

Affine ImageEntity::pixelToWorld() const
{
    Affine a;
    a.m11 = u.x;
    a.m12 = u.y;
    a.m21 = -v.x;
    a.m22 = -v.y;
    a.dx = insertion.x + v.x * height;
    a.dy = insertion.y + v.y * height;
    return a;
}

void ImageEntity::decompose(std::vector<Prim>& out) const
{
    const auto c = corners();
    appendPolyline(out, {c[0], c[1], c[2], c[3]}, true);
}

std::vector<SnapPoint> ImageEntity::snapPoints() const
{
    std::vector<SnapPoint> points;
    for (Vec2 p : corners())
        points.push_back({p, SnapKind::Endpoint});
    return points;
}

void ImageEntity::transform(const Affine& t)
{
    insertion = t.apply(insertion);
    u = t.applyVector(u);
    v = t.applyVector(v);
}

bool ImageEntity::contains(Vec2 p) const
{
    const auto c = corners();
    bool inside = false;
    for (std::size_t i = 0, j = 3; i < 4; j = i++) {
        const Vec2 a = c[i], b = c[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

double ImageEntity::distanceTo(Vec2 p) const
{
    // Si seleziona anche cliccando dentro l'immagine.
    return contains(p) ? 0.0 : Entity::distanceTo(p);
}

// -------------------------------------------------------------------- MLINE

Vec2 MlineEntity::segmentDirection(std::size_t i) const
{
    const std::size_t n = vertices.size();
    const Vec2 d = vertices[(i + 1) % n] - vertices[i];
    const double len = d.length();
    return len > 0.0 ? d / len : Vec2{1.0, 0.0};
}

Vec2 MlineEntity::miter(std::size_t i, double* along) const
{
    const std::size_t n = vertices.size();
    const bool hasIn = i > 0 || closed;
    const bool hasOut = i + 1 < n || closed;
    const Vec2 dOut = hasOut ? segmentDirection(i) : segmentDirection((i + n - 1) % n);
    const Vec2 dIn = hasIn ? segmentDirection((i + n - 1) % n) : dOut;
    const Vec2 nOut{-dOut.y, dOut.x};
    const Vec2 nIn{-dIn.y, dIn.x};
    Vec2 m = nIn + nOut;
    const double len = m.length();
    m = len > 1e-9 ? m / len : nOut;
    const double cosine = m.dot(nOut);
    if (along)
        *along = std::abs(cosine) > 1e-6 ? 1.0 / cosine : 1.0;
    return m;
}

std::vector<std::vector<Vec2>> MlineEntity::elementLines() const
{
    std::vector<std::vector<Vec2>> lines(offsets.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        double k = 1.0;
        const Vec2 m = miter(i, &k);
        for (std::size_t e = 0; e < offsets.size(); ++e)
            lines[e].push_back(vertices[i] + m * (offsets[e] * k));
    }
    return lines;
}

void MlineEntity::decompose(std::vector<Prim>& out) const
{
    if (vertices.size() < 2)
        return;
    const auto lines = elementLines();
    for (const auto& l : lines)
        appendPolyline(out, l, closed);
    if (!closed && lines.size() >= 2) {   // chiusura delle estremità fra il primo e l'ultimo elemento
        out.push_back(Prim::segment(lines.front().front(), lines.back().front()));
        out.push_back(Prim::segment(lines.front().back(), lines.back().back()));
    }
}

std::vector<SnapPoint> MlineEntity::snapPoints() const
{
    std::vector<SnapPoint> points;
    for (const auto& l : elementLines())
        for (Vec2 p : l)
            points.push_back({p, SnapKind::Endpoint});
    return points;
}

void MlineEntity::transform(const Affine& t)
{
    for (Vec2& v : vertices)
        v = t.apply(v);
    const double s = scaleOf(t);
    for (double& o : offsets)
        o *= t.determinant() < 0.0 ? -s : s;   // specchiando, la sinistra diventa destra
    scale *= s;
}

// -------------------------------------------------------------------- TABLE

void TableEntity::resize(int r, int c, double colWidth, double rowHeight)
{
    const std::vector<std::string> old = cells;
    const int oldCols = columns;
    const int oldRows = rows;
    rows = std::max(1, r);
    columns = std::max(1, c);
    columnWidths.resize(static_cast<std::size_t>(columns), colWidth);
    rowHeights.resize(static_cast<std::size_t>(rows), rowHeight);
    cells.assign(static_cast<std::size_t>(rows * columns), std::string());
    for (int i = 0; i < std::min(rows, oldRows); ++i)
        for (int j = 0; j < std::min(columns, oldCols); ++j)
            cell(i, j) = old[static_cast<std::size_t>(i * oldCols + j)];
}

namespace {
// Dal sistema della tabella (origine nell'angolo in alto a sinistra, x a destra, y in su) al disegno.
Affine tableAffine(const TableEntity& t)
{
    return Affine::translation(t.position) * Affine::rotation({0.0, 0.0}, t.rotation);
}
} // namespace

std::unique_ptr<MTextEntity> TableEntity::cellText(int row, int col) const
{
    if (row < 0 || col < 0 || row >= rows || col >= columns)
        return nullptr;
    const Affine a = tableAffine(*this);
    double x = 0.0, y = 0.0;
    for (int j = 0; j < col; ++j)
        x += columnWidths[static_cast<std::size_t>(j)];
    for (int i = 0; i < row; ++i)
        y += rowHeights[static_cast<std::size_t>(i)];
    const double w = columnWidths[static_cast<std::size_t>(col)];
    const double h = rowHeights[static_cast<std::size_t>(row)];
    auto m = std::make_unique<MTextEntity>(a.apply({x + w / 2.0, -y - h / 2.0}), textHeight, cell(row, col));
    m->attachment = 5;   // centro-centro
    m->rotation = rotation;
    m->copyStyleFrom(*this);
    return m;
}

std::vector<std::unique_ptr<Entity>> TableEntity::geometry() const
{
    std::vector<std::unique_ptr<Entity>> out;
    const Affine a = tableAffine(*this);
    double width = 0.0, height = 0.0;
    for (double w : columnWidths)
        width += w;
    for (double h : rowHeights)
        height += h;
    double y = 0.0;
    for (int i = 0; i <= rows; ++i) {
        out.push_back(std::make_unique<LineEntity>(a.apply({0.0, -y}), a.apply({width, -y})));
        if (i < rows)
            y += rowHeights[static_cast<std::size_t>(i)];
    }
    double x = 0.0;
    for (int j = 0; j <= columns; ++j) {
        out.push_back(std::make_unique<LineEntity>(a.apply({x, 0.0}), a.apply({x, -height})));
        if (j < columns)
            x += columnWidths[static_cast<std::size_t>(j)];
    }
    // Testi centrati nelle celle.
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < columns; ++j)
            if (!cell(i, j).empty())
                out.push_back(cellText(i, j));
    for (auto& e : out)
        e->copyStyleFrom(*this);
    return out;
}

void TableEntity::decompose(std::vector<Prim>& out) const
{
    for (const auto& e : geometry())
        if (e->type() == EntityType::Line)
            e->decompose(out);
}

std::vector<SnapPoint> TableEntity::snapPoints() const
{
    double width = 0.0, height = 0.0;
    for (double w : columnWidths)
        width += w;
    for (double h : rowHeights)
        height += h;
    const Affine a = tableAffine(*this);
    return {{position, SnapKind::Insertion}, {a.apply({width, 0.0}), SnapKind::Endpoint},
            {a.apply({width, -height}), SnapKind::Endpoint}, {a.apply({0.0, -height}), SnapKind::Endpoint}};
}

BBox TableEntity::bounds() const
{
    BBox b;
    for (const SnapPoint& s : snapPoints())
        b.expand(s.position);
    return b;
}

std::pair<int, int> TableEntity::cellAt(Vec2 p) const
{
    const Affine inv = Affine::rotation({0.0, 0.0}, -rotation) * Affine::translation(Vec2{} - position);
    const Vec2 q = inv.apply(p);
    double y = 0.0;
    for (int i = 0; i < rows; ++i) {
        const double h = rowHeights[static_cast<std::size_t>(i)];
        if (-q.y >= y && -q.y <= y + h) {
            double x = 0.0;
            for (int j = 0; j < columns; ++j) {
                const double w = columnWidths[static_cast<std::size_t>(j)];
                if (q.x >= x && q.x <= x + w)
                    return {i, j};
                x += w;
            }
        }
        y += h;
    }
    return {-1, -1};
}

void TableEntity::transform(const Affine& t)
{
    position = t.apply(position);
    rotation += rotationOf(t);
    const double s = scaleOf(t);
    for (double& w : columnWidths)
        w *= s;
    for (double& h : rowHeights)
        h *= s;
    textHeight *= s;
}

} // namespace cartesio

namespace cartesio {

const std::vector<BBox>& BlockDef::childBounds() const
{
    if (boundsCache_.size() != entities.size()) {
        boundsCache_.clear();
        boundsCache_.reserve(entities.size());
        for (const auto& e : entities)
            boundsCache_.push_back(e->bounds());
    }
    return boundsCache_;
}

} // namespace cartesio
