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
    double cableArea = 0.0;    // mm², ingombro dei cavi della partenza (per le risalite)
    double cableSection = 0.0; // mm², sezione del conduttore più grande della partenza
    // Dal catalogo degli apparecchi (applyDeviceCatalog): blocco da inserire nel disegno, riferimento e fonte.
    std::string block, reference, source;
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
// Risalita dei cavi nei quadri a pavimento: automatica (vano laterale e, per i cavi grandi, colonna dedicata),
// nessuna, solo vani laterali, solo colonna dedicata.
enum class RiserMode { Auto, None, Side, Column };

struct BoardOptions {
    double reserve = 0.25;          // spazio di riserva (0.25 = 25 %)
    bool terminals = true;          // morsettiera nelle file in basso
    RiserMode risers = RiserMode::Auto;  // risalita dei cavi (quadri a pavimento)
    bool busbars = true;            // vano delle sbarre verticali nelle colonne (forme 2a e oltre)
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
    std::string blockFile;   // disegno del fronte (DWG/DXF) dal catalogo dell'utente; `block` = nome nel documento
    std::string blockName;   // blocco da prendere nel file (vuoto = tutto il disegno)
    std::string block;
};

// Catalogo interno: solo tipi e misure tipiche, nessun produttore.
std::vector<EnclosureType> builtinEnclosures();
// Catalogo CSV (separatore ";" o ","; prima riga di intestazione):
//   codice;tipo;larghezza;altezza;profondita;ip;moduli_per_fila;riferimento;fonte[;blocco;nome_blocco]
// tipo = parete | pavimento | cassetta. Le righe non valide si saltano e si contano in `skipped`.
std::vector<EnclosureType> parseEnclosureCatalog(const std::string& csv, int* skipped = nullptr);

// Catalogo degli apparecchi dell'utente: misure vere e blocchi del costruttore per tipo, poli e corrente.
struct DeviceType {
    std::string id;
    BoardDevice::Kind kind = BoardDevice::Kind::Modular;
    bool residual = false;            // tipo "differenziale": solo apparecchi con differenziale (moduli compresi)
    int polesMin = 0, polesMax = 99;
    double ratedMin = 0.0, ratedMax = 1e9;   // A
    double modules = 0.0;             // moduli DIN (0 = dalla larghezza del blocco, o il valore tipico)
    double width = 0.0, height = 0.0; // mm del fronte (0 = dal blocco)
    std::string blockFile, blockName, reference, source;
    std::string block;                // nome del blocco nel documento (lo assegna chi carica i file)
};
// CSV: codice;tipo;poli;in_min;in_max;moduli;larghezza;altezza;blocco;nome_blocco;riferimento;fonte
// tipo = modulare | differenziale | sezionatore | fusibile | spd | scatolato | sezionatore-scatolato | aperto;
// poli = "4", "3-4" o vuoto (tutti); in_min/in_max vuoti = qualsiasi corrente.
std::vector<DeviceType> parseDeviceCatalog(const std::string& csv, int* skipped = nullptr);
// A ogni apparecchio la voce più specifica del catalogo (intervallo di corrente più stretto): misure, blocco,
// riferimento. Restituisce gli apparecchi trovati.
int applyDeviceCatalog(std::vector<BoardDevice>& devices, const std::vector<DeviceType>& catalog);

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
    enum class Kind { Devices, Riser };  // Riser = colonna dedicata alla risalita dei cavi
    Kind kind = Kind::Devices;
    double width = 0.0;                  // mm (vano apparecchi, o l'intera colonna di risalita)
    double busbarWidth = 0.0;            // vano sbarre verticali, a sinistra (0 = nessuno)
    double cableWidth = 0.0;             // vano risalita cavi, a destra (0 = nessuno)
    double cableArea = 0.0;              // mm² di cavi che salgono nel vano o nella colonna
    std::vector<BoardSection> sections;
    EnclosureType enclosure;             // carpenteria della colonna (quadri a pavimento)
    bool enclosureFound = false;
    double total() const { return busbarWidth + width + cableWidth; }
};

struct BoardLayout {
    std::string board;
    std::vector<BoardDevice> devices;
    BoardMounting mounting = BoardMounting::Floor;
    std::vector<BoardColumn> columns;
    double width = 0.0, height = 0.0, depth = 0.0;   // ingombro richiesto (mm)
    EnclosureType enclosure;             // carpenteria scelta (a pavimento: la colonna apparecchi più frequente)
    int enclosureCount = 1;              // colonne (a pavimento) o 1
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
// Gli apparecchi e le colonne con un blocco (`block`) già presente in `doc` si disegnano con quel blocco, centrato
// nel loro posto; gli altri con il disegno generico.
// `extent` riceve l'ingombro del disegno (per affiancare il quadro successivo).
std::vector<std::unique_ptr<Entity>> drawBoardFront(Document& doc, const BoardLayout& layout, Vec2 origin,
                                                    BBox* extent = nullptr);

// Distinta CSV di uno o più quadri (separatore ";" e virgola decimale, UTF-8 senza BOM).
std::string boardBillOfMaterials(const std::vector<BoardLayout>& layouts);

// Blocchi dei costruttori usati nel disegno (apparecchi e carpenterie), per caricarli prima di drawBoardFront.
std::vector<std::string> boardBlocks(const BoardLayout& layout);

// IFC4 (STEP): un IfcElectricDistributionBoard per quadro con l'ingombro (una colonna = un parallelepipedo) e le
// proprietà (IP, forma, carpenteria, corrente, moduli). I quadri si affiancano lungo X a 1 m l'uno dall'altro.
std::string boardsToIfc(const std::vector<BoardLayout>& layouts, const std::string& projectName);

} // namespace cartesio
