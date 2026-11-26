#pragma once

// Fronte quadro: dagli apparecchi di uno schema unifilare (SingleLine) all'ingombro del quadro, alla carpenteria,
// al disegno del fronte, alla distinta e al file IFC per il BIM.
//
// Gli apparecchi si riconoscono solo da indizi generici dello schema (esecuzione modulare/scatolato/aperto, poli e
// corrente nominale, curva o tipo di fusibile, classe dell'SPD, presenza del differenziale), mai dal nome del
// prodotto. Gli ingombri sono valori tipici (modulo DIN da 18 mm, taglie degli scatolati e degli aperti per
// corrente): servono a dimensionare il quadro e vanno confermati con il catalogo della carpenteria scelta.
// Le carpenterie vengono da un catalogo: quello interno ha solo tipi neutri; un catalogo CSV dell'utente aggiunge
// per ogni tipo il riferimento commerciale da mostrare nel disegno.

#include "core/Document.h"
#include "core/SingleLine.h"

#include <string>
#include <vector>

namespace cartesio {

struct BoardDevice {
    enum class Kind { Modular, Fuse, Spd, Switch, MouldedCase, AirCircuit, Meter, Supply };
    Kind kind = Kind::Modular;
    std::string circuit;       // "QGBT-P C-12"
    std::string description;
    int poles = 0;
    double rated = 0.0;        // A
    double modules = 0.0;      // moduli DIN (apparecchio e accessori modulari)
    double width = 0.0;        // mm, solo scatolati e aperti
    double height = 0.0;       // mm, solo scatolati e aperti
    bool residual = false;     // con differenziale
    std::string note;          // accessori riconosciuti ("differenziale", "relè differenziale"...)
};

const char* boardDeviceKindName(BoardDevice::Kind kind);

// Morsetti di una partenza (conduttori fino a 35 mm²; oltre, il cavo va direttamente all'apparecchio).
struct BoardTerminals {
    int count = 0;
    double width = 0.0;   // mm sulla guida
};

struct BoardInput {
    std::string board;
    std::vector<BoardDevice> devices;
    BoardTerminals terminals;
    double incomer = 0.0;   // corrente nominale dell'arrivo (o la più alta)
};

// Quadri dello schema (in ordine di comparsa) con i loro apparecchi. Le colonne senza apparecchio (linee sotto una
// protezione comune, collegamenti) non occupano spazio; i quadri senza apparecchi di bassa tensione riconosciuti
// (per esempio la media tensione) restano con l'elenco vuoto.
std::vector<BoardInput> boardsFromCircuits(const std::vector<Circuit>& circuits);

enum class BoardEnvironment { TechnicalRoom, Ordinary, Outdoor, Washdown };
enum class BoardMounting { Auto, Wall, Floor, Box };

struct BoardOptions {
    double reserve = 0.25;          // spazio di riserva (0.25 = 25 %)
    bool terminals = true;          // morsettiera nelle file in basso
    bool cableCompartment = false;  // vano cavi laterale da 300 mm nei quadri a pavimento
    BoardEnvironment environment = BoardEnvironment::TechnicalRoom;
    BoardMounting mounting = BoardMounting::Auto;
    int ip = 0;                     // 0 = proposto dall'ambiente
    std::string form;               // "" = proposta dalla corrente ("1", "2b", "3b", "4b")
};

int proposedIp(BoardEnvironment environment);
std::string proposedForm(double incomer);

// Carpenteria del catalogo.
struct EnclosureType {
    std::string id;          // codice neutro ("PAV-800x2000x400")
    BoardMounting mounting = BoardMounting::Floor;   // Wall, Floor o Box
    double width = 0.0, height = 0.0, depth = 0.0;   // mm
    int ip = 0;
    int modulesPerRow = 24;  // moduli DIN per fila (parete e cassette)
    std::string reference;   // riferimento commerciale (solo dal catalogo dell'utente)
    std::string source;      // da dove viene il dato
};

// Catalogo interno: solo tipi e misure tipiche, nessun produttore.
std::vector<EnclosureType> builtinEnclosures();
// Catalogo CSV (separatore ";" o ","; prima riga di intestazione):
//   codice;tipo;larghezza;altezza;profondita;ip;moduli_per_fila;riferimento;fonte
// tipo = parete | pavimento | cassetta. Le righe non valide si saltano e si contano in `skipped`.
std::vector<EnclosureType> parseEnclosureCatalog(const std::string& csv, int* skipped = nullptr);

// Una zona del fronte, dall'alto in basso dentro una colonna.
struct BoardSection {
    enum class Kind { Air, MouldedCase, Modular, Terminals, Busbar, Reserve };
    Kind kind = Kind::Modular;
    double height = 0.0;                 // mm
    std::vector<int> devices;            // indici in BoardLayout::devices (in ordine da sinistra)
    double usedModules = 0.0;            // file DIN: moduli occupati
    double capacity = 0.0;               // file DIN: moduli disponibili
};

struct BoardColumn {
    double width = 0.0;                  // mm (vano apparecchi)
    double cableWidth = 0.0;             // vano cavi laterale (0 = nessuno)
    std::vector<BoardSection> sections;
};

struct BoardLayout {
    std::string board;
    std::vector<BoardDevice> devices;
    BoardMounting mounting = BoardMounting::Floor;
    std::vector<BoardColumn> columns;
    double width = 0.0, height = 0.0, depth = 0.0;   // ingombro richiesto (mm)
    EnclosureType enclosure;             // carpenteria scelta (per i quadri a pavimento: una colonna tipo)
    int enclosureCount = 1;              // colonne o cassette uguali
    bool enclosureFound = false;
    int ip = 0;
    std::string form;
    double incomer = 0.0;
    double modules = 0.0;                // moduli DIN occupati
    double modulesInstalled = 0.0;       // moduli DIN disponibili nelle file previste
    int terminals = 0;
    std::vector<std::string> warnings;
};

BoardLayout layoutBoard(const BoardInput& input, const BoardOptions& options, const std::vector<EnclosureType>& catalog);

// Disegno del fronte nel documento, con l'angolo in basso a sinistra in `origin` (mm, scala 1:1): carpenteria,
// colonne, file, apparecchi con sigla e corrente, quote e dati (carpenteria, IP, forma, riserva).
// Gli oggetti si aggiungono poi con Document::commit (un solo annulla); i layer FQ-* si creano in `doc`.
// `extent` riceve l'ingombro del disegno (per affiancare il quadro successivo).
std::vector<std::unique_ptr<Entity>> drawBoardFront(Document& doc, const BoardLayout& layout, Vec2 origin,
                                                    BBox* extent = nullptr);

// Distinta CSV di uno o più quadri (separatore ";" e virgola decimale, UTF-8 senza BOM).
std::string boardBillOfMaterials(const std::vector<BoardLayout>& layouts);

// IFC4 (STEP): un IfcElectricDistributionBoard per quadro con l'ingombro (una colonna = un parallelepipedo) e le
// proprietà (IP, forma, carpenteria, corrente, moduli). I quadri si affiancano lungo X a 1 m l'uno dall'altro.
std::string boardsToIfc(const std::vector<BoardLayout>& layouts, const std::string& projectName);

} // namespace cartesio
