#pragma once

#include "core/DimStyle.h"
#include "core/Layer.h"
#include "core/Prim.h"
#include "core/Vec2.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cartesio {

using EntityId = std::uint64_t;

enum class EntityType {
    Line, Circle, Arc, Point, Polyline, Ellipse, Spline, Text, MText, Solid, Hatch, Insert, Dimension, AttDef, Viewport,
    Leader, XLine, Wipeout, Mline, Table, Image
};

// Tipi di snap ad oggetto (OSNAP).
enum class SnapKind { Endpoint, Midpoint, Center, Quadrant, Node, Insertion, Intersection, Perpendicular, Tangent, Nearest };

struct SnapPoint {
    Vec2 position;
    SnapKind kind;
};

// Classe base di tutti gli oggetti del disegno.
// Le entità copiate con clone() mantengono id, layer e colore: è ciò che permette annulla/ripeti.
class Entity {
public:
    virtual ~Entity() = default;

    virtual EntityType type() const = 0;
    virtual std::unique_ptr<Entity> clone() const = 0;
    // Scomposizione in segmenti e archi (vuota per testi e punti).
    virtual void decompose(std::vector<Prim>& out) const = 0;
    virtual std::vector<SnapPoint> snapPoints() const = 0;
    // Maniglie dell'oggetto selezionato: di solito i punti di snap; il testo multilinea ha anche le frecce della
    // larghezza (che non sono punti di snap).
    virtual std::vector<SnapPoint> gripPoints() const { return snapPoints(); }
    // Trasformazione affine (spostamento, rotazione, scala, specchiatura).
    virtual void transform(const Affine& t) = 0;

    // Le implementazioni predefinite usano la scomposizione in primitive.
    virtual BBox bounds() const;
    virtual double distanceTo(Vec2 p) const;
    virtual bool touchesRect(const BBox& rect) const;
    // Solo le primitive / i punti di snap entro `radius` da `p` (per lo snap: i blocchi grandi non si
    // scompongono interi a ogni movimento del mouse).
    virtual void primsNear(Vec2 p, double radius, std::vector<Prim>& out) const;
    virtual void snapPointsNear(Vec2 p, double radius, std::vector<SnapPoint>& out) const;

    std::vector<Prim> prims() const;
    void translate(Vec2 delta) { transform(Affine::translation(delta)); }

    EntityId id() const { return id_; }
    void setId(EntityId id) { id_ = id; }

    // -1 = non ancora assegnato: il documento userà il layer corrente.
    int layer() const { return layer_; }
    void setLayer(int layer) { layer_ = layer; }

    // Colore ACI 1..255, kColorByLayer (256) o kColorByBlock (0).
    // -1 = non ancora assegnato: il documento userà il colore corrente.
    int color() const { return color_; }
    void setColor(int color) { color_ = color; }

    // Spazio di appartenenza: 0 = modello, 1.. = layout (spazio carta). -1 = quello attivo.
    int space() const { return space_; }
    void setSpace(int space) { space_ = space; }

    // Tipo di linea: nome della tabella, "ByLayer", "ByBlock"; vuoto = non assegnato (il documento userà
    // quello corrente; altrimenti vale come ByLayer).
    const std::string& linetype() const { return linetype_; }
    void setLinetype(std::string name) { linetype_ = std::move(name); }
    // Spessore in centesimi di mm, o kLineweightByLayer/ByBlock/Default (kLineweightUnset = da assegnare).
    int lineweight() const { return lineweight_; }
    void setLineweight(int lw) { lineweight_ = lw; }
    // Scala del tipo di linea del singolo oggetto (CELTSCALE, codice 48).
    double linetypeScale() const { return ltscale_; }
    void setLinetypeScale(double s) { ltscale_ = s > 0.0 ? s : 1.0; }
    // Colore RGB (codice 420, 0xRRGGBB; -1 = nessuno): ha la precedenza sul colore ACI.
    int trueColor() const { return trueColor_; }
    void setTrueColor(int rgb) { trueColor_ = rgb; }
    // Trasparenza come nel DXF (codice 440): 0x02000000 | alfa (255 = opaco); -1 = DaLayer.
    int transparency() const { return transparency_; }
    void setTransparency(int t) { transparency_ = t; }

    // Oggetto annotativo (testi, quote, direttrici): nomi delle scale di annotazione che supporta; vuoto = non
    // annotativo. La geometria è quella della scala con fattore annoFactor() (unità di disegno per unità di carta).
    bool annotative() const { return !annoScales_.empty(); }
    const std::vector<std::string>& annoScales() const { return annoScales_; }
    void setAnnoScales(std::vector<std::string> scales) { annoScales_ = std::move(scales); }
    // Tratta dell'impianto (tubo, canale, passerella) a cui appartiene la linea del percorso: il suo codice nel
    // file di progetto, vuoto se nessuna.
    const std::string& plantRun() const { return plantRun_; }
    void setPlantRun(std::string id) { plantRun_ = std::move(id); }
    // Canalina portacavi (tavolozza Canalina) a cui appartiene l'oggetto: sezione, percorso in pianta o tavola. Il
    // codice (CN1, CN2...) è quello dello stato nel documento (Document::canaloTrays); vuoto se nessuna.
    const std::string& canaloTray() const { return canaloTray_; }
    void setCanaloTray(std::string id) { canaloTray_ = std::move(id); }
    double annoFactor() const { return annoFactor_; }
    void setAnnoFactor(double f) { annoFactor_ = f > 0.0 ? f : 1.0; }

    // Copia layer, colore, tipo e spessore di linea da un altro oggetto (pezzi creati dai comandi).
    void copyStyleFrom(const Entity& o)
    {
        layer_ = o.layer_;
        color_ = o.color_;
        trueColor_ = o.trueColor_;
        transparency_ = o.transparency_;
        linetype_ = o.linetype_;
        lineweight_ = o.lineweight_;
        ltscale_ = o.ltscale_;
    }

public:
    // Numero di serie unico dell'oggetto in memoria: cambia a ogni copia (clone). Le cache (ingombri, scena GPU)
    // lo usano per riconoscere gli oggetti cambiati senza fidarsi degli indirizzi, che possono essere riusati.
    std::uint64_t serial() const { return serial_.value; }

private:
    struct UniqueSerial {
        std::uint64_t value = next();
        UniqueSerial() = default;
        UniqueSerial(const UniqueSerial&) : value(next()) {}
        UniqueSerial& operator=(const UniqueSerial&)
        {
            value = next();
            return *this;
        }
        static std::uint64_t next();
    };
    UniqueSerial serial_;
    EntityId id_ = 0;
    int layer_ = -1;
    int color_ = -1;
    int space_ = -1;
    std::string linetype_;
    int lineweight_ = kLineweightUnset;
    int trueColor_ = -1;
    int transparency_ = -1;
    double ltscale_ = 1.0;
    std::vector<std::string> annoScales_;
    std::string plantRun_;
    std::string canaloTray_;
    double annoFactor_ = 1.0;
};

// Helper per le classi concrete.
template <typename Derived, EntityType Type>
class EntityBase : public Entity {
public:
    EntityType type() const override { return Type; }
    std::unique_ptr<Entity> clone() const override
    {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

// ------------------------------------------------------------------ geometrie

class LineEntity final : public EntityBase<LineEntity, EntityType::Line> {
public:
    LineEntity(Vec2 p1, Vec2 p2) : start(p1), end(p2) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    Vec2 start;
    Vec2 end;
};

class CircleEntity final : public EntityBase<CircleEntity, EntityType::Circle> {
public:
    CircleEntity(Vec2 c, double r) : center(c), radius(r) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    Vec2 center;
    double radius;
};

// Arco di cerchio percorso in senso antiorario da startAngle a endAngle (radianti).
class ArcEntity final : public EntityBase<ArcEntity, EntityType::Arc> {
public:
    ArcEntity(Vec2 c, double r, double start, double end)
        : center(c), radius(r), startAngle(start), endAngle(end) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    // Ampiezza in (0, 2π].
    double sweep() const;
    bool containsAngle(double angle) const;
    Vec2 pointAt(double angle) const { return center + Vec2::polar(radius, angle); }
    Vec2 startPoint() const { return pointAt(startAngle); }
    Vec2 endPoint() const { return pointAt(endAngle); }

    Vec2 center;
    double radius;
    double startAngle;
    double endAngle;
};

class PointEntity final : public EntityBase<PointEntity, EntityType::Point> {
public:
    explicit PointEntity(Vec2 p) : position(p) {}
    void decompose(std::vector<Prim>&) const override {}
    std::vector<SnapPoint> snapPoints() const override { return {{position, SnapKind::Node}}; }
    void transform(const Affine& t) override { position = t.apply(position); }
    BBox bounds() const override;
    double distanceTo(Vec2 p) const override { return distance(p, position); }
    bool touchesRect(const BBox& rect) const override;

    Vec2 position;
};

struct PolyVertex {
    Vec2 point;
    double bulge = 0.0;        // curvatura del tratto che parte da questo vertice
    double startWidth = 0.0;   // larghezza all'inizio e alla fine del tratto (0 = linea sottile)
    double endWidth = 0.0;
};

// Polilinea 2D (LWPOLYLINE): tratti rettilinei e ad arco, aperta o chiusa.
class PolylineEntity final : public EntityBase<PolylineEntity, EntityType::Polyline> {
public:
    PolylineEntity() = default;
    explicit PolylineEntity(std::vector<PolyVertex> v, bool isClosed = false)
        : vertices(std::move(v)), closed(isClosed) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    BBox bounds() const override;
    double distanceTo(Vec2 p) const override;
    bool touchesRect(const BBox& rect) const override;

    int segmentCount() const;
    double length() const;
    double area() const;      // solo per polilinee chiuse

    // Polilinea con spessore (larghezze dei vertici, come PLINE/PEDIT Larghezza).
    bool hasWidth() const;
    double maxWidth() const;
    // Aree piene dei tratti con spessore: una striscia per tratto (archi suddivisi, larghezza che varia
    // linearmente) e i raccordi a spigolo vivo tra tratti consecutivi. I tratti senza spessore non ci sono:
    // si disegnano come linee.
    std::vector<std::vector<Vec2>> widthPolygons() const;
    // Tratti senza spessore di una polilinea con spessore, come polilinee sottili (stessi layer, colore,
    // tipo di linea): si disegnano come linee normali accanto alle aree piene.
    std::vector<std::unique_ptr<PolylineEntity>> thinParts() const;
    // Stessa larghezza su tutti i tratti.
    void setWidth(double width);

    std::vector<PolyVertex> vertices;
    bool closed = false;
};

// Ellisse o arco di ellisse: punto(t) = centro + asseMaggiore·cos t + asseMinore·sin t.
class EllipseEntity final : public EntityBase<EllipseEntity, EntityType::Ellipse> {
public:
    EllipseEntity(Vec2 c, Vec2 major, double r, double t0 = 0.0, double t1 = 2.0 * kPi)
        : center(c), majorAxis(major), ratio(r), startParam(t0), endParam(t1) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    Vec2 minorAxis() const { return Vec2{-majorAxis.y, majorAxis.x} * ratio; }
    Vec2 pointAt(double t) const { return center + majorAxis * std::cos(t) + minorAxis() * std::sin(t); }
    bool isFull() const;
    double paramSweep() const;

    Vec2 center;
    Vec2 majorAxis;     // vettore dal centro all'estremo dell'asse maggiore
    double ratio;       // asse minore / asse maggiore
    double startParam;
    double endParam;
};

// Spline NURBS. Se mancano i punti di controllo si usano i punti di interpolazione.
class SplineEntity final : public EntityBase<SplineEntity, EntityType::Spline> {
public:
    SplineEntity() = default;
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    std::vector<Vec2> evaluate() const;   // spezzata che approssima la curva

    int degree = 3;
    std::vector<Vec2> control;
    std::vector<double> knots;
    std::vector<double> weights;
    std::vector<Vec2> fit;
    bool closed = false;
};

// Testo su una riga (TEXT).
class TextEntity final : public EntityBase<TextEntity, EntityType::Text> {
public:
    enum HAlign { Left = 0, Center = 1, Right = 2, Aligned = 3, Middle = 4, Fit = 5 };
    enum VAlign { Baseline = 0, Bottom = 1, VMiddle = 2, Top = 3 };

    TextEntity(Vec2 p, double h, std::string s, double rot = 0.0)
        : position(p), alignPoint(p), height(h), rotation(rot), text(std::move(s)) {}
    void decompose(std::vector<Prim>&) const override {}
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    BBox bounds() const override;
    double distanceTo(Vec2 p) const override;
    bool touchesRect(const BBox& rect) const override;

    // Testo senza codici di controllo (%%d -> °, %%c -> Ø, %%p -> ±).
    std::string displayText() const;
    // Larghezza stimata (i caratteri veri dipendono dal font usato per disegnare).
    double estimatedWidth() const;
    // Origine (sinistra, linea di base) e rotazione effettive, considerando l'allineamento.
    Vec2 origin() const;
    double effectiveRotation() const;
    // Angoli del riquadro occupato dal testo.
    std::array<Vec2, 4> box() const;

    Vec2 position;      // primo punto di allineamento (codice DXF 10)
    Vec2 alignPoint;    // secondo punto (codice 11), usato se l'allineamento non è Left/Baseline
    double height;
    double rotation;    // radianti
    double widthFactor = 1.0;
    double oblique = 0.0;        // obliquità in radianti (codice 51)
    std::string style = "Standard";   // stile di testo (codice 7)
    int hAlign = Left;
    int vAlign = Baseline;
    std::string text;
};

// Testo multilinea (MTEXT).
class MTextEntity final : public EntityBase<MTextEntity, EntityType::MText> {
public:
    MTextEntity(Vec2 p, double h, std::string s, double w = 0.0)
        : position(p), height(h), width(w), text(std::move(s)) {}
    void decompose(std::vector<Prim>&) const override {}
    std::vector<SnapPoint> snapPoints() const override { return {{position, SnapKind::Insertion}}; }
    // Inserimento, poi le frecce della larghezza a destra e a sinistra (a metà dei lati del riquadro).
    std::vector<SnapPoint> gripPoints() const override;
    void transform(const Affine& t) override;
    BBox bounds() const override;
    double distanceTo(Vec2 p) const override;
    bool touchesRect(const BBox& rect) const override;
    // Cambia la larghezza trascinando il lato destro (`right`) o sinistro fino a `to`: il lato opposto resta fermo.
    void dragWidth(bool right, Vec2 to);

    // Righe da disegnare: codici di formattazione rimossi, \P = a capo, a capo automatico su `width`.
    std::vector<std::string> lines() const;
    double lineHeight() const { return height * 1.667 * lineSpacing; }
    std::array<Vec2, 4> box() const;

    Vec2 position;
    double height;
    double width;          // 0 = nessun a capo automatico
    double rotation = 0.0;
    int attachment = 1;    // 1 alto-sx, 2 alto-centro, 3 alto-dx, 4..6 centro, 7..9 basso
    double lineSpacing = 1.0;
    std::string text;      // testo con i codici di formattazione
    std::string style = "Standard";   // stile di testo (codice 7)
    // Maschera: sotto il testo un riquadro del colore dello sfondo (codici 90, 45), più largo del testo di
    // (fillScale - 1) volte l'altezza.
    bool backgroundFill = false;
    double fillScale = 1.5;
    // Colonne: le righe si dividono in parti uguali su `columns` colonne larghe `width`, distanti `columnGutter`
    // (0 = 1,25 volte l'altezza del testo).
    int columns = 1;
    double columnGutter = 0.0;
    double gutter() const { return columnGutter > 0.0 ? columnGutter : height * 1.25; }
};

// Area piena con 3 o 4 vertici (SOLID), nell'ordine di disegno.
class SolidEntity final : public EntityBase<SolidEntity, EntityType::Solid> {
public:
    explicit SolidEntity(std::array<Vec2, 4> c) : corners(c) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    double distanceTo(Vec2 p) const override;

    std::array<Vec2, 4> corners;
};

// Tratteggio o riempimento: uno o più contorni chiusi (regola pari/dispari).
// Una famiglia di linee parallele di un motivo di tratteggio (codici DXF 53, 43/44, 45/46, 49).
struct HatchPatternLine {
    double angle = 0.0;         // radianti
    Vec2 base;
    Vec2 offset;                // passo fra una linea e la successiva
    std::vector<double> dashes; // tratti (> 0), spazi (< 0), punti (0); vuoto = linea continua
};

// Motivi di CartesioCAD per nome, già scalati e ruotati. Vuoto se il nome non è noto.
std::vector<HatchPatternLine> standardHatchPattern(const std::string& name, double scale, double angle);
const std::vector<std::string>& standardHatchPatternNames();

class HatchEntity final : public EntityBase<HatchEntity, EntityType::Hatch> {
public:
    HatchEntity() = default;
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override { return {}; }
    void transform(const Affine& t) override;
    double distanceTo(Vec2 p) const override;

    bool contains(Vec2 p) const;
    std::vector<std::vector<Vec2>> tessellatedLoops() const;
    // Cambia motivo, scala e angolo (radianti), come HATCHEDIT. I motivi standard si ricostruiscono dal nome;
    // un motivo letto dal file (non standard) tiene le sue linee, scalate e ruotate. "SOLID" = pieno.
    // Falso se il nome non è né standard né quello attuale.
    bool setPattern(const std::string& name, double scale, double angle);

    std::vector<std::vector<PolyVertex>> loops;
    bool solid = true;
    std::string pattern = "SOLID";
    double patternAngle = 0.0;
    double patternScale = 1.0;
    // Linee del motivo come nel DXF, in coordinate del disegno (scala e angolo già applicati).
    // Vuoto = motivo standard ricostruito dal nome.
    std::vector<HatchPatternLine> patternLines;
    int hatchStyle = 0;          // 75: 0 normale, 1 esterno, 2 ignora
    int patternType = 1;         // 76: 0 definito dall'utente, 1 predefinito, 2 personalizzato
    bool patternDouble = false;  // 77
    // Sfumatura (codici 450...): conservata così com'è e riscritta al salvataggio.
    std::vector<std::pair<int, std::string>> gradient;
};

// Flag degli attributi.
enum AttributeFlags { AttInvisible = 1, AttConstant = 2, AttVerify = 4, AttPreset = 8 };

// Definizione di attributo (ATTDEF): campo di testo variabile di un blocco.
// Dentro un blocco non viene disegnata: ogni inserimento mostra il proprio valore.
class AttDefEntity final : public EntityBase<AttDefEntity, EntityType::AttDef> {
public:
    AttDefEntity(std::string tagName, std::string promptText, TextEntity defaultValue)
        : tag(std::move(tagName)), prompt(std::move(promptText)), text(std::move(defaultValue)) {}
    void decompose(std::vector<Prim>&) const override {}
    std::vector<SnapPoint> snapPoints() const override { return text.snapPoints(); }
    void transform(const Affine& t) override { text.transform(t); }
    BBox bounds() const override { return text.bounds(); }
    double distanceTo(Vec2 p) const override { return text.distanceTo(p); }
    bool touchesRect(const BBox& rect) const override { return text.touchesRect(rect); }

    // Fuori dai blocchi si vede l'etichetta.
    TextEntity tagText() const
    {
        TextEntity t = text;
        t.text = tag;
        return t;
    }

    std::string tag;
    std::string prompt;
    TextEntity text;      // posizione, altezza, allineamento e valore predefinito (text.text)
    int flags = 0;
};

// Valore di un attributo su un inserimento (ATTRIB).
struct Attribute {
    std::string tag;
    TextEntity text;      // valore e posizione già nel sistema del disegno
    int flags = 0;
};

// Definizione di blocco: insieme di oggetti riutilizzabile con un punto base.
struct BlockDef {
    std::string name;
    Vec2 base;
    std::vector<std::unique_ptr<Entity>> entities;

    // Riferimento esterno (XREF): il disegno `xrefPath` ne fornisce gli oggetti, caricati all'apertura e
    // non salvati nel file (che conserva solo il percorso).
    // Blocco anonimo (*U...) di un blocco dinamico: il nome del blocco dinamico (stato salvato).
    std::string dynamicName;
    std::string xrefPath;
    bool xrefOverlay = false;   // sovrapposizione: non si propaga ai disegni che riferiscono questo
    bool xrefLoaded = false;    // file trovato e letto
    std::string xrefFound;      // file effettivamente caricato
    bool isXref() const { return !xrefPath.empty(); }

    // Ingombri degli oggetti del blocco, calcolati la prima volta che servono (ridisegno: si saltano
    // gli oggetti fuori vista o più piccoli di un pixel). Si ricalcolano se cambia il numero di oggetti.
    const std::vector<BBox>& childBounds() const;
    // Da chiamare se gli oggetti cambiano ordine (gli ingombri sono nello stesso ordine).
    void invalidateBounds() { boundsCache_.clear(); }

private:
    mutable std::vector<BBox> boundsCache_;
};

// Riferimento a un blocco (INSERT).
class InsertEntity final : public EntityBase<InsertEntity, EntityType::Insert> {
public:
    InsertEntity(std::string name, Vec2 p) : blockName(std::move(name)), position(p) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    BBox bounds() const override;
    BBox fullBounds() const;   // senza il ritaglio
    // Con gli ingombri dei figli del blocco: si scompongono solo quelli vicini (blocchi di migliaia di oggetti).
    double distanceTo(Vec2 p) const override;
    bool touchesRect(const BBox& rect) const override;
    void primsNear(Vec2 p, double radius, std::vector<Prim>& out) const override;
    void snapPointsNear(Vec2 p, double radius, std::vector<SnapPoint>& out) const override;

    Affine affine() const;
    // Nome "vero": per lo stato di un blocco dinamico (*U...) quello del blocco dinamico.
    std::string effectiveName() const { return block && !block->dynamicName.empty() ? block->dynamicName : blockName; }

    std::string blockName;
    std::shared_ptr<const BlockDef> block;   // collegato dal documento
    Vec2 position;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;
    std::vector<Attribute> attributes;
    // Ritaglio (XCLIP): contorno nelle coordinate del blocco (2 punti = rettangolo, 3 o più = poligono); resta
    // giusto se l'inserimento si sposta, ruota o scala. Vuoto = nessun ritaglio; `clipOn` falso = spento.
    std::vector<Vec2> clip;
    bool clipOn = true;
    bool clipped() const { return clipOn && clip.size() >= 2; }
    // Contorno del ritaglio come poligono nelle coordinate del blocco.
    std::vector<Vec2> clipPolygon() const;
};

// Linea di costruzione (XLINE, infinita nei due versi) o semiretta (RAY). Non conta nelle estensioni.
class XLineEntity final : public EntityBase<XLineEntity, EntityType::XLine> {
public:
    XLineEntity(Vec2 b, Vec2 dir, bool isRay = false);
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override { return {{base, SnapKind::Endpoint}}; }
    void transform(const Affine& t) override;

    static constexpr double kReach = 1e7;   // lunghezza usata per selezione e intersezioni

    Vec2 base;
    Vec2 direction;   // versore
    bool ray = false;
};

// Copertura (WIPEOUT): area poligonale riempita con il colore di sfondo, che nasconde gli oggetti
// disegnati prima. La cornice si vede secondo WIPEOUTFRAME.
class WipeoutEntity final : public EntityBase<WipeoutEntity, EntityType::Wipeout> {
public:
    explicit WipeoutEntity(std::vector<Vec2> o) : outline(std::move(o)) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    bool contains(Vec2 p) const;
    double distanceTo(Vec2 p) const override;

    std::vector<Vec2> outline;
};

// Immagine raster (IMAGE): un file esterno (JPG, PNG, TIF...) posato nel disegno. `u` e `v` sono i vettori di
// un pixel (larghezza e altezza, anche ruotati); l'angolo in basso a sinistra dell'immagine è in `insertion`.
class ImageEntity final : public EntityBase<ImageEntity, EntityType::Image> {
public:
    ImageEntity() = default;
    void decompose(std::vector<Prim>& out) const override;   // la cornice
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    double distanceTo(Vec2 p) const override;
    bool contains(Vec2 p) const;
    // Angoli in senso antiorario a partire da quello in basso a sinistra.
    std::array<Vec2, 4> corners() const;
    // Dal pixel (x verso destra, y verso il basso, origine nell'angolo in alto a sinistra) al disegno.
    Affine pixelToWorld() const;

    Vec2 insertion;
    Vec2 u{1.0, 0.0};
    Vec2 v{0.0, 1.0};
    double width = 1.0;          // pixel
    double height = 1.0;
    std::string path;            // file com'è salvato nel disegno (IMAGEDEF)
    bool show = true;            // codice 70, bit 1
    bool clipping = false;       // codice 280
    std::vector<Vec2> clip;      // contorno di ritaglio in pixel (codici 14/24), rettangolo se 2 punti
    int brightness = 50;
    int contrast = 50;
    int fade = 0;

    // Oggetto OLE incorporato (OLE2FRAME: loghi incollati da Paint, Word, Excel...): i dati così come sono nel
    // disegno (si riscrivono uguali); l'immagine si ricava da questi, non da un file.
    std::shared_ptr<const std::string> ole;
    std::string oleType;    // codice 3 (es. "Paintbrush Picture")
    int oleObjectType = 2;  // codice 71: 1 collegato, 2 incorporato, 3 statico
    bool isOle() const { return ole && !ole->empty(); }
};

// Multilinea (MLINE): linee parallele lungo una spezzata, unite con giunzioni a spigolo.
class MlineEntity final : public EntityBase<MlineEntity, EntityType::Mline> {
public:
    enum Justification { Top = 0, Zero = 1, Bottom = 2 };
    MlineEntity() = default;
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    // Per ogni elemento i suoi punti (uno per vertice), già con le giunzioni.
    std::vector<std::vector<Vec2>> elementLines() const;
    // Direzione di giunzione al vertice i (versore) e fattore per passare dalla distanza perpendicolare
    // a quella lungo la giunzione.
    Vec2 miter(std::size_t i, double* along = nullptr) const;
    Vec2 segmentDirection(std::size_t i) const;

    std::vector<Vec2> vertices;
    std::vector<double> offsets{0.0, -20.0};   // distanze degli elementi (a sinistra positive), già in scala
    bool closed = false;
    double scale = 20.0;
    int justification = Top;
    std::string style = "Standard";
};

// Tabella (TABLE): griglia di celle con testo. Il punto di inserimento è l'angolo in alto a sinistra.
class TableEntity final : public EntityBase<TableEntity, EntityType::Table> {
public:
    TableEntity() = default;
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    BBox bounds() const override;

    // Linee della griglia e testi delle celle, nel disegno.
    std::vector<std::unique_ptr<Entity>> geometry() const;
    // Cella sotto il punto (riga, colonna), oppure -1.
    std::pair<int, int> cellAt(Vec2 p) const;
    // Testo della cella come viene disegnato (anche vuoto: per scriverci dentro).
    std::unique_ptr<MTextEntity> cellText(int row, int col) const;
    std::string& cell(int row, int col) { return cells[static_cast<std::size_t>(row * columns + col)]; }
    const std::string& cell(int row, int col) const { return cells[static_cast<std::size_t>(row * columns + col)]; }
    void resize(int rows, int cols, double colWidth, double rowHeight);

    Vec2 position;
    double rotation = 0.0;
    int rows = 0;
    int columns = 0;
    std::vector<double> columnWidths;
    std::vector<double> rowHeights;
    std::vector<std::string> cells;   // riga per riga
    double textHeight = 2.5;
};

// Direttrice (LEADER/MLEADER): freccia sul primo punto, spezzata fino all'ultimo, testo accanto all'ultimo.
// Testo di un MTEXT senza i codici di formattazione ({}, \W..;, \f..;...): "\P" diventa a capo, i codici %%
// restano come sono. Per mostrarlo e modificarlo come testo semplice.
std::string mtextPlainText(const std::string& text);
// Nuovo contenuto di un MTEXT dal testo semplice `plain` (a capo = \P), tenendo la formattazione iniziale
// che vale per tutto il testo (es. "{\W0.7;...}" o "\A1;..."); quella dentro il testo si perde, come
// incollando testo semplice.
std::string mtextWithPlainText(const std::string& original, const std::string& plain);

class LeaderEntity final : public EntityBase<LeaderEntity, EntityType::Leader> {
public:
    LeaderEntity() = default;
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    BBox bounds() const override;
    // Anche il testo fa parte della direttrice (multidirettrice): cliccandolo si seleziona la direttrice.
    double distanceTo(Vec2 p) const override;
    bool touchesRect(const BBox& rect) const override;

    // Freccia, linee e testo (MTEXT allineato a sinistra o a destra secondo il verso dell'ultimo tratto).
    std::vector<std::unique_ptr<Entity>> geometry() const;
    std::unique_ptr<MTextEntity> annotation() const;

    std::vector<Vec2> points;   // points[0] = punta della freccia
    // Multidirettrice: altre frecce, ognuna una linea dritta dalla sua punta al punto d'attacco (junction()).
    std::vector<Vec2> extraTips;
    // Punto d'attacco delle frecce: l'inizio del tratto orizzontale d'approdo (o l'ultimo punto).
    Vec2 junction() const;
    std::string text;           // testo (\P = a capo, come MTEXT)
    double textHeight = 2.5;
    double arrowSize = 2.5;
    std::string style;                    // stile della direttrice (MLEADERSTYLE), vuoto = nessuno
    std::string textStyle = "Standard";   // stile del testo
    double landingGap = -1.0;             // distanza del testo dall'approdo; < 0 = metà altezza del testo
};

// Finestra di un layout (VIEWPORT): mostra lo spazio modello in scala dentro lo spazio carta.
class ViewportEntity final : public EntityBase<ViewportEntity, EntityType::Viewport> {
public:
    ViewportEntity(Vec2 c, double w, double h) : center(c), width(w), height(h) {}
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;

    // Scala: unità di carta per unità del modello (es. 1:100 in mm = 0.01).
    double scale() const { return viewHeight > 0.0 ? height / viewHeight : 1.0; }
    void setScale(double s) { if (s > 0.0) viewHeight = height / s; }
    // Da coordinate del modello a coordinate della carta.
    Affine modelToPaper() const;
    BBox paperRect() const;
    BBox modelRect() const;   // area del modello visibile

    Vec2 center;          // centro sul foglio
    double width;
    double height;
    Vec2 viewCenter;      // centro della vista nel modello
    double viewHeight = 1.0;
    bool locked = false;
    // Contorno poligonale sul foglio (vuoto = rettangolare), come VPCLIP.
    std::vector<Vec2> clip;
    // Layer congelati solo in questa finestra (indici dei layer del documento).
    std::vector<int> frozenLayers;

    // Ritaglia la finestra su un poligono: il riquadro diventa il suo ingombro, la vista del modello
    // resta ferma sul foglio. Con meno di 3 punti torna rettangolare.
    void setClip(std::vector<Vec2> polygon);
    bool containsPaperPoint(Vec2 p) const;
    bool isLayerFrozen(int layer) const;
    // Contorno sul foglio: il poligono o i quattro angoli del riquadro.
    std::vector<Vec2> outline() const;
};

// Quota (lineare, allineata, radiale, diametrale).
class DimensionEntity final : public EntityBase<DimensionEntity, EntityType::Dimension> {
public:
    enum Kind { Linear = 0, Aligned = 1, Angular = 2, Diameter = 3, Radius = 4, Angular3 = 5, Ordinate = 6 };

    DimensionEntity() = default;
    void decompose(std::vector<Prim>& out) const override;
    std::vector<SnapPoint> snapPoints() const override;
    void transform(const Affine& t) override;
    BBox bounds() const override;
    // Anche il testo fa parte della quota: cliccandolo si seleziona la quota.
    double distanceTo(Vec2 p) const override;
    bool touchesRect(const BBox& rect) const override;

    double measurement() const;
    std::string displayText() const;
    // Oggetti che compongono la quota: dal blocco associato, o calcolati dai punti di definizione.
    std::vector<std::unique_ptr<Entity>> geometry() const;

    int kind = Aligned;
    Vec2 p1;          // DXF 13: origine della prima linea di estensione
    Vec2 p2;          // DXF 14: origine della seconda linea di estensione
    Vec2 dimLine;     // DXF 10: punto sulla linea di quota (per le radiali: centro)
    Vec2 textPos;     // DXF 11: punto medio del testo
    Vec2 curvePoint;  // DXF 15: punto sulla curva (radiali e diametrali)
    double rotation = 0.0;          // quote lineari: direzione della misura
    // Angolare (Angular3, DXF tipo 5): p1 e p2 sui due lati, curvePoint il vertice, dimLine sull'arco di quota.
    // Coordinata (Ordinate, tipo 6): p1 il punto quotato, p2 la fine della direttrice, dimLine l'origine.
    bool ordinateX = false;         // coordinata X (altrimenti Y)
    // Angolare: l'angolo misurato e l'arco (da `startAngle` in senso antiorario).
    double angularSweep(double* startAngle = nullptr) const;
    std::string textOverride;       // "" = misura; "<>" nel testo = misura
    // Testo spostato (DIMTEDIT, maniglia del testo): il suo centro sta in textPos (DXF 70 bit 128).
    bool textMoved = false;
    std::optional<double> textRotation;   // rotazione del testo (DXF 53); nessuna = parallela alla quota
    double oblique = 0.0;                 // linee di estensione oblique (DXF 52, radianti); 0 = perpendicolari
    // Centro del testo com'è disegnato.
    Vec2 textMidpoint() const;
    // Estremi della linea di quota (quote lineari e allineate).
    std::pair<Vec2, Vec2> dimensionLineEnds() const;
    std::string blockName;
    std::shared_ptr<const BlockDef> block;
    Affine blockTransform;          // trasformazioni applicate dopo la lettura del blocco
    // Stile di quota: nome e valori (copiati dallo stile del disegno quando la quota viene creata
    // o quando lo stile cambia).
    DimStyle style;
};

} // namespace cartesio
