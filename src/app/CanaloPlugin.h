#pragma once

// Plugin CanaloCAD: scambio di file con CanaloCAD, l'app web (Canalo.html) per progettare e
// verificare le canaline portacavi.
//  - verso CanaloCAD: gli oggetti selezionati (la sezione con cavi e testi) in un DXF da importare;
//  - da CanaloCAD: le tavole esportate in DXF (layer Canalina, Cavi...) inserite come blocco, in scala
//    con le unità del disegno; la cartella dei download viene sorvegliata per le tavole nuove.
// CanaloCAD gira nel browser: non si può comandare da qui, quindi lo scambio passa dai file.

#include "core/Canalo.h"
#include "core/Entity.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace cartesio {

class Document;

// Canalo.html: opzione, copia distribuita (plugins/CanaloCAD accanto all'eseguibile), oppure
// Documenti/Canalo/Canalo.html; vuoto se non c'è.
QString canaloHtmlPath();
// Cartella di scambio (opzione, oppure Download).
QString canaloExchangeFolder();
// True se il file è una tavola DXF esportata da CanaloCAD (ha i suoi layer).
bool isCanaloDrawing(const QString& path);
// La tavola di CanaloCAD più recente nella cartella (modificata dopo `after`, se valido), vuoto se nessuna.
QString latestCanaloDrawing(const QString& folder, const QDateTime& after = {});
// Sezione disegnata per i calcoli di CanaloCAD: linee, polilinee, cerchi e testi degli oggetti, in millimetri.
canalo::Drawn drawnForCanalo(const Document& doc, const std::vector<EntityId>& ids);
// Scrive gli oggetti in un DXF 2000 da importare in CanaloCAD (con layer e unità del disegno).
bool exportForCanalo(const Document& doc, const std::vector<EntityId>& ids, const QString& path, QString* error);

} // namespace cartesio
