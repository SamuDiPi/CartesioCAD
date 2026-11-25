// Scrittura DXF 2000 (AC1015) con la struttura completa che si aspettano gli altri CAD e LibreDWG:
// handle e proprietari, tabelle con i record di sistema, BLOCK_RECORD collegati ai LAYOUT,
// dizionari di sistema, layout con finestre, blocchi con attributi.
#include "core/io/DxfWriter2000.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <cstring>
#include <set>
#include <sstream>
#include <string_view>

namespace cartesio::detail {

std::string escapeNonAscii(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        unsigned code = 0;
        std::size_t len = 1;
        if ((c >> 5) == 0x6) { code = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { code = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { code = c & 0x07; len = 4; }
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k)
            code = (code << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += len;
        if (code > 0xFFFF)
            code = '?';
        char buffer[12];
        std::snprintf(buffer, sizeof buffer, "\\U+%04X", code);
        out += buffer;
    }
    return out;
}

namespace {

// Handle fissi di tabelle e dizionari di sistema.
enum : unsigned {
    kLayerTable = 0x1, kLtypeTable = 0x2, kAppidTable = 0x3, kDimstyleTable = 0x4, kStyleTable = 0x5,
    kUcsTable = 0x6, kViewTable = 0x7, kVportTable = 0x8, kBlockRecordTable = 0x9,
    kRootDict = 0xA, kColorDict = 0xB, kGroupDict = 0xC, kLayoutDict = 0xD, kMlineDict = 0x10,
    kPlotSettingsDict = 0x11, kPlotStyleDict = 0x12, kPlotStylePlaceholder = 0x13, kScaleListDict = 0x14,
    kTableStyleDict = 0x15,
    kFirstFree = 0x20,
};

std::string hex(unsigned h)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%X", h);
    return buffer;
}

class Writer2000 {
public:
    Writer2000(const Document& doc, std::ostream& out, std::vector<std::string>* warnings)
        : doc_(doc), out_(out), warnings_(warnings)
    {
        body_.imbue(std::locale::classic());
        body_ << std::setprecision(15);
    }

    void write();
    bool forLibreDwg = false;   // DXF intermedio per dxf2dwg

    // LibreDWG accetta nelle etichette degli attributi solo lettere, cifre, "-", "_" e "$" (gli altri CAD anche il
    // punto, l'apostrofo...): con un carattere diverso scarta l'attributo e il blocco che lo contiene resta vuoto.
    // Nel DXF per il DWG gli altri caratteri diventano "_" (e si avvisa).
    std::string tagFor(const std::string& tag)
    {
        if (!forLibreDwg)
            return tag;
        std::string out;
        for (unsigned char c : tag)
            out += (std::isalnum(c) || c == '-' || c == '_' || c == '$') ? static_cast<char>(c) : '_';
        if (out != tag)
            renamedTags_.insert(tag + " -> " + out);
        return out;
    }
    std::set<std::string> renamedTags_;
    struct ClipRecord {
        unsigned insert = 0;
        unsigned xdict = 0;
        const InsertEntity* entity = nullptr;
    };
    std::vector<ClipRecord> clips_;

private:
    // Un registro di blocco: blocco del disegno, blocco di una quota, spazio modello o un layout.
    struct BlockRecord {
        std::string name;
        unsigned handle = 0;
        const BlockDef* block = nullptr;
        const DimensionEntity* dim = nullptr;
        const Layout* layout = nullptr;   // layout non attivo: contenuto scritto nel blocco
        unsigned layoutHandle = 0;        // oggetto LAYOUT collegato (340)
        const TableEntity* table = nullptr;   // tabella: blocco anonimo con griglia e testi
    };

    struct LayoutOut {
        const Layout* layout;             // nullptr = Model
        std::string name;
        unsigned handle;                  // oggetto LAYOUT
        unsigned recordHandle;            // BLOCK_RECORD
        unsigned mainViewport = 0;        // finestra generale (331)
        int tabOrder;
    };

    template <typename T>
    void pair(int code, const T& value) { body_ << code << '\n' << value << '\n'; }
    void text(int code, const std::string& s) { pair(code, escapeNonAscii(s)); }
    void point(int base, Vec2 p)
    {
        pair(base, p.x);
        pair(base + 10, p.y);
        pair(base + 20, 0.0);
    }
    unsigned next() { return handle_++; }
    std::vector<unsigned> layerHandles_;   // handle dei record LAYER, per indice di layer

    std::string layerName(int index) const;
    // Nome del tipo di linea come nella tabella LTYPE ("" se sconosciuto).
    std::string linetypeName(const std::string& name) const;
    void classes();
    void tables();
    void tableHeader(const char* name, unsigned handle, std::size_t count);
    unsigned tableRecord(const char* type, unsigned owner, const char* subclass);
    void blocks();
    void spaceContent(int space, unsigned owner, bool paper, LayoutOut* layout);
    void viewport(const ViewportEntity* vp, const Layout& layout, unsigned owner, int id, LayoutOut* out);
    void objects();
    void layoutObject(const LayoutOut& l);
    void plotSettings(const PlotSettings& s, Vec2 imageOrigin);
    void header();

    unsigned entity(const Entity& e, unsigned owner, bool paper = false);
    unsigned common(const char* type, const Entity& e, unsigned owner, bool paper);
    void textBody(const TextEntity& t, const char* subclass);
    unsigned lwpolyline(const std::vector<PolyVertex>& vertices, bool closed, const Entity& e, unsigned owner, bool paper);

    const Document& doc_;
    std::ostream& out_;
    std::vector<std::string>* warnings_;
    std::ostringstream body_;
    unsigned handle_ = kFirstFree;
    std::vector<BlockRecord> records_;
    std::vector<LayoutOut> layouts_;
    unsigned modelSpace_ = 0;
    unsigned paperSpace_ = 0;
    unsigned mlineStyle_ = 0;
    std::map<const DimensionEntity*, std::string> dimBlocks_;
    std::map<const TableEntity*, std::string> tableBlocks_;
    std::map<EntityId, unsigned> entityHandles_;   // per i gruppi
    std::map<std::string, unsigned> imageDefs_;    // file delle immagini raster -> handle di IMAGEDEF
    std::map<std::string, Vec2> imageSizes_;
    unsigned imageDef(const std::string& path, Vec2 size)
    {
        auto it = imageDefs_.find(path);
        if (it == imageDefs_.end())
            it = imageDefs_.emplace(path, next()).first;
        imageSizes_.emplace(path, size);
        return it->second;
    }
};

std::string Writer2000::layerName(int index) const
{
    const auto& layers = doc_.layers();
    if (index < 0 || index >= static_cast<int>(layers.size()))
        return "0";
    return layers[static_cast<std::size_t>(index)].name;
}

std::string Writer2000::linetypeName(const std::string& name) const
{
    const auto same = [&name](const char* other) {
        const std::string_view o(other);
        return name.size() == o.size() && std::equal(name.begin(), name.end(), o.begin(), [](char a, char b) {
                   return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
               });
    };
    if (name.empty() || same("ByLayer"))
        return "ByLayer";
    if (same("ByBlock"))
        return "ByBlock";
    if (same("Continuous"))
        return "Continuous";
    const Linetype* lt = doc_.findLinetype(name);
    return lt ? lt->name : std::string();
}

// ---------------------------------------------------------------------- classi

void Writer2000::classes()
{
    pair(0, "SECTION");
    pair(2, "CLASSES");
    const char* const defs[][3] = {
        {"ACDBDICTIONARYWDFLT", "AcDbDictionaryWithDefault", "ObjectDBX Classes"},
        {"ACDBPLACEHOLDER", "AcDbPlaceHolder", "ObjectDBX Classes"},
        {"LAYOUT", "AcDbLayout", "ObjectDBX Classes"},
        {"WIPEOUT", "AcDbWipeout", "WipeOut"},
        {"IMAGE", "AcDbRasterImage", "ISM"},
        {"IMAGEDEF", "AcDbRasterImageDef", "ISM"},
    };
    for (const auto& d : defs) {
        pair(0, "CLASS");
        pair(1, d[0]);
        pair(2, d[1]);
        pair(3, d[2]);
        pair(90, 0);
        pair(280, 0);
        pair(281, std::string(d[0]) == "WIPEOUT" || std::string(d[0]) == "IMAGE" ? 1 : 0);   // 1 = classe di oggetti grafici
    }
    pair(0, "ENDSEC");
}

// --------------------------------------------------------------------- tabelle

void Writer2000::tableHeader(const char* name, unsigned handle, std::size_t count)
{
    pair(0, "TABLE");
    pair(2, name);
    pair(5, hex(handle));
    pair(330, "0");
    pair(100, "AcDbSymbolTable");
    pair(70, count);
}

unsigned Writer2000::tableRecord(const char* type, unsigned owner, const char* subclass)
{
    const unsigned handle = next();
    pair(0, type);
    pair(5, hex(handle));
    pair(330, hex(owner));
    pair(100, "AcDbSymbolTableRecord");
    pair(100, subclass);
    return handle;
}

void Writer2000::tables()
{
    pair(0, "SECTION");
    pair(2, "TABLES");

    // Vista attiva dello spazio modello, inquadrata sulle estensioni del disegno.
    const BBox ext = doc_.extents(0);
    const Vec2 center = ext.isValid() ? ext.center() : Vec2{};
    const double viewHeight = ext.isValid() ? std::max({ext.height() * 1.1, ext.width() * 0.75, 1.0}) : 100.0;
    tableHeader("VPORT", kVportTable, 1);
    tableRecord("VPORT", kVportTable, "AcDbViewportTableRecord");
    pair(2, "*Active");
    pair(70, 0);
    pair(10, 0.0); pair(20, 0.0);
    pair(11, 1.0); pair(21, 1.0);
    pair(12, center.x); pair(22, center.y);
    pair(13, 0.0); pair(23, 0.0);
    pair(14, 10.0); pair(24, 10.0);
    pair(15, 10.0); pair(25, 10.0);
    pair(16, 0.0); pair(26, 0.0); pair(36, 1.0);
    pair(17, 0.0); pair(27, 0.0); pair(37, 0.0);
    pair(40, viewHeight);
    pair(41, 1.6);
    pair(42, 50.0);
    pair(43, 0.0); pair(44, 0.0);
    pair(50, 0.0); pair(51, 0.0);
    pair(71, 0); pair(72, 1000); pair(73, 1); pair(74, 3); pair(75, 0); pair(76, 0); pair(77, 0); pair(78, 0);
    pair(281, 0); pair(65, 1);
    pair(0, "ENDTAB");

    // ByBlock, ByLayer e Continuous ci sono sempre; poi i tipi di linea del disegno.
    std::vector<const Linetype*> extraTypes;
    for (const Linetype& lt : doc_.linetypes())
        if (linetypeName(lt.name) == lt.name && lt.name != "Continuous")
            extraTypes.push_back(&lt);
    tableHeader("LTYPE", kLtypeTable, 3 + extraTypes.size());
    for (const char* name : {"ByBlock", "ByLayer", "Continuous"}) {
        tableRecord("LTYPE", kLtypeTable, "AcDbLinetypeTableRecord");
        pair(2, name);
        pair(70, 0);
        pair(3, std::string(name) == "Continuous" ? "Solid line" : "");
        pair(72, 65);
        pair(73, 0);
        pair(40, 0.0);
    }
    for (const Linetype* lt : extraTypes) {
        tableRecord("LTYPE", kLtypeTable, "AcDbLinetypeTableRecord");
        text(2, lt->name);
        pair(70, 0);
        text(3, lt->description);
        pair(72, 65);
        pair(73, lt->pattern.size());
        pair(40, lt->length());
        for (double d : lt->pattern) {
            pair(49, d);
            pair(74, 0);
        }
    }
    pair(0, "ENDTAB");

    tableHeader("LAYER", kLayerTable, doc_.layers().size());
    for (const Layer& layer : doc_.layers()) {
        layerHandles_.push_back(tableRecord("LAYER", kLayerTable, "AcDbLayerTableRecord"));
        text(2, layer.name);
        pair(70, (layer.frozen ? 1 : 0) | (layer.locked ? 4 : 0)
                     | (layer.name.find('|') != std::string::npos ? 16 | 32 : 0));   // layer di un XREF
        pair(62, layer.visible ? layer.color : -layer.color);
        if (layer.trueColor >= 0)
            pair(420, layer.trueColor);
        text(6, linetypeName(layer.linetype).empty() || linetypeName(layer.linetype) == "ByLayer"
                    || linetypeName(layer.linetype) == "ByBlock" ? std::string("Continuous") : linetypeName(layer.linetype));
        if (!layer.plot)
            pair(290, 0);
        pair(370, layer.lineweight);
        pair(390, hex(kPlotStylePlaceholder));
    }
    pair(0, "ENDTAB");

    tableHeader("STYLE", kStyleTable, doc_.textStyles().size());
    for (const TextStyle& s : doc_.textStyles()) {
        tableRecord("STYLE", kStyleTable, "AcDbTextStyleTableRecord");
        text(2, s.name);
        pair(70, 0);
        pair(40, s.height);
        pair(41, s.widthFactor);
        pair(50, radToDeg(s.oblique));
        pair(71, (s.backwards ? 2 : 0) | (s.upsideDown ? 4 : 0));
        pair(42, 2.5);
        text(3, s.font.empty() && s.family.empty() ? std::string("arial.ttf") : s.font);
        text(4, s.bigFont);
        // Famiglia TrueType e stile del carattere, come si scrivono nei DXF.
        if (!s.family.empty()) {
            pair(1001, "ACAD");
            text(1000, s.family);
            pair(1071, 34 | (s.italic ? 0x01000000 : 0) | (s.bold ? 0x02000000 : 0));
        }
        if (s.annotative) {   // stile annotativo (il DXF 2000 non lo prevede: dati di CartesioCAD)
            pair(1001, "CARTESIOCAD");
            pair(1000, "ANNOTATIVE");
        }
    }
    pair(0, "ENDTAB");

    tableHeader("VIEW", kViewTable, doc_.namedViews().size());
    for (const Document::NamedView& v : doc_.namedViews()) {
        tableRecord("VIEW", kViewTable, "AcDbViewTableRecord");
        text(2, v.name);
        pair(70, v.space != 0 ? 1 : 0);
        pair(40, v.height);
        point(10, v.center);
        pair(41, v.width);
        pair(11, 0.0); pair(21, 0.0); pair(31, 1.0);   // direzione di vista: dall'alto
        pair(12, 0.0); pair(22, 0.0); pair(32, 0.0);   // punto di mira
        pair(42, 50.0);
        pair(43, 0.0);
        pair(44, 0.0);
        pair(50, 0.0);
        pair(71, 0);
        pair(281, 0);
    }
    pair(0, "ENDTAB");
    tableHeader("UCS", kUcsTable, 0);
    pair(0, "ENDTAB");

    tableHeader("APPID", kAppidTable, 6);
    tableRecord("APPID", kAppidTable, "AcDbRegAppTableRecord");
    pair(2, "ACAD");
    pair(70, 0);
    tableRecord("APPID", kAppidTable, "AcDbRegAppTableRecord");
    pair(2, "CARTESIOCAD");   // dati delle tabelle (XDATA)
    pair(70, 0);
    tableRecord("APPID", kAppidTable, "AcDbRegAppTableRecord");
    pair(2, "AcDbBlockRepBTag");   // stati dei blocchi dinamici
    pair(70, 0);
    tableRecord("APPID", kAppidTable, "AcDbRegAppTableRecord");
    pair(2, "CARTESIOCAD_ANNO");   // oggetti annotativi: scale supportate e fattore della geometria
    pair(70, 0);
    tableRecord("APPID", kAppidTable, "AcDbRegAppTableRecord");
    pair(2, "CARTESIOCAD_IMPIANTO");   // linee delle tratte dell'impianto: codice della tratta
    pair(70, 0);
    tableRecord("APPID", kAppidTable, "AcDbRegAppTableRecord");
    pair(2, "CARTESIOCAD_CANALO");   // oggetti delle canaline portacavi: codice della canalina
    pair(70, 0);
    pair(0, "ENDTAB");

    // DIMSTYLE ha un formato particolare: sottoclasse della tabella e handle con codice 105.
    pair(0, "TABLE");
    pair(2, "DIMSTYLE");
    pair(5, hex(kDimstyleTable));
    pair(330, "0");
    pair(100, "AcDbSymbolTable");
    pair(70, doc_.dimStyles().size());
    pair(100, "AcDbDimStyleTable");
    for (const DimStyle& s : doc_.dimStyles()) {
        pair(0, "DIMSTYLE");
        pair(105, hex(next()));
        pair(330, hex(kDimstyleTable));
        pair(100, "AcDbSymbolTableRecord");
        pair(100, "AcDbDimStyleTableRecord");
        text(2, s.name);
        pair(70, 0);
        if (!s.post.empty())
            text(3, s.post);       // DIMPOST
        pair(40, s.scale);         // DIMSCALE
        pair(41, s.arrowSize);     // DIMASZ
        pair(42, s.extOffset);     // DIMEXO
        pair(43, 3.75);            // DIMDLI
        pair(44, s.extExtension);  // DIMEXE
        pair(140, s.textHeight);   // DIMTXT
        pair(141, 2.5);            // DIMCEN
        pair(142, s.tickSize);     // DIMTSZ
        pair(144, s.linearFactor); // DIMLFAC
        pair(147, s.textGap);      // DIMGAP
        pair(77, 1);               // DIMTAD
        pair(78, 8);               // DIMZIN
        pair(271, s.decimals);     // DIMDEC
        if (s.annotative) {
            pair(1001, "CARTESIOCAD");
            pair(1000, "ANNOTATIVE");
        }
    }
    pair(0, "ENDTAB");

    tableHeader("BLOCK_RECORD", kBlockRecordTable, records_.size());
    for (const BlockRecord& r : records_) {
        pair(0, "BLOCK_RECORD");
        pair(5, hex(r.handle));
        pair(330, hex(kBlockRecordTable));
        pair(100, "AcDbSymbolTableRecord");
        pair(100, "AcDbBlockTableRecord");
        text(2, r.name);
        pair(340, hex(r.layoutHandle));
        // Stato di un blocco dinamico: il collegamento al blocco dinamico, perché gli altri CAD lo riconoscano.
        if (r.block && !r.block->dynamicName.empty())
            for (const BlockRecord& other : records_)
                if (other.name == r.block->dynamicName) {
                    pair(1001, "AcDbBlockRepBTag");
                    pair(1070, 1);
                    pair(1005, hex(other.handle));
                    break;
                }
    }
    pair(0, "ENDTAB");

    pair(0, "ENDSEC");
}

// ---------------------------------------------------------------------- oggetti

unsigned Writer2000::common(const char* type, const Entity& e, unsigned owner, bool paper)
{
    const unsigned handle = next();
    pair(0, type);
    pair(5, hex(handle));
    // Blocco ritagliato (XCLIP): dizionario esteso con il filtro spaziale, scritto con gli oggetti.
    if (e.type() == EntityType::Insert && static_cast<const InsertEntity&>(e).clip.size() >= 2) {
        const unsigned xdict = next();
        pair(102, "{ACAD_XDICTIONARY");
        pair(360, hex(xdict));
        pair(102, "}");
        clips_.push_back({handle, xdict, static_cast<const InsertEntity*>(&e)});
    }
    pair(330, hex(owner));
    pair(100, "AcDbEntity");
    if (paper)
        pair(67, 1);
    text(8, layerName(e.layer() >= 0 ? e.layer() : 0));
    if (const std::string lt = linetypeName(e.linetype()); !lt.empty() && lt != "ByLayer")
        text(6, lt);
    if (e.color() >= 0 && e.color() != kColorByLayer)
        pair(62, e.color());
    if (e.trueColor() >= 0)
        pair(420, e.trueColor());
    if (e.lineweight() != kLineweightByLayer && e.lineweight() != kLineweightUnset)
        pair(370, e.lineweight());
    if (e.linetypeScale() != 1.0)
        pair(48, e.linetypeScale());
    if (e.transparency() >= 0)
        pair(440, e.transparency());
    return handle;
}

unsigned Writer2000::lwpolyline(const std::vector<PolyVertex>& vertices, bool closed, const Entity& e, unsigned owner,
                                bool paper)
{
    const unsigned handle = common("LWPOLYLINE", e, owner, paper);
    pair(100, "AcDbPolyline");
    pair(90, vertices.size());
    pair(70, closed ? 1 : 0);
    // Larghezza: costante (43) se uguale ovunque, altrimenti iniziale e finale di ogni vertice (40/41).
    bool constant = true;
    for (const PolyVertex& v : vertices)
        constant = constant && v.startWidth == vertices.front().startWidth && v.endWidth == vertices.front().startWidth;
    if (constant)
        pair(43, vertices.empty() ? 0.0 : vertices.front().startWidth);
    for (const PolyVertex& v : vertices) {
        pair(10, v.point.x);
        pair(20, v.point.y);
        if (!constant) {
            pair(40, v.startWidth);
            pair(41, v.endWidth);
        }
        if (v.bulge != 0.0)
            pair(42, v.bulge);
    }
    return handle;
}

// Parte comune di TEXT, ATTDEF e ATTRIB (sottoclasse AcDbText).
void Writer2000::textBody(const TextEntity& t, const char* subclass)
{
    pair(100, "AcDbText");
    point(10, t.position);
    pair(40, t.height);
    text(1, t.text);
    if (t.rotation != 0.0)
        pair(50, radToDeg(t.rotation));
    if (t.widthFactor != 1.0)
        pair(41, t.widthFactor);
    text(7, doc_.findTextStyle(t.style) ? t.style : std::string("Standard"));
    if (t.oblique != 0.0)
        pair(51, radToDeg(t.oblique));
    if (t.hAlign != TextEntity::Left || t.vAlign != TextEntity::Baseline) {
        pair(72, t.hAlign);
        point(11, t.alignPoint);
    }
    pair(100, subclass);
}

unsigned Writer2000::entity(const Entity& e, unsigned owner, bool paper)
{
    unsigned handle = 0;
    switch (e.type()) {
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        handle = common("LINE", e, owner, paper);
        pair(100, "AcDbLine");
        point(10, l.start);
        point(11, l.end);
        break;
    }
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        handle = common("CIRCLE", e, owner, paper);
        pair(100, "AcDbCircle");
        point(10, c.center);
        pair(40, c.radius);
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        handle = common("ARC", e, owner, paper);
        pair(100, "AcDbCircle");
        point(10, a.center);
        pair(40, a.radius);
        pair(100, "AcDbArc");
        pair(50, radToDeg(normalizeAngle(a.startAngle)));
        pair(51, radToDeg(normalizeAngle(a.endAngle)));
        break;
    }
    case EntityType::Point:
        handle = common("POINT", e, owner, paper);
        pair(100, "AcDbPoint");
        point(10, static_cast<const PointEntity&>(e).position);
        break;
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        handle = lwpolyline(p.vertices, p.closed, e, owner, paper);
        break;
    }
    case EntityType::Ellipse: {
        const auto& el = static_cast<const EllipseEntity&>(e);
        handle = common("ELLIPSE", e, owner, paper);
        pair(100, "AcDbEllipse");
        point(10, el.center);
        point(11, el.majorAxis);
        pair(40, el.ratio);
        pair(41, el.startParam);
        pair(42, el.isFull() ? el.startParam + 2.0 * kPi : el.startParam + el.paramSweep());
        break;
    }
    case EntityType::Spline: {
        const auto& original = static_cast<const SplineEntity&>(e);
        const auto isValid = [](const SplineEntity& sp) {
            return sp.degree >= 1 && sp.control.size() >= static_cast<std::size_t>(sp.degree) + 1
                   && sp.knots.size() == sp.control.size() + static_cast<std::size_t>(sp.degree) + 1;
        };
        SplineEntity converted = original;
        if (!isValid(original) && original.fit.size() >= 2) {
            // Spline con i soli punti di interpolazione (non conservata da LibreDWG): si converte nella
            // stessa curva che disegna CartesioCAD, una Catmull-Rom = sequenza di Bézier cubiche,
            // cioè una B-spline cubica con nodi di molteplicità 3.
            const auto& f = original.fit;
            const std::size_t n = f.size() - 1;
            converted.control.clear();
            converted.knots = {0.0, 0.0, 0.0, 0.0};
            converted.weights.clear();
            converted.degree = 3;
            converted.control.push_back(f[0]);
            for (std::size_t i = 0; i < n; ++i) {
                const Vec2 p0 = i > 0 ? f[i - 1] : f[i];
                const Vec2 p3 = i + 2 < f.size() ? f[i + 2] : f[i + 1];
                converted.control.push_back(f[i] + (f[i + 1] - p0) / 6.0);
                converted.control.push_back(f[i + 1] - (p3 - f[i]) / 6.0);
                converted.control.push_back(f[i + 1]);
                const double k = static_cast<double>(i + 1);
                for (int r = 0; r < (i + 1 == n ? 4 : 3); ++r)
                    converted.knots.push_back(k);
            }
        }
        const SplineEntity& s = converted;
        if (!isValid(s)) {   // senza punti di controllo validi: polilinea che la approssima
            std::vector<PolyVertex> vertices;
            for (Vec2 p : s.evaluate())
                vertices.push_back({p, 0.0});
            handle = lwpolyline(vertices, false, e, owner, paper);
            break;
        }
        const bool rational = s.weights.size() == s.control.size();
        handle = common("SPLINE", e, owner, paper);
        pair(100, "AcDbSpline");
        pair(210, 0.0);
        pair(220, 0.0);
        pair(230, 1.0);
        pair(70, 8 | (s.closed ? 1 : 0) | (rational ? 4 : 0));
        pair(71, s.degree);
        pair(72, s.knots.size());
        pair(73, s.control.size());
        pair(74, 0);
        pair(42, 1e-10);
        pair(43, 1e-10);
        for (double k : s.knots)
            pair(40, k);
        if (rational)
            for (double w : s.weights)
                pair(41, w);
        for (Vec2 p : s.control)
            point(10, p);
        break;
    }
    case EntityType::Text: {
        const auto& t = static_cast<const TextEntity&>(e);
        handle = common("TEXT", e, owner, paper);
        textBody(t, "AcDbText");
        if (t.vAlign != TextEntity::Baseline)
            pair(73, t.vAlign);
        break;
    }
    case EntityType::AttDef: {
        const auto& a = static_cast<const AttDefEntity&>(e);
        handle = common("ATTDEF", e, owner, paper);
        textBody(a.text, "AcDbAttributeDefinition");
        text(3, a.prompt);
        text(2, tagFor(a.tag));
        pair(70, a.flags);
        if (a.text.vAlign != TextEntity::Baseline)
            pair(74, a.text.vAlign);
        break;
    }
    case EntityType::MText: {
        const auto& m = static_cast<const MTextEntity&>(e);
        handle = common("MTEXT", e, owner, paper);
        pair(100, "AcDbMText");
        point(10, m.position);
        pair(40, m.height);
        pair(41, m.width);
        pair(71, m.attachment);
        pair(72, 1);
        // Il testo va diviso in blocchi da 250 caratteri: i primi con codice 3, l'ultimo con 1.
        const std::string content = escapeNonAscii(m.text);
        std::size_t pos = 0;
        while (content.size() - pos > 250) {
            pair(3, content.substr(pos, 250));
            pos += 250;
        }
        pair(1, content.substr(pos));
        text(7, doc_.findTextStyle(m.style) ? m.style : std::string("Standard"));
        point(11, Vec2::polar(1.0, m.rotation));   // direzione del testo
        pair(73, 1);
        pair(44, m.lineSpacing);
        if (m.backgroundFill) {   // maschera con il colore dello sfondo
            pair(90, 2);
            pair(63, 256);
            pair(45, m.fillScale);
            pair(441, 0);
        }
        if (m.columns > 1) {   // colonne (il DXF 2000 non le prevede: dati di CartesioCAD)
            pair(1001, "CARTESIOCAD");
            pair(1000, "MTEXTCOLUMNS");
            pair(1070, m.columns);
            pair(1040, m.columnGutter);
        }
        break;
    }
    case EntityType::Solid: {
        const auto& s = static_cast<const SolidEntity&>(e);
        handle = common("SOLID", e, owner, paper);
        pair(100, "AcDbTrace");
        point(10, s.corners[0]);
        point(11, s.corners[1]);
        point(12, s.corners[3]);   // ordine DXF: 1, 2, 4, 3
        point(13, s.corners[2]);
        break;
    }
    case EntityType::Hatch: {
        const auto& h = static_cast<const HatchEntity&>(e);
        handle = common("HATCH", e, owner, paper);
        pair(100, "AcDbHatch");
        point(10, {0.0, 0.0});
        pair(210, 0.0);
        pair(220, 0.0);
        pair(230, 1.0);
        pair(2, h.solid ? "SOLID" : h.pattern);
        pair(70, h.solid ? 1 : 0);
        pair(71, 0);
        pair(91, h.loops.size());
        for (const auto& loop : h.loops) {
            pair(92, 2);    // contorno a polilinea
            pair(72, 1);
            pair(73, 1);
            pair(93, loop.size());
            for (const PolyVertex& v : loop) {
                pair(10, v.point.x);
                pair(20, v.point.y);
                pair(42, v.bulge);
            }
            pair(97, 0);
        }
        pair(75, h.hatchStyle);
        pair(76, h.patternType);
        if (!h.solid) {
            // Linee del motivo: quelle lette dal disegno, oppure il motivo standard ricostruito dal nome.
            const double s = h.patternScale > 0.0 ? h.patternScale : 1.0;
            std::vector<HatchPatternLine> lines = h.patternLines;
            if (lines.empty())
                lines = standardHatchPattern(h.pattern, s, h.patternAngle);
            if (lines.empty())
                lines = standardHatchPattern("ANSI31", s, h.patternAngle);
            pair(52, radToDeg(h.patternAngle));
            pair(41, s);
            pair(77, h.patternDouble ? 1 : 0);
            pair(78, lines.size());
            for (const HatchPatternLine& line : lines) {
                pair(53, radToDeg(line.angle));
                pair(43, line.base.x);
                pair(44, line.base.y);
                pair(45, line.offset.x);
                pair(46, line.offset.y);
                pair(79, line.dashes.size());
                for (double d : line.dashes)
                    pair(49, d);
            }
        }
        pair(98, 0);
        // Sfumatura: riscritta com'era nel disegno originale.
        for (const auto& [code, value] : h.gradient)
            text(code, value);
        break;
    }
    case EntityType::Insert: {
        const auto& in = static_cast<const InsertEntity&>(e);
        handle = common("INSERT", e, owner, paper);
        pair(100, "AcDbBlockReference");
        if (!in.attributes.empty())
            pair(66, 1);
        text(2, in.blockName);
        point(10, in.position);
        pair(41, in.scaleX);
        pair(42, in.scaleY);
        pair(43, 1.0);
        if (in.rotation != 0.0)
            pair(50, radToDeg(in.rotation));
        if (in.attributes.empty())
            break;
        // Attributi: ATTRIB di proprietà dell'inserimento, chiusi da SEQEND.
        for (const Attribute& a : in.attributes) {
            common("ATTRIB", a.text, handle, paper);
            textBody(a.text, "AcDbAttribute");
            text(2, tagFor(a.tag));
            pair(70, a.flags);
            if (a.text.vAlign != TextEntity::Baseline)
                pair(74, a.text.vAlign);
        }
        pair(0, "SEQEND");
        pair(5, hex(next()));
        pair(330, hex(handle));
        pair(100, "AcDbEntity");
        if (paper)
            pair(67, 1);
        text(8, layerName(e.layer() >= 0 ? e.layer() : 0));
        break;
    }
    case EntityType::Dimension: {
        const auto& d = static_cast<const DimensionEntity&>(e);
        const auto it = dimBlocks_.find(&d);
        if (it == dimBlocks_.end())
            break;
        handle = common("DIMENSION", e, owner, paper);
        pair(100, "AcDbDimension");
        pair(2, it->second);
        point(10, d.dimLine);
        point(11, d.textPos);
        pair(70, d.kind | 32 | (d.kind == DimensionEntity::Ordinate && d.ordinateX ? 64 : 0) | (d.textMoved ? 128 : 0));
        if (!d.textOverride.empty())
            text(1, d.textOverride);
        if (d.textRotation)
            pair(53, radToDeg(*d.textRotation));
        text(3, doc_.findDimStyle(d.style.name) ? doc_.findDimStyle(d.style.name)->name : std::string("Standard"));
        switch (d.kind) {
        case DimensionEntity::Linear:
            pair(100, "AcDbAlignedDimension");
            point(13, d.p1);
            point(14, d.p2);
            pair(50, radToDeg(d.rotation));
            if (d.oblique != 0.0)
                pair(52, radToDeg(d.oblique));
            pair(100, "AcDbRotatedDimension");
            break;
        case DimensionEntity::Aligned:
            pair(100, "AcDbAlignedDimension");
            point(13, d.p1);
            point(14, d.p2);
            if (d.oblique != 0.0)
                pair(52, radToDeg(d.oblique));
            break;
        case DimensionEntity::Radius:
            pair(100, "AcDbRadialDimension");
            point(15, d.curvePoint);
            pair(40, 0.0);
            break;
        case DimensionEntity::Diameter:
            pair(100, "AcDbDiametricDimension");
            point(15, d.curvePoint);
            pair(40, 0.0);
            break;
        case DimensionEntity::Angular:
        case DimensionEntity::Angular3:
            pair(100, "AcDb3PointAngularDimension");
            point(13, d.p1);
            point(14, d.p2);
            point(15, d.curvePoint);
            break;
        case DimensionEntity::Ordinate:
            pair(100, "AcDbOrdinateDimension");
            point(13, d.p1);
            point(14, d.p2);
            break;
        default:
            pair(100, "AcDbAlignedDimension");
            point(13, d.p1);
            point(14, d.p2);
            break;
        }
        break;
    }
    case EntityType::Image: {
        // Immagine raster: posizione, vettori di un pixel, dimensioni, definizione (IMAGEDEF) con il file.
        const auto& im = static_cast<const ImageEntity&>(e);
        if (im.isOle()) {
            // Oggetto OLE incorporato: i dati letti, riscritti uguali, fra i due angoli del riquadro.
            const auto c = im.corners();
            BBox box;
            for (Vec2 p : c)
                box.expand(p);
            handle = common("OLE2FRAME", e, owner, paper);
            pair(100, "AcDbOle2Frame");
            pair(70, 2);
            text(3, im.oleType.empty() ? std::string("OLE") : im.oleType);
            point(10, {box.min.x, box.max.y});
            point(11, {box.max.x, box.min.y});
            pair(71, im.oleObjectType);
            pair(72, paper ? 1 : 0);
            pair(90, static_cast<int>(im.ole->size()));
            static const char* digits = "0123456789ABCDEF";
            // Nei dati (che iniziano con 80 55) ci sono anche i quattro vertici: si aggiornano, così
            // un oggetto spostato o ruotato resta dove è stato messo.
            std::string data = *im.ole;
            if (data.size() >= 2 + 12 * 8 && static_cast<unsigned char>(data[0]) == 0x80
                && static_cast<unsigned char>(data[1]) == 0x55) {
                const double v[12] = {c[3].x, c[3].y, 0.0, c[2].x, c[2].y, 0.0, c[1].x, c[1].y, 0.0, c[0].x, c[0].y, 0.0};
                std::memcpy(data.data() + 2, v, sizeof(v));
            }
            // Il formato vuole righe da 127 byte; dxf2dwg con decine di migliaia di righe impiega minuti (53 s per due
            // loghi da 4 MB), con righe lunghe meno della metà.
            const std::size_t step = forLibreDwg ? 512 * 1024 : 127;
            for (std::size_t i = 0; i < data.size(); i += step) {
                std::string chunk;
                for (std::size_t j = i; j < std::min(data.size(), i + step); ++j) {
                    const auto byte = static_cast<unsigned char>(data[j]);
                    chunk += digits[byte >> 4];
                    chunk += digits[byte & 15];
                }
                text(310, chunk);
            }
            text(1, "OLE");
            break;
        }
        handle = common("IMAGE", e, owner, paper);
        pair(100, "AcDbRasterImage");
        pair(90, 0);
        point(10, im.insertion);
        point(11, im.u);
        point(12, im.v);
        pair(13, im.width);
        pair(23, im.height);
        pair(340, hex(imageDef(im.path, {im.width, im.height})));
        pair(70, im.show ? 7 : 6);
        pair(280, im.clipping ? 1 : 0);
        pair(281, im.brightness);
        pair(282, im.contrast);
        pair(283, im.fade);
        pair(360, "0");
        const bool polygon = im.clip.size() > 2;
        pair(71, polygon ? 2 : 1);
        if (im.clip.size() >= 2) {
            pair(91, im.clip.size());
            for (Vec2 p : im.clip) {
                pair(14, p.x);
                pair(24, p.y);
            }
        } else {   // contorno: l'intera immagine
            pair(91, 2);
            pair(14, -0.5);
            pair(24, -0.5);
            pair(14, im.width - 0.5);
            pair(24, im.height - 0.5);
        }
        break;
    }
    case EntityType::Wipeout: {
        // Come un'immagine raster: punto di inserimento, vettori u/v e contorno in coordinate normalizzate.
        const auto& w = static_cast<const WipeoutEntity&>(e);
        if (w.outline.size() < 3)
            break;
        BBox b;
        for (Vec2 v : w.outline)
            b.expand(v);
        const double width = std::max(b.width(), 1e-9);
        const double height = std::max(b.height(), 1e-9);
        handle = common("WIPEOUT", e, owner, paper);
        pair(100, "AcDbWipeout");
        pair(90, 0);
        point(10, b.min);
        point(11, {width, 0.0});
        point(12, {0.0, height});
        point(13, {1.0, 1.0});
        pair(340, "0");
        pair(70, 7);
        pair(280, 1);
        pair(281, 50);
        pair(282, 50);
        pair(283, 0);
        pair(360, "0");
        pair(71, 2);   // contorno poligonale
        pair(91, w.outline.size());
        for (Vec2 v : w.outline) {
            pair(14, (v.x - b.min.x) / width - 0.5);
            pair(24, 0.5 - (v.y - b.min.y) / height);
        }
        pair(290, 0);
        break;
    }
    case EntityType::Mline: {
        // Per ogni vertice: punto, direzione del tratto, giunzione e distanza di ogni elemento lungo la giunzione.
        const auto& m = static_cast<const MlineEntity&>(e);
        if (m.vertices.size() < 2)
            break;
        handle = common("MLINE", e, owner, paper);
        pair(100, "AcDbMline");
        pair(2, "Standard");
        pair(340, hex(mlineStyle_));
        pair(40, m.scale);
        pair(70, m.justification);
        pair(71, 1 | (m.closed ? 2 : 0));
        pair(72, m.vertices.size());
        pair(73, m.offsets.size());
        point(10, m.vertices.front());
        pair(210, 0.0);
        pair(220, 0.0);
        pair(230, 1.0);
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            double k = 1.0;
            const Vec2 miter = m.miter(i, &k);
            const bool last = i + 1 == m.vertices.size() && !m.closed;
            point(11, m.vertices[i]);
            point(12, m.segmentDirection(last ? i - 1 : i));
            point(13, miter);
            for (double offset : m.offsets) {
                pair(74, 2);
                pair(41, offset * k);
                pair(41, 0.0);
                pair(75, 0);
            }
        }
        break;
    }
    case EntityType::Table: {
        // Il DXF 2000 non ha tabelle: si salva un blocco anonimo con griglia e testi (che gli altri CAD
        // mostra uguale) e, nei dati estesi, il contenuto per ritrovare la tabella in CartesioCAD.
        const auto& t = static_cast<const TableEntity&>(e);
        const auto it = tableBlocks_.find(&t);
        if (it == tableBlocks_.end())
            break;
        handle = common("INSERT", e, owner, paper);
        pair(100, "AcDbBlockReference");
        text(2, it->second);
        point(10, {0.0, 0.0});
        pair(1001, "CARTESIOCAD");
        pair(1000, "TABLE");
        pair(1010, t.position.x);
        pair(1020, t.position.y);
        pair(1030, 0.0);
        pair(1040, t.rotation);
        pair(1040, t.textHeight);
        pair(1070, t.rows);
        pair(1070, t.columns);
        for (double w : t.columnWidths)
            pair(1040, w);
        for (double h : t.rowHeights)
            pair(1040, h);
        for (const std::string& c : t.cells)
            text(1000, c.substr(0, 250));
        break;
    }
    case EntityType::XLine: {
        const auto& x = static_cast<const XLineEntity&>(e);
        handle = common(x.ray ? "RAY" : "XLINE", e, owner, paper);
        pair(100, x.ray ? "AcDbRay" : "AcDbXline");
        point(10, x.base);
        point(11, x.direction);
        break;
    }
    case EntityType::Leader: {
        // LEADER con il testo in un MTEXT collegato (codice 340).
        const auto& l = static_cast<const LeaderEntity&>(e);
        if (l.points.size() < 2)
            break;
        unsigned textHandle = 0;
        if (const auto m = l.annotation())
            textHandle = entity(*m, owner, paper);
        handle = common("LEADER", e, owner, paper);
        pair(100, "AcDbLeader");
        pair(3, "Standard");
        pair(71, l.arrowSize > 0.0 ? 1 : 0);   // freccia
        pair(72, 0);                            // tratti rettilinei
        pair(73, textHandle ? 0 : 3);           // 0 = con testo, 3 = senza annotazione
        pair(74, 0);
        pair(75, 0);
        pair(40, l.textHeight);
        pair(41, 0.0);
        pair(76, l.points.size());
        for (Vec2 p : l.points)
            point(10, p);
        pair(210, 0.0); pair(220, 0.0); pair(230, 1.0);
        pair(211, 1.0); pair(221, 0.0); pair(231, 0.0);
        pair(212, 0.0); pair(222, 0.0); pair(232, 0.0);
        pair(213, 0.0); pair(223, 0.0); pair(233, 0.0);
        if (textHandle)
            pair(340, hex(textHandle));
        if (!l.style.empty() || l.landingGap >= 0.0 || (!l.textStyle.empty() && l.textStyle != "Standard")) {
            pair(1001, "CARTESIOCAD");   // stile della direttrice e del testo
            pair(1000, "MLEADERSTYLE");
            text(1000, l.style);
            text(1000, l.textStyle);
            pair(1040, l.landingGap);
        }
        break;
    }
    case EntityType::Viewport:
        break;   // scritte con il loro layout
    }
    // Oggetto annotativo: scale supportate e fattore della geometria (dati di CartesioCAD, in coda al record).
    if (handle != 0 && e.annotative()) {
        pair(1001, "CARTESIOCAD_ANNO");
        pair(1040, e.annoFactor());
        for (const std::string& name : e.annoScales())
            text(1000, name);
    }
    // Linea di una tratta dell'impianto: il codice della tratta nel file di progetto.
    if (handle != 0 && !e.plantRun().empty()) {
        pair(1001, "CARTESIOCAD_IMPIANTO");
        text(1000, e.plantRun());
    }
    // Oggetto di una canalina portacavi: il suo codice (lo stato è nel dizionario CARTESIO_CANALO).
    if (handle != 0 && !e.canaloTray().empty()) {
        pair(1001, "CARTESIOCAD_CANALO");
        text(1000, e.canaloTray());
    }
    // Multidirettrice: ogni freccia in più è un LEADER senza testo dalla sua punta al punto d'attacco (gli altri
    // programmi la vedono così), con i dati di CartesioCAD che la legano alla direttrice principale.
    if (handle != 0 && e.type() == EntityType::Leader) {
        const auto& l = static_cast<const LeaderEntity&>(e);
        const Vec2 join = l.junction();
        for (Vec2 tip : l.extraTips) {
            common("LEADER", e, owner, paper);
            pair(100, "AcDbLeader");
            pair(3, "Standard");
            pair(71, l.arrowSize > 0.0 ? 1 : 0);
            pair(72, 0);
            pair(73, 3);
            pair(74, 0);
            pair(75, 0);
            pair(40, l.textHeight);
            pair(41, 0.0);
            pair(76, 2);
            point(10, tip);
            point(10, join);
            pair(210, 0.0); pair(220, 0.0); pair(230, 1.0);
            pair(211, 1.0); pair(221, 0.0); pair(231, 0.0);
            pair(212, 0.0); pair(222, 0.0); pair(232, 0.0);
            pair(213, 0.0); pair(223, 0.0); pair(233, 0.0);
            pair(1001, "CARTESIOCAD");
            pair(1000, "MLEADERLEG");
            pair(1005, hex(handle));
        }
    }
    return handle;
}

// Finestra di un layout. id 1 = vista generale del foglio, obbligatoria per ogni layout.
void Writer2000::viewport(const ViewportEntity* vp, const Layout& layout, unsigned owner, int id, LayoutOut* out)
{
    const Vec2 paperCenter = layout.origin + Vec2{layout.paperWidth / 2.0, layout.paperHeight / 2.0};
    const Vec2 center = vp ? vp->center : paperCenter;
    const double w = vp ? vp->width : layout.paperWidth * 1.1;
    const double h = vp ? vp->height : layout.paperHeight * 1.1;
    const Vec2 viewCenter = vp ? vp->viewCenter : paperCenter;
    const double viewHeight = vp ? vp->viewHeight : h;

    const unsigned handle = next();
    if (id == 1 && out)
        out->mainViewport = handle;
    const bool clipped = vp && vp->clip.size() >= 3;
    const unsigned clipHandle = clipped ? next() : 0;
    pair(0, "VIEWPORT");
    pair(5, hex(handle));
    pair(330, hex(owner));
    pair(100, "AcDbEntity");
    pair(67, 1);
    text(8, vp ? layerName(vp->layer() >= 0 ? vp->layer() : 0) : std::string("0"));
    if (vp && vp->color() >= 0 && vp->color() != kColorByLayer)
        pair(62, vp->color());
    pair(100, "AcDbViewport");
    point(10, center);
    pair(40, w);
    pair(41, h);
    pair(68, id);   // stato: >0 = acceso (ordine)
    pair(69, id);
    pair(12, viewCenter.x);
    pair(22, viewCenter.y);
    pair(13, 0.0); pair(23, 0.0);
    pair(14, 10.0); pair(24, 10.0);
    pair(15, 10.0); pair(25, 10.0);
    pair(16, 0.0); pair(26, 0.0); pair(36, 1.0);
    pair(17, 0.0); pair(27, 0.0); pair(37, 0.0);
    pair(42, 50.0);
    pair(43, 0.0); pair(44, 0.0);
    pair(45, viewHeight);
    pair(50, 0.0); pair(51, 0.0);
    pair(72, 1000);
    if (vp)
        for (int layer : vp->frozenLayers)
            if (layer >= 0 && layer < static_cast<int>(layerHandles_.size()))
                pair(forLibreDwg ? 341 : 331, hex(layerHandles_[static_cast<std::size_t>(layer)]));
    pair(90, id == 1 ? 557088 : ((vp && vp->locked ? 16384 : 0) | (clipped ? 65536 : 0)));
    if (clipped)
        pair(340, hex(clipHandle));
    pair(1, "");
    pair(281, 0);
    pair(71, 0);
    pair(74, 0);
    pair(110, 0.0); pair(120, 0.0); pair(130, 0.0);
    pair(111, 1.0); pair(121, 0.0); pair(131, 0.0);
    pair(112, 0.0); pair(122, 1.0); pair(132, 0.0);
    pair(79, 0);
    pair(146, 0.0);

    // Contorno della finestra non rettangolare: una polilinea chiusa legata alla finestra.
    if (clipped) {
        pair(0, "LWPOLYLINE");
        pair(5, hex(clipHandle));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(handle));
        pair(102, "}");
        pair(330, hex(owner));
        pair(100, "AcDbEntity");
        pair(67, 1);
        text(8, layerName(vp->layer() >= 0 ? vp->layer() : 0));
        pair(100, "AcDbPolyline");
        pair(90, vp->clip.size());
        pair(70, 1);
        pair(43, 0.0);
        for (Vec2 v : vp->clip) {
            pair(10, v.x);
            pair(20, v.y);
        }
    }
}

// Oggetti di uno spazio: per un layout prima la vista generale, poi le finestre e gli altri oggetti.
void Writer2000::spaceContent(int space, unsigned owner, bool paper, LayoutOut* layout)
{
    if (paper && layout && layout->layout) {
        viewport(nullptr, *layout->layout, owner, 1, layout);
        int id = 2;
        for (const auto& e : doc_.entities())
            if (e->space() == space && e->type() == EntityType::Viewport)
                viewport(static_cast<const ViewportEntity*>(e.get()), *layout->layout, owner, id++, nullptr);
    }
    for (const auto& e : doc_.entities())
        if ((e->space() < 0 ? 0 : e->space()) == space && e->type() != EntityType::Viewport)
            entityHandles_[e->id()] = entity(*e, owner, paper);
}

// ---------------------------------------------------------------------- blocchi

void Writer2000::blocks()
{
    pair(0, "SECTION");
    pair(2, "BLOCKS");
    // Ordine: spazi modello e carta, poi i blocchi con gli annidati prima di chi li usa, poi quote e tabelle.
    // Per gli altri CAD l'ordine è indifferente; LibreDWG (dxf2dwg) invece perde un blocco annidato definito dopo il
    // blocco che lo usa (il cartiglio associato di un riferimento esterno arrivava vuoto nel DWG).
    std::vector<const BlockRecord*> ordered;
    std::map<std::string, const BlockRecord*> byName;
    for (const BlockRecord& r : records_)
        if (r.block)
            byName[r.name] = &r;
    std::set<const BlockRecord*> done, visiting;
    std::function<void(const BlockRecord*)> visit = [&](const BlockRecord* r) {
        if (done.count(r) || visiting.count(r))
            return;
        visiting.insert(r);
        for (const auto& child : r->block->entities)
            if (child->type() == EntityType::Insert)
                if (const auto it = byName.find(static_cast<const InsertEntity&>(*child).blockName); it != byName.end())
                    visit(it->second);
        visiting.erase(r);
        done.insert(r);
        ordered.push_back(r);
    };
    for (const BlockRecord& r : records_)
        if (!r.block && !r.dim && !r.table)
            ordered.push_back(&r);
    for (const BlockRecord& r : records_)
        if (r.block)
            visit(&r);
    for (const BlockRecord& r : records_)
        if (!r.block && (r.dim || r.table))
            ordered.push_back(&r);
    for (const BlockRecord* record : ordered) {
        const BlockRecord& r = *record;
        const bool paper = r.name.rfind("*Paper_Space", 0) == 0;
        bool hasAttDefs = false;
        if (r.block)
            for (const auto& child : r.block->entities)
                hasAttDefs = hasAttDefs || child->type() == EntityType::AttDef;

        pair(0, "BLOCK");
        pair(5, hex(next()));
        pair(330, hex(r.handle));
        pair(100, "AcDbEntity");
        if (paper)
            pair(67, 1);
        pair(8, "0");
        pair(100, "AcDbBlockBegin");
        text(2, r.name);
        const bool anonymous = !r.name.empty() && r.name.front() == '*' && !paper && r.name.rfind("*Model", 0) != 0;
        const bool xref = r.block && r.block->isXref();
        pair(70, (anonymous ? 1 : 0) | (hasAttDefs ? 2 : 0)
                     | (xref ? 4 | (r.block->xrefOverlay ? 8 : 0) | (r.block->xrefLoaded ? 32 : 0) : 0));
        point(10, r.block ? r.block->base : Vec2{});
        text(3, r.name);
        text(1, xref ? r.block->xrefPath : std::string());

        if (xref) {
            // Riferimento esterno: solo il percorso, gli oggetti restano nel file esterno.
        } else if (r.block) {
            for (const auto& child : r.block->entities)
                entity(*child, r.handle);
        } else if (r.table) {
            for (const auto& part : r.table->geometry())
                entity(*part, r.handle);
        } else if (r.dim) {
            for (const auto& part : r.dim->geometry()) {
                part->setLayer(0);
                part->setColor(kColorByBlock);
                entity(*part, r.handle);
            }
        } else if (r.layout) {
            for (LayoutOut& l : layouts_)
                if (l.layout == r.layout)
                    spaceContent(r.layout->id, r.handle, true, &l);
        }

        pair(0, "ENDBLK");
        pair(5, hex(next()));
        pair(330, hex(r.handle));
        pair(100, "AcDbEntity");
        if (paper)
            pair(67, 1);
        pair(8, "0");
        pair(100, "AcDbBlockEnd");
    }
    pair(0, "ENDSEC");
}

// ---------------------------------------------------------------------- oggetti

void Writer2000::plotSettings(const PlotSettings& s, Vec2 imageOrigin)
{
    pair(100, "AcDbPlotSettings");
    text(1, s.pageSetupName);
    text(2, s.device);
    text(4, s.mediaName);
    text(6, s.viewName);
    pair(40, s.marginLeft); pair(41, s.marginBottom); pair(42, s.marginRight); pair(43, s.marginTop);
    pair(44, s.paperWidth);
    pair(45, s.paperHeight);
    pair(46, s.plotOrigin.x); pair(47, s.plotOrigin.y);
    pair(48, s.windowMin.x); pair(49, s.windowMin.y);
    pair(140, s.windowMax.x); pair(141, s.windowMax.y);
    pair(142, s.customPaper); pair(143, s.customDrawing);
    pair(70, s.flags);
    pair(72, s.paperUnits);
    pair(73, s.rotation);
    pair(74, static_cast<int>(s.plotType));
    text(7, s.styleSheet);
    pair(75, s.standardScaleType);
    // 76...78 (ombreggiatura) non esistono nel formato R2000.
    pair(147, s.standardScaleFactor);
    pair(148, imageOrigin.x); pair(149, imageOrigin.y);
}

void Writer2000::layoutObject(const LayoutOut& l)
{
    const bool model = l.layout == nullptr;
    const double w = model ? 420.0 : l.layout->paperWidth;
    const double h = model ? 297.0 : l.layout->paperHeight;
    const double m = model ? 0.0 : l.layout->margin;

    pair(0, "LAYOUT");
    pair(5, hex(l.handle));
    pair(330, hex(kLayoutDict));
    // Origine di stampa: lo 0,0 dello spazio carta rispetto all'area stampabile (il foglio può non partire da 0,0).
    const Vec2 o = model ? Vec2{} : l.layout->origin;
    if (model)
        plotSettings(doc_.modelPlot(), doc_.modelPlot().paperImageOrigin);
    else
        plotSettings(layoutPlotSettings(*l.layout), {-(o.x + m), -(o.y + m)});
    pair(100, "AcDbLayout");
    text(1, l.name);
    pair(70, 1);
    pair(71, l.tabOrder);
    // Limiti: quelli del disegno per il modello, il foglio per i layout.
    const Vec2 lmin = model ? doc_.limitsMin() : o;
    const Vec2 lmax = model ? doc_.limitsMax() : o + Vec2{w, h};
    pair(10, lmin.x); pair(20, lmin.y);
    pair(11, lmax.x); pair(21, lmax.y);
    pair(12, 0.0); pair(22, 0.0); pair(32, 0.0);
    pair(14, 1e20); pair(24, 1e20); pair(34, 1e20);
    pair(15, -1e20); pair(25, -1e20); pair(35, -1e20);
    pair(146, 0.0);
    pair(13, 0.0); pair(23, 0.0); pair(33, 0.0);
    pair(16, 1.0); pair(26, 0.0); pair(36, 0.0);
    pair(17, 0.0); pair(27, 1.0); pair(37, 0.0);
    pair(76, 1);
    pair(330, hex(l.recordHandle));
    if (l.mainViewport)
        pair(331, hex(l.mainViewport));
}

void Writer2000::objects()
{
    pair(0, "SECTION");
    pair(2, "OBJECTS");

    const auto dictionary = [this](unsigned handle, unsigned owner,
                                   const std::vector<std::pair<std::string, unsigned>>& entries) {
        pair(0, "DICTIONARY");
        pair(5, hex(handle));
        pair(330, hex(owner));
        pair(100, "AcDbDictionary");
        pair(280, 0);
        pair(281, 1);
        for (const auto& [name, h] : entries) {
            text(3, name);
            pair(350, hex(h));
        }
    };

    std::vector<std::pair<std::string, unsigned>> rootEntries = {
        {"ACAD_COLOR", kColorDict},           {"ACAD_GROUP", kGroupDict},
        {"ACAD_LAYOUT", kLayoutDict},         {"ACAD_MLINESTYLE", kMlineDict},
        {"ACAD_PLOTSETTINGS", kPlotSettingsDict}, {"ACAD_PLOTSTYLENAME", kPlotStyleDict},
        {"ACAD_SCALELIST", kScaleListDict},   {"ACAD_TABLESTYLE", kTableStyleDict}};
    const unsigned imageDict = imageDefs_.empty() ? 0 : next();
    if (imageDict)
        rootEntries.insert(rootEntries.begin() + 2, {"ACAD_IMAGE_DICT", imageDict});
    // Stati dei layer di CartesioCAD: dizionario proprio (gli altri CAD lo conservano e lo ignorano).
    const unsigned statesDict = doc_.layerStates().empty() ? 0 : next();
    if (statesDict)
        rootEntries.emplace_back("CARTESIO_LAYERSTATES", statesDict);
    // Stili delle direttrici di CartesioCAD (MLEADERSTYLE), allo stesso modo.
    const unsigned leaderStylesDict = doc_.mleaderStyles().empty() ? 0 : next();
    if (leaderStylesDict)
        rootEntries.emplace_back("CARTESIO_MLEADERSTYLES", leaderStylesDict);
    // Scale di annotazione (lista, corrente, ANNOALLVISIBLE): il DXF 2000 non ha gli oggetti SCALE.
    const unsigned scalesRecord = next();
    rootEntries.emplace_back("CARTESIO_ANNOSCALES", scalesRecord);
    // Gruppi delle schede dei layout (1 nome del layout, 2 gruppo).
    bool anyGroup = false;
    for (const Layout& l : doc_.layouts())
        anyGroup = anyGroup || !l.group.empty();
    const unsigned groupsRecord = anyGroup ? next() : 0;
    if (groupsRecord)
        rootEntries.emplace_back("CARTESIO_LAYOUTGROUPS", groupsRecord);
    // File di progetto dell'impianto collegato (1 percorso).
    const unsigned plantRecord = doc_.plantFile().empty() ? 0 : next();
    if (plantRecord)
        rootEntries.emplace_back("CARTESIO_IMPIANTO", plantRecord);
    // Canaline portacavi: per ognuna 2 codice e lo stato JSON di CanaloCAD in pezzi da 250 caratteri (1).
    const unsigned canaloRecord = doc_.canaloTrays().empty() && doc_.canaloTabProject().empty() ? 0 : next();
    if (canaloRecord)
        rootEntries.emplace_back("CARTESIO_CANALO", canaloRecord);
    dictionary(kRootDict, 0, rootEntries);
    if (canaloRecord) {
        pair(0, "XRECORD");
        pair(5, hex(canaloRecord));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(kRootDict));
        pair(102, "}");
        pair(330, hex(kRootDict));
        pair(100, "AcDbXrecord");
        pair(280, 1);
        // Più il progetto della scheda CanaloCAD, con il codice riservato.
        std::vector<std::pair<std::string, const std::string*>> records;
        for (const auto& [id, json] : doc_.canaloTrays())
            records.emplace_back(id, &json);
        if (!doc_.canaloTabProject().empty())
            records.emplace_back(Document::kCanaloTabProject, &doc_.canaloTabProject());
        for (const auto& [id, jsonPtr] : records) {
            std::string json = *jsonPtr;
            std::replace(json.begin(), json.end(), '\n', ' ');
            std::replace(json.begin(), json.end(), '\r', ' ');
            text(2, id);
            // Pezzi di caratteri interi lunghi al massimo 250 una volta scritti (un carattere non ASCII diventa
            // \U+XXXX, 7 caratteri): le righe dei DXF non superano i 255.
            std::size_t i = 0;
            while (i < json.size()) {
                std::size_t n = 0, written = 0;
                while (i + n < json.size()) {
                    std::size_t len = 1;
                    const auto lead = static_cast<unsigned char>(json[i + n]);
                    if (lead >= 0xC0)
                        while (i + n + len < json.size() && (static_cast<unsigned char>(json[i + n + len]) & 0xC0) == 0x80)
                            ++len;
                    const std::size_t cost = lead < 0x80 ? 1 : 7;
                    if (written + cost > 250)
                        break;
                    written += cost;
                    n += len;
                }
                text(1, json.substr(i, n));
                i += n;
            }
        }
    }
    if (plantRecord) {
        pair(0, "XRECORD");
        pair(5, hex(plantRecord));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(kRootDict));
        pair(102, "}");
        pair(330, hex(kRootDict));
        pair(100, "AcDbXrecord");
        pair(280, 1);
        text(1, doc_.plantFile());
        if (doc_.plantMetersPerUnit() > 0.0)
            pair(40, doc_.plantMetersPerUnit());
    }
    if (groupsRecord) {
        pair(0, "XRECORD");
        pair(5, hex(groupsRecord));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(kRootDict));
        pair(102, "}");
        pair(330, hex(kRootDict));
        pair(100, "AcDbXrecord");
        pair(280, 1);
        for (const Layout& l : doc_.layouts())
            if (!l.group.empty()) {
                text(1, l.name);
                text(2, l.group);
            }
    }
    pair(0, "XRECORD");
    pair(5, hex(scalesRecord));
    pair(102, "{ACAD_REACTORS");
    pair(330, hex(kRootDict));
    pair(102, "}");
    pair(330, hex(kRootDict));
    pair(100, "AcDbXrecord");
    pair(280, 1);
    text(300, doc_.annotationScale());
    pair(290, doc_.annoAllVisible() ? 1 : 0);
    for (const AnnoScale& s : doc_.annoScales()) {
        text(1, s.name);
        pair(40, s.paperUnits);
        pair(41, s.drawingUnits);
    }
    if (leaderStylesDict) {
        std::vector<std::pair<std::string, unsigned>> entries;
        for (const auto& s : doc_.mleaderStyles())
            entries.emplace_back(s.name, next());
        dictionary(leaderStylesDict, kRootDict, entries);
        for (std::size_t i = 0; i < doc_.mleaderStyles().size(); ++i) {
            const auto& s = doc_.mleaderStyles()[i];
            pair(0, "XRECORD");
            pair(5, hex(entries[i].second));
            pair(102, "{ACAD_REACTORS");
            pair(330, hex(leaderStylesDict));
            pair(102, "}");
            pair(330, hex(leaderStylesDict));
            pair(100, "AcDbXrecord");
            pair(280, 1);
            text(1, s.textStyle);
            pair(40, s.textHeight);
            pair(41, s.arrowSize);
            pair(42, s.landingGap);
            pair(70, s.name == doc_.currentMLeaderStyle() ? 1 : 0);
            pair(290, s.annotative ? 1 : 0);
        }
    }
    if (statesDict) {
        std::vector<std::pair<std::string, unsigned>> entries;
        for (const auto& s : doc_.layerStates())
            entries.emplace_back(s.name, next());
        dictionary(statesDict, kRootDict, entries);
        for (std::size_t i = 0; i < doc_.layerStates().size(); ++i) {
            const auto& s = doc_.layerStates()[i];
            pair(0, "XRECORD");
            pair(5, hex(entries[i].second));
            pair(102, "{ACAD_REACTORS");
            pair(330, hex(statesDict));
            pair(102, "}");
            pair(330, hex(statesDict));
            pair(100, "AcDbXrecord");
            pair(280, 1);
            text(1, s.description);
            text(2, s.currentLayer);
            for (const Layer& l : s.layers) {
                text(8, l.name);
                pair(70, (l.visible ? 0 : 1) | (l.frozen ? 2 : 0) | (l.locked ? 4 : 0) | (l.plot ? 0 : 8));
                pair(62, l.color);
                text(6, l.linetype);
                pair(370, l.lineweight);
                if (l.trueColor >= 0)
                    pair(420, l.trueColor);
            }
        }
    }
    // Immagini raster: dizionario con un nome per file e le definizioni (IMAGEDEF) con il percorso.
    if (imageDict) {
        std::vector<std::pair<std::string, unsigned>> entries;
        std::set<std::string> used;
        for (const auto& [path, h] : imageDefs_) {
            std::string name = path.substr(path.find_last_of("/\\") + 1);
            name = name.substr(0, name.find_last_of('.'));
            if (name.empty())
                name = "Immagine";
            std::string unique = name;
            for (int n = 2; used.count(unique); ++n)
                unique = name + "_" + std::to_string(n);
            used.insert(unique);
            entries.emplace_back(unique, h);
        }
        dictionary(imageDict, kRootDict, entries);
        for (const auto& [path, h] : imageDefs_) {
            const Vec2 size = imageSizes_[path];
            pair(0, "IMAGEDEF");
            pair(5, hex(h));
            pair(102, "{ACAD_REACTORS");
            pair(330, hex(imageDict));
            pair(102, "}");
            pair(330, hex(imageDict));
            pair(100, "AcDbRasterImageDef");
            pair(90, 0);
            text(1, path);
            pair(10, size.x);
            pair(20, size.y);
            pair(11, 1.0);
            pair(21, 1.0);
            pair(280, 1);
            pair(281, 0);
        }
    }
    // Ritagli dei blocchi: dizionario esteso dell'inserimento -> ACAD_FILTER -> SPATIAL -> SPATIAL_FILTER (punti
    // nel disegno, poi la matrice che li porta nelle coordinate del blocco e quella del contorno).
    for (const ClipRecord& c : clips_) {
        const unsigned filters = next(), filter = next();
        dictionary(c.xdict, c.insert, {{"ACAD_FILTER", filters}});
        dictionary(filters, c.xdict, {{"SPATIAL", filter}});
        const Affine a = c.entity->affine();
        const double det = a.m11 * a.m22 - a.m12 * a.m21;
        pair(0, "SPATIAL_FILTER");
        pair(5, hex(filter));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(filters));
        pair(102, "}");
        pair(330, hex(filters));
        pair(100, "AcDbFilter");
        pair(100, "AcDbSpatialFilter");
        pair(70, c.entity->clip.size());
        for (Vec2 p : c.entity->clip) {
            const Vec2 w = a.apply(p);
            pair(10, w.x);
            pair(20, w.y);
        }
        pair(210, 0.0); pair(220, 0.0); pair(230, 1.0);
        pair(11, 0.0); pair(21, 0.0); pair(31, 0.0);
        pair(71, c.entity->clipOn ? 1 : 0);
        pair(72, 0);
        pair(73, 0);
        // Inversa dell'inserimento (disegno -> blocco), righe da 4.
        const double i11 = a.m22 / det, i12 = -a.m21 / det, i21 = -a.m12 / det, i22 = a.m11 / det;
        const double itx = -(i11 * a.dx + i12 * a.dy), ity = -(i21 * a.dx + i22 * a.dy);
        for (double v : {i11, i12, 0.0, itx, i21, i22, 0.0, ity, 0.0, 0.0, 1.0, 0.0})
            pair(40, v);
        for (double v : {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0})
            pair(40, v);
    }
    dictionary(kColorDict, kRootDict, {});
    // Gruppi: voci del dizionario ACAD_GROUP e oggetti GROUP con gli handle dei loro oggetti.
    std::vector<std::pair<std::string, unsigned>> groupEntries;
    for (const auto& g : doc_.groups())
        groupEntries.emplace_back(g.name, next());
    dictionary(kGroupDict, kRootDict, groupEntries);
    for (std::size_t i = 0; i < doc_.groups().size(); ++i) {
        const auto& g = doc_.groups()[i];
        pair(0, "GROUP");
        pair(5, hex(groupEntries[i].second));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(kGroupDict));
        pair(102, "}");
        pair(330, hex(kGroupDict));
        pair(100, "AcDbGroup");
        text(300, g.description);
        pair(70, !g.name.empty() && g.name.front() == '*' ? 1 : 0);
        pair(71, g.selectable ? 1 : 0);
        for (EntityId id : g.ids)
            if (const auto it = entityHandles_.find(id); it != entityHandles_.end())
                pair(340, hex(it->second));
    }
    std::vector<std::pair<std::string, unsigned>> layoutEntries;
    for (const LayoutOut& l : layouts_)
        layoutEntries.emplace_back(l.name, l.handle);
    dictionary(kLayoutDict, kRootDict, layoutEntries);
    dictionary(kMlineDict, kRootDict, {{"Standard", mlineStyle_}});
    // Impostazioni di pagina con nome: oggetti PLOTSETTINGS.
    std::vector<std::pair<std::string, unsigned>> setupEntries;
    for (const auto& s : doc_.pageSetups())
        setupEntries.emplace_back(s.pageSetupName, next());
    dictionary(kPlotSettingsDict, kRootDict, setupEntries);
    for (std::size_t i = 0; i < doc_.pageSetups().size(); ++i) {
        pair(0, "PLOTSETTINGS");
        pair(5, hex(setupEntries[i].second));
        pair(102, "{ACAD_REACTORS");
        pair(330, hex(kPlotSettingsDict));
        pair(102, "}");
        pair(330, hex(kPlotSettingsDict));
        const PlotSettings& s = doc_.pageSetups()[i];
        plotSettings(s, s.paperImageOrigin);
    }

    // Stili di stampa: dizionario con valore predefinito "Normal".
    pair(0, "ACDBDICTIONARYWDFLT");
    pair(5, hex(kPlotStyleDict));
    pair(330, hex(kRootDict));
    pair(100, "AcDbDictionary");
    pair(281, 1);
    pair(3, "Normal");
    pair(350, hex(kPlotStylePlaceholder));
    pair(100, "AcDbDictionaryWithDefault");
    pair(340, hex(kPlotStylePlaceholder));
    pair(0, "ACDBPLACEHOLDER");
    pair(5, hex(kPlotStylePlaceholder));
    pair(330, hex(kPlotStyleDict));

    dictionary(kScaleListDict, kRootDict, {});
    dictionary(kTableStyleDict, kRootDict, {});

    pair(0, "MLINESTYLE");
    pair(5, hex(mlineStyle_));
    pair(330, hex(kMlineDict));
    pair(100, "AcDbMlineStyle");
    pair(2, "Standard");
    pair(70, 0);
    pair(3, "");
    pair(62, 256);
    pair(51, 90.0);
    pair(52, 90.0);
    pair(71, 2);
    pair(49, 0.5);
    pair(62, 256);
    pair(6, "BYLAYER");
    pair(49, -0.5);
    pair(62, 256);
    pair(6, "BYLAYER");

    for (const LayoutOut& l : layouts_)
        layoutObject(l);

    pair(0, "ENDSEC");
}

void Writer2000::header()
{
    const BBox ext = doc_.extents(0);
    const Vec2 lo = ext.isValid() ? ext.min : Vec2{};
    const Vec2 hi = ext.isValid() ? ext.max : Vec2{420.0, 297.0};
    std::ostringstream h;
    h.imbue(std::locale::classic());
    h << std::setprecision(15);
    const auto var = [&h](const char* name) { h << "9\n" << name << '\n'; };
    h << "0\nSECTION\n2\nHEADER\n";
    var("$ACADVER");     h << "1\nAC1015\n";
    var("$ACADMAINTVER"); h << "70\n6\n";
    var("$DWGCODEPAGE"); h << "3\nANSI_1252\n";
    var("$INSBASE");     h << "10\n0.0\n20\n0.0\n30\n0.0\n";
    var("$EXTMIN");      h << "10\n" << lo.x << "\n20\n" << lo.y << "\n30\n0.0\n";
    var("$EXTMAX");      h << "10\n" << hi.x << "\n20\n" << hi.y << "\n30\n0.0\n";
    var("$LIMMIN");      h << "10\n" << doc_.limitsMin().x << "\n20\n" << doc_.limitsMin().y << '\n';
    var("$LIMMAX");      h << "10\n" << doc_.limitsMax().x << "\n20\n" << doc_.limitsMax().y << '\n';
    var("$ORTHOMODE");   h << "70\n0\n";
    var("$FILLMODE");    h << "70\n1\n";
    var("$MIRRTEXT");    h << "70\n0\n";
    var("$LTSCALE");     h << "40\n" << doc_.ltscale() << '\n';
    var("$ATTMODE");     h << "70\n1\n";
    var("$TEXTSIZE");    h << "40\n2.5\n";
    var("$TEXTSTYLE");
    h << "7\n"
      << escapeNonAscii(doc_.findTextStyle(doc_.currentTextStyle()) ? doc_.currentTextStyle() : std::string("Standard"))
      << '\n';
    var("$CLAYER");      h << "8\n" << escapeNonAscii(layerName(doc_.currentLayer())) << '\n';
    {
        const std::string celtype = linetypeName(doc_.currentLinetype());
        var("$CELTYPE");     h << "6\n" << escapeNonAscii(celtype.empty() ? std::string("ByLayer") : celtype) << '\n';
    }
    var("$CELWEIGHT");   h << "370\n" << doc_.currentLineweight() << '\n';
    var("$CECOLOR");     h << "62\n" << doc_.currentColor() << '\n';
    {
        const DimStyle& ds = doc_.currentDimStyle();
        var("$DIMSTYLE");    h << "2\n" << escapeNonAscii(ds.name) << '\n';
        var("$DIMSCALE");    h << "40\n" << ds.scale << '\n';
        var("$DIMASZ");      h << "40\n" << ds.arrowSize << '\n';
        var("$DIMTXT");      h << "40\n" << ds.textHeight << '\n';
        var("$DIMDEC");      h << "70\n" << ds.decimals << '\n';
    }
    var("$LUNITS");      h << "70\n2\n";
    var("$LUPREC");      h << "70\n4\n";
    var("$AUNITS");      h << "70\n0\n";
    var("$AUPREC");      h << "70\n2\n";
    var("$INSUNITS");    h << "70\n" << (doc_.insUnits() > 0 ? doc_.insUnits() : 4) << '\n';
    var("$MEASUREMENT"); h << "70\n1\n";
    var("$TILEMODE");    h << "70\n1\n";
    var("$PSTYLEMODE");  h << "290\n1\n";
    var("$HANDSEED");    h << "5\n" << hex(handle_ + 1) << '\n';
    h << "0\nENDSEC\n";
    out_ << h.str();
}

void Writer2000::write()
{
    // Layout: "Model" più i layout del disegno; il primo layout è quello attivo (*Paper_Space).
    mlineStyle_ = next();
    modelSpace_ = next();
    paperSpace_ = next();
    layouts_.push_back({nullptr, "Model", next(), modelSpace_, 0, 0});
    records_.push_back({"*Model_Space", modelSpace_, nullptr, nullptr, nullptr, layouts_.back().handle});

    const auto& docLayouts = doc_.layouts();
    for (std::size_t i = 0; i < docLayouts.size(); ++i) {
        const bool active = i == 0;
        const unsigned record = active ? paperSpace_ : next();
        layouts_.push_back({&docLayouts[i], docLayouts[i].name, next(), record, 0, static_cast<int>(i) + 1});
        records_.push_back({active ? "*Paper_Space" : "*Paper_Space" + std::to_string(i - 1), record, nullptr, nullptr,
                            active ? nullptr : &docLayouts[i], layouts_.back().handle});
    }
    if (docLayouts.empty())   // il formato vuole sempre almeno un foglio
        records_.push_back({"*Paper_Space", paperSpace_, nullptr, nullptr, nullptr, 0});

    for (const auto& b : doc_.blocks())
        if (b->name.rfind("*D", 0) != 0)   // i blocchi delle quote vengono rigenerati
            records_.push_back({b->name, next(), b.get(), nullptr, nullptr, 0});

    const auto collect = [this](const Entity& e) {
        if (e.type() == EntityType::Dimension) {
            const auto* d = static_cast<const DimensionEntity*>(&e);
            const std::string name = "*D" + std::to_string(dimBlocks_.size() + 1);
            dimBlocks_.emplace(d, name);
            records_.push_back({name, next(), nullptr, d, nullptr, 0});
        }
        if (e.type() == EntityType::Table) {
            const auto* t = static_cast<const TableEntity*>(&e);
            std::string name;
            int n = static_cast<int>(tableBlocks_.size()) + 1;
            do
                name = "*U" + std::to_string(9000 + n++);
            while (doc_.findBlock(name));
            tableBlocks_.emplace(t, name);
            records_.push_back({name, next(), nullptr, nullptr, nullptr, 0, t});
        }
    };
    for (const auto& e : doc_.entities())
        collect(*e);
    for (const auto& b : doc_.blocks())
        for (const auto& e : b->entities)
            collect(*e);

    classes();
    tables();
    blocks();

    pair(0, "SECTION");
    pair(2, "ENTITIES");
    spaceContent(0, modelSpace_, false, nullptr);
    if (!docLayouts.empty())
        spaceContent(docLayouts.front().id, paperSpace_, true, &layouts_[1]);
    pair(0, "ENDSEC");

    objects();
    pair(0, "EOF");

    header();   // contiene il primo handle libero: va calcolata per ultima
    out_ << body_.str();
    if (warnings_ && !renamedTags_.empty()) {
        std::string list;
        for (const std::string& t : renamedTags_)
            list += (list.empty() ? "" : ", ") + t;
        warnings_->push_back("Nel DWG (LibreDWG) le etichette degli attributi ammettono solo lettere, cifre, - _ $: "
                             "rinominate " + list + ".");
    }
}

} // namespace

void writeDxf2000(const Document& doc, std::ostream& out, std::vector<std::string>* warnings, bool forLibreDwg)
{
    Writer2000 writer(doc, out, warnings);
    writer.forLibreDwg = forLibreDwg;
    writer.write();
}

} // namespace cartesio::detail
