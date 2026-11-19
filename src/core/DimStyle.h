#pragma once

#include <string>
#include <vector>

namespace cartesio {

// Stile di quota (DIMSTYLE) con le variabili principali (valori metrici predefiniti).
struct DimStyle {
    std::string name = "Standard";
    double textHeight = 2.5;     // DIMTXT
    double arrowSize = 2.5;      // DIMASZ
    double extOffset = 0.625;    // DIMEXO: distacco delle linee di estensione dall'oggetto
    double extExtension = 1.25;  // DIMEXE: prolungamento oltre la linea di quota
    double textGap = 0.625;      // DIMGAP: distanza del testo dalla linea di quota
    double scale = 1.0;          // DIMSCALE: scala di tutte le dimensioni
    double linearFactor = 1.0;   // DIMLFAC: fattore delle misure (es. 0.1 per misurare in cm)
    double tickSize = 0.0;       // DIMTSZ: > 0 = trattini obliqui al posto delle frecce
    int decimals = 2;            // DIMDEC
    std::string post;            // DIMPOST: prefisso/suffisso, "<>" = misura (es. "<> mm")
    bool annotative = false;     // misure sulla carta: DIMSCALE = scala di annotazione
    bool operator==(const DimStyle&) const = default;
};

} // namespace cartesio
