#pragma once

// Parole di un PDF con le loro coordinate, lette con Poppler (poppler-cpp, GPL-2.0-or-later).
// Senza Qt, così lo usano sia l'applicazione sia lo strumento cartesio_pdfcircuits.

#include "core/SingleLine.h"

#include <string>
#include <vector>

namespace cartesio {

// True se CartesioCAD è stato compilato con Poppler.
bool pdfSupport();
// Legge le parole di tutte le pagine dal contenuto del file (si evita il percorso: niente problemi
// con le cartelle con lettere accentate).
bool readPdfWords(const std::string& fileBytes, std::vector<PdfWord>& words, std::string* error);

} // namespace cartesio
