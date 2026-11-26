#include "core/Switchboard.h"

#include "core/Entity.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <regex>
#include <set>
#include <sstream>

namespace cartesio {

namespace {

constexpr double kModule = 18.0;        // mm, modulo DIN
constexpr double kDinRow = 150.0;       // mm, fila DIN (apparecchi e coperture)
constexpr double kFloorHeight = 2000.0; // mm, colonna a pavimento
constexpr double kFloorUsable = 1800.0; // mm utili (sopra le sbarre e sotto l'arrivo cavi)
constexpr double kWallMargin = 150.0;   // mm sopra e sotto le file (totale)
constexpr double kSideMargin = 50.0;    // mm per lato dentro la colonna

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

double toNumber(std::string s)
{
    std::replace(s.begin(), s.end(), ',', '.');
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// "MARCA / SERIE / 4 x 2.500" -> 4 poli, 2500 A (l'ultimo "n x In"; il punto separa le migliaia).
bool polesAndRated(const std::string& field, int* poles, double* rated)
{
    static const std::regex re(R"((\d)\s*x\s*(\d[\d.]*))");
    bool found = false;
    for (auto it = std::sregex_iterator(field.begin(), field.end(), re); it != std::sregex_iterator(); ++it) {
        std::string value = (*it)[2];
        value.erase(std::remove(value.begin(), value.end(), '.'), value.end());
        *poles = std::stoi((*it)[1]);
        *rated = toNumber(value);
        found = true;
    }
    return found && *poles > 0 && *rated > 0.0;
}

// Formato del fusibile ("10.3x38", "22x58"): moduli per polo del portafusibili.
double fuseModulesPerPole(const std::string& model)
{
    static const std::regex re(R"((\d+(?:[.,]\d+)?)\s*x\s*(\d+(?:[.,]\d+)?))");
    std::smatch m;
    if (!std::regex_search(model, m, re))
        return 1.0;
    const double diameter = toNumber(m[1]);
    if (diameter >= 20.0)
        return 2.0;
    if (diameter >= 13.0)
        return 1.5;
    return 1.0;
}

// Classe dell'SPD dal modello o dalla descrizione: 1 = tipo 1+2 (o 1), 2 = tipo 2 o 3, 0 = nessun SPD.
int spdClass(const std::string& model, const std::string& description)
{
    const std::string m = lower(model);
    if (contains(m, "t1+t2") || std::regex_search(m, std::regex(R"((^|[^a-z0-9])t1([^0-9]|$))")))
        return 1;
    if (std::regex_search(m, std::regex(R"((^|[^a-z0-9])t[23]([^0-9]|$))")))
        return 2;
    if (contains(" " + lower(description) + " ", " spd "))
        return 2;
    return 0;
}

int accessoryCount(const std::string& model)
{
    const auto plus = std::count(model.begin(), model.end(), '+');
    return static_cast<int>(std::min<std::ptrdiff_t>(plus, 2));
}

double modularPerPole(double rated) { return rated > 63.0 ? 1.5 : 1.0; }

// Scatolati: ingombro tipico per taglia di corrente (larghezza per polo e altezza) e altezza della fila.
struct Frame {
    double perPole;
    double height;
    double row;
};
Frame mouldedFrame(double rated)
{
    if (rated <= 250.0)
        return {35.0, 161.0, 300.0};
    if (rated <= 630.0)
        return {46.0, 255.0, 400.0};
    return {70.0, 327.0, 500.0};
}

double airColumnWidth(double width)
{
    for (const double w : {600.0, 800.0, 1000.0, 1200.0})
        if (w >= width + 140.0)
            return w;
    return 1200.0;
}

// Morsetti di una linea dalla formazione del cavo ("1(5G4)", "4(1x35)+(1PE16)", "3(2x1x240)+(1x240)+(1PE240)").
void addTerminals(const std::string& cable, BoardTerminals& t)
{
    static const std::regex group(R"((\d*)\(([^)]*)\))");
    for (auto it = std::sregex_iterator(cable.begin(), cable.end(), group); it != std::sregex_iterator(); ++it) {
        const std::string multiplierText = (*it)[1];
        const int multiplier = multiplierText.empty() ? 1 : std::max(1, std::atoi(multiplierText.c_str()));
        std::string inner = (*it)[2];
        double cores = 1.0, section = 0.0;
        const auto g = inner.find_first_of("Gg");
        if (g != std::string::npos) {
            cores = toNumber(inner.substr(0, g));
            section = toNumber(inner.substr(g + 1));
        } else {
            const auto pe = inner.find("PE");
            if (pe != std::string::npos) {
                cores = std::max(1.0, toNumber(inner.substr(0, pe)));
                section = toNumber(inner.substr(pe + 2));
            } else {
                std::vector<double> parts;
                std::stringstream ss(inner);
                std::string part;
                while (std::getline(ss, part, 'x'))
                    parts.push_back(toNumber(part));
                if (parts.empty())
                    continue;
                section = parts.back();
                cores = 1.0;
                for (std::size_t i = 0; i + 1 < parts.size(); ++i)
                    cores *= std::max(1.0, parts[i]);
            }
        }
        if (section <= 0.0 || section > 35.0)
            continue;   // oltre 35 mm² il cavo arriva all'apparecchio
        const double w = section <= 2.5 ? 5.2 : section <= 4 ? 6.2 : section <= 10 ? 8.2 : section <= 16 ? 10.2
                       : section <= 25 ? 12.2 : 16.2;
        const int n = static_cast<int>(std::lround(multiplier * cores));
        t.count += n;
        t.width += n * w;
    }
}

// Ingombro dei cavi di una partenza: area esterna (mm²) e sezione più grande, dalla formazione. Diametri tipici
// dei cavi in gomma o PVC: unipolare 1,35*sqrt(S) + 4,5 mm, multipolare 1,7*sqrt(n*S) + 5 mm.
void cableBundle(const std::string& cable, double* area, double* maxSection)
{
    static const std::regex group(R"((\d*)\(([^)]*)\))");
    const auto add = [&](double count, double cores, double section, bool multipolar) {
        if (section <= 0.0 || count <= 0.0)
            return;
        const double d = multipolar ? 1.7 * std::sqrt(cores * section) + 5.0 : 1.35 * std::sqrt(section) + 4.5;
        *area += count * kPi * d * d / 4.0;
        *maxSection = std::max(*maxSection, section);
    };
    for (auto it = std::sregex_iterator(cable.begin(), cable.end(), group); it != std::sregex_iterator(); ++it) {
        const std::string multiplierText = (*it)[1];
        const double multiplier = multiplierText.empty() ? 1.0 : std::max(1, std::atoi(multiplierText.c_str()));
        const std::string inner = (*it)[2];
        if (const auto g = inner.find_first_of("Gg"); g != std::string::npos) {
            add(multiplier, std::max(1.0, toNumber(inner.substr(0, g))), toNumber(inner.substr(g + 1)), true);
            continue;
        }
        if (const auto pe = inner.find("PE"); pe != std::string::npos) {
            add(multiplier * std::max(1.0, toNumber(inner.substr(0, pe))), 1.0, toNumber(inner.substr(pe + 2)), false);
            continue;
        }
        std::vector<double> parts;
        std::stringstream ss(inner);
        std::string part;
        while (std::getline(ss, part, 'x'))
            parts.push_back(toNumber(part));
        if (parts.empty())
            continue;
        const double section = parts.back();
        if (parts.size() == 1)
            add(multiplier, 1.0, section, false);
        else if (parts.size() == 2)   // "1x95" unipolare, "4x16" multipolare
            add(multiplier, parts[0], section, parts[0] > 1.0);
        else                          // "2x1x240": 2 cavi in parallelo (unipolari se il secondo numero è 1)
            add(multiplier * std::max(1.0, parts[0]), parts[1], section, parts[1] > 1.0);
    }
}

std::string number(double v, int decimals = 0)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, v);
    std::string s = buffer;
    if (decimals > 0) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    return s;
}

std::string shortCode(const std::string& circuit)
{
    const auto p = circuit.rfind(' ');
    return p == std::string::npos ? circuit : circuit.substr(p + 1);
}

} // namespace

const char* boardDeviceKindName(BoardDevice::Kind kind)
{
    switch (kind) {
    case BoardDevice::Kind::Modular: return "interruttore modulare";
    case BoardDevice::Kind::Fuse: return "portafusibili";
    case BoardDevice::Kind::Spd: return "SPD";
    case BoardDevice::Kind::Switch: return "sezionatore";
    case BoardDevice::Kind::MouldedCase: return "scatolato";
    case BoardDevice::Kind::AirCircuit: return "aperto";
    case BoardDevice::Kind::Meter: return "strumento";
    case BoardDevice::Kind::Supply: return "alimentatore/trasformatore";
    }
    return "";
}

std::vector<BoardInput> boardsFromCircuits(const std::vector<Circuit>& circuits)
{
    std::vector<BoardInput> boards;
    std::map<std::string, std::size_t> index;
    for (const Circuit& c : circuits) {
        auto [it, added] = index.emplace(c.board, boards.size());
        if (added) {
            boards.push_back(BoardInput{});
            boards.back().board = c.board;
        }
        BoardInput& board = boards[it->second];

        if (c.number != 0 && !contains(lower(c.cable), "nessuna linea"))
            addTerminals(c.cable, board.terminals);

        BoardDevice d;
        d.circuit = c.code;
        d.description = c.description;
        if (c.number != 0 && !contains(lower(c.cable), "nessuna linea"))
            cableBundle(c.cable, &d.cableArea, &d.cableSection);
        const std::string exec = lower(c.breakerExecution);
        const std::string descr = lower(c.description);
        const bool hasDevice = polesAndRated(c.breakerPoles, &d.poles, &d.rated);
        if (!hasDevice) {
            if (contains(descr, "trasformatore") || contains(descr, "alimentatore")) {
                d.kind = BoardDevice::Kind::Supply;
                d.modules = 4.0;
                board.devices.push_back(d);
            }
            continue;
        }
        // "SCATOLATO/MODULARE / N.C." -> esecuzione principale, accessorio, curva.
        std::string left = exec, curve;
        if (const auto slash = exec.find(" / "); slash != std::string::npos) {
            left = exec.substr(0, slash);
            curve = trim(exec.substr(slash + 3));
        }
        std::string main = trim(left), accessory;
        if (const auto s = left.find('/'); s != std::string::npos) {
            main = trim(left.substr(0, s));
            accessory = trim(left.substr(s + 1));
        }
        const std::string diff = trim(c.differential);
        d.residual = !diff.empty() && diff.rfind("---", 0) != 0;
        const bool noTrip = curve.empty() || curve.rfind("---", 0) == 0;
        const bool fuse = curve == "gl" || curve == "gg" || curve == "am" || curve == "gl-gg";
        const int spd = spdClass(c.breakerModel, c.description);
        const double poles = d.poles;

        if (main == "aperto") {
            d.kind = BoardDevice::Kind::AirCircuit;
            d.width = poles * (d.rated > 1600.0 ? 130.0 : 115.0);
            d.height = 440.0;
        } else if (main == "scatolato") {
            d.kind = noTrip ? BoardDevice::Kind::Switch : BoardDevice::Kind::MouldedCase;
            const Frame f = mouldedFrame(d.rated);
            d.width = poles * f.perPole;
            d.height = f.height;
            if (accessory == "modulare") {
                d.modules = 2.0;   // relè differenziale o accessorio su guida
                d.note = d.residual ? "relè differenziale su guida" : "accessorio su guida";
            }
        } else if (main == "modulare") {
            if (fuse) {
                d.kind = spd ? BoardDevice::Kind::Spd : BoardDevice::Kind::Fuse;
                d.modules = poles * fuseModulesPerPole(c.breakerModel);
                if (spd) {
                    d.modules += poles * (spd == 1 ? 2.0 : 1.0);
                    d.note = spd == 1 ? "SPD tipo 1+2 con portafusibili" : "SPD tipo 2 con portafusibili";
                }
            } else if (noTrip) {
                d.kind = BoardDevice::Kind::Switch;
                d.modules = poles * modularPerPole(d.rated);
            } else {
                d.kind = BoardDevice::Kind::Modular;
                d.modules = poles * modularPerPole(d.rated);
                int extra = accessory == "modulare" ? std::max(1, accessoryCount(c.breakerModel))
                                                    : accessoryCount(c.breakerModel);
                if (extra > 0) {
                    d.modules += extra * poles * modularPerPole(d.rated);
                    d.note = d.residual ? "blocco differenziale" : "accessorio modulare";
                    if (extra > 1)
                        d.note += " + accessorio";
                }
            }
            if (contains(descr, "multimetro") || contains(descr, "strumento") || contains(descr, "presenza tensione")) {
                d.modules += 4.0;
                d.note = d.note.empty() ? "strumento su guida" : d.note + ", strumento su guida";
            }
        } else {
            continue;   // esecuzione non riconosciuta (per esempio media tensione)
        }
        board.devices.push_back(d);
        board.incomer = std::max(board.incomer, d.rated);   // l'apparecchio più grande è l'arrivo o il generale
    }
    return boards;
}

int proposedIp(BoardEnvironment environment)
{
    switch (environment) {
    case BoardEnvironment::TechnicalRoom: return 31;
    case BoardEnvironment::Ordinary: return 43;
    case BoardEnvironment::Outdoor: return 55;
    case BoardEnvironment::Washdown: return 65;
    }
    return 31;
}

std::string proposedForm(double incomer)
{
    if (incomer <= 250.0)
        return "1";
    if (incomer <= 1600.0)
        return "2b";
    return "3b";
}

std::vector<EnclosureType> builtinEnclosures()
{
    std::vector<EnclosureType> out;
    const auto add = [&out](BoardMounting m, double w, double h, double d, int ip, int modules) {
        EnclosureType e;
        const char* prefix = m == BoardMounting::Wall ? "PAR" : m == BoardMounting::Floor ? "PAV" : "CAS";
        e.id = std::string(prefix) + "-" + number(w) + "x" + number(h) + "x" + number(d) + "-IP" + std::to_string(ip);
        e.mounting = m;
        e.width = w;
        e.height = h;
        e.depth = d;
        e.ip = ip;
        e.modulesPerRow = modules;
        e.source = "tipo generico CartesioCAD (misure tipiche da confermare)";
        out.push_back(e);
    };
    for (const int ip : {31, 43, 55, 65}) {
        for (const auto& [w, modules] : {std::pair{600.0, 24}, std::pair{800.0, 36}})
            for (const double h : {450.0, 600.0, 750.0, 900.0, 1050.0, 1200.0})
                for (const double d : {200.0, 250.0})
                    add(BoardMounting::Wall, w, h, d, ip, modules);
        if (ip <= 55)
            for (const double w : {300.0, 400.0, 600.0, 800.0, 900.0, 1000.0, 1100.0, 1200.0})
                for (const double d : {400.0, 600.0, 800.0})
                    add(BoardMounting::Floor, w, kFloorHeight, d, ip, w >= 800.0 ? 36 : 24);
    }
    for (const int ip : {40, 65})
        for (const auto& [w, h, modules, rows] : {std::tuple{300.0, 250.0, 12, 1}, std::tuple{300.0, 400.0, 12, 2},
                                                  std::tuple{300.0, 550.0, 12, 3}, std::tuple{410.0, 550.0, 18, 3},
                                                  std::tuple{410.0, 700.0, 18, 4}}) {
            add(BoardMounting::Box, w, h, 150.0, ip, modules);
            out.back().id += "-" + std::to_string(modules * rows) + "M";
        }
    return out;
}

namespace {

// Riga CSV (";" se c'è, altrimenti ","), con le virgolette; celle senza spazi ai lati.
std::vector<std::string> splitCsvLine(const std::string& line)
{
    const char sep = line.find(';') != std::string::npos ? ';' : ',';
    std::vector<std::string> f;
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == sep && !quoted) {
            f.push_back(trim(cell));
            cell.clear();
        } else {
            cell += ch;
        }
    }
    f.push_back(trim(cell));
    return f;
}

// Righe utili di un catalogo: senza BOM, commenti (#), righe vuote e intestazione (prima riga con "codice").
std::vector<std::string> catalogLines(const std::string& csv)
{
    std::vector<std::string> out;
    std::stringstream in(csv);
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("\xEF\xBB\xBF", 0) == 0)
            line.erase(0, 3);
        if (trim(line).empty() || line[0] == '#')
            continue;
        if (header) {
            header = false;
            if (lower(line).find("codice") != std::string::npos)
                continue;
        }
        out.push_back(line);
    }
    return out;
}

} // namespace

std::vector<EnclosureType> parseEnclosureCatalog(const std::string& csv, int* skipped)
{
    std::vector<EnclosureType> out;
    int bad = 0;
    for (const std::string& line : catalogLines(csv)) {
        const std::vector<std::string> f = splitCsvLine(line);
        if (f.size() < 6) {
            ++bad;
            continue;
        }
        EnclosureType e;
        e.id = f[0];
        const std::string kind = lower(f[1]);
        e.mounting = kind.rfind("par", 0) == 0 ? BoardMounting::Wall
                   : kind.rfind("pav", 0) == 0 ? BoardMounting::Floor
                   : kind.rfind("cas", 0) == 0 ? BoardMounting::Box
                                               : BoardMounting::Auto;
        e.width = toNumber(f[2]);
        e.height = toNumber(f[3]);
        e.depth = toNumber(f[4]);
        std::string ip = lower(f[5]);
        if (ip.rfind("ip", 0) == 0)
            ip.erase(0, 2);
        e.ip = std::atoi(ip.c_str());
        e.modulesPerRow = f.size() > 6 ? std::atoi(f[6].c_str()) : 0;   // vuoto o 0: dalla larghezza
        if (e.modulesPerRow == 0)
            e.modulesPerRow = e.width >= 800.0 ? 36 : 24;
        e.reference = f.size() > 7 ? f[7] : std::string();
        e.source = f.size() > 8 ? f[8] : std::string();
        e.blockFile = f.size() > 9 ? f[9] : std::string();
        e.blockName = f.size() > 10 ? f[10] : std::string();
        if (e.id.empty() || e.mounting == BoardMounting::Auto || e.width <= 0.0 || e.height <= 0.0 || e.depth <= 0.0
            || e.ip <= 0 || e.modulesPerRow <= 0) {
            ++bad;
            continue;
        }
        out.push_back(e);
    }
    if (skipped)
        *skipped = bad;
    return out;
}

std::vector<DeviceType> parseDeviceCatalog(const std::string& csv, int* skipped)
{
    std::vector<DeviceType> out;
    int bad = 0;
    for (const std::string& line : catalogLines(csv)) {
        const std::vector<std::string> f = splitCsvLine(line);
        if (f.size() < 2) {
            ++bad;
            continue;
        }
        const auto cell = [&f](std::size_t i) { return i < f.size() ? f[i] : std::string(); };
        DeviceType t;
        t.id = f[0];
        const std::string kind = lower(f[1]);
        using K = BoardDevice::Kind;
        bool known = true;
        if (kind == "modulare")
            t.kind = K::Modular;
        else if (kind == "differenziale") {
            t.kind = K::Modular;
            t.residual = true;
        } else if (kind == "sezionatore" || kind == "sezionatore-scatolato")
            t.kind = K::Switch;
        else if (kind == "fusibile")
            t.kind = K::Fuse;
        else if (kind == "spd")
            t.kind = K::Spd;
        else if (kind == "scatolato")
            t.kind = K::MouldedCase;
        else if (kind == "aperto")
            t.kind = K::AirCircuit;
        else
            known = false;
        const bool frame = t.kind == K::MouldedCase || t.kind == K::AirCircuit || kind == "sezionatore-scatolato";
        if (const std::string poles = cell(2); !poles.empty()) {
            const auto dash = poles.find('-');
            t.polesMin = std::atoi(poles.substr(0, dash).c_str());
            t.polesMax = dash == std::string::npos ? t.polesMin : std::atoi(poles.substr(dash + 1).c_str());
        }
        if (!cell(3).empty())
            t.ratedMin = toNumber(cell(3));
        if (!cell(4).empty())
            t.ratedMax = toNumber(cell(4));
        t.modules = toNumber(cell(5));
        t.width = toNumber(cell(6));
        t.height = toNumber(cell(7));
        t.blockFile = cell(8);
        t.blockName = cell(9);
        t.reference = cell(10);
        t.source = cell(11);
        // Scatolati e aperti hanno larghezza (o il blocco); i modulari moduli, larghezza o blocco.
        if (t.id.empty() || !known || t.polesMin < 0 || t.polesMax < t.polesMin || t.ratedMax < t.ratedMin
            || (t.width <= 0.0 && t.modules <= 0.0 && t.blockFile.empty()) || (frame && t.modules > 0.0 && t.width <= 0.0
                                                                               && t.blockFile.empty())) {
            ++bad;
            continue;
        }
        if (kind == "sezionatore-scatolato")
            t.modules = 0.0;
        else if (!frame && t.modules <= 0.0 && t.width > 0.0)
            t.modules = std::round(t.width / kModule * 2.0) / 2.0;
        out.push_back(t);
    }
    if (skipped)
        *skipped = bad;
    return out;
}

int applyDeviceCatalog(std::vector<BoardDevice>& devices, const std::vector<DeviceType>& catalog)
{
    using K = BoardDevice::Kind;
    int found = 0;
    for (BoardDevice& d : devices) {
        const bool frameDevice = d.width > 0.0;   // scatolato, sezionatore scatolato o aperto
        const DeviceType* best = nullptr;
        for (const DeviceType& t : catalog) {
            if (t.kind != d.kind || d.poles < t.polesMin || d.poles > t.polesMax || d.rated < t.ratedMin - 1e-9
                || d.rated > t.ratedMax + 1e-9)
                continue;
            const bool frameType = t.kind == K::MouldedCase || t.kind == K::AirCircuit || (t.kind == K::Switch
                                                                                          && t.modules <= 0.0);
            if (frameType != frameDevice)
                continue;
            if (t.residual && !d.residual)
                continue;
            // Più specifica: differenziale per un apparecchio con differenziale, poi l'intervallo più stretto.
            const auto rank = [&d](const DeviceType& x) {
                return std::pair{x.residual == d.residual ? 0 : 1, (x.ratedMax - x.ratedMin) + (x.polesMax - x.polesMin)};
            };
            if (!best || rank(t) < rank(*best))
                best = &t;
        }
        if (!best)
            continue;
        ++found;
        if (frameDevice) {
            if (best->width > 0.0)
                d.width = best->width;
            if (best->height > 0.0)
                d.height = best->height;
        } else if (best->modules > 0.0) {
            // Gli accessori in più restano: si sostituisce solo l'apparecchio (con il blocco differenziale, se il
            // tipo del catalogo è "differenziale").
            const double base = d.poles * modularPerPole(d.rated);
            double extra = std::max(0.0, d.modules - base);
            if (best->residual && d.residual)
                extra = std::max(0.0, extra - base);
            if (d.kind == K::Fuse || d.kind == K::Spd)
                extra = 0.0;
            d.modules = best->modules + extra;
        }
        d.block = best->block;
        d.reference = best->reference;
        d.source = best->source;
    }
    return found;
}

namespace {

// File DIN: apparecchi in ordine, senza spezzarli; poi le file di riserva fino alla capacità richiesta.
std::vector<BoardSection> dinRows(const std::vector<BoardDevice>& devices, const std::vector<int>& modular, int perRow,
                                  double reserve, double* used, double* installed)
{
    std::vector<BoardSection> rows;
    double total = 0.0;
    for (const int i : modular) {
        const double m = devices[static_cast<std::size_t>(i)].modules;
        total += m;
        if (rows.empty() || rows.back().usedModules + m > perRow + 1e-9) {
            BoardSection s;
            s.kind = BoardSection::Kind::Modular;
            s.height = kDinRow;
            s.capacity = perRow;
            rows.push_back(s);
        }
        rows.back().devices.push_back(i);
        rows.back().usedModules += m;
    }
    double capacity = rows.size() * static_cast<double>(perRow);
    const double needed = total * (1.0 + reserve);
    while (capacity + 1e-9 < needed) {
        BoardSection s;
        s.kind = BoardSection::Kind::Modular;
        s.height = kDinRow;
        s.capacity = perRow;
        rows.push_back(s);
        capacity += perRow;
    }
    *used = total;
    *installed = capacity;
    return rows;
}

std::vector<BoardSection> mouldedRows(const std::vector<BoardDevice>& devices, const std::vector<int>& moulded,
                                      double usableWidth, double reserve)
{
    // Dal più grande al più piccolo; ogni taglia in file proprie.
    std::vector<int> order = moulded;
    std::stable_sort(order.begin(), order.end(), [&devices](int a, int b) {
        return devices[static_cast<std::size_t>(a)].rated > devices[static_cast<std::size_t>(b)].rated;
    });
    std::vector<BoardSection> rows;
    std::map<double, std::pair<int, double>> perFrame;   // altezza fila -> (apparecchi, larghezza massima)
    for (const int i : order) {
        const BoardDevice& d = devices[static_cast<std::size_t>(i)];
        const double rowHeight = std::max(mouldedFrame(d.rated).row, std::ceil((d.height + 100.0) / 50.0) * 50.0);
        double used = 0.0;
        if (!rows.empty() && rows.back().height == rowHeight)
            for (const int j : rows.back().devices)
                used += devices[static_cast<std::size_t>(j)].width + 40.0;
        if (rows.empty() || rows.back().height != rowHeight || used + d.width > usableWidth) {
            BoardSection s;
            s.kind = BoardSection::Kind::MouldedCase;
            s.height = rowHeight;
            rows.push_back(s);
        }
        rows.back().devices.push_back(i);
        auto& pf = perFrame[rowHeight];
        ++pf.first;
        pf.second = std::max(pf.second, d.width);
    }
    // Riserva: posti liberi della stessa taglia (in file aggiunte se quelle esistenti sono piene).
    for (const auto& [rowHeight, pf] : perFrame) {
        if (rowHeight <= 0.0)
            continue;
        int spare = static_cast<int>(std::ceil(pf.first * reserve - 1e-9));
        const int perRow = std::max(1, static_cast<int>(usableWidth / (pf.second + 40.0)));
        for (BoardSection& r : rows)
            if (r.height == rowHeight)
                spare -= std::max(0, perRow - static_cast<int>(r.devices.size()));
        while (spare > 0) {
            BoardSection s;
            s.kind = BoardSection::Kind::Reserve;
            s.height = rowHeight;
            rows.push_back(s);
            spare -= perRow;
        }
    }
    return rows;
}

std::vector<BoardSection> terminalRows(double width, int perRow, double reserve)
{
    std::vector<BoardSection> rows;
    const double rowWidth = perRow * kModule;
    double needed = width * (1.0 + reserve);
    while (needed > 1e-9) {
        BoardSection s;
        s.kind = BoardSection::Kind::Terminals;
        s.height = kDinRow;
        s.usedModules = std::min(needed, rowWidth) / kModule;
        s.capacity = perRow;
        rows.push_back(s);
        needed -= rowWidth;
    }
    return rows;
}

double sectionsHeight(const std::vector<BoardSection>& s)
{
    double h = 0.0;
    for (const BoardSection& x : s)
        h += x.height;
    return h;
}

const EnclosureType* chooseEnclosure(const std::vector<EnclosureType>& catalog, BoardMounting m, double w, double h,
                                     double d, int ip)
{
    const EnclosureType* best = nullptr;
    for (const EnclosureType& e : catalog) {
        if (e.mounting != m || e.ip < ip || e.width + 1e-6 < w || e.height + 1e-6 < h || e.depth + 1e-6 < d)
            continue;
        if (!best) {
            best = &e;
            continue;
        }
        const double a = e.width * e.height, b = best->width * best->height;
        if (a < b - 1e-6 || (std::abs(a - b) < 1e-6 && (e.depth < best->depth - 1e-6
                                                         || (std::abs(e.depth - best->depth) < 1e-6
                                                             && (e.ip < best->ip
                                                                 || (e.ip == best->ip && !e.reference.empty()
                                                                     && best->reference.empty()))))))
            best = &e;
    }
    return best;
}

} // namespace

BoardLayout layoutBoard(const BoardInput& input, const BoardOptions& options, const std::vector<EnclosureType>& catalog)
{
    BoardLayout L;
    L.board = input.board;
    L.devices = input.devices;
    L.incomer = input.incomer;
    L.ip = options.ip > 0 ? options.ip : proposedIp(options.environment);
    L.form = options.form.empty() ? proposedForm(input.incomer) : options.form;
    L.terminals = options.terminals ? input.terminals.count : 0;
    const double reserve = std::clamp(options.reserve, 0.0, 2.0);

    std::vector<int> air, moulded, modular;
    for (std::size_t i = 0; i < L.devices.size(); ++i) {
        const BoardDevice& d = L.devices[i];
        if (d.kind == BoardDevice::Kind::AirCircuit)
            air.push_back(static_cast<int>(i));
        else if ((d.kind == BoardDevice::Kind::MouldedCase || d.kind == BoardDevice::Kind::Switch) && d.width > 0.0)
            moulded.push_back(static_cast<int>(i));
        if (d.modules > 0.0)
            modular.push_back(static_cast<int>(i));
    }
    if (L.devices.empty()) {
        L.warnings.push_back("nessun apparecchio di bassa tensione riconosciuto nello schema");
        return L;
    }
    bool bigFrame = false;
    for (const int i : moulded)
        bigFrame = bigFrame || L.devices[static_cast<std::size_t>(i)].rated > 250.0;

    BoardMounting mounting = options.mounting;
    double totalModules = 0.0;
    for (const int i : modular)
        totalModules += L.devices[static_cast<std::size_t>(i)].modules;
    if (mounting == BoardMounting::Auto) {
        if (!air.empty() || bigFrame || input.incomer > 250.0)
            mounting = BoardMounting::Floor;
        else if (moulded.empty() && (options.environment == BoardEnvironment::Outdoor
                                     || options.environment == BoardEnvironment::Washdown)
                 && totalModules * (1.0 + reserve) + (options.terminals ? input.terminals.width / kModule : 0.0) <= 72.0)
            mounting = BoardMounting::Box;
        else
            mounting = BoardMounting::Wall;
    }

    const double termWidth = options.terminals ? input.terminals.width : 0.0;
    const auto buildWallOrBox = [&](BoardMounting m) -> bool {
        // Prova le larghezze del catalogo dalla più piccola: la prima carpenteria che contiene tutto.
        std::set<std::pair<double, int>> widths;
        for (const EnclosureType& e : catalog)
            if (e.mounting == m)
                widths.insert({e.width, e.modulesPerRow});
        for (const auto& [width, perRow] : widths) {
            BoardColumn col;
            col.width = width;
            const double usable = width - 2.0 * kSideMargin;
            for (const BoardSection& s : mouldedRows(L.devices, moulded, usable, reserve))
                col.sections.push_back(s);
            double used = 0.0, installed = 0.0;
            for (const BoardSection& s : dinRows(L.devices, modular, perRow, reserve, &used, &installed))
                col.sections.push_back(s);
            for (const BoardSection& s : terminalRows(termWidth, perRow, reserve))
                col.sections.push_back(s);
            const double h = sectionsHeight(col.sections) + (m == BoardMounting::Box ? 100.0 : kWallMargin);
            const double d = moulded.empty() ? (m == BoardMounting::Box ? 120.0 : 200.0) : 250.0;
            if (const EnclosureType* e = chooseEnclosure(catalog, m, width, h, d, L.ip)) {
                L.mounting = m;
                col.enclosure = *e;
                col.enclosureFound = true;
                L.columns = {col};
                L.width = width;
                L.height = h;
                L.depth = d;
                L.enclosure = *e;
                L.enclosureFound = true;
                L.modules = used;
                L.modulesInstalled = installed;
                return true;
            }
        }
        return false;
    };

    if (mounting == BoardMounting::Box && !moulded.empty())
        mounting = BoardMounting::Wall;
    if ((mounting == BoardMounting::Box || mounting == BoardMounting::Wall) && buildWallOrBox(mounting))
        return L;
    if (mounting == BoardMounting::Box && buildWallOrBox(BoardMounting::Wall))
        return L;
    if (options.mounting == BoardMounting::Wall || options.mounting == BoardMounting::Box)
        L.warnings.push_back("il contenuto non entra in una carpenteria a parete del catalogo: proposta a pavimento");

    // A pavimento: colonne apparecchi (la larghezza più grande richiesta), apparecchi aperti in colonne proprie;
    // a sinistra il vano delle sbarre verticali (forme 2a e oltre), a destra il vano di risalita dei cavi; i cavi
    // grandi in una colonna di risalita dedicata.
    L.mounting = BoardMounting::Floor;
    double columnWidth = bigFrame ? 800.0 : 600.0;
    for (const int i : air)
        columnWidth = std::max(columnWidth, airColumnWidth(L.devices[static_cast<std::size_t>(i)].width));
    const int perRow = columnWidth >= 800.0 ? 36 : 24;
    const double usable = columnWidth - 2.0 * kSideMargin;
    for (const int i : air) {
        BoardColumn c;
        c.width = columnWidth;
        BoardSection s;
        s.kind = BoardSection::Kind::Air;
        s.height = std::max(800.0, std::ceil((L.devices[static_cast<std::size_t>(i)].height + 300.0) / 100.0) * 100.0);
        s.devices = {i};
        c.sections.push_back(s);
        L.columns.push_back(c);
    }
    std::vector<BoardSection> rest = mouldedRows(L.devices, moulded, usable, reserve);
    double used = 0.0, installed = 0.0;
    for (const BoardSection& s : dinRows(L.devices, modular, perRow, reserve, &used, &installed))
        rest.push_back(s);
    for (const BoardSection& s : terminalRows(termWidth, perRow, reserve))
        rest.push_back(s);
    BoardColumn current;
    current.width = columnWidth;
    for (const BoardSection& s : rest) {
        if (sectionsHeight(current.sections) + s.height > kFloorUsable + 1e-6 && !current.sections.empty()) {
            L.columns.push_back(current);
            current.sections.clear();
        }
        current.sections.push_back(s);
    }
    if (!current.sections.empty())
        L.columns.push_back(current);
    L.modules = used;
    L.modulesInstalled = installed;
    L.depth = (!air.empty() || input.incomer > 630.0) ? 600.0 : 400.0;
    L.height = kFloorHeight;

    // Sbarre verticali: nelle forme 2a e oltre in un vano proprio accanto agli apparecchi (nella forma 1 stanno sul
    // fondo). Le colonne degli aperti hanno le sbarre orizzontali.
    const double busbar = !options.busbars || L.form.empty() || L.form[0] == '1' ? 0.0
                        : input.incomer <= 1600.0                               ? 150.0
                        : input.incomer <= 3200.0                               ? 200.0
                                                                                : 300.0;
    for (BoardColumn& c : L.columns)
        if (c.sections.empty() || c.sections[0].kind != BoardSection::Kind::Air)
            c.busbarWidth = busbar;

    // Risalita dei cavi. Riempimento massimo del vano 35% (cavi in fascio su scala o passerella verticale), sezione
    // utile del vano = larghezza x (profondità - 100 mm). Gli aperti (arrivi) hanno i cavi o il condotto in colonna.
    constexpr double kRiserFill = 0.35;
    const double riserDepth = L.depth - 100.0;
    const auto riserWidth = [&](double area, std::initializer_list<double> steps) {
        for (const double w : steps)
            if (area <= w * riserDepth * kRiserFill + 1e-6)
                return w;
        return -1.0;   // non basta il vano più grande
    };
    std::vector<double> columnArea(L.columns.size(), 0.0);
    double bigArea = 0.0;
    std::vector<std::pair<std::size_t, double>> big;   // (colonna, area) dei cavi da 95 mm² in su
    for (std::size_t ci = 0; ci < L.columns.size(); ++ci)
        for (const BoardSection& s : L.columns[ci].sections)
            for (const int i : s.devices) {
                const BoardDevice& d = L.devices[static_cast<std::size_t>(i)];
                if (d.kind == BoardDevice::Kind::AirCircuit || d.cableArea <= 0.0)
                    continue;
                columnArea[ci] += d.cableArea;
                if (d.cableSection >= 95.0) {
                    bigArea += d.cableArea;
                    big.push_back({ci, d.cableArea});
                }
            }
    RiserMode risers = options.risers;
    if (risers == RiserMode::Auto) {
        // Colonna dedicata quando i cavi grandi non stanno in un vano laterale da 300 mm.
        risers = bigArea > 300.0 * riserDepth * kRiserFill ? RiserMode::Column : RiserMode::Side;
        if (risers == RiserMode::Column && options.risers == RiserMode::Auto) {
            for (const auto& [ci, a] : big)
                columnArea[ci] -= a;   // i cavi grandi vanno nella colonna, il resto nei vani laterali
        }
    }
    double dedicated = 0.0;
    if (risers == RiserMode::Column) {
        dedicated = options.risers == RiserMode::Column
                        ? std::accumulate(columnArea.begin(), columnArea.end(), 0.0)
                        : bigArea;
        if (options.risers == RiserMode::Column)
            std::fill(columnArea.begin(), columnArea.end(), 0.0);
    }
    if (risers != RiserMode::None) {
        for (std::size_t ci = 0; ci < L.columns.size(); ++ci) {
            BoardColumn& c = L.columns[ci];
            if (columnArea[ci] <= 0.0 || (!c.sections.empty() && c.sections[0].kind == BoardSection::Kind::Air))
                continue;
            double w = riserWidth(columnArea[ci], {150.0, 200.0, 250.0, 300.0, 400.0});
            if (w < 0.0) {
                w = 400.0;
                L.warnings.push_back("risalita cavi della colonna " + std::to_string(ci + 1)
                                     + " oltre il 35% anche con un vano da 400 mm: prevedere una colonna di risalita");
            }
            c.cableWidth = w;
            c.cableArea = columnArea[ci];
        }
        if (dedicated > 0.0) {
            BoardColumn r;
            r.kind = BoardColumn::Kind::Riser;
            r.cableArea = dedicated;
            double w = riserWidth(dedicated, {400.0, 600.0, 800.0, 1000.0, 1200.0});
            if (w < 0.0) {
                w = 1200.0;
                L.warnings.push_back("cavi oltre la capacità di una colonna di risalita da 1200 mm: dividerli in più colonne");
            }
            r.width = w;
            L.columns.push_back(r);
        }
    }

    // Carpenteria di ogni colonna: la più piccola del catalogo che contiene sbarre, apparecchi e risalita; il vano
    // apparecchi prende lo spazio in più.
    std::map<std::string, int> count;
    bool allFound = true;
    double depth = 0.0;
    for (BoardColumn& c : L.columns) {
        const double needed = c.total();
        if (const EnclosureType* e = chooseEnclosure(catalog, BoardMounting::Floor, needed, kFloorHeight, L.depth, L.ip)) {
            c.enclosure = *e;
            c.enclosureFound = true;
            c.width = e->width - c.busbarWidth - c.cableWidth;
            depth = std::max(depth, e->depth);
            if (c.kind == BoardColumn::Kind::Devices)
                ++count[e->id];
        } else {
            allFound = false;
            L.warnings.push_back("nessuna carpenteria a pavimento del catalogo per una colonna da " + number(needed)
                                 + " mm, IP" + std::to_string(L.ip));
        }
    }
    if (depth > 0.0)
        L.depth = depth;
    // Carpenteria "tipo" del quadro: quella delle colonne apparecchi più frequente.
    int most = 0;
    for (const BoardColumn& c : L.columns)
        if (c.enclosureFound && c.kind == BoardColumn::Kind::Devices && count[c.enclosure.id] > most) {
            most = count[c.enclosure.id];
            L.enclosure = c.enclosure;
        }
    L.enclosureFound = allFound && most > 0;
    L.enclosureCount = static_cast<int>(L.columns.size());
    L.width = 0.0;
    for (const BoardColumn& c : L.columns)
        L.width += c.total();
    if (input.incomer > 4000.0)
        L.warnings.push_back("corrente di arrivo oltre 4000 A: verificare sbarre e colonne con il costruttore");
    return L;
}

// ------------------------------------------------------------------ disegno

namespace {

struct Painter {
    std::vector<std::unique_ptr<Entity>>& out;
    int frame, devices, texts, dims;

    void rect(int layer, double x, double y, double w, double h)
    {
        std::vector<PolyVertex> v{{{x, y}}, {{x + w, y}}, {{x + w, y + h}}, {{x, y + h}}};
        auto p = std::make_unique<PolylineEntity>(std::move(v), true);
        p->setLayer(layer);
        p->setColor(kColorByLayer);
        out.push_back(std::move(p));
    }
    void line(int layer, Vec2 a, Vec2 b)
    {
        auto l = std::make_unique<LineEntity>(a, b);
        l->setLayer(layer);
        l->setColor(kColorByLayer);
        out.push_back(std::move(l));
    }
    void text(int layer, Vec2 p, double h, const std::string& s, double rot = 0.0, bool centered = false)
    {
        auto t = std::make_unique<TextEntity>(p, h, s, rot);
        if (centered) {
            t->hAlign = TextEntity::Middle;
            t->alignPoint = p;
        }
        t->setLayer(layer);
        t->setColor(kColorByLayer);
        out.push_back(std::move(t));
    }
    // Quota disegnata con linee e testo (leggibile a qualsiasi scala di stampa del fronte).
    void dimension(Vec2 a, Vec2 b, Vec2 offset, double textHeight)
    {
        const Vec2 a2 = a + offset, b2 = b + offset;
        line(dims, a, a2);
        line(dims, b, b2);
        line(dims, a2, b2);
        const Vec2 dir = b2 - a2;
        const double len = dir.length();
        const Vec2 u = dir * (1.0 / len);
        const Vec2 tick = Vec2{u.x + u.y, u.y - u.x} * (textHeight * 0.4);
        line(dims, a2 - tick, a2 + tick);
        line(dims, b2 - tick, b2 + tick);
        const double angle = std::atan2(u.y, u.x);
        const Vec2 normal{-u.y, u.x};
        text(dims, (a2 + b2) * 0.5 + normal * (textHeight * 0.9), textHeight, number(len), angle, true);
    }
};

int ensureLayer(Document& doc, const char* name, int color)
{
    const int i = doc.findLayer(name);
    return i >= 0 ? i : doc.addLayer(Layer{name, color, true});
}

std::string deviceLabel(const BoardDevice& d)
{
    std::string s = shortCode(d.circuit);
    if (d.kind == BoardDevice::Kind::Supply)
        return s + " alim.";
    if (d.poles > 0)
        s += " " + std::to_string(d.poles) + "P " + number(d.rated) + "A";
    if (d.kind == BoardDevice::Kind::Spd)
        s += " SPD";
    if (d.residual)
        s += " Id";
    return s;
}

// Ingombro di un blocco nelle sue coordinate (falso se il blocco non c'è o è vuoto).
bool blockExtent(const Document& doc, const std::string& name, BBox* box)
{
    if (name.empty())
        return false;
    const auto block = doc.findBlock(name);
    if (!block)
        return false;
    BBox b;
    for (const auto& e : block->entities) {
        const BBox eb = e->bounds();
        if (eb.isValid()) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    if (!b.isValid())
        return false;
    *box = b;
    return true;
}

} // namespace

std::vector<std::string> boardBlocks(const BoardLayout& L)
{
    std::set<std::string> names;
    for (const BoardDevice& d : L.devices)
        if (!d.block.empty())
            names.insert(d.block);
    for (const BoardColumn& c : L.columns)
        if (c.enclosureFound && !c.enclosure.block.empty())
            names.insert(c.enclosure.block);
    return {names.begin(), names.end()};
}

std::vector<std::unique_ptr<Entity>> drawBoardFront(Document& doc, const BoardLayout& L, Vec2 origin, BBox* extent)
{
    std::vector<std::unique_ptr<Entity>> out;
    Painter p{out, ensureLayer(doc, "FQ-CARPENTERIA", 7), ensureLayer(doc, "FQ-APPARECCHI", 4),
              ensureLayer(doc, "FQ-TESTI", 7), ensureLayer(doc, "FQ-QUOTE", 3)};
    const int blocks = ensureLayer(doc, "FQ-BLOCCHI", 7);
    const int risers = ensureLayer(doc, "FQ-RISALITE", 30);
    // Blocco centrato (nel suo ingombro) in un riquadro; falso se il blocco non c'è.
    const auto insertBlock = [&](const std::string& name, double x, double y, double w, double h, bool bottomLeft) {
        BBox b;
        if (!blockExtent(doc, name, &b))
            return false;
        const auto block = doc.findBlock(name);
        const Vec2 anchor = bottomLeft ? Vec2{x, y} - b.min : Vec2{x + w / 2.0, y + h / 2.0} - (b.min + b.max) * 0.5;
        auto insert = std::make_unique<InsertEntity>(name, anchor + block->base);
        insert->block = block;
        insert->setLayer(blocks);
        insert->setColor(kColorByLayer);
        out.push_back(std::move(insert));
        return true;
    };

    const bool floor = L.mounting == BoardMounting::Floor;
    const double plinth = floor ? 100.0 : 0.0;
    const double x0 = origin.x, y0 = origin.y + plinth;
    double totalWidth = 0.0;
    for (const BoardColumn& c : L.columns)
        totalWidth += c.total();
    const double height = floor ? kFloorHeight : std::max(L.height, L.enclosureFound ? L.enclosure.height : L.height);
    const double topMargin = floor ? 100.0 : (height - sectionsHeight(L.columns.empty() ? std::vector<BoardSection>{}
                                                                                        : L.columns[0].sections)) / 2.0;
    if (floor)
        p.rect(p.frame, x0, origin.y, totalWidth, plinth);
    p.rect(p.frame, x0, y0, totalWidth, height);

    double cx = x0;
    for (const BoardColumn& c : L.columns) {
        // Carpenteria del costruttore (blocco del fronte), dall'angolo in basso a sinistra (zoccolo compreso).
        const bool frameBlock = c.enclosureFound && insertBlock(c.enclosure.block, cx, origin.y, c.total(), height + plinth, true);
        if (!frameBlock)
            p.rect(p.frame, cx, y0, c.total(), height);
        if (c.kind == BoardColumn::Kind::Riser) {
            p.rect(risers, cx + 25.0, y0 + 25.0, c.width - 50.0, height - 50.0);
            p.text(p.texts, {cx + c.width / 2.0, y0 + height / 2.0}, 40.0, "colonna risalita cavi", kPi / 2.0, true);
            p.text(p.texts, {cx + c.width / 2.0 + 60.0, y0 + height / 2.0}, 25.0,
                   "cavi " + number(c.cableArea / 100.0) + " cm²", kPi / 2.0, true);
            cx += c.total();
            continue;
        }
        if (c.busbarWidth > 0.0) {
            p.rect(p.frame, cx, y0, c.busbarWidth, height);
            p.text(p.texts, {cx + c.busbarWidth / 2.0, y0 + height / 2.0}, 30.0, "sbarre verticali", kPi / 2.0, true);
            cx += c.busbarWidth;
        }
        p.rect(p.frame, cx, y0, c.width, height);
        double y = y0 + height - topMargin;
        for (const BoardSection& s : c.sections) {
            const double top = y, bottom = y - s.height;
            const double left = cx + kSideMargin, inner = c.width - 2.0 * kSideMargin;
            p.rect(p.frame, left, bottom, inner, s.height);
            const double mid = (top + bottom) / 2.0;
            if (s.kind == BoardSection::Kind::Modular || s.kind == BoardSection::Kind::Terminals) {
                const double railLeft = left + (inner - s.capacity * kModule) / 2.0;
                p.line(p.frame, {railLeft, mid}, {railLeft + s.capacity * kModule, mid});
                if (s.kind == BoardSection::Kind::Terminals) {
                    const double w = s.usedModules * kModule;
                    p.rect(p.devices, railLeft, mid - 30.0, w, 60.0);
                    p.text(p.texts, {railLeft + w / 2.0, mid}, 14.0, "morsettiera", 0.0, true);
                } else {
                    double x = railLeft;
                    for (const int i : s.devices) {
                        const BoardDevice& d = L.devices[static_cast<std::size_t>(i)];
                        const double w = d.modules * kModule;
                        // Il blocco è quello dell'apparecchio: per uno scatolato qui c'è solo l'accessorio su guida.
                        if (d.width <= 0.0 && insertBlock(d.block, x, mid - 45.0, w, 90.0, false))
                            p.text(p.texts, {x + w / 2.0, bottom + 12.0}, std::min(7.0, w * 0.3), shortCode(d.circuit),
                                   kPi / 2.0, true);
                        else {
                            p.rect(p.devices, x, mid - 45.0, w, 90.0);
                            p.text(p.texts, {x + w / 2.0, mid}, std::min(10.0, w * 0.55), deviceLabel(d), kPi / 2.0, true);
                        }
                        x += w;
                    }
                    const double free = s.capacity * kModule - (x - railLeft);
                    if (free >= kModule)
                        p.text(p.texts, {x + free / 2.0, mid}, 12.0, number(free / kModule) + " mod. liberi", 0.0, true);
                }
            } else if (s.kind == BoardSection::Kind::MouldedCase || s.kind == BoardSection::Kind::Air) {
                double used = 0.0;
                for (const int i : s.devices)
                    used += L.devices[static_cast<std::size_t>(i)].width + 40.0;
                double x = left + (inner - (used - 40.0)) / 2.0;
                for (const int i : s.devices) {
                    const BoardDevice& d = L.devices[static_cast<std::size_t>(i)];
                    if (insertBlock(d.block, x, mid - d.height / 2.0, d.width, d.height, false))
                        p.text(p.texts, {x + d.width / 2.0, bottom + 20.0}, 14.0, deviceLabel(d), 0.0, true);
                    else {
                        p.rect(p.devices, x, mid - d.height / 2.0, d.width, d.height);
                        p.text(p.texts, {x + d.width / 2.0, mid}, 16.0, deviceLabel(d), kPi / 2.0, true);
                    }
                    x += d.width + 40.0;
                }
            } else if (s.kind == BoardSection::Kind::Reserve) {
                p.text(p.texts, {left + inner / 2.0, mid}, 16.0, "riserva", 0.0, true);
            }
            y = bottom;
        }
        cx += c.width;
        if (c.cableWidth > 0.0) {
            p.rect(risers, cx, y0, c.cableWidth, height);
            p.text(p.texts, {cx + c.cableWidth / 2.0, y0 + height / 2.0}, 25.0,
                   "risalita cavi " + number(c.cableArea / 100.0) + " cm²", kPi / 2.0, true);
            cx += c.cableWidth;
        }
    }

    const double dimText = 40.0;
    p.dimension({x0, origin.y}, {x0 + totalWidth, origin.y}, {0.0, -150.0}, dimText);
    p.dimension({x0, y0 + height}, {x0, origin.y}, {-150.0, 0.0}, dimText);
    // Larghezze delle colonne (quando sono più di una).
    if (L.columns.size() > 1) {
        double qx = x0;
        for (const BoardColumn& c : L.columns) {
            p.dimension({qx, y0 + height}, {qx + c.total(), y0 + height}, {0.0, 60.0}, 30.0);
            qx += c.total();
        }
    }

    // Dati del quadro sopra il fronte.
    double ty = y0 + height + (L.columns.size() > 1 ? 200.0 : 80.0);
    std::vector<std::string> lines;
    lines.push_back("Carpenteria: " + (L.enclosureFound ? L.enclosure.id : std::string("da scegliere"))
                    + (L.enclosureCount > 1 ? " - " + std::to_string(L.enclosureCount) + " colonne" : std::string()));
    if (L.enclosureFound && !L.enclosure.reference.empty())
        lines.push_back("Riferimento: " + L.enclosure.reference);
    lines.push_back("Ingombro: " + number(totalWidth) + " x " + number(height + plinth) + " x " + number(L.depth)
                    + " mm (L x H x P)" + (floor ? ", zoccolo 100 mm" : ""));
    lines.push_back("IP" + std::to_string(L.ip) + " - Forma " + L.form + " - In arrivo " + number(L.incomer) + " A");
    lines.push_back("Moduli DIN occupati " + number(L.modules, 1) + " su " + number(L.modulesInstalled, 1)
                    + (L.terminals > 0 ? " - morsetti " + std::to_string(L.terminals) : std::string()));
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        p.text(p.texts, {x0, ty}, 30.0, *it);
        ty += 50.0;
    }
    p.text(p.texts, {x0, ty + 20.0}, 60.0, "Fronte quadro " + L.board);

    if (extent) {
        extent->min = {x0 - 250.0, origin.y - 250.0};
        extent->max = {x0 + std::max(totalWidth, 1400.0), ty + 100.0};
    }
    return out;
}

// ------------------------------------------------------------------ distinta

namespace {

std::string csvCell(std::string v)
{
    if (!v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '-' || v[0] == '@'))
        v.insert(v.begin(), '\'');
    if (v.find_first_of(";\"\r\n") == std::string::npos)
        return v;
    std::string out = "\"";
    for (char c : v) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + "\"";
}

std::string decimal(double v, int decimals = 1)
{
    std::string s = number(v, decimals);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

} // namespace

std::string boardBillOfMaterials(const std::vector<BoardLayout>& layouts)
{
    std::string out = "Quadro;Voce;Quantità;Descrizione;Riferimento;Note\r\n";
    const auto row = [&out](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i)
                out += ';';
            out += csvCell(cells[i]);
        }
        out += "\r\n";
    };
    for (const BoardLayout& L : layouts) {
        if (L.devices.empty())
            continue;
        const char* kind = L.mounting == BoardMounting::Floor ? "a pavimento"
                         : L.mounting == BoardMounting::Wall  ? "a parete"
                                                              : "cassetta";
        // Carpenterie: una riga per tipo (le colonne a pavimento possono essere diverse).
        std::map<std::string, std::pair<int, const BoardColumn*>> frames;
        for (const BoardColumn& c : L.columns) {
            const std::string key = c.enclosureFound ? c.enclosure.id : "?" + number(c.total());
            auto& f = frames[key];
            ++f.first;
            f.second = &c;
        }
        for (const auto& [key, f] : frames) {
            const BoardColumn& c = *f.second;
            const EnclosureType& e = c.enclosureFound ? c.enclosure : L.enclosure;
            const double w = c.enclosureFound ? e.width : c.total();
            const double h = L.mounting == BoardMounting::Floor ? kFloorHeight : e.height;
            row({L.board, c.kind == BoardColumn::Kind::Riser ? "Carpenteria colonna risalita cavi" : "Carpenteria",
                 std::to_string(f.first),
                 std::string(kind) + " " + number(w) + "x" + number(h) + "x" + number(L.depth) + " mm IP"
                     + std::to_string(L.ip) + " forma " + L.form,
                 c.enclosureFound || L.enclosureFound ? (e.reference.empty() ? e.id : e.reference) : "da scegliere",
                 c.enclosureFound || L.enclosureFound ? e.source : std::string()});
        }
        if (L.mounting == BoardMounting::Floor)
            row({L.board, "Zoccolo", std::to_string(L.enclosureCount), "h 100 mm", "", ""});
        int din = 0, terminals = 0, reserveRows = 0;
        std::map<double, int> plates;
        for (const BoardColumn& c : L.columns) {
            if (c.busbarWidth > 0.0)
                row({L.board, "Vano sbarre verticali", "1", "larghezza " + number(c.busbarWidth) + " mm", "",
                     "sbarre da dimensionare sulla corrente della colonna"});
            if (c.cableWidth > 0.0)
                row({L.board, "Vano risalita cavi", "1", "larghezza " + number(c.cableWidth) + " mm",
                     "cavi " + decimal(c.cableArea / 100.0, 0) + " cm²", "riempimento massimo 35%"});
            if (c.kind == BoardColumn::Kind::Riser)
                row({L.board, "Colonna risalita cavi", "1", "larghezza " + number(c.width) + " mm",
                     "cavi " + decimal(c.cableArea / 100.0, 0) + " cm²", "cavi da 95 mm² in su, riempimento massimo 35%"});
            for (const BoardSection& s : c.sections) {
                if (s.kind == BoardSection::Kind::Modular)
                    ++din;
                else if (s.kind == BoardSection::Kind::Terminals)
                    ++terminals;
                else if (s.kind == BoardSection::Kind::MouldedCase || s.kind == BoardSection::Kind::Air)
                    ++plates[s.height];
                else if (s.kind == BoardSection::Kind::Reserve)
                    ++reserveRows;
            }
        }
        if (din)
            row({L.board, "File DIN con pannello finestrato", std::to_string(din),
                 "h " + number(kDinRow) + " mm, " + decimal(L.modulesInstalled, 0) + " moduli installati", "", ""});
        if (L.modulesInstalled > L.modules)
            row({L.board, "Coprimoduli di riserva", decimal(std::floor(L.modulesInstalled - L.modules), 0), "moduli DIN",
                 "", ""});
        for (const auto& [h, n] : plates)
            row({L.board, "Piastra e pannello per apparecchi", std::to_string(n), "h " + number(h) + " mm", "", ""});
        if (reserveRows)
            row({L.board, "Pannello cieco di riserva", std::to_string(reserveRows), "per scatolati futuri", "", ""});
        if (terminals)
            row({L.board, "Morsettiera", std::to_string(L.terminals), "morsetti su " + std::to_string(terminals)
                                                                        + " file DIN", "", "conduttori fino a 35 mm²"});
        for (const BoardDevice& d : L.devices) {
            std::string what = boardDeviceKindName(d.kind);
            if (d.poles > 0)
                what += " " + std::to_string(d.poles) + "P " + number(d.rated) + " A";
            if (d.modules > 0.0)
                what += ", " + decimal(d.modules) + " moduli";
            if (d.width > 0.0)
                what += ", " + number(d.width) + "x" + number(d.height) + " mm";
            row({L.board, "Apparecchio " + shortCode(d.circuit), "1", what,
                 d.reference.empty() ? d.description : d.reference + " - " + d.description,
                 d.note.empty() ? d.source : d.source.empty() ? d.note : d.note + " - " + d.source});
        }
        for (const std::string& w : L.warnings)
            row({L.board, "Avviso", "", w, "", ""});
    }
    return out;
}

// ------------------------------------------------------------------ IFC

namespace {

std::string ifcGuid(const std::string& seed)
{
    static const char* chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
    std::seed_seq seq(seed.begin(), seed.end());
    std::mt19937_64 rng(seq);
    std::uint64_t hi = rng(), lo = rng();
    // 128 bit: 2 bit nel primo carattere, poi 21 caratteri da 6 bit.
    std::string out(22, '0');
    for (int i = 21; i >= 1; --i) {
        out[static_cast<std::size_t>(i)] = chars[lo & 63u];
        lo = (lo >> 6) | (hi << 58);
        hi >>= 6;
    }
    out[0] = chars[lo & 3u];
    return out;
}

// Stringa STEP: apostrofi raddoppiati, barre raddoppiate, caratteri non ASCII come \X2\hhhh\X0\.
std::string stepString(const std::string& s)
{
    std::string out = "'";
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c == '\'')
                out += "''";
            else if (c == '\\')
                out += "\\\\";
            else if (c >= 0x20)
                out += static_cast<char>(c);
            ++i;
            continue;
        }
        unsigned code = 0;
        int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        code = len == 1 ? c : c & (0xFFu >> (len + 1));
        for (int k = 1; k < len && i + static_cast<std::size_t>(k) < s.size(); ++k)
            code = (code << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3Fu);
        i += static_cast<std::size_t>(len);
        char buffer[32];
        if (code <= 0xFFFF)
            std::snprintf(buffer, sizeof buffer, "\\X2\\%04X\\X0\\", code);
        else
            std::snprintf(buffer, sizeof buffer, "\\X4\\%08X\\X0\\", code);
        out += buffer;
    }
    return out + "'";
}

std::string real(double v)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6f", v);
    std::string s = buffer;
    while (s.back() == '0')
        s.pop_back();
    return s;   // termina con "." se intero (forma STEP valida)
}

} // namespace

std::string boardsToIfc(const std::vector<BoardLayout>& layouts, const std::string& projectName)
{
    std::ostringstream data;
    int next = 1;
    const auto add = [&](const std::string& entity) {
        const int id = next++;
        data << '#' << id << '=' << entity << ";\n";
        return id;
    };
    const auto ref = [](int id) { return "#" + std::to_string(id); };
    int guidCounter = 0;
    const auto guid = [&](const std::string& what) {
        return stepString(ifcGuid(projectName + "|" + what + "|" + std::to_string(guidCounter++)));
    };

    const int origin = add("IFCCARTESIANPOINT((0.,0.,0.))");
    const int zAxis = add("IFCDIRECTION((0.,0.,1.))");
    const int place = add("IFCAXIS2PLACEMENT3D(" + ref(origin) + ",$,$)");
    const int length = add("IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.)");
    const int area = add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)");
    const int volume = add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)");
    const int angle = add("IFCSIUNIT(*,.PLANEANGLEUNIT.,$,.RADIAN.)");
    const int units = add("IFCUNITASSIGNMENT((" + ref(length) + "," + ref(area) + "," + ref(volume) + "," + ref(angle) + "))");
    const int context = add("IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05," + ref(place) + ",$)");
    const int body = add("IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*," + ref(context) + ",$,.MODEL_VIEW.,$)");
    const int project = add("IFCPROJECT(" + guid("project") + ",$," + stepString(projectName) + ",$,$,$,$,(" + ref(context)
                            + ")," + ref(units) + ")");
    const int sitePlace = add("IFCLOCALPLACEMENT($," + ref(place) + ")");
    const int site = add("IFCSITE(" + guid("site") + ",$,'Sito',$,$," + ref(sitePlace) + ",$,$,.ELEMENT.,$,$,$,$,$)");
    const int buildingPlace = add("IFCLOCALPLACEMENT(" + ref(sitePlace) + "," + ref(place) + ")");
    const int building = add("IFCBUILDING(" + guid("building") + ",$,'Edificio',$,$," + ref(buildingPlace)
                             + ",$,$,.ELEMENT.,$,$,$)");
    const int storeyPlace = add("IFCLOCALPLACEMENT(" + ref(buildingPlace) + "," + ref(place) + ")");
    const int storey = add("IFCBUILDINGSTOREY(" + guid("storey") + ",$,'Quadri elettrici',$,$," + ref(storeyPlace)
                           + ",$,$,.ELEMENT.,0.)");
    add("IFCRELAGGREGATES(" + guid("agg1") + ",$,$,$," + ref(project) + ",(" + ref(site) + "))");
    add("IFCRELAGGREGATES(" + guid("agg2") + ",$,$,$," + ref(site) + ",(" + ref(building) + "))");
    add("IFCRELAGGREGATES(" + guid("agg3") + ",$,$,$," + ref(building) + ",(" + ref(storey) + "))");

    std::vector<int> boards;
    double x = 0.0;
    for (const BoardLayout& L : layouts) {
        if (L.devices.empty() || L.columns.empty())
            continue;
        const bool floor = L.mounting == BoardMounting::Floor;
        const double h = floor ? kFloorHeight + 100.0 : std::max(L.height, L.enclosure.height);
        const double z0 = floor ? 0.0 : 1000.0;   // a parete: base a 1 m dal pavimento
        const int p0 = add("IFCCARTESIANPOINT((" + real(x) + ",0.," + real(z0) + "))");
        const int a0 = add("IFCAXIS2PLACEMENT3D(" + ref(p0) + ",$,$)");
        const int local = add("IFCLOCALPLACEMENT(" + ref(storeyPlace) + "," + ref(a0) + ")");
        std::string solids;
        double cx = 0.0;
        for (const BoardColumn& c : L.columns) {
            const double w = c.total();
            const int center = add("IFCCARTESIANPOINT((" + real(cx + w / 2.0) + "," + real(L.depth / 2.0) + "))");
            const int a2 = add("IFCAXIS2PLACEMENT2D(" + ref(center) + ",$)");
            const int profile = add("IFCRECTANGLEPROFILEDEF(.AREA.,$," + ref(a2) + "," + real(w) + "," + real(L.depth) + ")");
            const int solid = add("IFCEXTRUDEDAREASOLID(" + ref(profile) + "," + ref(place) + "," + ref(zAxis) + "," + real(h) + ")");
            solids += (solids.empty() ? "" : ",") + ref(solid);
            cx += w;
        }
        const int rep = add("IFCSHAPEREPRESENTATION(" + ref(body) + ",'Body','SweptSolid',(" + solids + "))");
        const int shape = add("IFCPRODUCTDEFINITIONSHAPE($,$,(" + ref(rep) + "))");
        const std::string type = floor ? ".SWITCHBOARD." : ".DISTRIBUTIONBOARD.";
        const int board = add("IFCELECTRICDISTRIBUTIONBOARD(" + guid("board " + L.board) + ",$," + stepString(L.board)
                              + "," + stepString("Quadro elettrico " + L.board) + ",$," + ref(local) + "," + ref(shape)
                              + "," + stepString(L.board) + "," + type + ")");
        boards.push_back(board);

        std::vector<int> props;
        const auto label = [&](const std::string& name, const std::string& value) {
            props.push_back(add("IFCPROPERTYSINGLEVALUE(" + stepString(name) + ",$,IFCLABEL(" + stepString(value) + "),$)"));
        };
        const auto measure = [&](const std::string& name, double value, const char* type) {
            props.push_back(add("IFCPROPERTYSINGLEVALUE(" + stepString(name) + ",$," + type + "(" + real(value) + "),$)"));
        };
        label("GradoIP", "IP" + std::to_string(L.ip));
        label("FormaSegregazione", L.form);
        label("Carpenteria", L.enclosureFound ? L.enclosure.id : "da scegliere");
        if (L.enclosureFound && !L.enclosure.reference.empty())
            label("Riferimento", L.enclosure.reference);
        label("Montaggio", floor ? "a pavimento" : L.mounting == BoardMounting::Wall ? "a parete" : "cassetta");
        measure("Larghezza", cx, "IFCPOSITIVELENGTHMEASURE");
        measure("Altezza", h, "IFCPOSITIVELENGTHMEASURE");
        measure("Profondita", L.depth, "IFCPOSITIVELENGTHMEASURE");
        measure("CorrenteArrivo", L.incomer, "IFCELECTRICCURRENTMEASURE");
        props.push_back(add("IFCPROPERTYSINGLEVALUE('Colonne',$,IFCINTEGER(" + std::to_string(L.enclosureCount) + "),$)"));
        measure("ModuliOccupati", L.modules, "IFCREAL");
        measure("ModuliDisponibili", L.modulesInstalled, "IFCREAL");
        std::string list;
        for (const int pr : props)
            list += (list.empty() ? "" : ",") + ref(pr);
        const int pset = add("IFCPROPERTYSET(" + guid("pset " + L.board) + ",$,'CartesioCAD_FronteQuadro',$,(" + list + "))");
        add("IFCRELDEFINESBYPROPERTIES(" + guid("rel " + L.board) + ",$,$,$,(" + ref(board) + ")," + ref(pset) + ")");
        x += cx + 1000.0;
    }
    if (!boards.empty()) {
        std::string list;
        for (const int b : boards)
            list += (list.empty() ? "" : ",") + ref(b);
        add("IFCRELCONTAINEDINSPATIALSTRUCTURE(" + guid("contained") + ",$,$,$,(" + list + ")," + ref(storey) + ")");
    }

    std::ostringstream out;
    out << "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('ViewDefinition [ReferenceView_V1.2]'),'2;1');\n"
        << "FILE_NAME(" << stepString(projectName + ".ifc") << ",'2026-01-01T00:00:00',(''),(''),'CartesioCAD','CartesioCAD','');\n"
        << "FILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n"
        << data.str() << "ENDSEC;\nEND-ISO-10303-21;\n";
    return out.str();
}

} // namespace cartesio
