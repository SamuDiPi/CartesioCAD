#pragma once

// Impianto: il database di un progetto elettrico condiviso dai disegni (file .cdb accanto alle piante).
//  - circuiti: sigla, descrizione, cavo e formazione, sistema (energia, sicurezza, segnale), corrente d'impiego,
//    lunghezza... importati dallo schema unifilare in PDF o scritti a mano;
//  - cavi propri: tipi non presenti nel catalogo (diametro, peso, sezione);
//  - tratte: tubi, canali e passerelle disegnati come linee o polilinee del percorso, con i circuiti che
//    contengono.
// Le verifiche delle tratte: riempimento (tubi: diametro interno almeno 1,3 volte quello del fascio di cavi;
// canali: cavi al massimo metà della sezione; passerelle: i calcoli di CanaloCAD), segregazione dei sistemi e,
// per le passerelle, carico e freccia dello staffaggio. La lettura e la scrittura del file sono dell'applicazione.

#include "core/Canalo.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cartesio {
class Entity;
class TableEntity;
}

namespace cartesio::plant {

// Sistema del circuito: energia (BT), sicurezza (resistenti al fuoco) o segnale (bassissima tensione, dati).
enum class System { Power, Safety, Signal };
const char* systemName(System s);
System systemFromName(const std::string& name);

struct Circuit {
    std::string code;          // "QPT-SX-N C-14"
    std::string description;
    std::string cableType;     // "FG16OM16"
    std::string formation;     // "3G2,5", "4(1x95)", "3(1x95)+1G50"
    System system = System::Power;
    double ib = 0.0;           // corrente d'impiego, A (0 = non nota)
    double lengthM = 0.0;      // lunghezza dallo schema
    std::string breaker;
    double diameter = 0.0;     // diametro imposto, mm (0 = dal catalogo)
    std::string notes;
    bool fromPdf = false;      // importato dallo schema (si aggiorna a ogni importazione)
    double breakerIn = 0.0;    // corrente nominale dell'interruttore In, A (0 = non nota)
    double izRef = 0.0;        // portata di riferimento imposta, A (0 = dal catalogo, posa in aria libera)
};

enum class RunKind { Tube, Duct, Tray };
const char* runKindName(RunKind k);

struct Run {
    std::string id;            // "T1", "T2"... legato alle linee del disegno
    std::string name;
    RunKind kind = RunKind::Tube;
    // Tubo: diametro esterno nominale e interno (0 = dalla tabella del tipo).
    std::string tubeType = "rigido";   // rigido, corrugato
    double tubeOuter = 25.0;
    double tubeInner = 0.0;
    // Canale e passerella: larghezza e altezza utili, setti, coperchio, staffaggio.
    double width = 100.0, height = 60.0;
    int septums = 0;
    std::string cover = "snap";
    std::string support = "none";
    double spanM = 1.5;
    std::string brand = "TIPOLOGIE", seriesId = "PAS_ASO";
    std::vector<std::string> circuits;   // sigle
    std::string notes;
    // Posa per la portata dei cavi: tubi "vista" (su parete o soffitto) o "incassato" (in parete isolante);
    // temperatura ambiente.
    std::string laying = "vista";
    double ambientC = 30.0;
};

struct Project {
    std::string name;
    std::vector<Circuit> circuits;
    std::vector<canalo::CableType> cables;   // cavi propri (si aggiungono al catalogo)
    std::vector<Run> runs;

    Circuit* circuit(const std::string& code);
    const Circuit* circuit(const std::string& code) const;
    Run* run(const std::string& id);
    const Run* run(const std::string& id) const;
    // Prossimo codice libero per una tratta ("T1", "T2"...).
    std::string nextRunId() const;
};

// Tubi: diametri interni indicativi dei tubi in PVC (serie pesante) per diametro esterno nominale.
struct TubeSize { double outer, inner; };
const std::vector<TubeSize>& tubeSizes(const std::string& tubeType);
double tubeInnerDiameter(const Run& run);

// Una parte della formazione: `count` cavi con `cores` anime da `section` mm² ("4(1x95)" = 4 cavi 1×95);
// `pe` per i conduttori di protezione ("(1PE16)").
struct FormationPart { int count = 1; int cores = 1; double section = 0.0; bool pe = false; };
std::vector<FormationPart> parseFormation(const std::string& formation);
// Formazioni riconosciute: "3G2,5", "4(1x95)", "3x(1x120)+70", "2(1x1,5)+(1PE1,5)" (PE), "3(1x185)+(1x95)+(1PE95)"
// (neutro tra parentesi), "3(3x1x240)+(2x240)" (3 terne in parallelo; tra parentesi senza numero davanti "2x240"
// sono 2 unipolari).
// Tipo e formazione da una descrizione libera del cavo ("FG16OM16 3G2,5", "Cavo / GENERAL - FG16OM16 - 1(3G2,5)").
void splitCableText(const std::string& text, std::string& type, std::string& formation);

// Cavi fisici di un circuito, con diametro e peso dal catalogo (più i cavi propri del progetto). `note`
// riceve come sono stati trovati quando non c'è una corrispondenza esatta ("stimato dalla sezione"...).
std::vector<canalo::Cable> cablesOf(const Project& project, const Circuit& circuit, std::string* note = nullptr);

// Diametro del cerchio più piccolo che contiene cerchi dei diametri dati (fascio di cavi in un tubo).
double bundleDiameter(std::vector<double> diameters);
// Centri dei cerchi del fascio, rispetto al centro del cerchio che li contiene, nell'ordine dei diametri dati;
// `bundle` riceve il diametro del fascio.
std::vector<Vec2> bundleLayout(const std::vector<double>& diameters, double* bundle = nullptr);

// Esito di una verifica.
enum class Level { Ok, Warning, Error };
struct Finding {
    Level level = Level::Ok;
    std::string text;
};

struct RunCheck {
    Level level = Level::Ok;          // il peggiore dei risultati
    std::vector<Finding> findings;
    int cableCount = 0;
    double cablesArea = 0.0;          // mm²
    double fillPct = 0.0;             // area dei cavi sulla sezione utile
    double bundle = 0.0;              // tubi: diametro del fascio
    double tubeInner = 0.0;
    std::vector<std::string> missing; // sigle senza circuito nel progetto
    canalo::Metrics tray;             // passerelle e canali: calcoli di CanaloCAD
};
RunCheck checkRun(const Project& project, const Run& run, double lengthM = 0.0);

// Fasi del circuito dalla formazione: 3 (4G, 5G, tre o più unipolari) oppure 1.
int phasesOf(const Circuit& circuit);
// Coefficiente di riduzione per più circuiti raggruppati in tubo o canale (CEI-UNEL 35024/1, IEC 60364-5-52
// tab. B.52.17): 1 circuito 1,00; 2 0,80; 3 0,70; 4 0,65; 5 0,60; 6 0,57; 7 0,54; 8 0,52; 9 0,50; 12 0,45; 16 0,41;
// 20 e oltre 0,38.
double groupingFactor(int circuits);

// Portata e caduta di tensione di un circuito, con le tratte che lo contengono (vale la più sfavorevole).
struct CircuitCheck {
    Level level = Level::Ok;
    std::vector<Finding> findings;
    double iz0 = 0.0;        // portata di tabella di un cavo per il metodo di posa (30 °C), o quella imposta
    std::string method;      // metodo di posa (A1, B2, E...) della tratta più sfavorevole
    bool fromTable = false;  // portata dalle tabelle (altrimenti imposta o dal catalogo)
    int parallel = 1;        // cavi in parallelo per fase
    double kGroup = 1.0;     // raggruppamento
    double kTemp = 1.0;      // temperatura ambiente
    double iz = 0.0;         // portata ridotta Iz'
    double lengthM = 0.0;    // lunghezza usata (schema, altrimenti tratte disegnate)
    bool measured = false;   // lunghezza misurata sul disegno
    double dvPct = 0.0;      // caduta di tensione, %
    std::vector<std::string> runs;   // tratte che contengono il circuito
};
// `runLengthsM`: lunghezza di ogni tratta nel disegno corrente (per i circuiti senza lunghezza nello schema).
CircuitCheck checkCircuit(const Project& project, const Circuit& circuit, const std::map<std::string, double>& runLengthsM = {});

// Sezione della tratta disegnata: tubo (con il fascio di cavi appoggiato sul fondo), canale o passerella (con i
// cavi posati come in CanaloCAD), titolo con l'esito sopra e legenda dei circuiti sotto. `origin` è il centro del
// tubo o lo spigolo in basso a sinistra del canale; `scale` le unità di disegno per millimetro reale. Gli oggetti
// hanno il layer `layer` e il codice `tag` (per sostituirli quando la sezione si ridisegna).
struct SectionStyle {
    Vec2 origin;
    double scale = 1.0;
    double textHeight = 2.5;
    int layer = -1;
    std::string tag;
    std::string scaleText;   // "1:2", scritto nel titolo
};
std::vector<std::unique_ptr<Entity>> drawRunSection(const Project& project, const Run& run, const SectionStyle& style);
// Etichetta della tratta in pianta: "T1 tubo Ø32 - 3 circuiti - 48% - NON VERIFICATA".
std::string runLabel(const Run& run, const RunCheck& check);

// Distinta materiali dell'impianto, dalle tratte disegnate: tubi, canali e passerelle con coperchi, setti, curve,
// derivazioni, giunzioni e staffe; metri di cavo per tipo e formazione (lunghezza dello schema, altrimenti delle
// tratte). Quantità senza sfrido.
struct RunGeometry {
    double lengthM = 0.0;
    int bends = 0;   // cambi di direzione del percorso
    int tees = 0;    // derivazioni (punti dove si incontrano tre o più tratti)
};
struct BomItem {
    std::string category, description;
    double qty = 0.0;
    std::string unit, note;
};
std::vector<BomItem> plantBom(const Project& project, const std::map<std::string, RunGeometry>& geometry);
// CSV per Excel in italiano: separatore ";", virgola decimale, con l'intestazione.
std::string bomCsv(const std::vector<BomItem>& items);
// Tabella della distinta (intestazione e una riga per voce) con lo spigolo in alto a sinistra in `topLeft`.
std::unique_ptr<TableEntity> bomTable(const std::vector<BomItem>& items, Vec2 topLeft, double textHeight);

} // namespace cartesio::plant
