#pragma once

#include "core/Document.h"

#include <map>
#include <string>
#include <vector>

namespace cartesio {

// Confronto fra due disegni (es. prima del salvataggio e dopo averlo riletto): oggetti per tipo, spazio e layer,
// testi e valori degli attributi, layer con le loro proprietà, blocchi con il numero di oggetti, layout.
// Serve a scoprire cosa si perde in un formato (DWG con LibreDWG, DXF R12...).
struct DocDifference {
    std::string what;     // es. "Oggetti TESTO, Modello, layer Muri"
    long long before = 0;
    long long after = 0;
    std::string detail;   // es. proprietà del layer cambiate
};

struct DocComparison {
    std::vector<DocDifference> differences;
    long long entitiesBefore = 0;
    long long entitiesAfter = 0;
    bool identical() const { return differences.empty(); }
    // Riepilogo leggibile (al più `maxLines` differenze).
    std::string report(std::size_t maxLines = 40) const;
};

// Riepilogo di un disegno: si può fare subito (al salvataggio) e confrontare dopo con quello del file riletto.
struct DocSummary {
    std::map<std::string, long long> objects;    // tipo | spazio | layer
    std::map<std::string, long long> texts;      // contenuto dei testi e degli attributi
    std::map<std::string, std::string> layers;   // nome -> proprietà
    std::map<std::string, long long> blocks;     // nome -> oggetti
    std::map<std::string, long long> layouts;    // nome -> 1
    long long total = 0;
};
DocSummary summarizeDocument(const Document& doc);
DocComparison compareSummaries(const DocSummary& before, const DocSummary& after);
DocComparison compareDocuments(const Document& before, const Document& after);

} // namespace cartesio
