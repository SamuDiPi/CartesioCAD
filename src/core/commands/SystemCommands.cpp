// Comandi "da riga di comando" pensati soprattutto per gli script (.scr):
// variabili di sistema (SETVAR e i loro nomi), -LAYER e COLOR.
#include "core/commands/CommandManager.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cartesio {

namespace {

std::string upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

std::optional<int> toInt(const std::string& s)
{
    int v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || ptr != s.data() + s.size())
        return std::nullopt;
    return v;
}

// Colore da testo: numero ACI, nome italiano o inglese, DALAYER/BYLAYER, DABLOCCO/BYBLOCK.
std::optional<int> parseColor(const std::string& text)
{
    const std::string t = upper(trimmed(text));
    if (const auto n = toInt(t); n && *n >= 0 && *n <= 256)
        return *n;
    static const std::pair<const char*, int> names[] = {
        {"ROSSO", 1}, {"RED", 1}, {"GIALLO", 2}, {"YELLOW", 2}, {"VERDE", 3}, {"GREEN", 3}, {"CIANO", 4},
        {"CYAN", 4}, {"BLU", 5}, {"BLUE", 5}, {"MAGENTA", 6}, {"BIANCO", 7}, {"WHITE", 7}, {"NERO", 7},
        {"BLACK", 7}, {"DALAYER", kColorByLayer}, {"BYLAYER", kColorByLayer}, {"DABLOCCO", kColorByBlock},
        {"BYBLOCK", kColorByBlock},
    };
    for (const auto& [name, value] : names)
        if (t == name)
            return value;
    return std::nullopt;
}

// ---------------------------------------------------------------- SETVAR

// Variabili gestite dal nucleo (CLAYER, CECOLOR, TEXTSIZE); le altre passano dall'interfaccia.
std::string coreVariable(const CommandContext& ctx, const std::string& name, bool* known)
{
    *known = true;
    if (name == "CLAYER")
        return ctx.document.layers()[static_cast<std::size_t>(ctx.document.currentLayer())].name;
    if (name == "CECOLOR") {
        const int c = ctx.document.currentColor();
        return c == kColorByLayer ? "BYLAYER" : c == kColorByBlock ? "BYBLOCK" : std::to_string(c);
    }
    if (name == "TEXTSIZE") {
        std::ostringstream out;
        out << textSizeSetting();
        return out.str();
    }
    if (name == "LTSCALE") {
        std::ostringstream out;
        out << ctx.document.ltscale();
        return out.str();
    }
    if (name == "INSUNITS")
        return std::to_string(ctx.document.insUnits());
    if (name == "PICKSTYLE")
        return ctx.document.groupSelection ? "1" : "0";
    if (name == "WIPEOUTFRAME")
        return std::to_string(ctx.document.wipeoutFrame);
    if (name == "CELTYPE")
        return ctx.document.currentLinetype();
    {
        const DimStyle& ds = ctx.document.currentDimStyle();
        std::ostringstream out;
        if (name == "DIMSTYLE")
            return ds.name;
        if (name == "DIMSCALE")
            return (out << ds.scale, out.str());
        if (name == "DIMTXT")
            return (out << ds.textHeight, out.str());
        if (name == "DIMASZ")
            return (out << ds.arrowSize, out.str());
        if (name == "DIMDEC")
            return std::to_string(ds.decimals);
    }
    if (name == "CELWEIGHT")
        return std::to_string(ctx.document.currentLineweight());
    if (name == "CANNOSCALE")
        return ctx.document.annotationScale();
    if (name == "TEXTSTYLE")
        return ctx.document.currentTextStyle();
    if (name == "ANNOALLVISIBLE")
        return ctx.document.annoAllVisible() ? "1" : "0";
    *known = false;
    return {};
}

// Scala di annotazione da testo: "1:50", "2:1", "1:2.5" (unità di carta : unità di disegno).
std::optional<AnnoScale> parseAnnoScale(const std::string& text)
{
    const std::string t = trimmed(text);
    const auto colon = t.find(':');
    if (colon == std::string::npos)
        return std::nullopt;
    try {
        const double paper = std::stod(t.substr(0, colon));
        const double drawing = std::stod(t.substr(colon + 1));
        if (paper > 0.0 && drawing > 0.0)
            return AnnoScale{t, paper, drawing};
    } catch (...) {
    }
    return std::nullopt;
}

// Scala dalla lista del disegno, per nome; un nome nella forma "1:75" che manca si aggiunge alla lista.
const AnnoScale* scaleByNameOrAdd(Document& doc, const std::string& text)
{
    if (const AnnoScale* s = doc.findAnnoScale(trimmed(text)))
        return s;
    const auto parsed = parseAnnoScale(text);
    if (!parsed)
        return nullptr;
    if (const AnnoScale* s = doc.annoScaleForFactor(parsed->factor()))
        return s;
    auto scales = doc.annoScales();
    scales.push_back(*parsed);
    doc.setAnnoScales(std::move(scales));
    return doc.findAnnoScale(parsed->name);
}

bool setCoreVariable(CommandContext& ctx, const std::string& name, const std::string& value, bool* known)
{
    *known = true;
    if (name == "CLAYER") {
        const int layer = ctx.document.findLayer(trimmed(value));
        if (layer < 0) {
            ctx.message("Layer \"" + trimmed(value) + "\" inesistente.");
            return false;
        }
        ctx.document.setCurrentLayer(layer);
        return true;
    }
    if (name == "CECOLOR") {
        const auto color = parseColor(value);
        if (!color) {
            ctx.message("Colore non valido.");
            return false;
        }
        ctx.document.setCurrentColor(*color);
        return true;
    }
    if (name == "INSUNITS") {
        try {
            const int v = std::stoi(value);
            if (v >= 0 && v <= 24) {
                ctx.document.setInsUnits(v);
                ctx.document.touch();
                return true;
            }
        } catch (...) {
        }
        ctx.message("Unità non valide (0 senza unità, 1 pollici, 4 mm, 5 cm, 6 m...).");
        return false;
    }
    if (name == "LTSCALE") {
        try {
            const double v = std::stod(value);
            if (v > 0.0) {
                ctx.document.setLtscale(v);
                return true;
            }
        } catch (...) {
        }
        ctx.message("Scala non valida.");
        return false;
    }
    if (name == "CELTYPE") {
        const std::string v = trimmed(value);
        std::string upper = v;
        for (char& c : upper)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == "BYLAYER" || upper == "BYBLOCK" || ctx.document.findLinetype(v)
            || ctx.document.loadStandardLinetype(v)) {
            const Linetype* lt = ctx.document.findLinetype(v);
            ctx.document.setCurrentLinetype(lt ? lt->name : upper == "BYLAYER" ? "ByLayer" : "ByBlock");
            return true;
        }
        ctx.message("Tipo di linea \"" + v + "\" sconosciuto.");
        return false;
    }
    if (name == "PICKSTYLE" || name == "WIPEOUTFRAME") {
        try {
            const int v = std::stoi(value);
            if (name == "PICKSTYLE")
                ctx.document.groupSelection = v != 0;
            else if (v >= 0 && v <= 2)
                ctx.document.wipeoutFrame = v;
            ctx.document.touch();
            return true;
        } catch (...) {
        }
        ctx.message("Valore non valido.");
        return false;
    }
    if (name == "DIMSTYLE") {
        const DimStyle* ds = ctx.document.findDimStyle(trimmed(value));
        if (!ds) {
            ctx.message("Stile di quota \"" + trimmed(value) + "\" inesistente.");
            return false;
        }
        ctx.document.setCurrentDimStyle(ds->name);
        return true;
    }
    if (name == "DIMSCALE" || name == "DIMTXT" || name == "DIMASZ" || name == "DIMDEC") {
        // Cambia lo stile corrente e le quote che lo usano.
        try {
            const double v = std::stod(value);
            DimStyle ds = ctx.document.currentDimStyle();
            if (name == "DIMDEC" && v >= 0 && v <= 8)
                ds.decimals = static_cast<int>(v);
            else if (v > 0.0 && name == "DIMSCALE")
                ds.scale = v;
            else if (v > 0.0 && name == "DIMTXT")
                ds.textHeight = v;
            else if (v > 0.0 && name == "DIMASZ")
                ds.arrowSize = v;
            else
                throw std::invalid_argument("valore");
            const std::string styleName = ds.name;
            ctx.document.setDimStyle(std::move(ds));
            ctx.document.restyleDimensions(styleName);
            return true;
        } catch (...) {
        }
        ctx.message("Valore non valido.");
        return false;
    }
    if (name == "CELWEIGHT") {
        try {
            const int v = std::stoi(value);
            if (v >= -3 && v <= 211) {
                ctx.document.setCurrentLineweight(v);
                return true;
            }
        } catch (...) {
        }
        ctx.message("Spessore non valido (centesimi di mm, -1 DaLayer, -2 DaBlocco, -3 Default).");
        return false;
    }
    if (name == "CANNOSCALE") {
        const AnnoScale* s = scaleByNameOrAdd(ctx.document, value);
        if (!s) {
            ctx.message("Scala \"" + trimmed(value) + "\" inesistente (es. 1:50).");
            return false;
        }
        ctx.document.setAnnotationScale(s->name);
        return true;
    }
    if (name == "TEXTSTYLE") {
        const TextStyle* s = ctx.document.findTextStyle(trimmed(value));
        if (!s) {
            ctx.message("Stile di testo \"" + trimmed(value) + "\" inesistente.");
            return false;
        }
        ctx.document.setCurrentTextStyle(s->name);
        return true;
    }
    if (name == "ANNOALLVISIBLE") {
        const std::string v = trimmed(value);
        if (v != "0" && v != "1") {
            ctx.message("Valore non valido (0 o 1).");
            return false;
        }
        ctx.document.setAnnoAllVisible(v == "1");
        return true;
    }
    if (name == "TEXTSIZE") {
        try {
            const double v = std::stod(value);
            if (v > 0.0) {
                textSizeSetting() = v;
                return true;
            }
        } catch (...) {
        }
        ctx.message("Altezza non valida.");
        return false;
    }
    *known = false;
    return false;
}

class SetvarCommand final : public Command {
public:
    explicit SetvarCommand(std::string name = {}) : name_(std::move(name)) {}

    std::string prompt() const override
    {
        if (name_.empty())
            return "Immettere nome della variabile:";
        return "Immettere nuovo valore per " + name_ + " <" + current_ + ">:";
    }

    CommandStatus start(CommandContext& ctx) override
    {
        if (!name_.empty())
            readCurrent(ctx);
        return CommandStatus::Continue;
    }

    bool wantsText() const override { return true; }
    bool textIsLine() const override { return false; }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        if (name_.empty()) {
            name_ = upper(trimmed(text));
            if (!readCurrent(ctx)) {
                ctx.message("Variabile sconosciuta: " + name_ + ".");
                return CommandStatus::Finished;
            }
            return CommandStatus::Continue;
        }
        bool known = false;
        if (!setCoreVariable(ctx, name_, text, &known) && !known) {
            if (!ctx.setVar || !ctx.setVar(name_, trimmed(text)))
                ctx.message("Valore non valido per " + name_ + ".");
        }
        return CommandStatus::Finished;
    }

private:
    bool readCurrent(CommandContext& ctx)
    {
        bool known = false;
        current_ = coreVariable(ctx, name_, &known);
        if (!known) {
            current_ = ctx.variable(name_);
            known = !current_.empty();
        }
        return known;
    }

    std::string name_;
    std::string current_;
};

// ---------------------------------------------------------------- -LAYER

// Gestione dei layer da riga di comando, come "-LAYER":
// Nuovo, Crea (crea e rende corrente), Imposta, Colore, ON, OFF, Congela, Scongela, Blocca, Sblocca.
class LayerCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Option:
            return "Immettere un'opzione [?/Crea/Imposta/Nuovo/ON/OFF/Colore/TIpolinea/SPessore/Congela/Scongela/Blocca/"
                   "Sblocca]:";
        case Stage::Color: return "Immettere colore (numero o nome):";
        case Stage::Linetype: return "Immettere tipo di linea (es. DASHED, CENTER):";
        case Stage::Lineweight: return "Immettere spessore di linea in mm (es. 0.35) o Default:";
        default: return "Immettere nome del layer:";
        }
    }

    bool wantsText() const override { return true; }
    bool textIsLine() const override { return false; }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const std::string value = trimmed(text);
        if (stage_ == Stage::Option) {
            const std::string o = upper(value);
            if (o == "?") {
                std::string list;
                for (const Layer& l : ctx.document.layers())
                    list += (list.empty() ? "" : ", ") + l.name;
                ctx.message("Layer: " + list);
            } else if (o == "N" || o == "NUOVO" || o == "NEW") {
                stage_ = Stage::New;
            } else if (o == "CR" || o == "CREA" || o == "M" || o == "MAKE") {
                stage_ = Stage::Make;
            } else if (o == "I" || o == "IMPOSTA" || o == "S" || o == "SET") {
                stage_ = Stage::Set;
            } else if (o == "ON") {
                stage_ = Stage::On;
            } else if (o == "OFF") {
                stage_ = Stage::Off;
            } else if (o == "C" || o == "COLORE" || o == "COLOR") {
                stage_ = Stage::Color;
            } else if (o == "TI" || o == "TIPOLINEA" || o == "L" || o == "LTYPE") {
                stage_ = Stage::Linetype;
            } else if (o == "SP" || o == "SPESSORE" || o == "LW" || o == "LWEIGHT") {
                stage_ = Stage::Lineweight;
            } else if (o == "CO" || o == "CONGELA" || o == "F" || o == "FREEZE") {
                stage_ = Stage::Freeze;
            } else if (o == "SC" || o == "SCONGELA" || o == "T" || o == "THAW") {
                stage_ = Stage::Thaw;
            } else if (o == "B" || o == "BLOCCA" || o == "LO" || o == "LOCK") {
                stage_ = Stage::Lock;
            } else if (o == "SB" || o == "SBLOCCA" || o == "U" || o == "UNLOCK") {
                stage_ = Stage::Unlock;
            } else {
                ctx.message("Opzione non riconosciuta.");
            }
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Linetype) {
            if (!ctx.document.findLinetype(value) && !ctx.document.loadStandardLinetype(value)) {
                ctx.message("Tipo di linea \"" + value + "\" sconosciuto.");
                return CommandStatus::Continue;
            }
            linetype_ = ctx.document.findLinetype(value)->name;
            stage_ = Stage::LinetypeLayer;
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Lineweight) {
            const std::string o = upper(value);
            if (o == "DEFAULT" || o == "D") {
                lineweight_ = kLineweightDefault;
            } else {
                try {
                    const int hundredths = static_cast<int>(std::lround(std::stod(value) * 100.0));
                    const auto& all = standardLineweights();
                    // Il valore standard più vicino.
                    lineweight_ = *std::min_element(all.begin(), all.end(), [hundredths](int a, int b) {
                        return std::abs(a - hundredths) < std::abs(b - hundredths);
                    });
                } catch (...) {
                    ctx.message("Spessore non valido.");
                    return CommandStatus::Continue;
                }
            }
            stage_ = Stage::LineweightLayer;
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Color) {
            const auto c = parseColor(value);
            if (!c || *c < 1 || *c > 255) {
                ctx.message("Colore non valido (1-255 o un nome).");
                return CommandStatus::Continue;
            }
            color_ = *c;
            stage_ = Stage::ColorLayer;
            return CommandStatus::Continue;
        }
        apply(ctx, value);
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (stage_ == Stage::Option)
            return CommandStatus::Finished;
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

private:
    enum class Stage {
        Option, New, Make, Set, On, Off, Color, ColorLayer, Linetype, LinetypeLayer, Lineweight, LineweightLayer,
        Freeze, Thaw, Lock, Unlock
    };

    void apply(CommandContext& ctx, const std::string& names)
    {
        Document& doc = ctx.document;
        // Più layer separati da virgole.
        std::stringstream list(names);
        std::string name;
        while (std::getline(list, name, ',')) {
            name = trimmed(name);
            if (name.empty())
                continue;
            int index = doc.findLayer(name);
            if (stage_ == Stage::New || stage_ == Stage::Make) {
                if (index < 0)
                    index = doc.addLayer(Layer{name, 7, true});
                if (stage_ == Stage::Make) {
                    Layer& l = doc.layer(index);
                    l.frozen = false;
                    l.visible = true;
                    doc.setCurrentLayer(index);
                }
                doc.touch();
                continue;
            }
            // Caratteri jolly: * (qualunque testo) e ? (un carattere).
            std::vector<int> indices;
            if (name.find_first_of("*?") != std::string::npos) {
                for (int i = 0; i < static_cast<int>(doc.layers().size()); ++i)
                    if (wildcardMatch(name, doc.layers()[static_cast<std::size_t>(i)].name))
                        indices.push_back(i);
            } else if (index >= 0) {
                indices.push_back(index);
            }
            if (indices.empty()) {
                ctx.message("Layer \"" + name + "\" inesistente.");
                continue;
            }
            for (int i : indices)
                applyTo(ctx, i);
        }
    }

    static bool wildcardMatch(const std::string& pattern, const std::string& text)
    {
        // Confronto senza distinzione fra maiuscole e minuscole, con ritorno sull'ultimo *.
        std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;
        const auto same = [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); };
        while (t < text.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || same(pattern[p], text[t]))) {
                ++p;
                ++t;
            } else if (p < pattern.size() && pattern[p] == '*') {
                star = p++;
                mark = t;
            } else if (star != std::string::npos) {
                p = star + 1;
                t = ++mark;
            } else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*')
            ++p;
        return p == pattern.size();
    }

    void applyTo(CommandContext& ctx, int index)
    {
        Document& doc = ctx.document;
        {
            Layer& l = doc.layer(index);
            switch (stage_) {
            case Stage::Set:
                if (l.frozen)
                    ctx.message("Un layer congelato non può diventare corrente.");
                else
                    doc.setCurrentLayer(index);
                break;
            case Stage::On: l.visible = true; break;
            case Stage::Off: l.visible = false; break;
            case Stage::ColorLayer: l.color = color_; break;
            case Stage::LinetypeLayer: l.linetype = linetype_; break;
            case Stage::LineweightLayer: l.lineweight = lineweight_; break;
            case Stage::Freeze:
                if (index == doc.currentLayer())
                    ctx.message("Impossibile congelare il layer corrente.");
                else
                    l.frozen = true;
                break;
            case Stage::Thaw: l.frozen = false; break;
            case Stage::Lock: l.locked = true; break;
            case Stage::Unlock: l.locked = false; break;
            default: break;
            }
            doc.touch();
        }
    }

    Stage stage_ = Stage::Option;
    int color_ = 7;
    std::string linetype_ = "Continuous";
    int lineweight_ = kLineweightDefault;
};

// ------------------------------------------------------------------ COLOR

class ColorCommand final : public Command {
public:
    std::string prompt() const override { return "Immettere colore corrente (numero, nome o DALAYER):"; }
    bool wantsText() const override { return true; }
    bool textIsLine() const override { return false; }
    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const auto c = parseColor(text);
        if (!c) {
            ctx.message("Colore non valido.");
            return CommandStatus::Continue;
        }
        ctx.document.setCurrentColor(*c);
        return CommandStatus::Finished;
    }
};


// -LINETYPE: carica i tipi di linea standard e imposta quello corrente (utile negli script).
class LinetypeCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Load: return "Tipi di linea da caricare (separati da virgola, * = tutti):";
        case Stage::Set: return "Tipo di linea corrente (nome, DaLayer o DaBlocco):";
        default: return "Immettere un'opzione [?/Carica/Imposta]:";
        }
    }

    bool wantsText() const override { return true; }
    bool textIsLine() const override { return false; }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const std::string value = trimmed(text);
        Document& doc = ctx.document;
        switch (stage_) {
        case Stage::Option: {
            const std::string o = upper(value);
            if (o == "?") {
                std::string list;
                for (const Linetype& lt : doc.linetypes())
                    list += (list.empty() ? "" : ", ") + lt.name;
                std::string available;
                for (const Linetype& lt : standardLinetypes())
                    available += (available.empty() ? "" : ", ") + lt.name;
                ctx.message("Caricati: " + list + ". Disponibili: " + available + ".");
            } else if (o == "C" || o == "CARICA" || o == "L" || o == "LOAD") {
                stage_ = Stage::Load;
            } else if (o == "I" || o == "IMPOSTA" || o == "S" || o == "SET") {
                stage_ = Stage::Set;
            } else {
                ctx.message("Opzione non riconosciuta.");
            }
            return CommandStatus::Continue;
        }
        case Stage::Load: {
            std::stringstream list(value);
            std::string name;
            while (std::getline(list, name, ',')) {
                name = trimmed(name);
                if (name == "*") {
                    for (const Linetype& lt : standardLinetypes())
                        doc.loadStandardLinetype(lt.name);
                } else if (!name.empty() && !doc.loadStandardLinetype(name)) {
                    ctx.message("Tipo di linea \"" + name + "\" sconosciuto.");
                }
            }
            stage_ = Stage::Option;
            return CommandStatus::Continue;
        }
        case Stage::Set: {
            const std::string o = upper(value);
            if (o == "DALAYER" || o == "BYLAYER") {
                doc.setCurrentLinetype("ByLayer");
            } else if (o == "DABLOCCO" || o == "BYBLOCK") {
                doc.setCurrentLinetype("ByBlock");
            } else if (doc.findLinetype(value) || doc.loadStandardLinetype(value)) {
                doc.setCurrentLinetype(doc.findLinetype(value)->name);
            } else {
                ctx.message("Tipo di linea \"" + value + "\" sconosciuto.");
                return CommandStatus::Continue;
            }
            stage_ = Stage::Option;
            return CommandStatus::Continue;
        }
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (stage_ == Stage::Option)
            return CommandStatus::Finished;
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

private:
    enum class Stage { Option, Load, Set };
    Stage stage_ = Stage::Option;
};


// ------------------------------------------------------------- strumenti layer

// Stato dei layer prima di LAYISO, per LAYUNISO (per disegno).
std::map<const Document*, std::vector<std::pair<std::string, bool>>>& isolationBackup()
{
    static std::map<const Document*, std::vector<std::pair<std::string, bool>>> backup;
    return backup;
}

// LAYOFF, LAYFRZ, LAYLCK, LAYISO: agiscono sui layer degli oggetti selezionati.
class LayerOfObjectsCommand final : public SelectingCommand {
public:
    enum Op { Off, Freeze, Lock, Isolate };
    explicit LayerOfObjectsCommand(Op op) : op_(op) {}

protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        Document& doc = ctx.document;
        std::set<int> layers;
        for (EntityId id : ctx.selection)
            if (const Entity* e = doc.find(id))
                layers.insert(e->layer() < 0 ? 0 : e->layer());
        ctx.selection.clear();
        std::string names;
        for (int l : layers)
            names += (names.empty() ? "" : ", ") + doc.layers()[static_cast<std::size_t>(l)].name;

        if (op_ == Isolate) {
            auto& backup = isolationBackup()[&doc];
            backup.clear();
            for (const Layer& l : doc.layers())
                backup.emplace_back(l.name, l.visible);
            if (!layers.count(doc.currentLayer()))
                doc.setCurrentLayer(*layers.begin());
            for (int i = 0; i < static_cast<int>(doc.layers().size()); ++i)
                doc.layer(i).visible = layers.count(i) > 0;
            ctx.message("Layer isolati: " + names + ". LAYUNISO per ripristinare.");
        } else {
            for (int l : layers) {
                Layer& layer = doc.layer(l);
                if (op_ == Off)
                    layer.visible = false;
                else if (op_ == Lock)
                    layer.locked = true;
                else if (l == doc.currentLayer())
                    ctx.message("Il layer corrente " + layer.name + " non può essere congelato.");
                else
                    layer.frozen = true;
            }
            ctx.message(std::string(op_ == Off ? "Spenti" : op_ == Lock ? "Bloccati" : "Congelati") + ": " + names + ".");
        }
        doc.touch();
        return CommandStatus::Finished;
    }

private:
    Op op_;
};

// LAYULK: un clic su un oggetto di un layer bloccato lo sblocca (gli oggetti bloccati non si selezionano).
class LayerUnlockCommand final : public Command {
public:
    std::string prompt() const override { return "Selezionare un oggetto sul layer da sbloccare:"; }
    bool wantsSelection() const override { return true; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* best = nullptr;
        double bestDistance = ctx.pickTolerance;
        for (const Entity* e : ctx.document.entitiesNear(p, ctx.pickTolerance)) {
            const double d = e->distanceTo(p);
            if (d <= bestDistance) {
                bestDistance = d;
                best = e;
            }
        }
        if (!best) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }
        Layer& layer = ctx.document.layer(best->layer() < 0 ? 0 : best->layer());
        layer.locked = false;
        ctx.document.touch();
        ctx.message("Layer sbloccato: " + layer.name + ".");
        return CommandStatus::Finished;
    }
};

// LAYTHW: scongela tutti i layer. LAYUNISO: annulla l'ultimo LAYISO.
class LayerAllCommand final : public Command {
public:
    explicit LayerAllCommand(bool unisolate) : unisolate_(unisolate) {}
    std::string prompt() const override { return {}; }

    CommandStatus start(CommandContext& ctx) override
    {
        Document& doc = ctx.document;
        if (unisolate_) {
            auto& backups = isolationBackup();
            const auto it = backups.find(&doc);
            if (it == backups.end()) {
                ctx.message("Nessun layer isolato con LAYISO.");
                return CommandStatus::Finished;
            }
            for (const auto& [name, visible] : it->second)
                if (const int l = doc.findLayer(name); l >= 0)
                    doc.layer(l).visible = visible;
            backups.erase(it);
            ctx.message("Layer ripristinati.");
        } else {
            for (int i = 0; i < static_cast<int>(doc.layers().size()); ++i)
                doc.layer(i).frozen = false;
            ctx.message("Tutti i layer sono scongelati.");
        }
        doc.touch();
        return CommandStatus::Finished;
    }

private:
    bool unisolate_;
};

// LAYMATCH: gli oggetti selezionati vanno sul layer di un oggetto di riferimento.
class LayerMatchCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        ids_ = ctx.selection;
        return CommandStatus::Continue;
    }
    std::string actionPrompt() const override { return "Selezionare l'oggetto sul layer di destinazione:"; }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* target = ctx.document.pick(p, ctx.pickTolerance);
        if (!target) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }
        const int layer = target->layer();
        std::vector<std::unique_ptr<Entity>> modified;
        for (EntityId id : ids_)
            if (const Entity* e = ctx.document.find(id)) {
                auto copy = e->clone();
                copy->setLayer(layer);
                modified.push_back(std::move(copy));
            }
        ctx.selection.clear();
        ctx.message(std::to_string(modified.size()) + " oggetti spostati sul layer "
                    + ctx.document.layers()[static_cast<std::size_t>(layer)].name + ".");
        ctx.document.commit(std::move(modified), ids_);
        return CommandStatus::Finished;
    }

private:
    std::vector<EntityId> ids_;
};

// OBJECTSCALE: aggiunge o toglie scale di annotazione agli oggetti annotativi selezionati.
class ObjectScaleCommand final : public SelectingCommand {
public:
    bool wantsText() const override { return selected_; }
    bool textIsLine() const override { return false; }

protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id); e && e->annotative())
                ids_.push_back(id);
        ctx.selection.clear();
        if (ids_.empty()) {
            ctx.message("Nessun oggetto annotativo selezionato (testi, quote e direttrici con stile annotativo, "
                        "o con la proprietà Annotativo).");
            return CommandStatus::Finished;
        }
        selected_ = true;
        report(ctx);
        return CommandStatus::Continue;
    }

    std::string actionPrompt() const override
    {
        switch (stage_) {
        case Stage::Add: return "Scala da aggiungere (es. 1:50):";
        case Stage::Remove: return "Scala da eliminare:";
        default: return "Immettere un'opzione [Aggiungi/Elimina] <Fine>:";
        }
    }

    CommandStatus actionEnter(CommandContext&) override
    {
        if (stage_ == Stage::Option)
            return CommandStatus::Finished;
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& word) override { return onText(ctx, word); }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const std::string v = trimmed(text);
        Document& doc = ctx.document;
        if (stage_ == Stage::Option) {
            const std::string o = upper(v);
            if (o == "A" || o == "AGGIUNGI" || o == "ADD")
                stage_ = Stage::Add;
            else if (o == "E" || o == "ELIMINA" || o == "D" || o == "DELETE")
                stage_ = Stage::Remove;
            else
                ctx.message("Opzione non riconosciuta.");
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Add) {
            const AnnoScale* s = scaleByNameOrAdd(doc, v);
            if (!s) {
                ctx.message("Scala \"" + v + "\" inesistente (es. 1:50).");
                return CommandStatus::Continue;
            }
            const int n = doc.addObjectScale(ids_, s->name);
            ctx.message("Scala " + s->name + " aggiunta a " + std::to_string(n) + " oggetti.");
        } else {
            const int n = doc.removeObjectScale(ids_, v);
            ctx.message(n > 0 ? "Scala " + v + " tolta da " + std::to_string(n) + " oggetti."
                              : "Nessun oggetto aveva la scala " + v + " (l'ultima scala di un oggetto resta).");
        }
        stage_ = Stage::Option;
        report(ctx);
        return CommandStatus::Continue;
    }

private:
    enum class Stage { Option, Add, Remove };
    void report(CommandContext& ctx) const
    {
        std::set<std::string> names;
        for (EntityId id : ids_)
            if (const Entity* e = ctx.document.find(id))
                for (const std::string& s : e->annoScales())
                    names.insert(s);
        std::string list;
        for (const std::string& s : names)
            list += (list.empty() ? "" : ", ") + s;
        ctx.message(std::to_string(ids_.size()) + " oggetti annotativi. Scale: " + list + ".");
    }
    std::vector<EntityId> ids_;
    bool selected_ = false;
    Stage stage_ = Stage::Option;
};

// SCALELISTEDIT: la lista delle scale di annotazione del disegno.
class ScaleListCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Add: return "Scala da aggiungere (carta:disegno, es. 1:75):";
        case Stage::Remove: return "Scala da eliminare:";
        default: return "Immettere un'opzione [?/Aggiungi/Elimina/Reimposta] <Fine>:";
        }
    }
    bool wantsText() const override { return true; }
    bool textIsLine() const override { return false; }

    CommandStatus start(CommandContext& ctx) override
    {
        list(ctx);
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (stage_ == Stage::Option)
            return CommandStatus::Finished;
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const std::string v = trimmed(text);
        Document& doc = ctx.document;
        if (stage_ == Stage::Option) {
            const std::string o = upper(v);
            if (o == "?")
                list(ctx);
            else if (o == "A" || o == "AGGIUNGI" || o == "ADD")
                stage_ = Stage::Add;
            else if (o == "E" || o == "ELIMINA" || o == "D" || o == "DELETE")
                stage_ = Stage::Remove;
            else if (o == "R" || o == "REIMPOSTA" || o == "RESET") {
                auto scales = defaultAnnoScales();
                for (const AnnoScale& s : doc.annoScales())   // le scale usate dagli oggetti restano
                    if (used(doc, s.name) && std::none_of(scales.begin(), scales.end(), [&](const AnnoScale& d) { return d.name == s.name; }))
                        scales.push_back(s);
                doc.setAnnoScales(std::move(scales));
                list(ctx);
            } else
                ctx.message("Opzione non riconosciuta.");
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Add) {
            const auto parsed = parseAnnoScale(v);
            if (!parsed) {
                ctx.message("Scala non valida (es. 1:75 o 2:1).");
                return CommandStatus::Continue;
            }
            if (doc.findAnnoScale(parsed->name)) {
                ctx.message("La scala " + parsed->name + " c'è già.");
            } else {
                auto scales = doc.annoScales();
                scales.push_back(*parsed);
                doc.setAnnoScales(std::move(scales));
                ctx.message("Scala " + parsed->name + " aggiunta.");
            }
        } else {
            const AnnoScale* s = doc.findAnnoScale(v);
            if (!s) {
                ctx.message("La scala " + v + " non è nella lista.");
            } else if (used(doc, s->name) || s->name == doc.annotationScale()) {
                ctx.message("La scala " + s->name + " è usata (dagli oggetti o come scala corrente): non si elimina.");
            } else {
                const std::string name = s->name;
                auto scales = doc.annoScales();
                std::erase_if(scales, [&](const AnnoScale& x) { return x.name == name; });
                doc.setAnnoScales(std::move(scales));
                ctx.message("Scala " + name + " eliminata.");
            }
        }
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

private:
    enum class Stage { Option, Add, Remove };
    static bool used(const Document& doc, const std::string& name)
    {
        for (const Entity* e : doc.entitiesNear({0.0, 0.0}, 1e12, 0))
            for (const std::string& s : e->annoScales())
                if (s == name)
                    return true;
        return false;
    }
    void list(CommandContext& ctx) const
    {
        std::string names;
        for (const AnnoScale& s : ctx.document.annoScales())
            names += (names.empty() ? "" : ", ") + s.name;
        ctx.message("Scale: " + names + ". Corrente: " + ctx.document.annotationScale() + ".");
    }
    Stage stage_ = Stage::Option;
};

} // namespace

void registerSystemCommands(CommandManager& manager)
{
    manager.add("OBJECTSCALE", {"SCALAOGGETTO", "-OBJECTSCALE"}, [] { return std::make_unique<ObjectScaleCommand>(); });
    manager.add("SCALELISTEDIT", {"-SCALELISTEDIT", "MODIFICAELENCOSCALE"}, [] { return std::make_unique<ScaleListCommand>(); });
    manager.add("SETVAR", {"SET", "IMPOSTAVAR"}, [] { return std::make_unique<SetvarCommand>(); });
    for (const char* name : {"CLAYER", "CECOLOR", "TEXTSIZE", "OSMODE", "ORTHOMODE", "GRIDMODE", "SNAPMODE",
                             "DYNMODE", "ATTREQ", "ATTDIA", "FILEDIA", "CMDECHO", "EXPERT", "TILEMODE",
                             "LTSCALE", "INSUNITS", "CELTYPE", "CELWEIGHT", "LWDISPLAY", "DIMSCALE", "DIMTXT", "DIMASZ", "DIMDEC", "PICKSTYLE", "WIPEOUTFRAME", "CANNOSCALE", "ANNOALLVISIBLE", "TEXTSTYLE"}) {
        const std::string n = name;
        manager.add(n, {}, [n] { return std::make_unique<SetvarCommand>(n); });
    }
    manager.add("-LAYER", {"-LA"}, [] { return std::make_unique<LayerCommand>(); });
    manager.add("-DIMSTYLE", {"-D"}, [] { return std::make_unique<SetvarCommand>("DIMSTYLE"); });
    manager.add("LAYOFF", {"LAYSPEGNI"}, [] { return std::make_unique<LayerOfObjectsCommand>(LayerOfObjectsCommand::Off); });
    manager.add("LAYFRZ", {"LAYCONGELA"}, [] { return std::make_unique<LayerOfObjectsCommand>(LayerOfObjectsCommand::Freeze); });
    manager.add("LAYLCK", {"LAYBLOCCA"}, [] { return std::make_unique<LayerOfObjectsCommand>(LayerOfObjectsCommand::Lock); });
    manager.add("LAYISO", {"LAYISOLA"}, [] { return std::make_unique<LayerOfObjectsCommand>(LayerOfObjectsCommand::Isolate); });
    manager.add("LAYULK", {"LAYSBLOCCA"}, [] { return std::make_unique<LayerUnlockCommand>(); });
    manager.add("LAYTHW", {"LAYSCONGELA"}, [] { return std::make_unique<LayerAllCommand>(false); });
    manager.add("LAYUNISO", {"LAYDISISOLA"}, [] { return std::make_unique<LayerAllCommand>(true); });
    manager.add("LAYMATCH", {"LAYCORRISP"}, [] { return std::make_unique<LayerMatchCommand>(); });
    manager.add("-LINETYPE", {"-LT", "-TIPOLINEA"}, [] { return std::make_unique<LinetypeCommand>(); });
    manager.add("COLOR", {"COL", "COLORE", "-COLOR"}, [] { return std::make_unique<ColorCommand>(); });
}

} // namespace cartesio
