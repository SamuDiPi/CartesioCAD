#pragma once

#include "core/Document.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace cartesio {

enum class DxfVersion {
    R12,     // AC1009: il più compatibile; ellissi, spline e MTEXT approssimati, tratteggi esclusi
    R2000,   // AC1015: conserva tutti gli oggetti; è anche il formato usato per creare i DWG
    // DXF 2000 intermedio per LibreDWG (dxf2dwg): uguale al 2000, ma con i codici che legge LibreDWG
    // (layer congelati nelle finestre: 341 invece del 331 della documentazione).
    R2000ForDwg,
};

// Scrive il disegno in formato DXF. Avvisi (es. oggetti non rappresentabili) in `warnings`, se fornito.
bool writeDxf(const Document& doc, const std::filesystem::path& path, std::string* error = nullptr,
              std::vector<std::string>* warnings = nullptr, DxfVersion version = DxfVersion::R12);

struct DxfReadResult {
    bool ok = false;
    std::string error;
    int imported = 0;                       // entità importate
    std::set<std::string> skippedTypes;     // tipi di entità non ancora supportati
    std::vector<std::string> notes;         // avvisi per l'utente (es. oggetti recuperati da una conversione)
};

// Legge un file DXF in `doc` (che dovrebbe essere un documento nuovo).
// Importa layer, blocchi e i principali oggetti 2D (linee, archi, polilinee, testi, quote, tratteggi...); gli altri tipi vengono elencati in skippedTypes.
DxfReadResult readDxf(const std::filesystem::path& path, Document& doc);

} // namespace cartesio
