#include "core/io/Dxf.h"

#include "core/io/DxfWriter2000.h"

#include "core/io/DxfWriter2000.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>

namespace cartesio {

namespace {

std::string upperName(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

class DxfStream {
public:
    explicit DxfStream(std::ostream& out) : out_(out) {}

    template <typename T>
    void pair(int code, const T& value)
    {
        out_ << code << '\n' << value << '\n';
    }

    // Le stringhe vengono scritte in ASCII: i caratteri accentati diventano \U+XXXX.
    void pair(int code, const std::string& value)
    {
        out_ << code << '\n' << detail::escapeNonAscii(value) << '\n';
    }

    void point(int baseCode, Vec2 p)
    {
        pair(baseCode, p.x);
        pair(baseCode + 10, p.y);
        pair(baseCode + 20, 0.0);
    }

private:
    std::ostream& out_;
};

// Scrittura in DXF R12 (AC1009): il formato più compatibile. Gli oggetti che R12 non conosce
// vengono convertiti nell'equivalente più vicino (ellissi e spline -> polilinee, MTEXT -> TEXT).
class Writer {
public:
    Writer(const Document& doc, DxfStream& dxf, std::vector<std::string>* warnings)
        : doc_(doc), dxf_(dxf), warnings_(warnings) {}

    void write();

private:
    void header();
    void tables();
    void blocks();
    void entities();
    void entity(const Entity& e, int parentLayer);
    void common(const char* type, const Entity& e, int parentLayer);
    void polyline(const std::vector<PolyVertex>& vertices, bool closed, const Entity& e, int parentLayer);
    void text(const TextEntity& t, int parentLayer);
    // Posizione, altezza, valore e allineamento di ATTDEF e ATTRIB.
    void attributeText(const TextEntity& t)
    {
        dxf_.point(10, t.position);
        dxf_.pair(40, t.height);
        dxf_.pair(1, t.text);
        if (doc_.findTextStyle(t.style))
            dxf_.pair(7, t.style);
        if (t.rotation != 0.0)
            dxf_.pair(50, radToDeg(t.rotation));
        if (t.hAlign != TextEntity::Left || t.vAlign != TextEntity::Baseline) {
            dxf_.pair(72, t.hAlign);
            dxf_.point(11, t.alignPoint);
            dxf_.pair(74, t.vAlign);
        }
    }
    void collectDimensions(const Entity& e);
    std::string layerName(int index) const;

    const Document& doc_;
    DxfStream& dxf_;
    std::vector<std::string>* warnings_;
    std::map<const DimensionEntity*, std::string> dimBlocks_;   // quota -> blocco anonimo generato
    int skippedHatches_ = 0;
};

std::string Writer::layerName(int index) const
{
    const auto& layers = doc_.layers();
    if (index < 0 || index >= static_cast<int>(layers.size()))
        return "0";
    return layers[static_cast<std::size_t>(index)].name;
}

void Writer::common(const char* type, const Entity& e, int parentLayer)
{
    dxf_.pair(0, type);
    dxf_.pair(8, layerName(e.layer() >= 0 ? e.layer() : parentLayer));
    // Tipo di linea (in R12 i nomi sono in maiuscolo); gli spessori non esistono in R12.
    if (!e.linetype().empty()) {
        if (const Linetype* lt = doc_.findLinetype(e.linetype()); lt && lt->name != "Continuous")
            dxf_.pair(6, upperName(lt->name));
        else if (e.linetype() == "ByBlock" || e.linetype() == "BYBLOCK")
            dxf_.pair(6, "BYBLOCK");
        else if (lt)
            dxf_.pair(6, "CONTINUOUS");
    }
    if (e.color() >= 0 && e.color() != kColorByLayer)
        dxf_.pair(62, e.color());
}

void Writer::polyline(const std::vector<PolyVertex>& vertices, bool closed, const Entity& e, int parentLayer)
{
    common("POLYLINE", e, parentLayer);
    dxf_.pair(66, 1);
    dxf_.pair(70, closed ? 1 : 0);
    dxf_.point(10, {0.0, 0.0});
    for (const PolyVertex& v : vertices) {
        common("VERTEX", e, parentLayer);
        dxf_.point(10, v.point);
        if (v.startWidth > 0.0 || v.endWidth > 0.0) {   // spessore del tratto che parte da qui
            dxf_.pair(40, v.startWidth);
            dxf_.pair(41, v.endWidth);
        }
        if (v.bulge != 0.0)
            dxf_.pair(42, v.bulge);
    }
    common("SEQEND", e, parentLayer);
}

void Writer::text(const TextEntity& t, int parentLayer)
{
    common("TEXT", t, parentLayer);
    dxf_.point(10, t.position);
    dxf_.pair(40, t.height);
    dxf_.pair(1, t.text);
    if (t.rotation != 0.0)
        dxf_.pair(50, radToDeg(t.rotation));
    if (t.widthFactor != 1.0)
        dxf_.pair(41, t.widthFactor);
    if (t.oblique != 0.0)
        dxf_.pair(51, radToDeg(t.oblique));
    if (doc_.findTextStyle(t.style))
        dxf_.pair(7, t.style);
    if (t.hAlign != TextEntity::Left || t.vAlign != TextEntity::Baseline) {
        dxf_.pair(72, t.hAlign);
        dxf_.point(11, t.alignPoint);
        dxf_.pair(73, t.vAlign);
    }
}

void Writer::entity(const Entity& e, int parentLayer)
{
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        common("LINE", e, parentLayer);
        dxf_.point(10, line.start);
        dxf_.point(11, line.end);
        break;
    }
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(e);
        common("CIRCLE", e, parentLayer);
        dxf_.point(10, circle.center);
        dxf_.pair(40, circle.radius);
        break;
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        common("ARC", e, parentLayer);
        dxf_.point(10, arc.center);
        dxf_.pair(40, arc.radius);
        dxf_.pair(50, radToDeg(normalizeAngle(arc.startAngle)));
        dxf_.pair(51, radToDeg(normalizeAngle(arc.endAngle)));
        break;
    }
    case EntityType::Point:
        common("POINT", e, parentLayer);
        dxf_.point(10, static_cast<const PointEntity&>(e).position);
        break;
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        polyline(p.vertices, p.closed, e, parentLayer);
        break;
    }
    case EntityType::Ellipse:
    case EntityType::Spline: {
        // R12 non ha ellissi e spline: si salvano come polilinee che le approssimano.
        std::vector<PolyVertex> vertices;
        const auto prims = e.prims();
        for (const Prim& p : prims)
            vertices.push_back({p.a, 0.0});
        if (!prims.empty())
            vertices.push_back({prims.back().b, 0.0});
        polyline(vertices, false, e, parentLayer);
        break;
    }
    case EntityType::Text:
        text(static_cast<const TextEntity&>(e), parentLayer);
        break;
    case EntityType::MText: {
        // Ogni riga diventa un TEXT.
        const auto& m = static_cast<const MTextEntity&>(e);
        const auto lines = m.lines();
        const auto box = m.box();
        const Vec2 down = Vec2::polar(1.0, m.rotation - kPi / 2.0);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            TextEntity line(box[3] + down * (m.height + static_cast<double>(i) * m.lineHeight()), m.height, lines[i],
                            m.rotation);
            line.setLayer(e.layer());
            line.setColor(e.color());
            line.style = m.style;
            text(line, parentLayer);
        }
        break;
    }
    case EntityType::Solid: {
        const auto& s = static_cast<const SolidEntity&>(e);
        common("SOLID", e, parentLayer);
        dxf_.point(10, s.corners[0]);
        dxf_.point(11, s.corners[1]);
        dxf_.point(12, s.corners[3]);   // ordine DXF: 1, 2, 4, 3
        dxf_.point(13, s.corners[2]);
        break;
    }
    case EntityType::Hatch:
        ++skippedHatches_;
        break;
    case EntityType::Insert: {
        const auto& in = static_cast<const InsertEntity&>(e);
        common("INSERT", e, parentLayer);
        if (!in.attributes.empty())
            dxf_.pair(66, 1);
        dxf_.pair(2, in.blockName);
        dxf_.point(10, in.position);
        if (in.scaleX != 1.0 || in.scaleY != 1.0) {
            dxf_.pair(41, in.scaleX);
            dxf_.pair(42, in.scaleY);
            dxf_.pair(43, 1.0);
        }
        if (in.rotation != 0.0)
            dxf_.pair(50, radToDeg(in.rotation));
        if (!in.attributes.empty()) {
            for (const Attribute& a : in.attributes) {
                common("ATTRIB", a.text, parentLayer);
                attributeText(a.text);
                dxf_.pair(2, a.tag);
                dxf_.pair(70, a.flags);
            }
            common("SEQEND", e, parentLayer);
        }
        break;
    }
    case EntityType::AttDef: {
        const auto& a = static_cast<const AttDefEntity&>(e);
        common("ATTDEF", e, parentLayer);
        attributeText(a.text);
        dxf_.pair(3, a.prompt);
        dxf_.pair(2, a.tag);
        dxf_.pair(70, a.flags);
        break;
    }
    case EntityType::Viewport:
        break;   // i layout non si salvano in R12
    case EntityType::Mline:
    case EntityType::Wipeout:
    case EntityType::Image:   // in R12 le immagini non esistono: resta la cornice
        // In R12 non esistono: si salvano le linee (della copertura il contorno).
        for (const Prim& p : e.prims()) {
            common("LINE", e, parentLayer);
            dxf_.point(10, p.a);
            dxf_.point(11, p.b);
        }
        break;
    case EntityType::Table:
        for (const auto& part : static_cast<const TableEntity&>(e).geometry())
            entity(*part, parentLayer);
        break;
    case EntityType::XLine: {
        // R12 non ha linee di costruzione: un segmento lungo (10 km) nella stessa direzione.
        const auto& x = static_cast<const XLineEntity&>(e);
        const LineEntity line(x.ray ? x.base : x.base - x.direction * 1e7, x.base + x.direction * 1e7);
        common("LINE", e, parentLayer);
        dxf_.point(10, line.start);
        dxf_.point(11, line.end);
        break;
    }
    case EntityType::Leader:
        // In R12 le direttrici non esistono: si salvano linee, freccia e testo.
        for (const auto& part : static_cast<const LeaderEntity&>(e).geometry())
            entity(*part, parentLayer);
        break;
    case EntityType::Dimension: {
        const auto& d = static_cast<const DimensionEntity&>(e);
        const auto it = dimBlocks_.find(&d);
        if (it == dimBlocks_.end())
            break;
        common("DIMENSION", e, parentLayer);
        dxf_.pair(2, it->second);
        dxf_.point(10, d.dimLine);
        dxf_.point(11, d.textPos);
        dxf_.pair(70, d.kind);
        if (!d.textOverride.empty())
            dxf_.pair(1, d.textOverride);
        dxf_.point(13, d.p1);
        dxf_.point(14, d.p2);
        dxf_.point(15, d.curvePoint);
        if (d.rotation != 0.0)
            dxf_.pair(50, radToDeg(d.rotation));
        break;
    }
    }
}

void Writer::collectDimensions(const Entity& e)
{
    if (e.type() == EntityType::Dimension)
        dimBlocks_.emplace(static_cast<const DimensionEntity*>(&e), "*D" + std::to_string(dimBlocks_.size() + 1));
}

void Writer::header()
{
    dxf_.pair(0, "SECTION");
    dxf_.pair(2, "HEADER");
    dxf_.pair(9, "$ACADVER");
    dxf_.pair(1, "AC1009");
    dxf_.pair(9, "$INSUNITS");
    dxf_.pair(70, doc_.insUnits() > 0 ? doc_.insUnits() : 4);   // senza unità: millimetri
    dxf_.pair(0, "ENDSEC");
}

void Writer::tables()
{
    dxf_.pair(0, "SECTION");
    dxf_.pair(2, "TABLES");

    dxf_.pair(0, "TABLE");
    dxf_.pair(2, "LTYPE");
    dxf_.pair(70, doc_.linetypes().size());
    for (const Linetype& lt : doc_.linetypes()) {
        dxf_.pair(0, "LTYPE");
        dxf_.pair(2, upperName(lt.name));
        dxf_.pair(70, 0);
        dxf_.pair(3, lt.pattern.empty() ? std::string("Solid line") : lt.description);
        dxf_.pair(72, 65);
        dxf_.pair(73, lt.pattern.size());
        dxf_.pair(40, lt.length());
        for (double d : lt.pattern)
            dxf_.pair(49, d);
    }
    dxf_.pair(0, "ENDTAB");


    dxf_.pair(0, "TABLE");
    dxf_.pair(2, "STYLE");
    dxf_.pair(70, doc_.textStyles().size());
    for (const TextStyle& s : doc_.textStyles()) {
        dxf_.pair(0, "STYLE");
        dxf_.pair(2, s.name);
        dxf_.pair(70, 0);
        dxf_.pair(40, s.height);
        dxf_.pair(41, s.widthFactor);
        dxf_.pair(50, radToDeg(s.oblique));
        dxf_.pair(71, (s.backwards ? 2 : 0) | (s.upsideDown ? 4 : 0));
        dxf_.pair(42, 2.5);
        dxf_.pair(3, s.font.empty() ? std::string("arial.ttf") : s.font);
        dxf_.pair(4, s.bigFont);
    }
    dxf_.pair(0, "ENDTAB");
    dxf_.pair(0, "TABLE");
    dxf_.pair(2, "LAYER");
    dxf_.pair(70, doc_.layers().size());
    for (const Layer& layer : doc_.layers()) {
        dxf_.pair(0, "LAYER");
        dxf_.pair(2, layer.name);
        dxf_.pair(70, (layer.frozen ? 1 : 0) | (layer.locked ? 4 : 0)
                          | (layer.name.find('|') != std::string::npos ? 16 | 32 : 0));   // layer di un XREF
        dxf_.pair(62, layer.visible ? layer.color : -layer.color);   // colore negativo = layer spento
        const Linetype* lt = doc_.findLinetype(layer.linetype);
        dxf_.pair(6, lt ? upperName(lt->name) : std::string("CONTINUOUS"));
    }
    dxf_.pair(0, "ENDTAB");
    dxf_.pair(0, "ENDSEC");
}

void Writer::blocks()
{
    dxf_.pair(0, "SECTION");
    dxf_.pair(2, "BLOCKS");

    // Definizioni di blocco del disegno (i blocchi delle quote vengono rigenerati).
    for (const auto& b : doc_.blocks()) {
        if (b->name.rfind("*D", 0) == 0)
            continue;
        dxf_.pair(0, "BLOCK");
        dxf_.pair(8, "0");
        dxf_.pair(2, b->name);
        const bool xref = b->isXref();
        dxf_.pair(70, (!b->name.empty() && b->name.front() == '*' ? 1 : 0)
                          | (xref ? 4 | (b->xrefOverlay ? 8 : 0) | (b->xrefLoaded ? 32 : 0) : 0));
        dxf_.point(10, b->base);
        dxf_.pair(3, b->name);
        if (xref)
            dxf_.pair(1, b->xrefPath);   // riferimento esterno: solo il percorso
        else
            for (const auto& e : b->entities)
                entity(*e, 0);
        dxf_.pair(0, "ENDBLK");
        dxf_.pair(8, "0");
    }

    // Un blocco anonimo per ogni quota, con la sua geometria.
    for (const auto& [dim, name] : dimBlocks_) {
        dxf_.pair(0, "BLOCK");
        dxf_.pair(8, "0");
        dxf_.pair(2, name);
        dxf_.pair(70, 1);
        dxf_.point(10, {0.0, 0.0});
        dxf_.pair(3, name);
        for (const auto& part : dim->geometry()) {
            part->setLayer(0);
            part->setColor(kColorByBlock);
            entity(*part, 0);
        }
        dxf_.pair(0, "ENDBLK");
        dxf_.pair(8, "0");
    }
    dxf_.pair(0, "ENDSEC");
}

void Writer::entities()
{
    dxf_.pair(0, "SECTION");
    dxf_.pair(2, "ENTITIES");
    int paper = 0;
    for (const auto& e : doc_.entities()) {
        if (e->space() > 0) {
            ++paper;
            continue;
        }
        entity(*e, 0);
    }
    dxf_.pair(0, "ENDSEC");
    if (paper > 0 && warnings_)
        warnings_->push_back(std::to_string(paper) + " oggetti dei layout non salvati: usare il formato DXF 2000.");
}

void Writer::write()
{
    for (const auto& e : doc_.entities())
        collectDimensions(*e);
    for (const auto& b : doc_.blocks())
        for (const auto& e : b->entities)
            collectDimensions(*e);

    header();
    tables();
    blocks();
    entities();
    dxf_.pair(0, "EOF");

    if (skippedHatches_ > 0 && warnings_)
        warnings_->push_back(std::to_string(skippedHatches_)
                             + " tratteggi non salvati: il formato DXF R12 non li supporta.");
}

} // namespace

bool writeDxf(const Document& doc, const std::filesystem::path& path, std::string* error,
              std::vector<std::string>* warnings, DxfVersion version)
{
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        if (error)
            *error = "Impossibile aprire il file in scrittura.";
        return false;
    }
    file.imbue(std::locale::classic());
    file << std::setprecision(15);

    if (version == DxfVersion::R2000 || version == DxfVersion::R2000ForDwg) {
        detail::writeDxf2000(doc, file, warnings, version == DxfVersion::R2000ForDwg);
    } else {
        DxfStream dxf(file);
        Writer(doc, dxf, warnings).write();
    }

    file.flush();
    if (!file) {
        if (error)
            *error = "Errore durante la scrittura del file.";
        return false;
    }
    return true;
}

} // namespace cartesio
