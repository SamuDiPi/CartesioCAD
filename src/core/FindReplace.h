#pragma once

// FIND: ricerca e sostituzione del testo in testi, testi multilinea, attributi dei blocchi, quote (testo
// sostituito) e direttrici.

#include "core/Document.h"

#include <string>
#include <vector>

namespace cartesio {

struct FindOptions {
    bool matchCase = false;
    bool wholeWord = false;
    bool texts = true;          // TEXT e MTEXT
    bool attributes = true;     // valori degli attributi dei blocchi
    bool dimensions = true;     // testo delle quote (se sostituito) e delle direttrici
    int space = -1;             // -1 = tutto il disegno, altrimenti solo quello spazio (0 modello, id del layout)
    std::vector<EntityId> only; // se non vuoto: solo questi oggetti (selezione)
};

struct FindMatch {
    EntityId id = 0;
    int attribute = -1;         // indice dell'attributo, -1 = il testo dell'oggetto
    int space = 0;
    std::string kind;           // "Testo", "Testo multilinea", "Attributo TAG", "Quota", "Direttrice"
    std::string text;           // il testo trovato (intero)
    BBox bounds;
};

std::vector<FindMatch> findText(const Document& doc, const std::string& query, const FindOptions& options);

// Sostituisce `query` con `replacement` nei risultati indicati (tutte le occorrenze nel testo di ciascuno), in un
// solo passo annullabile. Restituisce quante sostituzioni.
int replaceText(Document& doc, const std::vector<FindMatch>& matches, const std::string& query,
                const std::string& replacement, const FindOptions& options);

// Ricerca in un testo (per la logica di confronto, anche nei test).
std::vector<std::size_t> findInString(const std::string& text, const std::string& query, const FindOptions& options);

} // namespace cartesio
