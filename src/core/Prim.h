#pragma once

#include "core/Vec2.h"

#include <vector>

namespace cartesio {

// Primitiva geometrica elementare: segmento o arco di cerchio (antiorario).
// Ogni entità si scompone in primitive: su di esse si calcolano distanze, intersezioni,
// ingombri e selezione a finestra, in modo uniforme per tutti i tipi di oggetto.
struct Prim {
    enum class Kind { Segment, Arc };

    Kind kind = Kind::Segment;
    Vec2 a;                 // punto iniziale
    Vec2 b;                 // punto finale
    Vec2 center;            // solo archi
    double radius = 0.0;    // solo archi
    double start = 0.0;     // angolo iniziale (radianti), solo archi
    double sweep = 0.0;     // ampiezza antioraria > 0 (2π = cerchio completo), solo archi

    static Prim segment(Vec2 a, Vec2 b);
    static Prim arc(Vec2 center, double radius, double start, double sweep);
    // Tratto di polilinea con curvatura (bulge = tan(angolo/4), > 0 antiorario).
    static Prim bulge(Vec2 a, Vec2 b, double bulge);

    bool containsAngle(double angle) const;
    Vec2 pointAt(double angle) const { return center + Vec2::polar(radius, angle); }
};

// Trasformazione affine 2D: x' = m11*x + m21*y + dx,  y' = m12*x + m22*y + dy.
struct Affine {
    double m11 = 1.0, m12 = 0.0, m21 = 0.0, m22 = 1.0, dx = 0.0, dy = 0.0;

    static Affine translation(Vec2 t);
    static Affine rotation(Vec2 center, double angle);
    static Affine scaling(Vec2 center, double factor);
    // Specchiatura rispetto alla retta per a e b.
    static Affine mirror(Vec2 a, Vec2 b);
    // Inserimento di un blocco: punto base -> scala -> rotazione -> posizione.
    static Affine insert(Vec2 base, Vec2 position, double scaleX, double scaleY, double rotation);

    Vec2 apply(Vec2 p) const { return {m11 * p.x + m21 * p.y + dx, m12 * p.x + m22 * p.y + dy}; }
    Vec2 applyVector(Vec2 v) const { return {m11 * v.x + m21 * v.y, m12 * v.x + m22 * v.y}; }
    // this ∘ other: prima `other`, poi `this`.
    Affine operator*(const Affine& other) const;
    double determinant() const { return m11 * m22 - m12 * m21; }
    // Scala uniforme (eventualmente con specchiatura): gli archi restano archi.
    bool isSimilarity() const;
    double uniformScale() const;
};

void transformPrims(std::vector<Prim>& prims, const Affine& t);

double primDistance(const Prim& p, Vec2 point);
void primBounds(const Prim& p, BBox& box);
std::vector<Vec2> primIntersections(const Prim& p, const Prim& q);
bool primTouchesRect(const Prim& p, const BBox& rect);

// Approssima una curva con una spezzata (per ellissi, spline, archi deformati).
void appendPolyline(std::vector<Prim>& out, const std::vector<Vec2>& points, bool closed = false);

} // namespace cartesio
