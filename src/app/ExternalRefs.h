#pragma once

// Riferimenti esterni di un disegno (tavolozza RIFERIMENTI ESTERNI, comando XRIF): disegni DWG/DXF collegati
// (XREF) e immagini raster, con il percorso salvato, il file trovato e quante volte sono usati.

#include <QString>

#include <string>
#include <vector>

namespace cartesio {

class Document;

struct ExternalRef {
    enum Kind { Drawing, Image };
    Kind kind = Drawing;
    QString name;          // nome del blocco (XREF) o del file (immagine)
    std::string saved;     // percorso com'è salvato nel disegno
    QString found;         // file trovato (vuoto = non trovato)
    bool loaded = false;   // XREF: contenuto caricato; immagine: file leggibile
    bool overlay = false;  // XREF in sovrapposizione
    int uses = 0;          // inserimenti / immagini nel disegno
};

std::vector<ExternalRef> externalReferences(const Document& doc);

// Testo dello stato ("Caricato", "Non trovato"...).
QString externalRefStatus(const ExternalRef& r);

// Cambia il percorso salvato di un riferimento (tutte le immagini con quel percorso, o il blocco XREF).
void setExternalRefPath(Document& doc, const ExternalRef& r, const std::string& path);

// Tipo di percorso salvato nel disegno.
enum RefPathType { RefPathRelative = 0, RefPathFull = 1, RefPathNone = 2 };
// Percorso da salvare per `file` (già trovato) in un disegno salvato in `drawingFile`: relativo alla cartella del
// disegno (con .\ o ..\), completo, o solo il nome. Relativo senza disegno salvato o su un'altra unità: completo.
std::string refPath(const QString& file, const QString& drawingFile, int type);
// Tipo del percorso salvato.
int refPathTypeOf(const std::string& saved);
// Nomi per gli elenchi ("Relativo", "Completo", "Nessun percorso").
QString refPathTypeName(int type);
// Cambia il tipo di percorso di un riferimento trovato (false se il file non è stato trovato).
bool setExternalRefPathType(Document& doc, const ExternalRef& r, int type);
// Salvataggio in un'altra cartella: i percorsi relativi si ricalcolano per puntare agli stessi file.
// Restituisce quanti percorsi sono cambiati.
int rebaseRelativeRefs(Document& doc, const QString& oldDrawingFile, const QString& newDrawingFile);

// Stacca un riferimento: elimina il blocco XREF e i suoi inserimenti, o le immagini con quel file.
int detachExternalRef(Document& doc, const ExternalRef& r);

} // namespace cartesio
