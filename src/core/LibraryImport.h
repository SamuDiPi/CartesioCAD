#pragma once

// Importazione di una libreria di blocchi del costruttore installata sul PC dell'utente (comando IMPORTALIBRERIA).
// CartesioCAD non contiene né distribuisce nulla delle librerie: lavora solo sulla copia già installata e scrive il
// risultato nella cartella personale dell'utente, per suo uso. Qui la parte senza interfaccia:
//  - indice della libreria dai suoi menu testuali (.mns: categorie, voce, file);
//  - pulizia di un disegno: via loghi (blocchi con "logo" nel nome), attributi, quote, punti, layer Defpoints e i
//    testi che non sono scritte tecniche generiche (elenco di parole ammesse: nessun elenco di marche nel codice);
//    blocchi annidati con nomi univoci; scala alle unità in mm;
//  - blocchi riconosciuti per impronta SHA-256 del file (non per nome): per questi una voce del catalogo apparecchi
//    o carpenterie con la descrizione generica;
//  - unione nei cataloghi dell'utente: si sostituiscono solo le righe della famiglia importata.
// Le licenze delle librerie restano dell'utente da rispettare: con quelle che vietano modifiche non si usa.

#include "core/Document.h"

#include <map>
#include <string>
#include <vector>

namespace cartesio {

// Voce di menu: categorie (dalla più esterna), testo della voce, file indicato (relativo, senza estensione).
struct LibraryMenuEntry {
    std::vector<std::string> path;
    std::string label;
    std::string file;
};
// Menu .mns (testo in Windows-1252, già convertito in UTF-8 da chi chiama): voci con un inserimento di file.
std::vector<LibraryMenuEntry> parseLibraryMenu(const std::string& text);

// Testo tenuto nel blocco pulito: solo numeri, misure e parole tecniche generiche (ON, OFF, TRIP, Ue, Ir, ...).
bool libraryTextAllowed(const std::string& text);

struct LibraryCleanReport {
    int removed = 0;                                  // oggetti tolti
    std::vector<std::pair<std::string, std::string>> log;   // (cosa, testo) per il registro
    std::map<std::string, int> words;                 // parole dei testi tolti (per le parole della famiglia)
};
// Pulisce `doc` (letto da un file della libreria): vedi sopra. `uid` rende univoci i nomi dei blocchi annidati;
// `scale` porta le misure in mm (10 = disegni in centimetri).
void cleanLibraryDrawing(Document& doc, const std::string& uid, double scale, LibraryCleanReport& report);

// Blocco riconosciuto dall'impronta del file d'origine.
struct KnownLibraryBlock {
    const char* sha256;
    const char* code;        // codice della voce ("SC-250-4P")
    const char* kind;        // tipo del catalogo apparecchi, oppure "struttura" (carpenteria a colonna)
    int polesMin, polesMax;
    double ratedMin, ratedMax;   // A (per "struttura": larghezza e altezza in mm)
    double modules;
    bool plate;              // il blocco comprende la piastra di colonna
    const char* text;        // descrizione generica
};
// nullptr se l'impronta non è fra quelle note.
const KnownLibraryBlock* knownLibraryBlock(const std::string& sha256);
// Unità dei disegni delle librerie riconosciute: fattore per i mm (10 = centimetri).
double knownLibraryScale();

// File importato: percorso relativo alla cartella del catalogo, blocco riconosciuto (o nullptr).
struct ImportedLibraryBlock {
    std::string path;
    const KnownLibraryBlock* known = nullptr;
};
// Righe del catalogo apparecchi (famiglia `family`, con la riga "famiglia" e le sue parole) e del catalogo
// carpenterie, per i blocchi riconosciuti.
std::string libraryDeviceRows(const std::vector<ImportedLibraryBlock>& blocks, const std::string& family,
                              const std::vector<std::string>& keywords);
std::string libraryEnclosureRows(const std::vector<ImportedLibraryBlock>& blocks, const std::string& family);
// Parole della famiglia: le più frequenti fra quelle tolte (sigle e nomi commerciali), al più `count`.
std::vector<std::string> libraryKeywords(const std::map<std::string, int>& words, std::size_t count = 25);

// Unisce `rows` (con l'intestazione `header`) nel catalogo `existing`: toglie le righe della famiglia `family`
// (colonna `familyColumn`, e la riga "famiglia" con quel codice) e aggiunge le nuove. Commenti e altre famiglie
// restano. Se `existing` è vuoto il risultato è l'intestazione con le righe nuove.
std::string mergeLibraryCatalog(const std::string& existing, const std::string& header, const std::string& rows,
                                const std::string& family, std::size_t familyColumn);

} // namespace cartesio
