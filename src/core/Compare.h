#pragma once

// Confronto di due disegni (revisioni): gli oggetti che ci sono solo in uno o solo nell'altro e le zone in cui
// stanno le differenze. Due oggetti sono uguali se hanno stesso tipo, layer, colore, tipo di linea e geometria
// (a meno di una tolleranza), stesso testo o stesso blocco.

#include "core/Document.h"

#include <memory>
#include <string>
#include <vector>

namespace cartesio {

struct DrawingDiff {
    std::vector<EntityId> onlyHere;                    // nel disegno corrente e non nell'altro (aggiunti)
    std::vector<std::shared_ptr<const Entity>> onlyThere;   // nell'altro e non nel corrente (tolti): copie
    std::vector<BBox> regions;                         // zone delle differenze, dall'alto a sinistra
    int same = 0;                                      // oggetti uguali
};

// Firma di un oggetto per il confronto (tolleranza assoluta sulle coordinate).
std::string compareSignature(const Document& doc, const Entity& e, double tolerance);

// Confronta lo spazio `spaceHere` del disegno corrente con lo spazio `spaceThere` dell'altro (0 = modello).
DrawingDiff compareDrawings(const Document& here, const Document& there, int spaceHere = 0, int spaceThere = 0);

} // namespace cartesio
