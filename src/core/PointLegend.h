#pragma once

// Legenda delle lettere di identificazione dei punti (es. Tipo "D": alimentazione lama d'aria con
// sezionatore in loco) e sua interpretazione: quali circuiti dello schema unifilare corrispondono a
// ogni lettera. Le lettere cambiano da progetto a progetto, quindi la legenda si legge dal disegno
// (modello, layout o blocchi) e i carichi possono essere qualunque cosa: il confronto è fra parole
// (radici e sinonimi), e le sigle già scritte accanto ad altri punti con la stessa lettera hanno la
// precedenza sul testo della legenda.

#include "core/CircuitTags.h"
#include "core/Document.h"
#include "core/SingleLine.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace cartesio {

struct LegendEntry {
    std::string letter;        // "D"
    std::string description;   // "alimentazione lama d'aria con sezionatore in loco"
    std::string where;         // "Modello", nome del layout oppure "blocco <nome>"
};

// Una riga di legenda: Tipo "D": ..., "D" = ..., Tipo D - ..., D: ... (le forme senza "Tipo" e senza
// virgolette solo se `strict` è falso). nullopt se la riga non lo è.
std::optional<LegendEntry> parseLegendLine(std::string_view line, bool strict = true);

// Legenda ovunque si trovi: tutti gli spazi e le definizioni di blocco. `letters` = lettere usate in
// pianta: per quelle si accettano anche le forme brevi ("D: ...") e la lettera e la descrizione in due
// testi affiancati. `pointPositions` = punti in pianta: una lettera vicina a un punto non è legenda.
std::vector<LegendEntry> readPointLegend(const Document& doc, const std::set<std::string>& letters = {},
                                         const std::vector<Vec2>& pointPositions = {}, double letterRadius = 1500.0);

// Parole normalizzate (minuscole senza accenti, radici, sinonimi; senza parole vuote né numeri).
std::set<std::string> legendTokens(std::string_view text);
// La descrizione contiene tutte le parole chiave (confrontate per radice e sinonimi)?
bool descriptionMatches(std::string_view description, std::string_view keywords);

enum class LegendSource { None, Text, Plan, User };
const char* legendSourceName(LegendSource source);

struct LegendMatch {
    std::string letter;
    std::string legend;                  // descrizione della legenda ("" se la lettera non c'è)
    std::string keywords;                // parole che la descrizione del circuito deve contenere
    LegendSource source = LegendSource::None;
    std::vector<std::string> circuits;   // sigle dei circuiti corrispondenti
    int points = 0;                      // punti con questa lettera
    int tagged = 0;                      // ...di cui con una sigla vicina
    std::vector<std::string> outliers;   // sigle vicine a punti con questa lettera che non corrispondono
    std::string warning;
};

// Interpretazione: per ogni lettera della legenda e della pianta, le parole chiave (dell'utente, dalle
// sigle già in pianta oppure dal testo della legenda) e i circuiti corrispondenti dei quadri presenti in
// pianta (tutti, se in pianta non ci sono sigle).
std::vector<LegendMatch> interpretLegend(const Document& doc, const std::vector<Circuit>& circuits,
                                         const CircuitScanOptions& options = {},
                                         const std::map<std::string, std::string>& userKeywords = {});

} // namespace cartesio
