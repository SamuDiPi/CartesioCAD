#pragma once

// ESTRAIDATI: dati del disegno in una tabella CSV (per Excel, computi e distinte).
//  - Blocchi: un riga per ogni blocco inserito, con posizione, rotazione, scala e una colonna per attributo.
//  - Conteggio: i blocchi raggruppati per nome e valori degli attributi, con la quantità (distinta).
//  - Oggetti: una riga per oggetto con tipo, layer, colore, tipo di linea, lunghezza, area e testo.
// I testi che arrivano dal disegno e iniziano con = + - @ prendono un apostrofo davanti: un foglio di calcolo non
// li esegue come formule (un disegno ricevuto da altri non può far partire comandi aprendo il CSV).

#include "core/Document.h"

#include <string>
#include <vector>

namespace cartesio {

struct DataExportOptions {
    enum class Mode { Blocks, Count, Objects };
    Mode mode = Mode::Blocks;
    std::vector<EntityId> only;   // vuoto = tutti gli oggetti
    bool layouts = true;          // anche gli oggetti dei layout, oltre al modello
    // ';' = Excel in italiano (decimali con la virgola); ',' = CSV standard (decimali con il punto).
    char separator = ';';
};

struct DataExportResult {
    std::string csv;   // UTF-8, righe terminate da \r\n, senza BOM
    int rows = 0;      // righe di dati (senza l'intestazione)
};

DataExportResult exportData(const Document& doc, const DataExportOptions& options);

// Nome della modalità per i messaggi e gli script ("blocchi", "conteggio", "oggetti"); parse: falso se sconosciuta.
const char* dataExportModeName(DataExportOptions::Mode mode);
bool parseDataExportMode(const std::string& text, DataExportOptions::Mode* mode);

} // namespace cartesio
