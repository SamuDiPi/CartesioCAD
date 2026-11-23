#include "core/io/Dxf.h"

#include "core/Geometry.h"
#include "core/HatchBoundary.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <unordered_map>
#include <set>
#include <optional>
#include <utility>
#include <vector>

namespace cartesio {

namespace {

std::string trimmed(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::optional<double> toNumber(const std::string& s)
{
    double value = 0.0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    if (first != last && *first == '+')
        ++first;
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last || !std::isfinite(value))
        return std::nullopt;
    return value;
}

// Numero intero da un valore del file: un valore enorme (2147483648, 1e300) non si può convertire in int senza
// comportamento indefinito, quindi si ferma ai limiti; un valore assente o non numerico dà `fallback`.
int toInt(std::optional<double> v, int fallback)
{
    if (!v)
        return fallback;
    return static_cast<int>(std::clamp(*v, -2147483648.0, 2147483647.0));
}

bool validUtf8(const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + static_cast<std::size_t>(len) > s.size())
            return false;
        for (int k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80)
                return false;
        i += static_cast<std::size_t>(len);
    }
    return true;
}

void appendUtf8(std::string& out, unsigned code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// I DXF fino alla versione 2004 usano la codifica di Windows (ANSI_1252 in Italia), non UTF-8.
std::string cp1252ToUtf8(const std::string& s)
{
    static constexpr unsigned short high[32] = {
        0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    std::string out;
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        appendUtf8(out, c < 0x80 ? c : c < 0xA0 ? high[c - 0x80] : c);
    }
    return out;
}

// Sequenze \U+XXXX usate nei DXF per i caratteri fuori dalla codifica del file.
std::string decodeUnicodeEscapes(const std::string& s)
{
    if (s.find("\\U+") == std::string::npos && s.find("\\u+") == std::string::npos)
        return s;
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 6 < s.size() && (s[i + 1] == 'U' || s[i + 1] == 'u') && s[i + 2] == '+') {
            unsigned code = 0;
            const char* first = s.data() + i + 3;
            const char* last = s.data() + i + 7;
            const auto [ptr, ec] = std::from_chars(first, last, code, 16);
            if (ec == std::errc() && ptr == last) {
                appendUtf8(out, code);
                i += 6;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

using Pair = std::pair<int, std::string>;

// Un oggetto DXF: tipo (codice 0) e coppie codice/valore che lo seguono, nell'ordine del file.
struct Record {
    std::string type;
    std::vector<Pair> values;

    std::optional<std::string> text(int code) const
    {
        for (const auto& [c, v] : values)
            if (c == code)
                return v;
        return std::nullopt;
    }

    std::optional<double> number(int code) const
    {
        const auto t = text(code);
        return t ? toNumber(*t) : std::nullopt;
    }

    double num(int code, double fallback) const { return number(code).value_or(fallback); }
    int integer(int code, int fallback) const { return toInt(number(code), fallback); }

    std::optional<Vec2> point(int code) const
    {
        const auto x = number(code);
        const auto y = number(code + 10);
        if (!x || !y)
            return std::nullopt;
        return Vec2{*x, *y};
    }
};

// Lettura sequenziale dei valori di un record (per strutture ripetute come i contorni dei tratteggi).
class Cursor {
public:
    explicit Cursor(const std::vector<Pair>& v) : v_(v) {}

    // Avanza fino alla prossima coppia con il codice indicato; restituisce il suo valore numerico.
    std::optional<double> next(int code)
    {
        while (i_ < v_.size()) {
            const Pair& p = v_[i_++];
            if (p.first == code)
                return toNumber(p.second);
        }
        return std::nullopt;
    }

    // Legge la coppia successiva solo se ha il codice indicato.
    std::optional<double> optional(int code)
    {
        if (i_ < v_.size() && v_[i_].first == code)
            return toNumber(v_[i_++].second);
        return std::nullopt;
    }

    bool done() const { return i_ >= v_.size(); }

    int integer(int code, int fallback) { return toInt(next(code), fallback); }

    // Numero di elementi che seguono: ognuno occupa almeno una coppia, quindi non può superare le coppie rimaste
    // (un file rovinato che dichiara due miliardi di vertici non fa riempire la memoria).
    int count(int code)
    {
        const int n = integer(code, 0);
        return std::clamp(n, 0, static_cast<int>(std::min<std::size_t>(v_.size() - std::min(i_, v_.size()), 2147483647)));
    }

private:
    const std::vector<Pair>& v_;
    std::size_t i_ = 0;
};

// Spazio provvisorio degli oggetti dello spazio carta attivo, finché non si conoscono i layout.
constexpr int kPendingPaper = 1000000;

bool startsWithNoCase(const std::string& s, const char* prefix)
{
    std::size_t i = 0;
    for (; prefix[i]; ++i)
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

static PlotSettings readPlotSettings(const Record& r);

class Reader {
public:
    Reader(Document& doc, DxfReadResult& result) : doc_(doc), result_(result) {}

    void process(const Record& r, const std::string& section);
    void finish();

private:
    struct LayoutInfo {
        std::string name;
        int tabOrder = 0;
        std::string recordHandle;
        double width = 420.0;
        double height = 297.0;
        std::string paper;
        std::optional<Vec2> limitsMin, limitsMax;   // limiti del layout (10/11 in AcDbLayout)
        double margin = -1.0;                        // margini di stampa (40...43)
        PlotSettings plot;
    };

    void add(std::unique_ptr<Entity> e, const Record& r, bool ocs);
    int layerIndex(const Record& r);

    std::unique_ptr<Entity> readEntity(const Record& r, bool& ocs);
    std::unique_ptr<Entity> readText(const Record& r, int vAlignCode);
    std::unique_ptr<Entity> readMText(const Record& r);
    std::unique_ptr<Entity> readLwPolyline(const Record& r);
    std::unique_ptr<Entity> readSpline(const Record& r);
    std::unique_ptr<Entity> readHatch(const Record& r);
    std::unique_ptr<Entity> readInsert(const Record& r);
    std::unique_ptr<Entity> readDimension(const Record& r);
    std::unique_ptr<Entity> readAttDef(const Record& r);
    std::unique_ptr<Entity> readViewport(const Record& r);
    std::unique_ptr<Entity> readLeader(const Record& r);
    std::unique_ptr<Entity> readWipeout(const Record& r);
    std::unique_ptr<Entity> readImage(const Record& r);
    std::unique_ptr<Entity> readOle(const Record& r);
    std::unique_ptr<Entity> readMline(const Record& r);
    std::unique_ptr<Entity> readTableXdata(const Record& r);
    std::vector<std::unique_ptr<Entity>> readMultiLeader(const Record& r);
    void readLayout(const Record& r);
    void finishPolyline();

    Document& doc_;
    DxfReadResult& result_;

    std::shared_ptr<BlockDef> block_;   // blocco in lettura (sezione BLOCKS)
    bool skipBlock_ = false;            // spazio modello
    bool xrefBlock_ = false;
    int emptyHatches_ = 0;              // tratteggi senza contorno (vuoti in qualsiasi programma)            // riferimento esterno: eventuali oggetti nel file ignorati
    bool paperBlock_ = false;           // *Paper_SpaceN: contenuto di un layout non attivo

    // POLYLINE ... VERTEX ... SEQEND
    std::optional<Record> polyline_;
    std::vector<PolyVertex> polyVertices_;
    // INSERT ... ATTRIB ... SEQEND
    bool inAttributes_ = false;
    InsertEntity* lastInsert_ = nullptr;

    // Layout: registri dei blocchi (handle -> nome), oggetti LAYOUT, contenuto dei layout non attivi.
    std::map<std::string, std::string> blockRecords_;
    std::vector<LayoutInfo> layouts_;
    std::map<std::string, std::vector<std::unique_ptr<Entity>>> paperBlocks_;
    bool hasPendingPaper_ = false;
    std::set<std::string> viewportContainers_;   // layout di cui è già stata vista la prima finestra
    // Finestre avanzate: handle dei layer (congelati per finestra, 331) e contorni di ritaglio (340).
    std::map<std::string, int> layerHandles_;
    std::map<std::string, Entity*> paperHandles_;
    std::vector<std::pair<ViewportEntity*, std::string>> pendingClips_;
    // Direttrici: testo in un MTEXT separato (codice 340).
    std::map<std::string, MTextEntity*> mtextHandles_;
    std::vector<std::pair<LeaderEntity*, std::string>> pendingLeaderTexts_;
    std::set<const LeaderEntity*> leaderStyled_;   // con i dati estesi (lo stile del testo è già giusto)
    // Immagini raster: definizioni (IMAGEDEF, handle -> file) e immagini che le usano (codice 340).
    std::map<std::string, std::string> imageDefs_;
    std::vector<std::pair<ImageEntity*, std::string>> pendingImages_;
    // Ritaglio dei blocchi (XCLIP): inserimento -> dizionario esteso; dizionari -> voci; filtri spaziali.
    std::vector<std::pair<InsertEntity*, std::string>> pendingXClips_;
    std::map<std::string, std::map<std::string, std::string>> dictEntries_;
    struct SpatialFilter {
        std::vector<Vec2> points;
        std::vector<double> numbers40;
        bool display = true;
    };
    std::map<std::string, SpatialFilter> spatialFilters_;
    // Tabelle salvate da CartesioCAD come blocchi: i blocchi della grafica vanno tolti.
    std::set<std::string> tableBlocks_;
    // Gruppi: handle degli oggetti, nomi dal dizionario ACAD_GROUP, oggetti GROUP.
    std::map<std::string, Entity*> entityHandles_;
    std::map<std::string, std::string> sortHandles_;
    std::map<std::string, std::string> dynamicOrigins_;   // blocco anonimo -> handle del blocco dinamico
    std::unordered_map<std::string, const Entity*> blockEntityHandles_;   // SORTENTSTABLE: handle dell'oggetto -> handle d'ordine
    std::map<std::string, std::string> dictionaryNames_;
    struct GroupInfo {
        std::string handle, description;
        bool selectable = true;
        std::vector<std::string> members;
    };
    std::vector<GroupInfo> groups_;
    // Scale di annotazione: oggetti SCALE (handle -> scala, nell'ordine del file), proprietari dei dizionari e
    // dati di contesto degli oggetti annotativi (dizionario esteso -> AcDbContextDataManager ->
    // ACDB_ANNOTATIONSCALES -> un oggetto per scala, con la scala (340) e se è quella della geometria (290)).
    std::map<std::string, AnnoScale> scaleObjects_;
    std::vector<std::string> scaleOrder_;
    std::map<std::string, std::string> dictOwners_;
    struct ContextData {
        std::string owner, scale;
        bool isDefault = false;
    };
    std::vector<ContextData> contextData_;
    std::vector<Entity*> annotativeLoaded_;   // annotativi (XDATA AcadAnnotative) di cui cercare le scale
    std::vector<AnnoScale> savedScales_;      // lista salvata da CartesioCAD (CARTESIO_ANNOSCALES)
    std::string savedCurrentScale_;
    bool savedAllVisible_ = true;
    bool hasSavedScales_ = false;
    void resolveAnnotations();
    std::vector<std::pair<std::string, std::string>> layoutGroups_;   // layout -> gruppo (CARTESIO_LAYOUTGROUPS)
    std::vector<std::pair<LeaderEntity*, std::string>> leaderLegs_;   // frecce in più -> handle della principale

    // Le tabelle qui sopra tengono puntatori agli oggetti letti. In un file rovinato o costruito apposta un
    // blocco può essere ridefinito o non chiuso, e due oggetti possono riferirsi allo stesso testo o contorno:
    // gli oggetti restano quindi vivi fino alla fine della lettura e ognuno si toglie dal disegno una volta sola.
    std::vector<std::shared_ptr<BlockDef>> keptBlocks_;
    std::vector<std::unique_ptr<Entity>> keptEntities_;
    std::set<const Entity*> erased_;
    bool erased(const Entity* e) const { return erased_.count(e) != 0; }
    void eraseOnce(const Entity* e)
    {
        if (e && e->id() != 0 && erased_.insert(e).second)
            doc_.eraseLoaded(e->id());
    }
};

// XDATA che segna un oggetto o uno stile annotativo: "AcadAnnotative" (1070 versione, 1070 1 = annotativo) o
// quello di CartesioCAD (1000 ANNOTATIVE).
bool annotativeXdata(const Record& r)
{
    std::string app;
    std::vector<int> flags;
    for (const auto& [code, value] : r.values) {
        if (code == 1001) {
            app = value;
            continue;
        }
        if (app == "AcadAnnotative" && code == 1070)
            flags.push_back(toInt(toNumber(value), 0));
        if (app == "CARTESIOCAD" && code == 1000 && value == "ANNOTATIVE")
            return true;
    }
    return flags.size() >= 2 && flags[1] == 1;
}

void Reader::resolveAnnotations()
{
    // Lista delle scale: quella di CartesioCAD, altrimenti gli oggetti SCALE del file.
    if (hasSavedScales_ && !savedScales_.empty()) {
        doc_.setAnnoScales(savedScales_);
    } else if (!scaleOrder_.empty()) {
        std::vector<AnnoScale> scales;
        for (const std::string& h : scaleOrder_) {
            const AnnoScale& s = scaleObjects_[h];
            if (std::none_of(scales.begin(), scales.end(), [&](const AnnoScale& x) { return x.name == s.name; }))
                scales.push_back(s);
        }
        doc_.setAnnoScales(std::move(scales));
    }
    // Dati di contesto: risalendo i proprietari (dizionari) si arriva all'oggetto annotativo.
    std::map<Entity*, double> defaults;
    for (const ContextData& c : contextData_) {
        const auto scale = scaleObjects_.find(c.scale);
        if (scale == scaleObjects_.end())
            continue;
        std::string h = c.owner;
        Entity* entity = nullptr;
        for (int depth = 0; depth < 5 && !h.empty() && !entity; ++depth) {
            if (const auto it = entityHandles_.find(h); it != entityHandles_.end())
                entity = it->second;
            else if (const auto o = dictOwners_.find(h); o != dictOwners_.end())
                h = o->second;
            else
                h.clear();
        }
        if (!entity || !canBeAnnotative(entity->type()))
            continue;
        auto names = entity->annoScales();
        if (std::find(names.begin(), names.end(), scale->second.name) == names.end())
            names.push_back(scale->second.name);
        entity->setAnnoScales(std::move(names));
        if (c.isDefault || !defaults.count(entity))
            defaults[entity] = scale->second.factor();
    }
    std::map<std::string, int> votes;   // scala della geometria più usata: diventa la corrente
    for (auto& [entity, factor] : defaults) {
        entity->setAnnoFactor(factor);
        if (const AnnoScale* s = doc_.annoScaleForFactor(factor))
            ++votes[s->name];
    }
    // Annotativi senza dati di contesto: valgono per la scala 1:1 (o la prima della lista).
    for (Entity* e : annotativeLoaded_)
        if (!e->annotative()) {
            const AnnoScale* s = doc_.annoScaleForFactor(1.0);
            e->setAnnoScales({s ? s->name : doc_.annoScales().front().name});
            e->setAnnoFactor(s ? 1.0 : doc_.annoScales().front().factor());
        }
    std::string current = savedCurrentScale_;
    if (current.empty() && !votes.empty())
        current = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    if (!current.empty() || !savedAllVisible_)
        doc_.setLoadedAnnotationScale(current.empty() ? doc_.annotationScale() : current, savedAllVisible_);
    doc_.normalizeAnnotations();
}

int Reader::layerIndex(const Record& r)
{
    const std::string name = r.text(8).value_or("0");
    const int index = doc_.findLayer(name);
    return index >= 0 ? index : doc_.addLayer(Layer{name, 7, true});
}

void Reader::add(std::unique_ptr<Entity> e, const Record& r, bool ocs)
{
    if (!e)
        return;
    // Sistema di coordinate dell'oggetto con estrusione (0,0,-1): specchiato rispetto all'asse Y.
    if (ocs && r.num(230, 1.0) < 0.0) {
        Affine mirror;
        mirror.m11 = -1.0;
        e->transform(mirror);
    }
    e->setLayer(layerIndex(r));
    e->setColor(r.integer(62, kColorByLayer));
    e->setTrueColor(r.integer(420, -1));
    e->setTransparency(r.integer(440, -1));
    e->setLinetype(r.text(6).value_or("ByLayer"));
    e->setLineweight(r.integer(370, kLineweightByLayer));
    e->setLinetypeScale(r.num(48, 1.0));

    lastInsert_ = e->type() == EntityType::Insert ? static_cast<InsertEntity*>(e.get()) : nullptr;
    // Oggetti annotativi: scale e fattore salvati da CartesioCAD, o il segno standard (le scale arrivano dai dati di
    // contesto, alla fine).
    if (!block_ && canBeAnnotative(e->type())) {
        std::string app;
        std::vector<std::string> scales;
        double factor = 0.0;
        for (const auto& [code, value] : r.values) {
            if (code == 1001)
                app = value;
            else if (app == "CARTESIOCAD_ANNO" && code == 1040)
                factor = toNumber(value).value_or(1.0);
            else if (app == "CARTESIOCAD_ANNO" && code == 1000)
                scales.push_back(value);
        }
        if (!scales.empty()) {
            e->setAnnoScales(std::move(scales));
            e->setAnnoFactor(factor);
        } else if (annotativeXdata(r)) {
            annotativeLoaded_.push_back(e.get());
        }
    }
    // Linea di una tratta dell'impianto.
    if (!block_) {
        std::string app;
        for (const auto& [code, value] : r.values) {
            if (code == 1001) {
                app = value;
            } else if (app == "CARTESIOCAD_IMPIANTO" && code == 1000) {
                e->setPlantRun(value);
            } else if (app == "CARTESIOCAD_CANALO" && code == 1000) {
                e->setCanaloTray(value);
            }
        }
    }
    if (e->type() == EntityType::MText)
        if (const auto handle = r.text(5); handle)
            mtextHandles_[*handle] = static_cast<MTextEntity*>(e.get());
    if (!block_ || paperBlock_) {
        if (const auto handle = r.text(5); handle)
            entityHandles_[*handle] = e.get();
    } else if (const auto handle = r.text(5); handle) {
        blockEntityHandles_[*handle] = e.get();   // per l'ordine di visualizzazione dentro i blocchi
    }
    // Oggetti dello spazio carta: possono essere il contorno di una finestra non rettangolare.
    if ((block_ && paperBlock_) || (!block_ && r.integer(67, 0) == 1))
        if (const auto handle = r.text(5); handle)
            paperHandles_[*handle] = e.get();

    if (block_) {
        if (!skipBlock_ && !xrefBlock_)
            block_->entities.push_back(std::move(e));
        return;
    }
    if (r.integer(67, 0) == 1) {   // spazio carta del layout attivo
        e->setSpace(kPendingPaper);
        hasPendingPaper_ = true;
    }
    doc_.insertLoaded(std::move(e));
    ++result_.imported;
}

void Reader::process(const Record& r, const std::string& section)
{
    if (section == "HEADER") {
        std::string variable;
        for (const auto& [code, value] : r.values) {
            if (code == 9) {
                variable = value;
                continue;
            }
            const auto number = toNumber(value);
            if (variable == "$LTSCALE" && code == 40 && number)
                doc_.setLtscale(*number);
            else if (variable == "$INSUNITS" && code == 70 && number)
                doc_.setInsUnits(toInt(number, 0));
            else if ((variable == "$LIMMIN" || variable == "$LIMMAX") && (code == 10 || code == 20) && number) {
                Vec2 lo = doc_.limitsMin(), hi = doc_.limitsMax();
                Vec2& p = variable == "$LIMMIN" ? lo : hi;
                (code == 10 ? p.x : p.y) = *number;
                doc_.setLimits(lo, hi);
            }
            else if (variable == "$CELTYPE" && code == 6)
                doc_.setCurrentLinetype(value);
            else if (variable == "$CELWEIGHT" && code == 370 && number)
                doc_.setCurrentLineweight(toInt(number, 0));
            else if (variable == "$TEXTSTYLE" && code == 7)
                doc_.setCurrentTextStyle(value);
            else if (variable == "$DIMSTYLE" && code == 2)
                doc_.setCurrentDimStyle(value);
        }
        return;
    }
    if (section == "TABLES" && r.type == "DIMSTYLE") {
        const auto name = r.text(2);
        if (!name || name->empty())
            return;
        DimStyle s;
        s.name = *name;
        s.arrowSize = r.num(41, s.arrowSize);
        s.extOffset = r.num(42, s.extOffset);
        s.extExtension = r.num(44, s.extExtension);
        s.textHeight = r.num(140, s.textHeight);
        s.textGap = r.num(147, s.textGap);
        s.scale = r.num(40, 1.0) > 0.0 ? r.num(40, 1.0) : 1.0;
        s.linearFactor = r.num(144, 1.0) != 0.0 ? r.num(144, 1.0) : 1.0;
        s.tickSize = r.num(142, 0.0);
        s.decimals = r.integer(271, 2);
        s.post = r.text(3).value_or("");
        s.annotative = annotativeXdata(r);
        doc_.setDimStyle(std::move(s));
        return;
    }
    if (section == "TABLES" && r.type == "STYLE") {
        const auto name = r.text(2);
        // Senza nome sono le forme caricate per i tipi di linea complessi (flag 1): non sono stili di testo.
        if (!name || name->empty() || (r.integer(70, 0) & 1) != 0)
            return;
        TextStyle s;
        s.name = *name;
        s.font = r.text(3).value_or("");
        s.bigFont = r.text(4).value_or("");
        s.height = r.num(40, 0.0);
        s.widthFactor = r.num(41, 1.0) > 0.0 ? r.num(41, 1.0) : 1.0;
        s.oblique = degToRad(r.num(50, 0.0));
        const int generation = r.integer(71, 0);
        s.backwards = (generation & 2) != 0;
        s.upsideDown = (generation & 4) != 0;
        // XDATA di ACAD: famiglia TrueType (1000) e stile del carattere (1071: corsivo 0x01000000,
        // grassetto 0x02000000).
        bool acad = false;
        for (const auto& [code, value] : r.values) {
            if (code == 1001)
                acad = value == "ACAD";
            else if (acad && code == 1000 && s.family.empty())
                s.family = value;
            else if (acad && code == 1071)
                if (const auto flags = toNumber(value)) {
                    const auto f = static_cast<long long>(*flags);
                    s.italic = (f & 0x01000000) != 0;
                    s.bold = (f & 0x02000000) != 0;
                }
        }
        s.annotative = annotativeXdata(r);
        doc_.setTextStyle(std::move(s));
        return;
    }
    if (section == "TABLES" && r.type == "VIEW") {   // viste con nome
        const auto name = r.text(2);
        if (!name || name->empty())
            return;
        Document::NamedView v;
        v.name = *name;
        v.center = r.point(10).value_or(Vec2{});
        v.height = r.num(40, 0.0);
        v.width = r.num(41, 0.0);
        v.space = (r.integer(70, 0) & 1) != 0 ? -1 : 0;   // spazio carta: il primo layout (si sistema alla fine)
        if (v.width > 0.0 && v.height > 0.0)
            doc_.setNamedView(std::move(v));
        return;
    }
    if (section == "TABLES" && r.type == "LTYPE") {
        const auto name = r.text(2);
        if (!name || name->empty())
            return;
        Linetype lt{*name, r.text(3).value_or(""), {}};
        for (const auto& [code, value] : r.values)
            if (code == 49)
                if (const auto d = toNumber(value))
                    lt.pattern.push_back(*d);
        if (lt.name != "ByBlock" && lt.name != "ByLayer" && lt.name != "BYBLOCK" && lt.name != "BYLAYER")
            doc_.addLinetype(std::move(lt));
        return;
    }
    if (section == "TABLES") {
        if (r.type == "LAYER") {
            const auto name = r.text(2);
            if (!name || name->empty())
                return;
            const int color = r.integer(62, 7);
            const int flags = r.integer(70, 0);
            const int index = doc_.addLayer(Layer{*name, 7, true});
            if (const auto handle = r.text(5); handle)
                layerHandles_[*handle] = index;
            Layer& layer = doc_.layer(index);
            // Colori fuori dall'intervallo 1-255 (anche -2147483648, il cui valore assoluto non esiste) -> bianco/nero.
            layer.color = color != 0 && color > -256 && color < 256 ? std::abs(color) : 7;
            layer.visible = color >= 0;
            layer.frozen = (flags & 1) != 0;
            layer.linetype = r.text(6).value_or("Continuous");
            layer.lineweight = r.integer(370, kLineweightDefault);
            layer.plot = r.integer(290, 1) != 0;
            layer.trueColor = r.integer(420, -1);
            layer.locked = (flags & 4) != 0;
        } else if (r.type == "BLOCK_RECORD") {
            if (const auto handle = r.text(5); handle)
                blockRecords_[*handle] = r.text(2).value_or("");
            // Stato di un blocco dinamico: XDATA AcDbBlockRepBTag con l'handle del blocco dinamico (1005).
            bool rep = false;
            for (const auto& [code, value] : r.values) {
                if (code == 1001)
                    rep = value == "AcDbBlockRepBTag";
                else if (rep && code == 1005) {
                    dynamicOrigins_[r.text(2).value_or("")] = value;
                    rep = false;
                }
            }
        }
        return;
    }

    if (section == "OBJECTS") {
        if (r.type == "LAYOUT")
            readLayout(r);
        if (r.type == "PLOTSETTINGS") {   // impostazioni di pagina con nome
            PlotSettings s = readPlotSettings(r);
            if (!s.pageSetupName.empty())
                doc_.setPageSetup(s);
        }
        if (r.type == "SCALE") {   // scala di annotazione della lista del disegno
            const std::string handle = r.text(5).value_or("");
            AnnoScale s{r.text(300).value_or(""), r.num(140, 1.0), r.num(141, 1.0)};
            if (!handle.empty() && !s.name.empty() && s.paperUnits > 0.0 && s.drawingUnits > 0.0) {
                scaleObjects_[handle] = s;
                scaleOrder_.push_back(handle);
            }
        }
        if (r.type.size() > 23 && r.type.compare(r.type.size() - 23, 23, "OBJECTCONTEXTDATA_CLASS") == 0)
            contextData_.push_back({r.text(330).value_or(""), r.text(340).value_or(""), r.integer(290, 0) != 0});
        if (r.type == "XRECORD" && dictionaryNames_.count(r.text(5).value_or(""))
            && dictionaryNames_[r.text(5).value_or("")] == "CARTESIO_IMPIANTO") {
            if (const auto path = r.text(1); path)
                doc_.setPlantFile(*path);
            if (const auto m = r.number(40); m && *m > 0.0)
                doc_.setPlantMetersPerUnit(*m);
        }
        // Canaline portacavi: 2 codice, poi i pezzi (1) dello stato JSON.
        if (r.type == "XRECORD" && dictionaryNames_.count(r.text(5).value_or(""))
            && dictionaryNames_[r.text(5).value_or("")] == "CARTESIO_CANALO") {
            std::string id, json;
            auto flush = [&] {
                if (!id.empty() && !json.empty())
                    doc_.setCanaloTray(id, json);
                json.clear();
            };
            for (const auto& [code, value] : r.values) {
                if (code == 2) {
                    flush();
                    id = value;
                } else if (code == 1) {
                    json += value;
                }
            }
            flush();
        }
        if (r.type == "XRECORD" && dictionaryNames_.count(r.text(5).value_or(""))
            && dictionaryNames_[r.text(5).value_or("")] == "CARTESIO_LAYOUTGROUPS") {
            for (const auto& [code, value] : r.values) {
                if (code == 1)
                    layoutGroups_.emplace_back(value, std::string());
                else if (code == 2 && !layoutGroups_.empty())
                    layoutGroups_.back().second = value;
            }
        }
        if (r.type == "XRECORD" && dictionaryNames_.count(r.text(5).value_or(""))
            && dictionaryNames_[r.text(5).value_or("")] == "CARTESIO_ANNOSCALES") {
            // Scale di annotazione di CartesioCAD: 1 nome, 40 carta, 41 disegno; 300 corrente; 290 ANNOALLVISIBLE.
            hasSavedScales_ = true;
            for (const auto& [code, value] : r.values) {
                if (code == 1)
                    savedScales_.push_back({value, 1.0, 1.0});
                else if (code == 40 && !savedScales_.empty())
                    savedScales_.back().paperUnits = toNumber(value).value_or(1.0);
                else if (code == 41 && !savedScales_.empty())
                    savedScales_.back().drawingUnits = toNumber(value).value_or(1.0);
                else if (code == 300)
                    savedCurrentScale_ = value;
                else if (code == 290)
                    savedAllVisible_ = toNumber(value).value_or(1.0) != 0.0;
            }
        }
        if (r.type == "DICTIONARY") {   // nomi delle voci (per i gruppi e i ritagli)
            std::string name;
            const std::string self = r.text(5).value_or("");
            dictOwners_[self] = r.text(330).value_or("");
            for (const auto& [code, value] : r.values) {
                if (code == 3)
                    name = value;
                else if ((code == 350 || code == 360) && !name.empty()) {
                    dictionaryNames_[value] = name;
                    dictEntries_[self][name] = value;
                }
            }
        }
        if (r.type == "SPATIAL_FILTER") {   // contorno di XCLIP
            SpatialFilter f;
            std::optional<double> x;
            for (const auto& [code, value] : r.values) {
                const double v = toNumber(value).value_or(0.0);
                if (code == 10)
                    x = v;
                else if (code == 20 && x) {
                    f.points.push_back({*x, v});
                    x.reset();
                } else if (code == 40) {
                    f.numbers40.push_back(v);
                } else if (code == 71) {
                    f.display = v != 0.0;
                }
            }
            spatialFilters_[r.text(5).value_or("")] = std::move(f);
        }
        if (r.type == "XRECORD") {   // stati dei layer di CartesioCAD
            const std::string owner = r.text(330).value_or("");
            const auto dict = dictionaryNames_.find(owner);
            const auto self = dictionaryNames_.find(r.text(5).value_or(""));
            if (dict != dictionaryNames_.end() && dict->second == "CARTESIO_MLEADERSTYLES" && self != dictionaryNames_.end()) {
                Document::MLeaderStyle style;   // stili delle direttrici di CartesioCAD
                style.name = self->second;
                style.textStyle = r.text(1).value_or("Standard");
                style.textHeight = r.num(40, 2.5);
                style.arrowSize = r.num(41, 2.5);
                style.landingGap = r.num(42, -1.0);
                style.annotative = r.integer(290, 0) != 0;
                if (r.integer(70, 0) == 1)
                    doc_.setCurrentMLeaderStyle(style.name);
                doc_.setMLeaderStyle(std::move(style));
            }
            if (dict != dictionaryNames_.end() && dict->second == "CARTESIO_LAYERSTATES" && self != dictionaryNames_.end()) {
                Document::LayerState state;
                state.name = self->second;
                for (const auto& [code, value] : r.values) {
                    if (code == 1)
                        state.description = value;
                    else if (code == 2)
                        state.currentLayer = value;
                    else if (code == 8) {
                        Layer l;
                        l.name = value;
                        state.layers.push_back(l);
                    } else if (!state.layers.empty()) {
                        Layer& l = state.layers.back();
                        const int v = toInt(toNumber(value), 0);
                        if (code == 70) {
                            l.visible = (v & 1) == 0;
                            l.frozen = (v & 2) != 0;
                            l.locked = (v & 4) != 0;
                            l.plot = (v & 8) == 0;
                        } else if (code == 62) {
                            l.color = v;
                        } else if (code == 6) {
                            l.linetype = value;
                        } else if (code == 370) {
                            l.lineweight = v;
                        } else if (code == 420) {
                            l.trueColor = v;
                        }
                    }
                }
                doc_.addLoadedLayerState(std::move(state));
            }
        }
        if (r.type == "SORTENTSTABLE") {   // ordine di visualizzazione: oggetto -> handle con cui ordinarlo
            bool table = false;
            std::string entity;
            for (const auto& [code, value] : r.values) {
                if (code == 100 && value == "AcDbSortentsTable")
                    table = true;
                else if (table && code == 331)
                    entity = value;
                else if (table && code == 5 && !entity.empty()) {
                    sortHandles_[entity] = value;
                    entity.clear();
                }
            }
        }
        if (r.type == "IMAGEDEF") {   // immagine raster: il file
            if (const auto handle = r.text(5); handle)
                imageDefs_[*handle] = r.text(1).value_or("");
        }
        if (r.type == "GROUP") {
            GroupInfo g;
            g.handle = r.text(5).value_or("");
            g.description = r.text(300).value_or("");
            g.selectable = r.integer(71, 1) != 0;
            for (const auto& [code, value] : r.values)
                if (code == 340)
                    g.members.push_back(value);
            groups_.push_back(std::move(g));
        }
        return;
    }

    if (section != "ENTITIES" && section != "BLOCKS")
        return;

    // Definizioni di blocco.
    if (r.type == "BLOCK") {
        block_ = std::make_shared<BlockDef>();
        keptBlocks_.push_back(block_);
        block_->name = r.text(2).value_or("");
        block_->base = r.point(10).value_or(Vec2{});
        const int flags = r.integer(70, 0);
        paperBlock_ = startsWithNoCase(block_->name, "*Paper_Space");
        // Riferimento esterno: si tiene il percorso; gli oggetti si caricano dal file esterno.
        if ((flags & 4) != 0) {
            block_->xrefPath = r.text(1).value_or("");
            block_->xrefOverlay = (flags & 8) != 0;
            if (block_->xrefPath.empty())
                block_->xrefPath = block_->name + ".dwg";
        }
        xrefBlock_ = (flags & 4) != 0;
        skipBlock_ = block_->name.empty() || startsWithNoCase(block_->name, "*Model_Space");
        return;
    }
    if (r.type == "ENDBLK") {
        if (block_ && !skipBlock_) {
            if (paperBlock_) {
                auto& content = paperBlocks_[block_->name];
                for (auto& e : content)   // stesso blocco due volte: gli oggetti della prima restano vivi
                    keptEntities_.push_back(std::move(e));
                content = std::move(block_->entities);
            }
            else
                doc_.addBlock(block_);
        }
        block_.reset();
        skipBlock_ = false;
        xrefBlock_ = false;
        paperBlock_ = false;
        lastInsert_ = nullptr;
        return;
    }

    // Polilinee vecchio stile e attributi: sequenze terminate da SEQEND.
    if (r.type == "VERTEX") {
        if (polyline_) {
            const int flags = r.integer(70, 0);
            // Larghezze: quelle del vertice o, se mancano, quelle predefinite della POLYLINE.
            const double w0 = std::max(0.0, r.num(40, polyline_->num(40, 0.0)));
            const double w1 = std::max(0.0, r.num(41, polyline_->num(41, 0.0)));
            if ((flags & 16) == 0)   // i punti di controllo delle spline vanno ignorati
                polyVertices_.push_back({r.point(10).value_or(Vec2{}), r.num(42, 0.0), w0, w1});
        }
        return;
    }
    if (r.type == "SEQEND") {
        if (polyline_)
            finishPolyline();
        inAttributes_ = false;
        lastInsert_ = nullptr;
        return;
    }
    if (r.type == "ATTRIB") {
        auto text = readText(r, 74);
        if (!text)
            return;
        if (inAttributes_ && lastInsert_) {
            auto& t = static_cast<TextEntity&>(*text);
            if (r.num(230, 1.0) < 0.0) {
                Affine mirror;
                mirror.m11 = -1.0;
                t.transform(mirror);
            }
            t.setLayer(layerIndex(r));
            t.setColor(r.integer(62, kColorByLayer));
            lastInsert_->attributes.push_back({r.text(2).value_or(""), t, r.integer(70, 0)});
        } else if ((r.integer(70, 0) & AttInvisible) == 0) {
            add(std::move(text), r, true);   // attributo senza inserimento: testo semplice
        }
        return;
    }
    if (r.type == "POLYLINE") {
        const int flags = r.integer(70, 0);
        if (flags & (16 | 64)) {   // superfici e polyface: non supportate
            result_.skippedTypes.insert("POLYLINE (mesh)");
            polyline_.reset();
            return;
        }
        polyline_ = r;
        polyVertices_.clear();
        return;
    }

    // Oggetti invisibili.
    if (r.integer(60, 0) == 1)
        return;

    if (r.type == "MULTILEADER") {   // direttrici multiple: una direttrice per ogni linea
        for (auto& leader : readMultiLeader(r))
            add(std::move(leader), r, false);
        return;
    }

    bool ocs = false;
    auto e = readEntity(r, ocs);
    if (e)
        add(std::move(e), r, ocs);
    else if (r.type == "HATCH" && r.integer(91, 0) == 0)
        ++emptyHatches_;   // senza contorno: non c'è niente da disegnare (in nessun programma)
    else if (r.type != "VIEWPORT")
        result_.skippedTypes.insert(r.type);
}

// Campi di AcDbPlotSettings (fino alla sottoclasse successiva).
static PlotSettings readPlotSettings(const Record& r)
{
    PlotSettings s;
    bool in = false;
    bool sawStandardScale = false;
    const auto num = [](const std::string& v, double fallback) { return toNumber(v).value_or(fallback); };
    const auto integer = [](const std::string& v, int fallback) {
        return toInt(toNumber(v), fallback);
    };
    for (const auto& [code, value] : r.values) {
        if (code == 100) {
            if (in)
                break;
            in = value == "AcDbPlotSettings";
            continue;
        }
        if (!in)
            continue;
        switch (code) {
        case 1: s.pageSetupName = value; break;
        case 2: s.device = value; break;
        case 4: s.mediaName = value; break;
        case 6: s.viewName = value; break;
        case 7: s.styleSheet = value; break;
        case 40: s.marginLeft = num(value, 0.0); break;
        case 41: s.marginBottom = num(value, 0.0); break;
        case 42: s.marginRight = num(value, 0.0); break;
        case 43: s.marginTop = num(value, 0.0); break;
        case 44: s.paperWidth = num(value, s.paperWidth); break;
        case 45: s.paperHeight = num(value, s.paperHeight); break;
        case 46: s.plotOrigin.x = num(value, 0.0); break;
        case 47: s.plotOrigin.y = num(value, 0.0); break;
        case 48: s.windowMin.x = num(value, 0.0); break;
        case 49: s.windowMin.y = num(value, 0.0); break;
        case 140: s.windowMax.x = num(value, 0.0); break;
        case 141: s.windowMax.y = num(value, 0.0); break;
        case 142: s.customPaper = num(value, 1.0); break;
        case 143: s.customDrawing = num(value, 1.0); break;
        case 70: s.flags = integer(value, s.flags); break;
        case 72: s.paperUnits = integer(value, 1); break;
        case 73: s.rotation = std::clamp(integer(value, 0), 0, 3); break;
        case 74: s.plotType = static_cast<PlotType>(std::clamp(integer(value, 5), 0, 5)); break;
        case 75:
            s.standardScaleType = integer(value, 16);
            sawStandardScale = true;
            break;
        case 76: s.shadeMode = integer(value, 0); break;
        case 77: s.shadeResolution = integer(value, 2); break;
        case 78: s.shadeDpi = integer(value, 300); break;
        case 147: s.standardScaleFactor = num(value, 1.0); break;
        case 148: s.paperImageOrigin.x = num(value, 0.0); break;
        case 149: s.paperImageOrigin.y = num(value, 0.0); break;
        default: break;
        }
    }
    if (!sawStandardScale && s.flag(kPlotUseStandardScale))
        s.standardScaleType = 16;
    if (s.paperWidth <= 0.0 || s.paperHeight <= 0.0) {
        s.paperWidth = 420.0;
        s.paperHeight = 297.0;
    }
    return s;
}

void Reader::readLayout(const Record& r)
{
    // Il nome e il registro del blocco seguono la sottoclasse AcDbLayout; prima ci sono le
    // impostazioni di stampa (con un altro codice 1, il nome dell'impostazione pagina).
    LayoutInfo info;
    info.plot = readPlotSettings(r);
    bool inLayout = false;
    for (const auto& [code, value] : r.values) {
        if (code == 100 && value == "AcDbLayout")
            inLayout = true;
        else if (inLayout && (code == 10 || code == 20 || code == 11 || code == 21)) {
            auto& p = code == 10 || code == 20 ? info.limitsMin : info.limitsMax;
            if (!p)
                p = Vec2{};
            (code == 10 || code == 11 ? p->x : p->y) = toNumber(value).value_or(0.0);
        }
        else if (inLayout && code == 1)
            info.name = value;
        else if (inLayout && code == 71)
            info.tabOrder = toInt(toNumber(value), 0);
        else if (inLayout && code == 330)
            info.recordHandle = value;
    }
    if (startsWithNoCase(info.name, "Model")) {
        doc_.setModelPlot(info.plot);
        return;
    }
    // Margini, misure e formato (44 e 45 sono sempre in mm).
    const PlotSettings& s = info.plot;
    info.margin = std::max({s.marginLeft, s.marginBottom, s.marginRight, s.marginTop});
    info.paper = s.mediaName;
    const SheetSize sheet = sheetSize(s);
    info.width = sheet.width;
    info.height = sheet.height;
    // Orientamento: quello dei limiti del layout, se ci sono; altrimenti quello della rotazione.
    if (info.limitsMin && info.limitsMax) {
        const bool portraitLimits = (info.limitsMax->x - info.limitsMin->x) < (info.limitsMax->y - info.limitsMin->y);
        if ((info.width < info.height) != portraitLimits)
            std::swap(info.width, info.height);
    }
    if (!info.name.empty())
        layouts_.push_back(std::move(info));
}

void Reader::finishPolyline()
{
    const Record r = *polyline_;
    polyline_.reset();
    if (polyVertices_.size() < 2)
        return;
    const bool closed = (r.integer(70, 0) & 1) != 0;
    const bool is3d = (r.integer(70, 0) & 8) != 0;
    add(std::make_unique<PolylineEntity>(std::move(polyVertices_), closed), r, !is3d);
    polyVertices_.clear();
}

std::unique_ptr<Entity> Reader::readEntity(const Record& r, bool& ocs)
{
    const std::string& t = r.type;
    ocs = t != "LINE" && t != "POINT" && t != "SPLINE" && t != "ELLIPSE" && t != "MTEXT" && t != "DIMENSION"
          && t != "3DFACE" && t != "VIEWPORT";

    if (t == "LINE") {
        const auto a = r.point(10), b = r.point(11);
        return a && b ? std::make_unique<LineEntity>(*a, *b) : nullptr;
    }
    if (t == "CIRCLE") {
        const auto c = r.point(10);
        const double radius = r.num(40, 0.0);
        return c && radius > 0.0 ? std::make_unique<CircleEntity>(*c, radius) : nullptr;
    }
    if (t == "ARC") {
        const auto c = r.point(10);
        const double radius = r.num(40, 0.0);
        if (!c || radius <= 0.0)
            return nullptr;
        return std::make_unique<ArcEntity>(*c, radius, degToRad(r.num(50, 0.0)), degToRad(r.num(51, 360.0)));
    }
    if (t == "POINT") {
        const auto p = r.point(10);
        return p ? std::make_unique<PointEntity>(*p) : nullptr;
    }
    if (t == "ELLIPSE") {
        const auto c = r.point(10), major = r.point(11);
        if (!c || !major)
            return nullptr;
        return std::make_unique<EllipseEntity>(*c, *major, r.num(40, 1.0), r.num(41, 0.0), r.num(42, 2.0 * kPi));
    }
    if (t == "LWPOLYLINE")
        return readLwPolyline(r);
    if (t == "SPLINE")
        return readSpline(r);
    if (t == "TEXT")
        return readText(r, 73);
    if (t == "MTEXT")
        return readMText(r);
    if (t == "SOLID" || t == "TRACE") {
        const auto a = r.point(10), b = r.point(11), c = r.point(12);
        if (!a || !b || !c)
            return nullptr;
        const Vec2 d = r.point(13).value_or(*c);
        return std::make_unique<SolidEntity>(std::array<Vec2, 4>{*a, *b, d, *c});   // ordine DXF 1,2,4,3
    }
    if (t == "3DFACE") {
        const auto a = r.point(10), b = r.point(11), c = r.point(12);
        if (!a || !b || !c)
            return nullptr;
        const Vec2 d = r.point(13).value_or(*c);
        return std::make_unique<PolylineEntity>(std::vector<PolyVertex>{{*a, 0}, {*b, 0}, {*c, 0}, {d, 0}}, true);
    }
    if (t == "HATCH")
        return readHatch(r);
    if (t == "INSERT") {
        if (auto table = readTableXdata(r))
            return table;
        return readInsert(r);
    }
    if (t == "DIMENSION")
        return readDimension(r);
    if (t == "ATTDEF")
        return readAttDef(r);
    if (t == "VIEWPORT")
        return readViewport(r);
    if (t == "IMAGE")
        return readImage(r);
    if (t == "OLE2FRAME")
        return readOle(r);
    if (t == "WIPEOUT")
        return readWipeout(r);
    if (t == "MLINE")
        return readMline(r);
    if (t == "ACAD_TABLE")   // tabelle: si vede il loro blocco con la grafica
        return readInsert(r);
    if (t == "XLINE" || t == "RAY") {
        const auto b = r.point(10);
        const auto d = r.point(11);
        if (!b || !d || d->length() == 0.0)
            return nullptr;
        return std::make_unique<XLineEntity>(*b, *d, t == "RAY");
    }
    if (t == "LEADER")
        return readLeader(r);
    return nullptr;
}

std::unique_ptr<Entity> Reader::readText(const Record& r, int vAlignCode)
{
    const auto p = r.point(10);
    const auto s = r.text(1);
    if (!p || !s)
        return nullptr;
    auto text = std::make_unique<TextEntity>(*p, r.num(40, 1.0), *s, degToRad(r.num(50, 0.0)));
    text->widthFactor = r.num(41, 1.0);
    text->oblique = degToRad(r.num(51, 0.0));
    if (const auto style = r.text(7); style && !style->empty())
        text->style = *style;
    text->hAlign = r.integer(72, 0);
    text->vAlign = r.integer(vAlignCode, 0);
    text->alignPoint = r.point(11).value_or(*p);
    return text;
}

std::unique_ptr<Entity> Reader::readMText(const Record& r)
{
    const auto p = r.point(10);
    if (!p)
        return nullptr;
    std::string content;
    for (const auto& [code, value] : r.values)   // codici 3 (blocchi da 250 caratteri) e poi 1
        if (code == 3)
            content += value;
    content += r.text(1).value_or("");
    auto m = std::make_unique<MTextEntity>(*p, r.num(40, 1.0), content, r.num(41, 0.0));
    m->attachment = std::clamp(r.integer(71, 1), 1, 9);
    m->lineSpacing = r.num(44, 1.0);
    m->backgroundFill = (r.integer(90, 0) & 3) != 0;   // maschera: colore di riempimento o dello sfondo
    m->fillScale = std::clamp(r.num(45, 1.5), 1.0, 5.0);
    // Colonne: dati di CartesioCAD (1070 numero, 1040 distanza), oppure 75/76/49 dei DXF più recenti.
    {
        std::string app;
        bool ours = false;
        for (const auto& [code, value] : r.values) {
            if (code == 1001)
                app = value;
            else if (app == "CARTESIOCAD" && code == 1000)
                ours = value == "MTEXTCOLUMNS";
            else if (ours && code == 1070)
                m->columns = std::clamp(toInt(toNumber(value), 1), 1, 20);
            else if (ours && code == 1040)
                m->columnGutter = std::max(0.0, toNumber(value).value_or(0.0));
        }
        if (!ours && r.integer(75, 0) != 0 && r.integer(76, 1) > 1) {
            m->columns = std::clamp(r.integer(76, 1), 1, 20);
            m->columnGutter = r.num(49, 0.0);
            if (r.num(48, 0.0) > 0.0)
                m->width = r.num(48, 0.0);
        }
    }
    if (const auto style = r.text(7); style && !style->empty())
        m->style = *style;
    if (const auto dir = r.point(11); dir && dir->length() > 0.0)
        m->rotation = angleOf(*dir);
    else
        m->rotation = degToRad(r.num(50, 0.0));
    return m;
}

std::unique_ptr<Entity> Reader::readLwPolyline(const Record& r)
{
    auto poly = std::make_unique<PolylineEntity>();
    poly->closed = (r.integer(70, 0) & 1) != 0;
    // 43 = larghezza costante (vale per tutti i vertici), 40/41 = larghezza iniziale e finale del vertice.
    const double constant = std::max(0.0, r.num(43, 0.0));
    for (const auto& [code, value] : r.values) {
        const double v = toNumber(value).value_or(0.0);
        if (code == 10)
            poly->vertices.push_back({{v, 0.0}, 0.0, constant, constant});
        else if (code == 20 && !poly->vertices.empty())
            poly->vertices.back().point.y = v;
        else if (code == 42 && !poly->vertices.empty())
            poly->vertices.back().bulge = v;
        else if (code == 40 && !poly->vertices.empty())
            poly->vertices.back().startWidth = std::max(0.0, v);
        else if (code == 41 && !poly->vertices.empty())
            poly->vertices.back().endWidth = std::max(0.0, v);
    }
    return poly->vertices.size() >= 2 ? std::move(poly) : nullptr;
}

std::unique_ptr<Entity> Reader::readSpline(const Record& r)
{
    auto spline = std::make_unique<SplineEntity>();
    spline->degree = r.integer(71, 3);
    spline->closed = (r.integer(70, 0) & 1) != 0;
    for (const auto& [code, value] : r.values) {
        const double v = toNumber(value).value_or(0.0);
        switch (code) {
        case 40: spline->knots.push_back(v); break;
        case 41: spline->weights.push_back(v); break;
        case 10: spline->control.push_back({v, 0.0}); break;
        case 20: if (!spline->control.empty()) spline->control.back().y = v; break;
        case 11: spline->fit.push_back({v, 0.0}); break;
        case 21: if (!spline->fit.empty()) spline->fit.back().y = v; break;
        default: break;
        }
    }
    if (spline->control.size() < 2 && spline->fit.size() < 2)
        return nullptr;
    return spline;
}

// I contorni "textbox" (flag 8) sono i rettangoli attorno ai testi tolti dal tratteggio:
// servono solo a fare buchi. Se il testo è stato spostato fuori dal contorno vero, lì non si disegna
// niente, mentre col pari/dispari il rettangolo diventerebbe una macchia di tratteggio isolata (i "retini a
// caso" di una pianta di prova). Si tengono solo quelli col centro dentro i contorni veri; se non ce ne sono, nessuno.
static void dropOutsideTextBoxes(std::vector<std::vector<PolyVertex>>& loops, const std::vector<bool>& textBoxes)
{
    if (std::find(textBoxes.begin(), textBoxes.end(), true) == textBoxes.end())
        return;
    std::vector<std::vector<Vec2>> real;
    for (std::size_t i = 0; i < loops.size(); ++i)
        if (!textBoxes[i]) {
            std::vector<Vec2> poly;
            for (const PolyVertex& v : loops[i])
                poly.push_back(v.point);
            real.push_back(std::move(poly));
        }
    std::vector<std::vector<PolyVertex>> kept;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        bool keep = !textBoxes[i];
        if (!keep) {
            Vec2 center{0.0, 0.0};
            for (const PolyVertex& v : loops[i])
                center = center + v.point;
            center = center * (1.0 / static_cast<double>(loops[i].size()));
            int crossings = 0;   // pari/dispari come il riempimento
            for (const auto& poly : real)
                crossings += pointInPolygon(poly, center) ? 1 : 0;
            keep = crossings % 2 == 1;
        }
        if (keep)
            kept.push_back(std::move(loops[i]));
    }
    loops = std::move(kept);
}

std::unique_ptr<Entity> Reader::readHatch(const Record& r)
{
    auto hatch = std::make_unique<HatchEntity>();
    hatch->pattern = r.text(2).value_or("SOLID");
    hatch->solid = r.integer(70, 0) == 1 || hatch->pattern == "SOLID";

    Cursor c(r.values);
    const int loops = c.count(91);
    std::vector<bool> textBoxes;   // contorno attorno a un testo (flag 8)
    for (int l = 0; l < loops && !c.done(); ++l) {
        const int flags = c.integer(92, 0);
        std::vector<PolyVertex> loop;

        if (flags & 2) {   // contorno a polilinea
            const bool hasBulge = c.next(72).value_or(0) != 0;
            c.next(73);
            const int n = c.count(93);
            for (int i = 0; i < n; ++i) {
                const double x = c.next(10).value_or(0.0);
                const double y = c.next(20).value_or(0.0);
                const double b = hasBulge ? c.optional(42).value_or(0.0) : 0.0;
                loop.push_back({{x, y}, b});
            }
        } else {           // contorno a lati
            const int edges = c.count(93);
            for (int e = 0; e < edges; ++e) {
                const int type = c.integer(72, 1);
                if (type == 1) {           // segmento
                    const double x1 = c.next(10).value_or(0), y1 = c.next(20).value_or(0);
                    c.next(11);
                    c.next(21);
                    loop.push_back({{x1, y1}, 0.0});
                } else if (type == 2) {    // arco
                    const Vec2 center{c.next(10).value_or(0), c.next(20).value_or(0)};
                    const double radius = c.next(40).value_or(0);
                    const double a0 = degToRad(c.next(50).value_or(0));
                    const double a1 = degToRad(c.next(51).value_or(0));
                    const bool ccw = c.next(73).value_or(1) != 0;
                    // Gli archi orari hanno gli angoli memorizzati specchiati.
                    const double start = ccw ? a0 : -a0;
                    double sweep = normalizeAngle(a1 - a0);   // anche per gli orari, con gli angoli specchiati
                    if (sweep <= 1e-12 || std::abs(a1 - a0) >= 2.0 * kPi - 1e-9)
                        sweep = 2.0 * kPi;   // arco intero (cerchio)
                    const double sign = ccw ? 1.0 : -1.0;
                    if (sweep > kPi) {
                        // Oltre mezzo giro (fino al cerchio intero) si divide in due: il bulge resta finito.
                        loop.push_back({center + Vec2::polar(radius, start), sign * std::tan(sweep / 8.0)});
                        loop.push_back({center + Vec2::polar(radius, start + sign * sweep / 2.0), sign * std::tan(sweep / 8.0)});
                    } else {
                        loop.push_back({center + Vec2::polar(radius, start), sign * std::tan(sweep / 4.0)});
                    }
                } else if (type == 3) {    // arco di ellisse: approssimato con segmenti
                    const Vec2 center{c.next(10).value_or(0), c.next(20).value_or(0)};
                    const Vec2 major{c.next(11).value_or(1), c.next(21).value_or(0)};
                    const double ratio = c.next(40).value_or(1);
                    const double t0 = degToRad(c.next(50).value_or(0));
                    const double t1 = degToRad(c.next(51).value_or(360));
                    const bool ccw = c.next(73).value_or(1) != 0;
                    const EllipseEntity el(center, major, ratio);
                    const double s0 = ccw ? t0 : -t0;
                    const double s1 = ccw ? t1 : -t1;
                    double sweep = ccw ? normalizeAngle(s1 - s0) : -normalizeAngle(s0 - s1);
                    if (sweep == 0.0)
                        sweep = ccw ? 2.0 * kPi : -2.0 * kPi;
                    const int steps = 24;
                    for (int k = 0; k < steps; ++k)
                        loop.push_back({el.pointAt(s0 + sweep * k / steps), 0.0});
                } else if (type == 4) {    // spline
                    SplineEntity sp;
                    sp.degree = c.integer(94, 3);
                    const bool rational = c.next(73).value_or(0) != 0;
                    c.next(74);
                    const int nk = c.count(95);
                    const int nc = c.count(96);
                    for (int k = 0; k < nk; ++k)
                        sp.knots.push_back(c.next(40).value_or(0));
                    for (int k = 0; k < nc; ++k) {
                        const double x = c.next(10).value_or(0), y = c.next(20).value_or(0);
                        sp.control.push_back({x, y});
                        if (rational)
                            sp.weights.push_back(c.optional(42).value_or(1.0));
                    }
                    auto pts = sp.evaluate();
                    if (!pts.empty())
                        pts.pop_back();
                    for (Vec2 p : pts)
                        loop.push_back({p, 0.0});
                }
            }
        }
        // Riferimenti agli oggetti di contorno (non usati).
        const int refs = c.count(97);
        for (int k = 0; k < refs; ++k)
            c.next(330);
        if (loop.size() >= 2) {
            hatch->loops.push_back(std::move(loop));
            textBoxes.push_back((flags & 8) != 0);
        }
    }
    dropOutsideTextBoxes(hatch->loops, textBoxes);
    hatch->hatchStyle = c.integer(75, 0);
    hatch->patternType = c.integer(76, 1);
    if (!hatch->solid) {
        // Definizione del motivo: angolo, scala e famiglie di linee (come sono salvate nel file).
        hatch->patternAngle = degToRad(c.next(52).value_or(0.0));
        hatch->patternScale = c.next(41).value_or(1.0);
        hatch->patternDouble = c.next(77).value_or(0.0) != 0.0;
        const int lines = c.count(78);
        for (int k = 0; k < lines && !c.done(); ++k) {
            HatchPatternLine line;
            line.angle = degToRad(c.next(53).value_or(0.0));
            line.base = {c.next(43).value_or(0.0), c.next(44).value_or(0.0)};
            line.offset = {c.next(45).value_or(0.0), c.next(46).value_or(0.0)};
            const int dashes = c.count(79);
            for (int d = 0; d < dashes; ++d)
                line.dashes.push_back(c.next(49).value_or(0.0));
            hatch->patternLines.push_back(std::move(line));
        }
    }
    // Sfumatura: tutto da 450 in poi si conserva per riscriverlo uguale.
    bool gradient = false;
    for (const auto& [code, value] : r.values) {
        if (code == 450)
            gradient = true;
        if (gradient)
            hatch->gradient.emplace_back(code, value);
    }
    if (!hatch->gradient.empty() && r.integer(450, 0) == 0)
        hatch->gradient.clear();   // 450 = 0: tratteggio normale

    return hatch->loops.empty() ? nullptr : std::move(hatch);
}

std::unique_ptr<Entity> Reader::readInsert(const Record& r)
{
    const auto name = r.text(2);
    const auto p = r.point(10);
    if (!name || !p)
        return nullptr;
    auto insert = std::make_unique<InsertEntity>(*name, *p);
    insert->scaleX = r.num(41, 1.0);
    insert->scaleY = r.num(42, insert->scaleX);
    insert->rotation = degToRad(r.num(50, 0.0));
    inAttributes_ = r.integer(66, 0) == 1;
    // Dizionario esteso (può contenere il ritaglio XCLIP).
    bool xdict = false;
    for (const auto& [code, value] : r.values) {
        if (code == 102)
            xdict = value == "{ACAD_XDICTIONARY";
        else if (code == 360 && xdict) {
            pendingXClips_.emplace_back(insert.get(), value);
            break;
        }
    }
    return insert;
}

std::unique_ptr<Entity> Reader::readDimension(const Record& r)
{
    auto dim = std::make_unique<DimensionEntity>();
    dim->blockName = r.text(2).value_or("");
    dim->kind = r.integer(70, 0) & 7;
    dim->dimLine = r.point(10).value_or(Vec2{});
    dim->textPos = r.point(11).value_or(dim->dimLine);
    dim->p1 = r.point(13).value_or(Vec2{});
    dim->p2 = r.point(14).value_or(Vec2{});
    dim->curvePoint = r.point(15).value_or(Vec2{});
    dim->rotation = degToRad(r.num(50, 0.0));
    dim->ordinateX = (r.integer(70, 0) & 64) != 0;
    dim->textMoved = (r.integer(70, 0) & 128) != 0;   // testo in una posizione scelta dall'utente
    if (r.text(53))
        dim->textRotation = degToRad(r.num(53, 0.0));
    dim->oblique = degToRad(r.num(52, 0.0));
    if (dim->kind == DimensionEntity::Angular) {
        // Angolare fra due linee (13-14 e 15-10, arco in 16): come quella a tre punti, con il vertice
        // all'incrocio delle due linee.
        const Vec2 a = dim->p1, b = dim->p2, c = dim->curvePoint, d = dim->dimLine;
        const Vec2 u = b - a, w = d - c;
        const double den = cross(u, w);
        if (std::abs(den) > 1e-12) {
            const Vec2 v = a + u * (cross(c - a, w) / den);
            dim->curvePoint = v;
            dim->p1 = distance(v, a) > distance(v, b) ? a : b;
            dim->p2 = distance(v, c) > distance(v, d) ? c : d;
            dim->dimLine = r.point(16).value_or(dim->dimLine);
            dim->kind = DimensionEntity::Angular3;
        }
    }
    dim->textOverride = r.text(1).value_or("");
    if (const DimStyle* s = doc_.findDimStyle(r.text(3).value_or("Standard")))
        dim->style = *s;
    return dim;
}

std::unique_ptr<Entity> Reader::readAttDef(const Record& r)
{
    auto text = readText(r, 74);
    if (!text)
        text = std::make_unique<TextEntity>(r.point(10).value_or(Vec2{}), r.num(40, 1.0), "");
    auto def = std::make_unique<AttDefEntity>(r.text(2).value_or("TAG"), r.text(3).value_or(""),
                                              static_cast<TextEntity&>(*text));
    def->flags = r.integer(70, 0);
    return def;
}



// MULTILEADER: dati di contesto con testo (304), altezza (41), e per ogni LEADER{ il punto di
// collegamento (10), la direzione dell'approdo (11), la sua lunghezza (40) e le LEADER_LINE{ con i vertici.
std::vector<std::unique_ptr<Entity>> Reader::readMultiLeader(const Record& r)
{
    std::vector<std::unique_ptr<Entity>> result;
    std::string text;
    double textHeight = 2.5;
    double arrowSize = 2.5;
    int depth = 0;              // 0 fuori, 1 contesto, 2 LEADER{, 3 LEADER_LINE{
    bool afterContext = false;
    Vec2 connection, direction{1.0, 0.0};
    double dogleg = 0.0;
    std::vector<std::vector<Vec2>> lines;
    std::vector<Vec2> line;
    std::optional<double> x;
    struct LeaderData {
        Vec2 connection, direction;
        double dogleg;
        std::vector<std::vector<Vec2>> lines;
    };
    std::vector<LeaderData> leaders;

    for (const auto& [code, value] : r.values) {
        if (code == 300 && value == "CONTEXT_DATA{") {
            depth = 1;
            continue;
        }
        if (code == 302 && value == "LEADER{") {
            depth = 2;
            lines.clear();
            dogleg = 0.0;
            continue;
        }
        if (code == 304 && value == "LEADER_LINE{") {
            depth = 3;
            line.clear();
            continue;
        }
        if (code == 305 && depth == 3) {
            lines.push_back(line);
            depth = 2;
            continue;
        }
        if (code == 303 && depth == 2) {
            leaders.push_back({connection, direction, dogleg, lines});
            depth = 1;
            continue;
        }
        if (code == 301) {
            depth = 0;
            afterContext = true;
            continue;
        }
        const auto number = toNumber(value);
        if (depth == 1) {
            if (code == 304)
                text = value;
            else if (code == 41 && number)
                textHeight = *number;
        } else if (depth == 2) {
            if (code == 10)
                x = number;
            else if (code == 20 && x && number) {
                connection = {*x, *number};
                x.reset();
            } else if (code == 11)
                x = number;
            else if (code == 21 && x && number) {
                direction = {*x, *number};
                x.reset();
            } else if (code == 40 && number)
                dogleg = *number;
        } else if (depth == 3) {
            if (code == 10)
                x = number;
            else if (code == 20 && x && number) {
                line.push_back({*x, *number});
                x.reset();
            }
        } else if (afterContext && code == 42 && number) {
            arrowSize = *number;
        }
    }

    bool first = true;
    for (const LeaderData& l : leaders)
        for (const auto& vertices : l.lines) {
            auto leader = std::make_unique<LeaderEntity>();
            leader->points = vertices;
            leader->points.push_back(l.connection);
            if (l.dogleg > 0.0 && l.direction.length() > 0.0)
                leader->points.push_back(l.connection + l.direction / l.direction.length() * l.dogleg);
            if (leader->points.size() < 2)
                continue;
            leader->textHeight = textHeight;
            leader->arrowSize = arrowSize;
            if (first)
                leader->text = text;   // il testo una volta sola, sulla prima linea
            first = false;
            result.push_back(std::move(leader));
        }
    return result;
}
std::unique_ptr<Entity> Reader::readLeader(const Record& r)
{
    auto leader = std::make_unique<LeaderEntity>();
    std::optional<double> x;
    for (const auto& [code, value] : r.values) {
        if (code == 10)
            x = toNumber(value);
        else if (code == 20 && x) {
            if (const auto y = toNumber(value))
                leader->points.push_back({*x, *y});
            x.reset();
        }
    }
    if (leader->points.size() < 2)
        return nullptr;
    const DimStyle* style = doc_.findDimStyle(r.text(3).value_or("Standard"));
    const double scale = style && style->scale > 0.0 ? style->scale : 1.0;
    leader->arrowSize = r.integer(71, 1) != 0 ? (style ? style->arrowSize : 2.5) * scale : 0.0;
    leader->textHeight = r.num(40, style ? style->textHeight * scale : 2.5);
    if (r.integer(73, 3) == 0)
        if (const auto handle = r.text(340); handle)
            pendingLeaderTexts_.emplace_back(leader.get(), *handle);
    // Dati estesi di CartesioCAD: stile della direttrice, stile del testo, distanza dall'approdo.
    bool ours = false;
    std::vector<std::string> xdata;
    for (const auto& [code, value] : r.values) {
        if (code == 1001)
            ours = value == "CARTESIOCAD";
        else if (ours && code >= 1000)
            xdata.push_back(value);
    }
    if (xdata.size() >= 2 && xdata[0] == "MLEADERLEG")   // freccia in più di una multidirettrice (si unisce alla fine)
        leaderLegs_.emplace_back(leader.get(), xdata[1]);
    if (xdata.size() >= 4 && xdata[0] == "MLEADERSTYLE") {
        leader->style = xdata[1];
        leader->textStyle = xdata[2].empty() ? std::string("Standard") : xdata[2];
        leader->landingGap = toNumber(xdata[3]).value_or(-1.0);
        leaderStyled_.insert(leader.get());
    }
    return leader;
}


std::unique_ptr<Entity> Reader::readImage(const Record& r)
{
    auto image = std::make_unique<ImageEntity>();
    image->insertion = r.point(10).value_or(Vec2{});
    image->u = r.point(11).value_or(Vec2{1.0, 0.0});
    image->v = r.point(12).value_or(Vec2{0.0, 1.0});
    const Vec2 size = r.point(13).value_or(Vec2{1.0, 1.0});
    image->width = size.x > 0.0 ? size.x : 1.0;
    image->height = size.y > 0.0 ? size.y : 1.0;
    image->show = (r.integer(70, 1) & 1) != 0;
    image->clipping = r.integer(280, 0) != 0;
    image->brightness = r.integer(281, 50);
    image->contrast = r.integer(282, 50);
    image->fade = r.integer(283, 0);
    std::optional<double> x;
    for (const auto& [code, value] : r.values) {
        const double v = toNumber(value).value_or(0.0);
        if (code == 14)
            x = v;
        else if (code == 24 && x) {
            image->clip.push_back({*x, v});
            x.reset();
        }
    }
    if (const auto handle = r.text(340); handle)
        pendingImages_.emplace_back(image.get(), *handle);
    return image;
}
// OLE2FRAME: oggetto OLE incorporato fra due angoli (10 in alto a sinistra, 11 in basso a destra); i dati binari
// (codici 310) si tengono così come sono. Diventa un'immagine "di 1000 pixel" di larghezza stesa sul riquadro.
std::unique_ptr<Entity> Reader::readOle(const Record& r)
{
    std::string data;
    data.reserve(static_cast<std::size_t>(std::max(0, r.integer(90, 0))));
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    for (const auto& [code, value] : r.values) {
        if (code != 310)
            continue;
        for (std::size_t i = 0; i + 1 < value.size(); i += 2) {
            const int hi = nibble(value[i]), lo = nibble(value[i + 1]);
            if (hi < 0 || lo < 0)
                break;
            data.push_back(static_cast<char>(hi * 16 + lo));
        }
    }
    // Angoli: nei dati, dopo 2 byte, i quattro vertici (x, y, z) in alto a sinistra, in alto a destra,
    // in basso a destra e in basso a sinistra (anche ruotati); altrimenti i codici 10 e 11 (non sempre giusti
    // nelle conversioni dei DWG).
    Vec2 ll, lr, ul;
    bool corners = false;
    if (data.size() >= 2 + 12 * 8) {
        double v[12];
        std::memcpy(v, data.data() + 2, sizeof(v));
        const Vec2 c0{v[0], v[1]}, c1{v[3], v[4]}, c2{v[6], v[7]}, c3{v[9], v[10]};
        const auto finite = [](Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) < 1e12 && std::abs(p.y) < 1e12; };
        if (finite(c0) && finite(c1) && finite(c2) && finite(c3) && (c1 - c0).length() > 1e-9 && (c0 - c3).length() > 1e-9
            && std::abs((c1 - c0).length() - (c2 - c3).length()) < 1e-6 * (c1 - c0).length() + 1e-9) {
            ul = c0;
            lr = c2;
            ll = c3;
            corners = true;
        }
    }
    if (!corners) {
        const Vec2 a = r.point(10).value_or(Vec2{});
        const Vec2 b = r.point(11).value_or(Vec2{});
        ll = {std::min(a.x, b.x), std::min(a.y, b.y)};
        lr = {std::max(a.x, b.x), ll.y};
        ul = {ll.x, std::max(a.y, b.y)};
    }
    const double w = (lr - ll).length(), h = (ul - ll).length();
    if (data.empty() || w <= 0.0 || h <= 0.0)
        return nullptr;
    auto image = std::make_unique<ImageEntity>();
    image->insertion = ll;
    image->width = 1000.0;
    image->height = std::max(1.0, std::round(1000.0 * h / w));
    image->u = (lr - ll) * (1.0 / image->width);
    image->v = (ul - ll) * (1.0 / image->height);
    image->ole = std::make_shared<const std::string>(std::move(data));
    image->oleType = r.text(3).value_or("OLE");
    image->oleObjectType = r.integer(71, 2);
    return image;
}

// WIPEOUT: contorno in coordinate normalizzate (-0,5…0,5) rispetto a inserimento e vettori u/v.
std::unique_ptr<Entity> Reader::readWipeout(const Record& r)
{
    const Vec2 insert = r.point(10).value_or(Vec2{});
    const Vec2 u = r.point(11).value_or(Vec2{1.0, 0.0});
    const Vec2 v = r.point(12).value_or(Vec2{0.0, 1.0});
    std::vector<Vec2> outline;
    std::optional<double> x;
    for (const auto& [code, value] : r.values) {
        if (code == 14)
            x = toNumber(value);
        else if (code == 24 && x) {
            if (const auto y = toNumber(value))
                outline.push_back(insert + u * (*x + 0.5) + v * (0.5 - *y));
            x.reset();
        }
    }
    // Contorno rettangolare: due angoli opposti.
    if (r.integer(71, 2) == 1 && outline.size() == 2) {
        const Vec2 a = outline[0], b = outline[1];
        outline = {a, {b.x, a.y}, b, {a.x, b.y}};
    }
    if (outline.size() > 3 && distance(outline.front(), outline.back()) < 1e-9)
        outline.pop_back();
    if (outline.size() < 3)
        return nullptr;
    return std::make_unique<WipeoutEntity>(std::move(outline));
}

// MLINE: per ogni vertice punto, direzione, giunzione e parametri degli elementi.
std::unique_ptr<Entity> Reader::readMline(const Record& r)
{
    auto m = std::make_unique<MlineEntity>();
    m->scale = r.num(40, 20.0);
    m->justification = r.integer(70, 0);
    m->closed = (r.integer(71, 1) & 2) != 0;
    m->style = r.text(2).value_or("Standard");
    Cursor c(r.values);
    // 72 e 73 possono essere in qualsiasi ordine; non più di quanti dati ci sono davvero nel file.
    const int available = static_cast<int>(std::min<std::size_t>(r.values.size(), 1000000));
    const int vertices = std::clamp(r.integer(72, 0), 0, available);
    const int elements = std::clamp(r.integer(73, 0), 0, available);
    c.next(10);                               // punto iniziale: poi vengono i vertici
    m->offsets.assign(static_cast<std::size_t>(std::max(0, elements)), 0.0);
    for (int i = 0; i < vertices && !c.done(); ++i) {
        const Vec2 p{c.next(11).value_or(0.0), c.next(21).value_or(0.0)};
        const Vec2 dir{c.next(12).value_or(1.0), c.next(22).value_or(0.0)};
        const Vec2 miter{c.next(13).value_or(0.0), c.next(23).value_or(1.0)};
        m->vertices.push_back(p);
        for (int e = 0; e < elements; ++e) {
            const int params = c.count(74);
            double first = 0.0;
            for (int k = 0; k < params; ++k) {
                const double value = c.next(41).value_or(0.0);
                if (k == 0)
                    first = value;
            }
            const int fills = c.count(75);
            for (int k = 0; k < fills; ++k)
                c.next(42);
            // Distanza perpendicolare dell'elemento, dal primo vertice.
            if (i == 0)
                m->offsets[static_cast<std::size_t>(e)] = first * miter.dot(Vec2{-dir.y, dir.x});
        }
    }
    if (m->vertices.size() < 2)
        return nullptr;
    return m;
}

// INSERT con i dati estesi di CartesioCAD: è una tabella salvata come blocco.
std::unique_ptr<Entity> Reader::readTableXdata(const Record& r)
{
    bool ours = false;
    std::vector<std::pair<int, std::string>> xdata;
    for (const auto& [code, value] : r.values) {
        if (code == 1001)
            ours = value == "CARTESIOCAD";
        else if (ours && code >= 1000)
            xdata.emplace_back(code, value);
    }
    if (xdata.empty() || xdata.front().second != "TABLE")
        return nullptr;
    std::size_t i = 1;
    const auto number = [&]() { return i < xdata.size() ? toNumber(xdata[i++].second).value_or(0.0) : 0.0; };
    auto t = std::make_unique<TableEntity>();
    t->position.x = number();
    t->position.y = number();
    number();   // z
    t->rotation = number();
    t->textHeight = number();
    t->rows = toInt(number(), 0);
    t->columns = toInt(number(), 0);
    if (t->rows <= 0 || t->columns <= 0 || static_cast<long long>(t->rows) * t->columns > 1000000)
        return nullptr;
    for (int k = 0; k < t->columns; ++k)
        t->columnWidths.push_back(number());
    for (int k = 0; k < t->rows; ++k)
        t->rowHeights.push_back(number());
    for (int k = 0; k < t->rows * t->columns; ++k)
        t->cells.push_back(i < xdata.size() ? xdata[i++].second : std::string());
    tableBlocks_.insert(r.text(2).value_or(""));   // il blocco con la grafica non serve più
    return t;
}
std::unique_ptr<Entity> Reader::readViewport(const Record& r)
{
    // Le finestre esistono solo nello spazio carta. La prima di ogni layout è la vista generale del
    // foglio (id 1 di solito; LibreDWG può rinumerarla): non è un oggetto da disegnare.
    const bool inPaperBlock = block_ && paperBlock_;
    if (!inPaperBlock && r.integer(67, 0) != 1)
        return nullptr;
    const std::string container = inPaperBlock ? block_->name : std::string("*Paper_Space");
    if (viewportContainers_.insert(container).second || r.integer(69, 2) == 1)
        return nullptr;
    const auto c = r.point(10);
    const double w = r.num(40, 0.0);
    const double h = r.num(41, 0.0);
    if (!c || w <= 0.0 || h <= 0.0)
        return nullptr;
    auto vp = std::make_unique<ViewportEntity>(*c, w, h);
    vp->viewCenter = r.point(12).value_or(Vec2{});
    vp->viewHeight = r.num(45, h);
    const int flags = r.integer(90, 0);
    vp->locked = (flags & 16384) != 0;
    for (const auto& [code, value] : r.values)
        if (code == 331 || code == 341)   // 341: DXF scritti da LibreDWG
            if (const auto it = layerHandles_.find(value); it != layerHandles_.end())
                vp->frozenLayers.push_back(it->second);
    if (flags & 65536)   // ritaglio non rettangolare: il contorno è un altro oggetto (340)
        if (const auto handle = r.text(340); handle)
            pendingClips_.emplace_back(vp.get(), *handle);
    return vp;
}

void Reader::finish()
{
    if (emptyHatches_ > 0)
        result_.notes.push_back(std::to_string(emptyHatches_)
                                + (emptyHatches_ == 1 ? " tratteggio senza contorno ignorato (vuoto in qualsiasi programma)."
                                                      : " tratteggi senza contorno ignorati (vuoti in qualsiasi programma)."));
    if (polyline_)
        finishPolyline();

    // Layout: si sostituiscono quelli predefiniti con quelli del file, nell'ordine delle schede.
    int activePaper = 0;
    if (!layouts_.empty()) {
        std::sort(layouts_.begin(), layouts_.end(),
                  [](const LayoutInfo& a, const LayoutInfo& b) { return a.tabOrder < b.tabOrder; });
        doc_.clearLayouts();
        for (const LayoutInfo& info : layouts_) {
            const int id = doc_.addLayout(info.name);
            Layout* layout = doc_.findLayout(id);
            layout->paperWidth = info.width;
            layout->paperHeight = info.height;
            // Il foglio sta dove dicono i limiti del layout, se hanno le misure del foglio.
            if (info.limitsMin && info.limitsMax) {
                const Vec2 size = *info.limitsMax - *info.limitsMin;
                if (std::abs(size.x - info.width) <= 0.02 * info.width && std::abs(size.y - info.height) <= 0.02 * info.height)
                    layout->origin = *info.limitsMin;
            }
            if (info.margin >= 0.0)
                layout->margin = info.margin;
            // Impostazioni di stampa; il foglio resta quello deciso sopra (applyPlotSettings ricava il formato).
            {
                PlotSettings plot = info.plot;
                const bool landscape = layout->paperWidth >= layout->paperHeight;
                setOrientation(plot, landscape, isUpsideDown(plot));
                const std::string mediaName = plot.mediaName;
                applyPlotSettings(*layout, plot);
                layout->paperWidth = info.width;
                layout->paperHeight = info.height;
                if (info.margin >= 0.0)
                    layout->margin = info.margin;
                if (!findMediaBySize(info.width, info.height) && !mediaName.empty())
                    layout->paperName = mediaName;
            }

            const auto record = blockRecords_.find(info.recordHandle);
            const std::string recordName = record != blockRecords_.end() ? record->second : std::string{};
            if (recordName.empty() || recordName == "*Paper_Space" || recordName == "*PAPER_SPACE") {
                if (activePaper == 0)
                    activePaper = id;
                continue;
            }
            // Layout non attivo: il contenuto è nel suo blocco *Paper_SpaceN.
            const auto content = paperBlocks_.find(recordName);
            if (content == paperBlocks_.end())
                continue;
            for (auto& e : content->second) {
                if (!e)
                    continue;   // due layout con lo stesso blocco: gli oggetti sono già nel primo
                e->setSpace(id);
                doc_.insertLoaded(std::move(e));
                ++result_.imported;
            }
        }
    }
    if (hasPendingPaper_) {
        if (activePaper == 0)
            activePaper = doc_.layouts().empty() ? doc_.addLayout("Layout1") : doc_.layouts().front().id;
        doc_.reassignSpace(kPendingPaper, activePaper);
    }
    // Dopo gli oggetti dei layout non attivi: anche i loro blocchi (cartigli, simboli) vanno collegati.
    doc_.linkBlocks();

    // Gruppi: gli handle degli oggetti diventano i loro id.
    for (const GroupInfo& g : groups_) {
        std::vector<EntityId> ids;
        for (const std::string& h : g.members)
            if (const auto it = entityHandles_.find(h); it != entityHandles_.end() && it->second->id() != 0)
                ids.push_back(it->second->id());
        if (ids.empty())
            continue;
        const auto name = dictionaryNames_.find(g.handle);
        doc_.addGroup(std::move(ids), name != dictionaryNames_.end() ? name->second : std::string(), g.description);
        doc_.groups().back().selectable = g.selectable;
    }
    // Blocchi con la grafica delle tabelle di CartesioCAD: le tabelle li rigenerano al salvataggio.
    for (const std::string& name : tableBlocks_)
        doc_.eraseBlock(name);

    // Ritagli dei blocchi (XCLIP): dizionario esteso -> ACAD_FILTER -> SPATIAL -> SPATIAL_FILTER. I punti sono
    // nel disegno; la prima matrice (12 valori 40) li porta nelle coordinate del blocco.
    for (auto& [insert, xdict] : pendingXClips_) {
        const auto a = dictEntries_.find(xdict);
        if (a == dictEntries_.end() || !a->second.count("ACAD_FILTER"))
            continue;
        const auto b = dictEntries_.find(a->second.at("ACAD_FILTER"));
        if (b == dictEntries_.end() || !b->second.count("SPATIAL"))
            continue;
        const auto f = spatialFilters_.find(b->second.at("SPATIAL"));
        if (f == spatialFilters_.end() || f->second.points.size() < 2)
            continue;
        const auto& n = f->second.numbers40;
        insert->clip.clear();
        if (n.size() >= 24) {
            const std::size_t m = n.size() - 24;   // prima delle matrici possono esserci i piani di taglio
            for (Vec2 p : f->second.points)
                insert->clip.push_back({n[m + 0] * p.x + n[m + 1] * p.y + n[m + 3], n[m + 4] * p.x + n[m + 5] * p.y + n[m + 7]});
        } else {
            const Affine t = insert->affine();
            const double det = t.m11 * t.m22 - t.m12 * t.m21;
            if (std::abs(det) < 1e-300)
                continue;
            for (Vec2 w : f->second.points) {
                const double x = w.x - t.dx, y = w.y - t.dy;
                insert->clip.push_back({(t.m22 * x - t.m21 * y) / det, (-t.m12 * x + t.m11 * y) / det});
            }
        }
        insert->clipOn = true;
    }

    resolveAnnotations();
    // Multidirettrici: le frecce in più tornano nella direttrice principale.
    for (auto& [leg, mainHandle] : leaderLegs_) {
        const auto it = entityHandles_.find(mainHandle);
        if (it == entityHandles_.end() || it->second == leg || erased(leg) || erased(it->second)
            || it->second->type() != EntityType::Leader || leg->points.empty() || leg->id() == 0)
            continue;
        static_cast<LeaderEntity*>(it->second)->extraTips.push_back(leg->points.front());
        eraseOnce(leg);
    }
    // Gruppi delle schede dei layout, per nome (i layout sono già letti).
    for (const auto& [name, group] : layoutGroups_)
        for (const Layout& l : doc_.layouts())
            if (l.name == name)
                if (Layout* layout = doc_.findLayout(l.id))
                    layout->group = group;

    // Immagini raster: il file dalla loro definizione (IMAGEDEF).
    for (auto& [image, handle] : pendingImages_)
        if (const auto it = imageDefs_.find(handle); it != imageDefs_.end())
            image->path = it->second;

    // Testi delle direttrici: l'MTEXT collegato diventa il testo della direttrice.
    for (auto& [leader, handle] : pendingLeaderTexts_) {
        const auto it = mtextHandles_.find(handle);
        if (it == mtextHandles_.end() || it->second->id() == 0 || erased(leader) || erased(it->second))
            continue;
        leader->text = it->second->text;
        leader->textHeight = it->second->height;
        if (!leaderStyled_.count(leader))
            leader->textStyle = it->second->style;
        eraseOnce(it->second);
    }

    // Contorni delle finestre non rettangolari: diventano parte della finestra (si ricreano al salvataggio).
    for (auto& [vp, handle] : pendingClips_) {
        const auto it = paperHandles_.find(handle);
        if (it == paperHandles_.end() || erased(it->second))
            continue;
        const std::vector<Vec2> outline = closedOutline(*it->second, true);
        if (outline.size() < 3)
            continue;
        vp->setClip(outline);
        eraseOnce(it->second);
    }

    // Blocchi dinamici: ogni stato salvato (*U...) sa di quale blocco dinamico è.
    for (const auto& [anonymous, origin] : dynamicOrigins_)
        if (const auto it = blockRecords_.find(origin); it != blockRecords_.end() && !it->second.empty())
            if (auto block = doc_.findBlock(anonymous))
                block->dynamicName = it->second;

    // Ordine di visualizzazione (DRAWORDER) salvato nel file: gli oggetti si ordinano con l'handle della tabella
    // SORTENTSTABLE se c'è, altrimenti con il proprio (l'ordine di creazione).
    if (!sortHandles_.empty()) {
        const auto hex = [](const std::string& h) -> std::optional<std::uint64_t> {
            try {
                return std::stoull(h, nullptr, 16);
            } catch (...) {
                return std::nullopt;
            }
        };
        std::unordered_map<const Entity*, std::uint64_t> keys;
        for (const auto& [handle, e] : entityHandles_) {
            const auto it = sortHandles_.find(handle);
            if (const auto k = hex(it != sortHandles_.end() ? it->second : handle))
                keys[e] = *k;
        }
        doc_.sortLoaded([&keys](const Entity& e) -> std::optional<std::uint64_t> {
            const auto it = keys.find(&e);
            return it == keys.end() ? std::nullopt : std::optional<std::uint64_t>(it->second);
        });
        // Dentro i blocchi (planimetrie con retini e coperture...): stessa regola.
        std::unordered_map<const Entity*, std::uint64_t> blockKeys;
        for (const auto& [handle, e] : blockEntityHandles_) {
            const auto it = sortHandles_.find(handle);
            if (const auto k = hex(it != sortHandles_.end() ? it->second : handle))
                blockKeys[e] = *k;
        }
        for (const auto& block : doc_.blocks()) {
            auto& list = block->entities;
            std::vector<std::pair<std::uint64_t, std::size_t>> order;
            order.reserve(list.size());
            std::uint64_t last = 0;
            bool changed = false;
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (const auto it = blockKeys.find(list[i].get()); it != blockKeys.end()) {
                    changed = changed || it->second < last;
                    last = it->second;
                }
                order.emplace_back(last, i);
            }
            if (!changed)
                continue;
            std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<std::unique_ptr<Entity>> sorted;
            sorted.reserve(list.size());
            for (const auto& [k, i] : order)
                sorted.push_back(std::move(list[i]));
            list = std::move(sorted);
            block->invalidateBounds();
        }
    }
}

} // namespace

DxfReadResult readDxf(const std::filesystem::path& path, Document& doc)
{
    DxfReadResult result;

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        result.error = "Impossibile aprire il file.";
        return result;
    }

    std::string codeLine;
    std::string valueLine;
    std::string section;
    bool expectSectionName = false;
    std::optional<Record> current;
    long lineNumber = 0;
    Reader reader(doc, result);

    while (std::getline(in, codeLine) && std::getline(in, valueLine)) {
        lineNumber += 2;
        const std::string codeText = trimmed(codeLine);
        // I valori di testo possono iniziare o finire con spazi: si toglie solo il ritorno a capo.
        std::string value = valueLine;
        if (!value.empty() && value.back() == '\r')
            value.pop_back();

        int code = 0;
        const auto [ptr, ec] = std::from_chars(codeText.data(), codeText.data() + codeText.size(), code);
        if (ec != std::errc() || ptr != codeText.data() + codeText.size()) {
            result.error = "File DXF non valido (riga " + std::to_string(lineNumber - 1)
                + "). I DXF binari non sono supportati.";
            return result;
        }
        if (code != 1 && code != 3)
            value = trimmed(value);
        if (!validUtf8(value))
            value = cp1252ToUtf8(value);
        if (code == 1 || code == 2 || code == 3)
            value = decodeUnicodeEscapes(value);

        if (code == 0) {
            if (current)
                reader.process(*current, section);
            current.reset();

            if (value == "SECTION") {
                expectSectionName = true;
            } else if (value == "ENDSEC") {
                section.clear();
            } else if (value == "EOF") {
                break;
            } else {
                current = Record{value, {}};
            }
            continue;
        }

        if (expectSectionName && code == 2) {
            section = value;
            expectSectionName = false;
            if (section == "HEADER")
                current = Record{"HEADER", {}};   // variabili: coppie 9 $NOME + valore
            continue;
        }

        if (current)
            current->values.emplace_back(code, std::move(value));
    }

    if (current)
        reader.process(*current, section);
    reader.finish();

    result.ok = true;
    return result;
}

} // namespace cartesio
