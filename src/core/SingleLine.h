#pragma once

// Schemi unifilari dei quadri in PDF: dalle parole di un PDF, con le loro
// coordinate, ai circuiti. Ogni circuito è una colonna sotto la riga "Sigla utenza"; a sinistra le
// voci (Descrizione, Potenza, Esecuzione linea, protezione, cavo...). Nessuna dipendenza da Qt:
// la lettura del PDF la fa l'applicazione (Poppler).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cartesio {

// Una parola del PDF: pagina (da 1) e riquadro in punti, con y verso il basso.
struct PdfWord {
    int page = 0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    std::string text;
};

enum class CircuitKind { Lighting, Power, Other };

struct Circuit {
    std::string code;          // "QPT-SX-N C-14"
    std::string board;         // "QPT-SX-N"
    int number = 0;            // 14
    std::string description;   // "FM Servizio Lato SX"
    std::string power;         // potenza contemporanea, com'è scritta ("1,5 / 1,579")
    double powerKw = 0.0;
    std::string current;       // Ib / cosfi
    std::string phase;         // "Monofase L3+N / NO"
    std::string breaker;       // marca, polarità, modello, curva, corrente nominale
    std::string differential;  // I differenziale / classe
    std::string cable;         // tipologia, sigla, sezione
    std::string length;        // lunghezza [m]
    int page = 0;
    CircuitKind kind = CircuitKind::Other;
};

// Sigla di un circuito ("QPT-SX-N C-14", anche "QPT-SX-N C14" o con spazi diversi) in forma normale,
// oppure nullopt se il testo non lo è.
std::optional<std::string> normalizeCircuitCode(std::string_view text);
// Luce, FM (prese, torrette, forza motrice) o altra utenza, dalla descrizione.
CircuitKind classifyCircuit(std::string_view description);
const char* circuitKindName(CircuitKind kind);

// Circuiti di tutte le pagine con la riga "Sigla utenza". Una sigla ripetuta tiene la prima occorrenza.
std::vector<Circuit> parseSingleLineDiagram(const std::vector<PdfWord>& words);

} // namespace cartesio
