#pragma once

// QSELECT: selezione rapida per tipo di oggetto e proprietà (colore, layer, tipo e spessore di linea, nome del
// blocco, valore di un attributo, contenuto e altezza dei testi).

#include "core/Document.h"

#include <optional>
#include <string>
#include <vector>

namespace cartesio {

struct QuickSelectQuery {
    std::vector<EntityId> within;            // vuoto = tutto lo spazio corrente
    int space = 0;
    std::optional<EntityType> type;          // nessuno = tutti i tipi
    // "" (solo il tipo), "color", "layer", "linetype", "lineweight", "block", "text", "height", "attribute:TAG"
    std::string property;
    enum class Op { Equal, NotEqual, Greater, Less, Wildcard };
    Op op = Op::Equal;
    std::string value;
    bool exclude = false;                    // gli oggetti che NON corrispondono
};

std::vector<EntityId> quickSelect(const Document& doc, const QuickSelectQuery& query);

// Valore di una proprietà di un oggetto come testo (es. colore "DaLayer" o "1", layer "EL-FM"); nullopt se
// l'oggetto non ha quella proprietà.
std::optional<std::string> propertyValue(const Document& doc, const Entity& e, const std::string& property);

// Nome del tipo di oggetto per gli elenchi ("Linea", "Testo multilinea", "Blocco"...).
std::string entityTypeName(EntityType type);

// Confronto con * e ? (senza maiuscole/minuscole).
bool wildcardMatches(const std::string& pattern, const std::string& text);

} // namespace cartesio
