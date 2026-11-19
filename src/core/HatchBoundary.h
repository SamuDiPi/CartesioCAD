#pragma once

#include "core/Entity.h"

#include <vector>

namespace cartesio {

// Contorno di un tratteggio "per punto interno" (come HATCH): gli oggetti vengono spezzati
// nelle intersezioni e si cerca la regione chiusa più piccola che contiene il punto.
// Restituisce i contorni come poligoni: il primo è quello esterno, gli altri sono le isole
// (oggetti chiusi interni, riempiti in modo alterno, stile "Normale").
// Vuoto se il punto non è racchiuso.
std::vector<std::vector<Vec2>> findHatchBoundary(const std::vector<const Entity*>& entities, Vec2 point);

// Area con segno di un poligono (positiva se antiorario).
double signedArea(const std::vector<Vec2>& polygon);
bool pointInPolygon(const std::vector<Vec2>& polygon, Vec2 p);

} // namespace cartesio
