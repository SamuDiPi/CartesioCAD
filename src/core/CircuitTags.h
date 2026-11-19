#pragma once

// Circuiti in pianta: sigle dei circuiti (testi come "QPT-SX-N C-4"), punti (blocchi degli utilizzatori,
// per esempio esportati da programmi BIM), verifica rispetto allo schema unifilare e scrittura delle sigle.

#include "core/Document.h"
#include "core/SingleLine.h"

#include <string>
#include <vector>

namespace cartesio {

struct CircuitTag {
    EntityId id = 0;
    std::string code;     // sigla normalizzata
    Vec2 position;
    double height = 0.0;
    int layer = 0;
};

struct CircuitPoint {
    EntityId id = 0;
    std::string family;   // nome del blocco fino a " - " (la famiglia del programma BIM)
    std::string letter;   // lettera di identificazione vicina (es. "D"), se c'è
    Vec2 position;        // centro della geometria (i blocchi esportati dai programmi BIM hanno l'inserimento nell'origine)
};

struct CircuitScanOptions {
    std::string pointLayers = "E-ELEC-FIXT";        // layer dei punti (prefisso; più prefissi separati da ;)
    std::string letterLayers = "E-ELEC-FIXT-IDEN";  // layer delle lettere di identificazione
    std::string tagLayers = "E-ELEC-CIRC";          // layer delle sigle (prefisso; vuoto = tutti)
    double letterRadius = 1500.0;                   // distanza massima lettera-punto (unità del disegno)
    double tagRadius = 3000.0;                      // distanza massima sigla-punto
    int space = 0;                                  // spazio (0 = modello)
};

std::vector<CircuitTag> findCircuitTags(const Document& doc, const CircuitScanOptions& options = {});
std::vector<CircuitPoint> findCircuitPoints(const Document& doc, const CircuitScanOptions& options = {});
// Punti dagli oggetti scelti (qualunque layer): i blocchi al centro del simbolo, gli altri al centro dell'ingombro.
std::vector<CircuitPoint> pointsFromObjects(const Document& doc, const std::vector<EntityId>& ids);

// Esito della verifica.
struct CircuitCheck {
    std::vector<CircuitTag> unknownTags;           // sigle in pianta che non sono nello schema
    std::vector<std::string> circuitsWithoutTags;  // circuiti dei quadri presenti in pianta, senza sigla
    std::vector<CircuitPoint> pointsWithoutTags;   // punti senza una sigla vicina
    int matchedTags = 0;
};
// I circuiti di riserva, senza linea o generali (C-0) non sono considerati mancanti.
CircuitCheck checkCircuits(const Document& doc, const std::vector<Circuit>& circuits,
                           const CircuitScanOptions& options = {});

// Sigla da scrivere accanto ai punti scelti: un testo per punto, a destra del simbolo, nello stile dato.
struct CircuitTagStyle {
    std::string layer = "E-ELEC-CIRC";
    int color = 6;          // magenta, se il layer va creato
    double height = 120.0;
    double offset = 250.0;  // distanza dal centro del punto
};
// Altezza delle sigle già presenti (la più frequente), oppure `fallback`.
double usualTagHeight(const std::vector<CircuitTag>& tags, double fallback);
// Testi da aggiungere (da passare a Document::commit, così l'operazione si annulla con un passo).
std::vector<std::unique_ptr<Entity>> makeCircuitTags(Document& doc, const std::vector<CircuitPoint>& points,
                                                     const std::string& code, const CircuitTagStyle& style);

} // namespace cartesio
