#pragma once

#include "core/Entity.h"

#include <optional>
#include <vector>

namespace cartesio {

// Modalità di snap ad oggetto, con gli stessi bit della variabile OSMODE.
namespace Osnap {
constexpr int Endpoint = 1;
constexpr int Midpoint = 2;
constexpr int Center = 4;
constexpr int Node = 8;
constexpr int Quadrant = 16;
constexpr int Intersection = 32;
constexpr int Insertion = 64;
constexpr int Perpendicular = 128;
constexpr int Tangent = 256;
constexpr int Nearest = 512;
constexpr int Off = 16384;   // bit che spegne gli snap mantenendo le modalità scelte
// Predefinite: tutte le modalità "precise" (Vicino escluso).
constexpr int Default = Endpoint | Midpoint | Center | Node | Quadrant | Intersection | Insertion | Perpendicular
                      | Tangent;
} // namespace Osnap

int snapBit(SnapKind kind);

// Cerca il punto di snap più vicino al cursore entro `radius`, fra gli oggetti `nearby`.
// `base` è l'ultimo punto del comando: serve a Perpendicolare e Tangente.
// Vicino vale solo se non c'è nessun altro punto di snap nel raggio.
std::optional<SnapPoint> findSnap(const std::vector<const Entity*>& nearby, Vec2 cursor, double radius, int mode,
                                  std::optional<Vec2> base = std::nullopt);

} // namespace cartesio
