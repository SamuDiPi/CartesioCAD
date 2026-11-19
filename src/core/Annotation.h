#pragma once

#include "core/Entity.h"

#include <string>
#include <vector>

namespace cartesio {

// Scala di annotazione (lista delle scale del disegno): "1:50" = 1 unità di carta per 50 unità di disegno.
struct AnnoScale {
    std::string name;
    double paperUnits = 1.0;
    double drawingUnits = 1.0;
    // Unità di disegno per unità di carta: un testo alto 2,5 sulla carta è alto 2,5 * factor() nel modello.
    double factor() const { return paperUnits > 0.0 ? drawingUnits / paperUnits : 1.0; }
    bool operator==(const AnnoScale&) const = default;
};

// Lista predefinita (come nei disegni nuovi): 1:1 ... 1:1000 e gli ingrandimenti.
std::vector<AnnoScale> defaultAnnoScales();

// Testi, testi multilinea, quote e direttrici possono essere annotativi.
bool canBeAnnotative(EntityType type);

// Ridimensiona la parte "di carta" dell'oggetto (altezze, frecce, distanze) di `ratio` attorno al suo punto di
// inserimento; la geometria misurata (punti quotati, punta della freccia) non cambia.
void rescaleAnnotation(Entity& e, double ratio);

// Copia dell'oggetto annotativo com'è a un'altra scala (fattore unità di disegno per unità di carta).
std::unique_ptr<Entity> annotationAt(const Entity& e, double factor);

// Stesso fattore di scala (con tolleranza relativa).
bool sameFactor(double a, double b);

} // namespace cartesio
