#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace cartesio {

constexpr double kPi = 3.14159265358979323846;

inline double degToRad(double deg) { return deg * kPi / 180.0; }
inline double radToDeg(double rad) { return rad * 180.0 / kPi; }

// Punto / vettore 2D in coordinate mondo (unità di disegno, asse Y verso l'alto).
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double px, double py) : x(px), y(py) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr bool operator==(const Vec2&) const = default;

    constexpr double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double length() const { return std::hypot(x, y); }

    static Vec2 polar(double length, double angleRad)
    {
        return {length * std::cos(angleRad), length * std::sin(angleRad)};
    }
};

inline double distance(Vec2 a, Vec2 b) { return (a - b).length(); }

// Prodotto vettoriale 2D: > 0 se b è a sinistra di a (senso antiorario).
constexpr double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

// Angolo del vettore rispetto all'asse X, in radianti.
inline double angleOf(Vec2 v) { return std::atan2(v.y, v.x); }

// Riporta un angolo nell'intervallo [0, 2π).
inline double normalizeAngle(double a)
{
    a = std::fmod(a, 2.0 * kPi);
    if (a < 0.0)
        a += 2.0 * kPi;
    if (a >= 2.0 * kPi)
        a = 0.0;
    return a;
}

// Rettangolo di ingombro allineato agli assi. Vuoto finché non si aggiunge un punto.
struct BBox {
    Vec2 min{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    Vec2 max{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    bool isValid() const { return min.x <= max.x && min.y <= max.y; }

    void expand(Vec2 p)
    {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
    }

    void expand(const BBox& other)
    {
        if (other.isValid()) {
            expand(other.min);
            expand(other.max);
        }
    }

    double width() const { return max.x - min.x; }
    double height() const { return max.y - min.y; }
    Vec2 center() const { return (min + max) / 2.0; }
};

} // namespace cartesio
