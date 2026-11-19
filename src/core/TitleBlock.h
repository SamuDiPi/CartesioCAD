#pragma once

#include "core/Document.h"

#include <memory>
#include <string>
#include <vector>

namespace cartesio {

// Formati ISO della serie A, in orizzontale (mm).
struct PaperFormat {
    const char* name;
    double width;
    double height;
};
const std::vector<PaperFormat>& isoPaperFormats();
// Nome del formato ("A3") se le misure corrispondono a un formato ISO (anche in verticale),
// altrimenti "420x297".
std::string paperFormatName(double width, double height);

// Scala di una finestra: "1:100", "2:1".
std::string scaleText(double scale);

// Cartiglio: blocco con attributi, inserito nell'angolo in basso a destra della cornice.
constexpr const char* kTitleBlockName = "CARTIGLIO";
constexpr const char* kTitleBlockLayer = "CARTIGLIO";
constexpr double kTitleBlockWidth = 180.0;
constexpr double kTitleBlockHeight = 32.0;

// Definizione del blocco (punto base nell'angolo in basso a destra).
std::shared_ptr<BlockDef> makeTitleBlockDef();

// Valori automatici di un layout: SCALA (finestra più grande), DATA (oggi), FOGLIO (formato).
std::string titleBlockScale(const Document& doc, int layoutId);
std::string todayText();

// Aggiorna SCALA, DATA e FOGLIO in tutti i cartigli (passo annullabile). Restituisce quanti.
int updateTitleBlocks(Document& doc);

} // namespace cartesio
