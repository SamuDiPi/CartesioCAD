#pragma once

// File di progetto dell'impianto (.cdb): JSON leggibile con circuiti, cavi propri e tratte (core/Plant.h).
// Si salva a ogni modifica: lo condividono tutti i disegni dell'impianto.

#include "core/Plant.h"
#include "core/SingleLine.h"

#include <QString>

namespace cartesio {

bool loadPlant(const QString& path, plant::Project& project, QString* error);
bool savePlant(const QString& path, const plant::Project& project, QString* error);

// Circuiti letti dallo schema unifilare in PDF, uniti a quelli del progetto: i nuovi si aggiungono, quelli già
// presenti aggiornano i dati dello schema (descrizione, cavo, corrente, lunghezza, interruttore) e conservano
// quelli scritti a mano (sistema, diametro, note). Restituisce quanti sono nuovi e quanti aggiornati.
std::pair<int, int> mergeSchema(plant::Project& project, const std::vector<Circuit>& schema);

} // namespace cartesio
