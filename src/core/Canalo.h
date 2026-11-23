#pragma once

// Calcoli di CanaloCAD (canaline portacavi) dentro CartesioCAD: catalogo dei cavi, staffaggi, disposizione dei
// cavi per gravità o come disegnati, riempimento, coefficienti di riduzione della portata (CEI UNEL 35024/1),
// segregazione (CEI 64-8 art. 528.1), carico e freccia dello staffaggio (CEI EN 61537), tiro, raggi di curvatura e
// distinta dei materiali. È la traduzione fedele dei calcoli della pagina CanaloCAD (src/js/01-data.js e
// 02-core.js): stessi numeri, stessi arrotondamenti. La lettura di una sezione dagli oggetti del disegno segue
// l'importazione DXF della pagina (04-import-cad.js).

#include "core/Vec2.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cartesio {
class Entity;
}

namespace cartesio::canalo {

// Modello di cavo del catalogo.
struct CableType {
    std::string id, cat, code, desc, type, form;
    double d = 10.0;      // diametro esterno, mm
    double w = 0.1;       // peso, kg/m
    double sec = 2.5;     // sezione del conduttore, mm²
    int cores = 2;        // conduttori attivi (1 unipolare, 2 monofase, 3 trifase)
    double iz0 = 20.0;    // portata di riferimento, A
    double bendK = 10.0;  // raggio minimo di curvatura = bendK × diametro
    std::string sys;      // BT_POWER, BT_FIRE, SPECIAL_ELV
    std::string standard;
};
const std::vector<CableType>& catalog();
const CableType* catalogItem(const std::string& id);

// Serie di canaline di un costruttore.
struct Series {
    std::string id, name, code, type;   // type: perforated, solid, mesh
};
const std::vector<std::pair<std::string, std::vector<Series>>>& brands();
const std::vector<Series>& seriesOf(const std::string& brand);
// Serie della distinta per la tipologia di canalina (come BOM_SERIES_FOR della pagina).
const Series& seriesForType(const std::string& trayType);
// Serie per codice; un codice non più elencato (progetti delle versioni precedenti) diventa la passerella asolata.
const Series& seriesById(const std::string& id);

// Staffaggio: carichi ammessi (kg) per passo 1, 1,5, 2, 2,5 e 3 m.
struct Support {
    std::string key, name, desc;
    double swl[5] = {};
    double armH = 0.0;
    bool strut = false;
    std::string type;
};
const std::vector<Support>& supports();
const Support* support(const std::string& key);

// Cavo nella sezione. Coordinate in mm dall'angolo in basso a sinistra della canalina.
struct Cable {
    std::string code, desc, type, form, sys = "BT_POWER";
    double d = 10.0, w = 0.1, sec = 2.5;
    int cores = 2;
    double iz0 = 20.0, ib = 0.0, bendK = 10.0;
    bool trefoil = false;
    std::string trefoilGroup;   // i tre unipolari di una terna a trifoglio
    double x = 0.0, y = 0.0, origX = 0.0, origY = 0.0;
};
Cable cableFrom(const CableType& t, std::string code = {});

// Canalina e dati del tratto (i valori iniziali sono quelli della pagina).
struct Tray {
    double width = 200.0, height = 75.0, thk = 1.5, lengthM = 30.0;
    int bends90 = 0, tees = 0, reducers = 0;
    std::string seismic = "none";       // none, cable_9m, strut_6m
    std::string trayType = "perforated";// perforated, solid, mesh, ladder
    std::string packing = "gravity";    // gravity, input (come disegnati)
    std::string cover = "snap";         // none, snap, clamped
    int septums = 0;
    std::vector<double> septumPositions;
    std::string earth = "gv16";         // none o sigla del conduttore di terra
    int reiCount = 0;                    // attraversamenti EI120
    std::string support = "none";
    double spanM = 1.5;
    std::string brand = "TIPOLOGIE", seriesId = "PAS_ASO";
    bool pdfLoaded = false;              // correnti d'impiego note (schema unifilare)
    std::vector<Cable> cables;
};

struct BomRow {
    std::string code, desc;
    double qty = 0.0;
    std::string um, note;
};

struct Metrics {
    double trayArea = 0.0, usedArea = 0.0;
    double fillPct = 0.0, sparePct = 100.0;
    double kr = 1.0, kFascio = 1.0, kStrati = 1.0, kPosa = 1.0;
    int nLayers = 1;
    bool segConform = true;
    std::string segDetail;
    double totalKgM = 0.0, trayKgM = 0.0, qMaxKgM = 0.0, pointLoad = 0.0, maxPoint = 0.0, loadPct = 0.0;
    double deflectionMm = 0.0, maxDeflectionMm = 0.0;
    double pullDaN = 0.0, maxPullDaN = 0.0;
    double maxBendR = 0.0, fitR = 150.0;
    std::string critCode = "-";
    double jouleWm = 0.0, maxDv = 0.0;
    std::string deratingFailCode;   // vuoto se tutti i cavi reggono la corrente d'impiego
    int seismicKits = 0, reiCushions = 0;
    std::vector<BomRow> bom;
};

// Posizioni dei setti (mm da sinistra), sistemate come nella pagina.
std::vector<double> septumX(Tray& tray);
// Dispone i cavi (per gravità o vicino alla posizione disegnata) e calcola tutto.
Metrics recalculate(Tray& tray);

double allowedLinearLoadKgM(const Tray& tray, const std::string& supportKey, double spanM);
double elasticDeflectionMm(const Tray& tray, double qTotKgM, const std::string& supportKey, double spanM);

// Dati del cavo dedotti dal diametro e dalla sigla (cavi importati da un disegno).
struct Inferred {
    std::string type, form;
    double sec = 2.5;
    int cores = 2;
    double iz0 = 20.0, bendK = 10.0;
    std::string sys = "BT_POWER";
};
Inferred inferByDiameter(double d, const std::string& code = {}, const std::string& layer = {});
bool isUnipolar(const std::string& form, const std::string& type, int cores);
double sectionFromForm(const std::string& form, double fallback = 2.5);

// Sezione disegnata: cerchi (cavi), segmenti (bordo della canalina e bandierine) e testi (sigle, misure).
struct Drawn {
    struct Circle { Vec2 center; double radius; std::string layer; };
    struct Segment { Vec2 a, b; std::string layer; };
    struct Text { std::string text; Vec2 position; std::string layer; };
    std::vector<Circle> circles;
    std::vector<Segment> segments;
    std::vector<Text> texts;
    std::string fileName;
};
// Canalina e cavi letti dalla sezione disegnata, come l'importazione DXF della pagina (senza le tabelle cavi
// del disegno e lo schema unifilare). `origin` riceve l'angolo della canalina nel disegno.
Tray readSection(const Drawn& drawn, Vec2* origin = nullptr);

// Numeri formattati come JavaScript (toFixed e String(numero)), per descrizioni identiche alla pagina.
double fixed(double v, int decimals);
std::string jsNumber(double v);

} // namespace cartesio::canalo
