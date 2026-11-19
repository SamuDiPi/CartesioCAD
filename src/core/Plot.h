#pragma once

// Stampa: impostazioni di stampa (AcDbPlotSettings), scale standard, formati
// carta, calcolo della posizione del disegno sul foglio, tabelle stili di stampa (CTB) e timbro.
// Nessuna dipendenza da Qt: il disegno vero lo fa l'applicazione.

#include "core/Vec2.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cartesio {

// --- Impostazioni di stampa (codici di gruppo DXF di AcDbPlotSettings tra parentesi)

// Cosa stampare (74).
enum class PlotType { Display = 0, Extents = 1, Limits = 2, View = 3, Window = 4, Layout = 5 };

// Bit del codice 70.
enum PlotFlag : int {
    kPlotViewportBorders = 1,
    kPlotShowPlotStyles = 2,
    kPlotCentered = 4,
    kPlotHidden = 8,              // nascondi oggetti dello spazio carta
    kPlotUseStandardScale = 16,
    kPlotPlotStyles = 32,         // stampa con gli stili di stampa
    kPlotScaleLineweights = 64,
    kPlotPrintLineweights = 128,
    kPlotDrawViewportsFirst = 512,   // stampa lo spazio carta per ultimo
    kPlotModelType = 1024,
};

// Scala standard (75): tipo, nome, unità carta e unità disegno.
struct StandardScale {
    int type;
    const char* name;
    double paper;
    double drawing;
};
// Tipo 0 = "Adatta al foglio"; 1...15 scale in pollici e piedi; 16...32 scale metriche.
const std::vector<StandardScale>& standardScales();
const StandardScale* findStandardScale(int type);
// Tipo della scala standard pari a paper:drawing (in mm), oppure -1.
int standardScaleTypeFor(double paper, double drawing);

struct PlotSettings {
    std::string pageSetupName;              // 1
    std::string device = "DWG To PDF.pc3";  // 2
    std::string mediaName;                  // 4 nome canonico del formato
    std::string viewName;                   // 6
    std::string styleSheet;                 // 7 tabella stili (es. "Nero.ctb")
    // Margini non stampabili (mm), nell'orientamento del supporto: sinistro, inferiore, destro, superiore.
    double marginLeft = 0.0, marginBottom = 0.0, marginRight = 0.0, marginTop = 0.0;   // 40...43
    // Dimensioni del supporto (mm) prima della rotazione (44, 45).
    double paperWidth = 420.0, paperHeight = 297.0;
    Vec2 plotOrigin;                        // 46, 47: spostamento dall'angolo dell'area stampabile (mm)
    Vec2 windowMin, windowMax;              // 48, 49 e 140, 141: finestra di stampa
    double customPaper = 1.0;               // 142: unità carta della scala personalizzata
    double customDrawing = 1.0;             // 143: unità disegno
    int flags = kPlotPlotStyles | kPlotPrintLineweights | kPlotDrawViewportsFirst;   // 70
    int paperUnits = 1;                     // 72: 0 pollici, 1 mm, 2 pixel
    int rotation = 0;                       // 73: 0, 1 = 90°, 2 = 180°, 3 = 270°
    PlotType plotType = PlotType::Layout;   // 74
    int standardScaleType = 16;             // 75: 1:1
    int shadeMode = 0;                      // 76: 0 come visualizzato, 1 wireframe, 2 nascosto, 3 render
    int shadeResolution = 2;                // 77: 0 bozza, 1 anteprima, 2 normale, 3 presentazione, 4 massima, 5 personalizzata
    int shadeDpi = 300;                     // 78
    double standardScaleFactor = 1.0;       // 147
    Vec2 paperImageOrigin;                  // 148, 149

    bool flag(int f) const { return (flags & f) != 0; }
    void setFlag(int f, bool on) { flags = on ? (flags | f) : (flags & ~f); }

    // Impostazioni predefinite per lo spazio modello: estensioni adattate al foglio, centrate.
    static PlotSettings forModel();
    // Impostazioni predefinite per un layout: il foglio a 1:1.
    static PlotSettings forLayout(double sheetWidth, double sheetHeight, double margin);
};

// Foglio come esce dalla stampante (mm), dopo la rotazione.
struct SheetSize {
    double width = 0.0;
    double height = 0.0;
};
SheetSize sheetSize(const PlotSettings& s);
// Margini nell'orientamento del foglio stampato: sinistro, inferiore, destro, superiore.
std::array<double, 4> sheetMargins(const PlotSettings& s);
// Area stampabile sul foglio (mm, origine nell'angolo in basso a sinistra del foglio).
BBox printableArea(const PlotSettings& s);
// Imposta il supporto in modo che il foglio stampato sia sheetWidth × sheetHeight
// (orientamento orizzontale o verticale), mantenendo la capovolta.
void setSheet(PlotSettings& s, double sheetWidth, double sheetHeight);
bool isLandscape(const PlotSettings& s);
bool isUpsideDown(const PlotSettings& s);
void setOrientation(PlotSettings& s, bool landscape, bool upsideDown);

// Millimetri sul foglio per unità di disegno; 0 se la scala è "Adatta al foglio".
double fixedPlotScale(const PlotSettings& s);
bool isFitToPaper(const PlotSettings& s);
// Testo della scala: "Adatta", "1:100", "2:1", oppure "1 mm = 12.5 unità".
std::string plotScaleText(const PlotSettings& s);

// Dove va il disegno sul foglio: paper = (a*x + c*y + e, b*x + d*y + f), in mm dall'angolo
// in basso a sinistra del foglio, y verso l'alto.
struct PlotFrame {
    bool valid = false;
    SheetSize sheet;
    BBox printable;       // area stampabile sul foglio
    BBox placed;          // dove finisce l'area stampata sul foglio
    double scale = 0.0;   // mm per unità di disegno (quella effettiva, anche con "Adatta")
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;

    Vec2 toPaper(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
};
// `area`: la parte di disegno da stampare (per un layout, il foglio nello spazio carta).
// Con PlotType::Layout l'angolo in basso a sinistra dell'area va nell'angolo del foglio.
PlotFrame computePlotFrame(const PlotSettings& s, const BBox& area);

// --- Formati carta

struct MediaFormat {
    const char* name;        // "ISO A3", "ANSI B", "ARCH D"
    double width;            // mm, orizzontale
    double height;
    bool inches;             // formato definito in pollici
};
const std::vector<MediaFormat>& plotMediaFormats();
// Nome canonico: "ISO_full_bleed_A3_(420.00_x_297.00_MM)".
std::string canonicalMediaName(const MediaFormat& m);
// Formato con queste misure (in entrambi gli orientamenti, tolleranza 0,5 mm), oppure nullptr.
const MediaFormat* findMediaBySize(double width, double height);
// Formato da nome leggibile o canonico ("A3", "ISO A3", "ISO_full_bleed_A3_(...)"), oppure nullptr.
const MediaFormat* findMediaByName(std::string_view name);
// Nome leggibile per un supporto: il formato noto o "420 x 297 mm".
std::string mediaDisplayName(double width, double height);

// --- Tabelle stili di stampa dipendenti dal colore (.ctb)

constexpr int kPlotStyleObjectColor = -1;
constexpr int kPlotStyleObjectLinetype = 31;
constexpr int kPlotStyleObjectLineweight = 0;
constexpr int kPlotStyleObjectFill = 73;
constexpr int kPlotStyleObjectEnd = 4;
constexpr int kPlotStyleObjectJoin = 5;

struct PlotStyle {
    std::string name;
    std::string localizedName;
    std::string description;
    int color = kPlotStyleObjectColor;   // -1 o 0xC2RRGGBB come intero con segno
    int modeColor = kPlotStyleObjectColor;
    int colorPolicy = 1;                 // 1 retinatura, 2 scala di grigi, 4 colore con nome
    int physicalPen = 0;                 // 0 = automatica
    int virtualPen = 0;
    int screen = 100;                    // intensità 0...100
    double linepatternSize = 0.5;
    int linetype = kPlotStyleObjectLinetype;
    bool adaptiveLinetype = true;
    int lineweight = kPlotStyleObjectLineweight;   // indice nella tabella degli spessori, 0 = dell'oggetto
    int fillStyle = kPlotStyleObjectFill;
    int endStyle = kPlotStyleObjectEnd;
    int joinStyle = kPlotStyleObjectJoin;

    bool hasObjectColor() const;
    // Colore come 0xRRGGBB, valido se !hasObjectColor().
    std::uint32_t rgb() const;
    void setRgb(std::uint32_t rgb);
    void setObjectColor();
    bool dithering() const { return (colorPolicy & 1) != 0; }
    bool grayscale() const { return (colorPolicy & 2) != 0; }
    void setDithering(bool on) { colorPolicy = on ? (colorPolicy | 1) : (colorPolicy & ~1); }
    void setGrayscale(bool on) { colorPolicy = on ? (colorPolicy | 2) : (colorPolicy & ~2); }
};

const std::vector<double>& defaultPlotLineweights();

struct PlotStyleTable {
    std::string description;
    double scaleFactor = 1.0;
    bool applyFactor = false;
    int customLineweightDisplayUnits = 0;   // 0 mm, 1 pollici
    std::array<PlotStyle, 255> styles;      // styles[i] è il colore ACI i+1
    std::vector<double> lineweights = defaultPlotLineweights();

    PlotStyleTable();
    // Stile del colore ACI (1...255; fuori intervallo: quello del 7).
    const PlotStyle& forAci(int aci) const;
    PlotStyle& forAci(int aci);
    // Spessore in mm dello stile; 0 = quello dell'oggetto.
    double lineweightMm(const PlotStyle& s) const;
    // Indice nella tabella dello spessore più vicino a mm (> 0).
    int lineweightIndex(double mm) const;

    // Testo (decompresso) di un file .ctb.
    std::string toText() const;
    static std::optional<PlotStyleTable> fromText(std::string_view text, std::string* error = nullptr);
};

// Tabelle fornite con il programma, come quelle.
const std::vector<std::string>& builtinPlotStyleTableNames();
std::optional<PlotStyleTable> builtinPlotStyleTable(std::string_view name);

// File .ctb: intestazione di 60 byte, poi i dati zlib (la compressione la fa chi chiama).
std::uint32_t adler32(std::string_view data);
std::string ctbFileBytes(std::string_view zlibData, std::size_t textLength);
// Dati zlib e lunghezza del testo dentro un file .ctb, oppure nullopt se non è un .ctb.
struct CtbPayload {
    std::string_view zlibData;
    std::size_t textLength = 0;
};
std::optional<CtbPayload> ctbPayload(std::string_view fileBytes, std::string* error = nullptr);

// --- Timbro di stampa

struct PlotStampSettings {
    bool drawingName = true;
    bool layoutName = true;
    bool dateTime = true;
    bool loginName = false;
    bool deviceName = true;
    bool paperSize = false;
    bool plotScale = true;
    std::string user1;
    std::string user2;
    int corner = 0;            // 0 in basso a sinistra, 1 in alto a sinistra, 2 in alto a destra, 3 in basso a destra
    double textHeight = 2.0;   // mm
    double offsetX = 5.0;      // mm dal bordo del foglio
    double offsetY = 5.0;
};

struct PlotStampValues {
    std::string drawingName;
    std::string layoutName;
    std::string dateTime;
    std::string loginName;
    std::string deviceName;
    std::string paperSize;
    std::string plotScale;
};
// Le voci scelte, separate da virgole.
std::string plotStampText(const PlotStampSettings& settings, const PlotStampValues& values);

} // namespace cartesio
