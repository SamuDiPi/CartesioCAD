#pragma once

// Attributi dei blocchi (cartigli, simboli con sigla): sincronizzazione con le definizioni (ATTSYNC), modifica dei
// valori (EATTEDIT) e delle definizioni (BATTMAN).

#include "core/Document.h"

#include <string>
#include <vector>

namespace cartesio {

// Definizioni di attributo (ATTDEF) di un blocco, nell'ordine del blocco.
std::vector<const AttDefEntity*> attributeDefinitions(const BlockDef& block);

// ATTSYNC di un inserimento: gli attributi seguono le definizioni del blocco (ordine, posizione, altezza, stile,
// layer); i valori si conservano per etichetta, i nuovi prendono il valore predefinito, quelli senza definizione
// si tolgono. Le costanti non diventano attributi (fanno parte del blocco).
void syncAttributes(InsertEntity& insert);

// ATTSYNC di tutti gli inserimenti di un blocco (nel disegno), in un passo annullabile. Restituisce quanti.
int syncBlockAttributes(Document& doc, const std::string& blockName);

// Valori degli attributi di un inserimento: etichetta -> nuovo valore (un passo annullabile).
bool setAttributeValues(Document& doc, EntityId insert, const std::vector<std::pair<std::string, std::string>>& values);

} // namespace cartesio
