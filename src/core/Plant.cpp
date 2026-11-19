#include "core/Plant.h"

#include "core/Ampacity.h"
#include "core/Entity.h"

#include <cmath>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <regex>

namespace cartesio::plant {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string upper(std::string s)
{
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n-:;,");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r\n-:;,");
    return s.substr(a, b - a + 1);
}

std::string compactType(const std::string& s)
{
    std::string out;
    for (char c : upper(s))
        if (c != ' ' && c != '\t')
            out += c;
    return out;
}

// Numero con la virgola, senza zeri inutili.
std::string fmt(double v, int decimals = 1)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

double number(std::string s)
{
    std::replace(s.begin(), s.end(), ',', '.');
    return std::strtod(s.c_str(), nullptr);
}

const char* sysOf(System s)
{
    switch (s) {
    case System::Safety: return "BT_FIRE";
    case System::Signal: return "SPECIAL_ELV";
    default: return "BT_POWER";
    }
}

bool shielded(const std::string& type)
{
    static const std::regex re(R"(FTG18|FG16|0,6\/1|1kV|C-4|SCHERM|OH2)", std::regex::ECMAScript | std::regex::icase);
    return std::regex_search(type, re);
}

// Anime e sezione di un modello di cavo dalla sua formazione di catalogo ("1(3G2,5)", "2x0,75", "1(1x16) PE").
bool catalogKey(const canalo::CableType& t, int& cores, double& section)
{
    std::string f = t.form;
    if (const auto space = f.find(' '); space != std::string::npos)
        f = f.substr(0, space);
    const auto parts = parseFormation(f);
    if (parts.size() != 1)
        return false;
    cores = parts[0].cores;
    section = parts[0].section;
    return true;
}

} // namespace

const char* systemName(System s)
{
    switch (s) {
    case System::Safety: return "sicurezza";
    case System::Signal: return "segnale";
    default: return "energia";
    }
}

System systemFromName(const std::string& name)
{
    const std::string n = upper(name);
    if (n.rfind("SIC", 0) == 0 || n == "BT_FIRE")
        return System::Safety;
    if (n.rfind("SEG", 0) == 0 || n == "SPECIAL_ELV")
        return System::Signal;
    return System::Power;
}

const char* runKindName(RunKind k)
{
    switch (k) {
    case RunKind::Duct: return "canale";
    case RunKind::Tray: return "passerella";
    default: return "tubo";
    }
}

Circuit* Project::circuit(const std::string& code)
{
    for (Circuit& c : circuits)
        if (c.code == code)
            return &c;
    return nullptr;
}

const Circuit* Project::circuit(const std::string& code) const
{
    return const_cast<Project*>(this)->circuit(code);
}

Run* Project::run(const std::string& id)
{
    for (Run& r : runs)
        if (r.id == id)
            return &r;
    return nullptr;
}

const Run* Project::run(const std::string& id) const
{
    return const_cast<Project*>(this)->run(id);
}

std::string Project::nextRunId() const
{
    int n = 1;
    while (run("T" + std::to_string(n)))
        ++n;
    return "T" + std::to_string(n);
}

// ------------------------------------------------------------------ tubi

const std::vector<TubeSize>& tubeSizes(const std::string& tubeType)
{
    // Valori indicativi dei tubi in PVC, serie pesante (diametro esterno, interno minimo): i dati del
    // costruttore si scrivono nella tratta (diametro interno).
    static const std::vector<TubeSize> rigid = {{16, 13.0}, {20, 16.9}, {25, 21.4}, {32, 27.8}, {40, 35.4}, {50, 44.3}, {63, 56.5}};
    static const std::vector<TubeSize> corrugated = {{16, 10.7}, {20, 14.1}, {25, 18.3}, {32, 24.3}, {40, 31.2}, {50, 39.6}, {63, 52.6}};
    return upper(tubeType).rfind("CORR", 0) == 0 || upper(tubeType).rfind("FLESS", 0) == 0 ? corrugated : rigid;
}

double tubeInnerDiameter(const Run& run)
{
    if (run.tubeInner > 0.0)
        return run.tubeInner;
    const auto& sizes = tubeSizes(run.tubeType);
    for (const TubeSize& s : sizes)
        if (std::abs(s.outer - run.tubeOuter) < 0.01)
            return s.inner;
    // Diametro fuori tabella: stesso rapporto del più vicino.
    const TubeSize* best = &sizes.front();
    for (const TubeSize& s : sizes)
        if (std::abs(s.outer - run.tubeOuter) < std::abs(best->outer - run.tubeOuter))
            best = &s;
    return run.tubeOuter * best->inner / best->outer;
}

// ------------------------------------------------------------------ formazioni e cavi

std::vector<FormationPart> parseFormation(const std::string& formation)
{
    std::string f;
    for (char c : upper(formation))
        if (c != ' ' && c != '\t')
            f += c == ',' ? '.' : c;
    std::vector<FormationPart> out;
    static const std::regex nestedRe(R"(^(\d+)\((\d+)X1X(\d+(?:\.\d+)?)\)$)");
    static const std::regex groupRe(R"(^(\d+)X?\((\d+)[XG](\d+(?:\.\d+)?)\))");
    static const std::regex peRe(R"(^\(?(\d+)PE(\d+(?:\.\d+)?)\)?$)");
    static const std::regex parenRe(R"(^\((\d+)([XG])(\d+(?:\.\d+)?)\)$)");
    static const std::regex coreRe(R"(^(\d+)[XG](\d+(?:\.\d+)?))");
    static const std::regex singleRe(R"(^(\d+(?:\.\d+)?)$)");
    std::size_t start = 0;
    bool first = true;
    while (start <= f.size()) {
        const std::size_t plus = f.find('+', start);
        const std::string part = f.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        std::smatch m;
        if (std::regex_search(part, m, nestedRe))   // "3(3x1x240)": 3 terne di unipolari
            out.push_back({std::atoi(m[1].str().c_str()) * std::atoi(m[2].str().c_str()), 1, number(m[3].str())});
        else if (std::regex_search(part, m, groupRe))
            out.push_back({std::atoi(m[1].str().c_str()), std::atoi(m[2].str().c_str()), number(m[3].str())});
        else if (std::regex_search(part, m, peRe))   // "(1PE16)": conduttori di protezione
            out.push_back({std::atoi(m[1].str().c_str()), 1, number(m[2].str()), true});
        else if (std::regex_search(part, m, parenRe))   // "(1x95)", "(2x240)": unipolari; "(3G2,5)": un multipolare
            out.push_back(m[2].str() == "X" ? FormationPart{std::atoi(m[1].str().c_str()), 1, number(m[3].str())}
                                             : FormationPart{1, std::atoi(m[1].str().c_str()), number(m[3].str())});
        else if (std::regex_search(part, m, coreRe))
            out.push_back({1, std::atoi(m[1].str().c_str()), number(m[2].str())});
        else if (!first && std::regex_search(part, m, singleRe))
            out.push_back({1, 1, number(m[1].str())});   // "+50": conduttore di protezione
        first = false;
        if (plus == std::string::npos)
            break;
        start = plus + 1;
    }
    std::erase_if(out, [](const FormationPart& p) { return p.count <= 0 || p.cores <= 0 || p.section <= 0.0; });
    return out;
}

void splitCableText(const std::string& text, std::string& type, std::string& formation)
{
    // Schemi unifilari: "Cavo / GENERAL - FG16OM16 - 1(3G2,5)", "NESSUNA LINEA / ---".
    if (text.find(" - ") != std::string::npos) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            const std::size_t dash = text.find(" - ", start);
            parts.push_back(trim(text.substr(start, dash == std::string::npos ? std::string::npos : dash - start)));
            if (dash == std::string::npos)
                break;
            start = dash + 3;
        }
        if (parts.size() >= 3 && !parseFormation(parts.back()).empty()) {
            formation = parts.back();
            type = parts[parts.size() - 2];
            return;
        }
    }
    if (upper(text).find("NESSUNA LINEA") != std::string::npos) {
        type.clear();
        formation.clear();
        return;
    }
    static const std::regex formRe(
        R"((\d+\s*[xX]?\s*\(\s*\d+\s*[xXgG]\s*\d+(?:[.,]\d+)?\s*\)|\d+\s*[xXgG]\s*\d+(?:[.,]\d+)?)(\s*\+\s*(\d+\s*[xX]?\s*\(\s*\d+\s*[xXgG]\s*\d+(?:[.,]\d+)?\s*\)|\d+\s*[xXgG]\s*\d+(?:[.,]\d+)?|\d+(?:[.,]\d+)?))*)");
    std::smatch m;
    if (!std::regex_search(text, m, formRe)) {
        type = trim(text);
        formation.clear();
        return;
    }
    formation = trim(m[0].str());
    type = trim(text.substr(0, static_cast<std::size_t>(m.position())));
    if (type.empty())
        type = trim(text.substr(static_cast<std::size_t>(m.position() + m.length())));
    // "1(3G2,5)": il "1" davanti fa parte della formazione, non del tipo.
}

std::vector<canalo::Cable> cablesOf(const Project& project, const Circuit& circuit, std::string* note)
{
    std::string type = circuit.cableType, formation = circuit.formation;
    if (formation.empty())
        splitCableText(circuit.cableType, type, formation);
    const auto parts = parseFormation(formation);
    std::vector<canalo::Cable> out;
    if (parts.empty()) {
        if (circuit.diameter > 0.0) {
            canalo::Cable c;
            c.code = circuit.code;
            c.type = type;
            c.form = formation;
            c.d = circuit.diameter;
            c.w = canalo::fixed(std::pow(c.d / 10.0, 2) * 0.145, 2);
            c.sys = sysOf(circuit.system);
            c.ib = circuit.ib;
            out.push_back(c);
        } else if (note) {
            *note = "formazione del cavo non riconosciuta: indicare la formazione (es. 3G2,5) o il diametro";
        }
        return out;
    }
    std::vector<const canalo::CableType*> pool;
    for (const canalo::CableType& t : project.cables)
        pool.push_back(&t);
    for (const canalo::CableType& t : canalo::catalog())
        pool.push_back(&t);
    // "FG16M16/FG17 PE": il primo per le fasi, il secondo per i conduttori di protezione.
    std::string phaseType = type, peType;
    if (const auto slash = type.find('/'); slash != std::string::npos && upper(type.substr(slash)).find("PE") != std::string::npos) {
        phaseType = trim(type.substr(0, slash));
        peType = trim(type.substr(slash + 1));
        if (peType.size() > 3 && upper(peType.substr(peType.size() - 3)) == " PE")
            peType = trim(peType.substr(0, peType.size() - 3));
    }
    std::vector<std::string> notes;
    int index = 0;
    const int total = [&] {
        int n = 0;
        for (const auto& p : parts)
            n += p.count;
        return n;
    }();
    for (const FormationPart& part : parts) {
        const std::string wantType = compactType(part.pe && !peType.empty() ? peType : phaseType);
        const canalo::CableType* exact = nullptr;
        const canalo::CableType* sameForm = nullptr;
        const canalo::CableType* nearest = nullptr;
        double nearestScore = 1e9;
        for (const canalo::CableType* t : pool) {
            int cores = 0;
            double sec = 0.0;
            if (!catalogKey(*t, cores, sec) || cores != part.cores)
                continue;
            if (std::abs(sec - part.section) < 1e-6) {
                if (!wantType.empty() && compactType(t->type) == wantType) {
                    exact = t;
                    break;
                }
                // Conduttore di protezione: meglio un PE del catalogo (cordina con una sola guaina).
                if (!sameForm || (part.pe && t->form.find("PE") != std::string::npos && sameForm->form.find("PE") == std::string::npos))
                    sameForm = t;
            }
            const double score = std::abs(std::log(sec / part.section));
            if (score < nearestScore) {
                nearestScore = score;
                nearest = t;
            }
        }
        const canalo::CableType* src = exact ? exact : sameForm ? sameForm : nearest;
        double d = 0.0, w = 0.0;
        if (src) {
            d = src->d;
            w = src->w;
            if (!exact && sameForm)
                notes.push_back("diametro del " + sameForm->type + " " + sameForm->form);
            else if (!exact) {
                int cores = 0;
                double sec = 0.0;
                catalogKey(*src, cores, sec);
                d = canalo::fixed(src->d * std::sqrt(part.section / sec), 1);
                w = canalo::fixed(std::pow(d / 10.0, 2) * 0.145, 2);
                notes.push_back("diametro stimato dal " + src->type + " " + src->form);
            }
        } else {
            d = canalo::fixed(4.0 + 1.6 * std::sqrt(part.cores * part.section) + (part.cores > 1 ? 2.5 : 0.0), 1);
            w = canalo::fixed(std::pow(d / 10.0, 2) * 0.145, 2);
            notes.push_back("diametro stimato dalla sezione");
        }
        if (circuit.diameter > 0.0 && parts.size() == 1) {
            d = circuit.diameter;
            w = canalo::fixed(std::pow(d / 10.0, 2) * 0.145, 2);
        }
        for (int k = 0; k < part.count; ++k) {
            canalo::Cable c;
            c.code = total > 1 ? circuit.code + " /" + std::to_string(++index) : circuit.code;
            c.desc = circuit.description;
            c.type = part.pe && !peType.empty() ? peType : (phaseType.empty() && src ? src->type : phaseType);
            if (total == 1) {
                c.form = formation;   // un solo cavo: la formazione com'è scritta ("2x1,5", "4x2x24AWG")
            } else {
                // Più cavi: la formazione di ognuno; G (con il giallo-verde) solo se c'era nella formazione.
                const bool g = upper(formation).find('G') != std::string::npos;
                char form[64];
                std::snprintf(form, sizeof form, "1(%d%s%s)%s", part.cores, part.cores == 1 ? "x" : (g ? "G" : "x"), fmt(part.section, 2).c_str(),
                              part.pe ? " PE" : "");
                c.form = form;
            }
            c.d = d;
            c.w = w;
            c.sec = part.section;
            c.cores = part.cores == 1 ? 1 : (part.cores >= 4 ? 3 : 2);
            c.iz0 = src ? src->iz0 : 20.0;
            c.bendK = src ? src->bendK : 10.0;
            c.sys = sysOf(circuit.system);
            c.ib = circuit.ib;
            out.push_back(c);
        }
    }
    if (note && !notes.empty()) {
        std::sort(notes.begin(), notes.end());
        notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
        std::string s;
        for (const std::string& n : notes)
            s += (s.empty() ? "" : "; ") + n;
        *note = s;
    }
    return out;
}

// ------------------------------------------------------------------ fascio di cavi in un tubo

namespace {

struct Placed { double x, y, r; };

// Mette i cerchi (già dal più grande) in un contenitore di raggio R, ciascuno il più esterno possibile
// (lascia libero il centro per i successivi). False se uno non trova posto.
bool fits(const std::vector<double>& radii, double R, std::vector<Placed>* result = nullptr)
{
    const double eps = 1e-9;
    std::vector<Placed> placed;
    auto ok = [&](double x, double y, double r) {
        if (std::hypot(x, y) + r > R + 1e-7)
            return false;
        for (const Placed& p : placed)
            if (std::hypot(x - p.x, y - p.y) < p.r + r - 1e-7)
                return false;
        return true;
    };
    for (double r : radii) {
        if (r > R + eps)
            return false;
        if (placed.empty()) {
            placed.push_back({0.0, -(R - r), r});
            continue;
        }
        double bestX = 0.0, bestY = 0.0, bestDist = -1.0, bestAngle = 0.0;
        bool found = false;
        auto consider = [&](double x, double y) {
            if (!ok(x, y, r))
                return;
            const double dist = std::hypot(x, y);
            const double angle = std::atan2(x, -y);   // dal basso, in senso antiorario
            if (!found || dist > bestDist + 1e-9 || (std::abs(dist - bestDist) <= 1e-9 && angle < bestAngle)) {
                found = true;
                bestX = x;
                bestY = y;
                bestDist = dist;
                bestAngle = angle;
            }
        };
        // Intersezioni di due circonferenze (centri c1, c2, raggi a, b).
        auto intersect = [&](double x1, double y1, double a, double x2, double y2, double b) {
            const double dx = x2 - x1, dy = y2 - y1;
            const double d = std::hypot(dx, dy);
            if (d < eps || d > a + b + 1e-9 || d < std::abs(a - b) - 1e-9)
                return;
            const double l = (a * a - b * b + d * d) / (2.0 * d);
            const double h = std::sqrt(std::max(0.0, a * a - l * l));
            const double mx = x1 + l * dx / d, my = y1 + l * dy / d;
            consider(mx + h * dy / d, my - h * dx / d);
            consider(mx - h * dy / d, my + h * dx / d);
        };
        for (const Placed& p : placed)
            intersect(0.0, 0.0, R - r, p.x, p.y, p.r + r);   // tangente al tubo e a un cavo
        for (std::size_t i = 0; i < placed.size(); ++i)
            for (std::size_t j = i + 1; j < placed.size(); ++j)
                intersect(placed[i].x, placed[i].y, placed[i].r + r, placed[j].x, placed[j].y, placed[j].r + r);
        if (!found && ok(0.0, 0.0, r))
            consider(0.0, 0.0);
        if (!found)
            return false;
        placed.push_back({bestX, bestY, r});
    }
    if (result)
        *result = placed;
    return true;
}

} // namespace

double bundleDiameter(std::vector<double> diameters)
{
    std::erase_if(diameters, [](double d) { return d <= 0.0; });
    if (diameters.empty())
        return 0.0;
    std::sort(diameters.begin(), diameters.end(), std::greater<>());
    if (diameters.size() == 1)
        return diameters[0];
    std::vector<double> radii;
    double area = 0.0, sum = 0.0;
    for (double d : diameters) {
        radii.push_back(d / 2.0);
        area += d * d;
        sum += d;
    }
    double lo = std::max(diameters[0], std::sqrt(area)) / 2.0;   // nessun contenitore può essere più piccolo
    double hi = sum / 2.0 + diameters[0];
    if (diameters.size() == 2)
        return diameters[0] + diameters[1];
    while (!fits(radii, hi))
        hi *= 1.5;
    for (int i = 0; i < 50 && hi - lo > 1e-4; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (fits(radii, mid))
            hi = mid;
        else
            lo = mid;
    }
    return hi * 2.0;
}

std::vector<Vec2> bundleLayout(const std::vector<double>& diameters, double* bundle)
{
    std::vector<Vec2> out(diameters.size(), Vec2{0.0, 0.0});
    const double D = bundleDiameter(diameters);
    if (bundle)
        *bundle = D;
    if (diameters.size() <= 1)
        return out;
    // Ordine dal più grande (come il calcolo), poi le posizioni tornano all'ordine dato.
    std::vector<std::size_t> order(diameters.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return diameters[a] > diameters[b]; });
    std::vector<double> radii;
    for (std::size_t i : order)
        radii.push_back(diameters[i] / 2.0);
    std::vector<Placed> placed;
    if (diameters.size() == 2) {
        placed = {{0.0, -(D / 2.0 - radii[0]), radii[0]}, {0.0, D / 2.0 - radii[1], radii[1]}};
    } else if (!fits(radii, D / 2.0 + 1e-6, &placed)) {
        return out;
    }
    for (std::size_t k = 0; k < order.size() && k < placed.size(); ++k)
        out[order[k]] = {placed[k].x, placed[k].y};
    return out;
}

// ------------------------------------------------------------------ verifiche

RunCheck checkRun(const Project& project, const Run& run, double lengthM)
{
    RunCheck rc;
    auto add = [&rc](Level level, std::string text) {
        rc.findings.push_back({level, std::move(text)});
        if (static_cast<int>(level) > static_cast<int>(rc.level))
            rc.level = level;
    };
    std::vector<canalo::Cable> cables;
    bool power = false, safety = false;
    std::vector<std::string> unshieldedSignals;
    for (const std::string& code : run.circuits) {
        const Circuit* c = project.circuit(code);
        if (!c) {
            rc.missing.push_back(code);
            continue;
        }
        std::string note;
        const auto list = cablesOf(project, *c, &note);
        if (list.empty())
            add(Level::Warning, code + ": " + (note.empty() ? std::string("nessun cavo") : note));
        else if (!note.empty())
            add(Level::Warning, code + ": " + note);
        if (c->system == System::Power)
            power = true;
        else if (c->system == System::Safety)
            safety = true;
        else if (!list.empty() && !shielded(list.front().type))
            unshieldedSignals.push_back(code);
        cables.insert(cables.end(), list.begin(), list.end());
    }
    for (const std::string& code : rc.missing)
        add(Level::Error, code + ": circuito non presente nel progetto");
    rc.cableCount = static_cast<int>(cables.size());
    for (const canalo::Cable& c : cables)
        rc.cablesArea += kPi * c.d * c.d / 4.0;
    if (cables.empty()) {
        add(Level::Warning, "nessun cavo nella tratta: assegnare i circuiti");
        return rc;
    }

    if (run.kind == RunKind::Tube) {
        rc.tubeInner = tubeInnerDiameter(run);
        std::vector<double> ds;
        for (const canalo::Cable& c : cables)
            ds.push_back(c.d);
        rc.bundle = bundleDiameter(ds);
        rc.fillPct = canalo::fixed(rc.cablesArea / (kPi * rc.tubeInner * rc.tubeInner / 4.0) * 100.0, 1);
        const double needed = 1.3 * rc.bundle;
        const std::string text = "tubo " + std::string(run.tubeType) + " Ø" + fmt(run.tubeOuter, 0) + " (interno " + fmt(rc.tubeInner) +
                                 " mm): fascio di " + std::to_string(rc.cableCount) + " cavi Ø" + fmt(rc.bundle) +
                                 " mm, serve un interno di almeno " + fmt(needed) + " mm (1,3 volte il fascio, CEI 64-8)";
        if (rc.tubeInner + 1e-9 >= needed) {
            add(Level::Ok, text + ": OK");
        } else {
            double suggest = 0.0;
            for (const TubeSize& s : tubeSizes(run.tubeType))
                if (s.inner >= needed) {
                    suggest = s.outer;
                    break;
                }
            add(Level::Error, text + ": NON VERIFICATO" + (suggest > 0.0 ? " (serve il Ø" + fmt(suggest, 0) + ")" : ""));
        }
        // Segregazione: un tubo è un solo scomparto.
        if (!unshieldedSignals.empty() && (power || safety)) {
            std::string list;
            for (const std::string& s : unshieldedSignals)
                list += (list.empty() ? "" : ", ") + s;
            add(Level::Error, "segregazione: segnali non schermati (" + list +
                                  ") nello stesso tubo dei circuiti di energia (CEI 64-8 art. 528.1): separarli");
        } else {
            add(Level::Ok, "segregazione: OK");
        }
        if (safety && power)
            add(Level::Warning, "circuiti di sicurezza nello stesso tubo di circuiti ordinari: verificare (CEI 64-8 Sez. 56)");
        return rc;
    }

    // Canali e passerelle: la sezione con i calcoli di CanaloCAD.
    canalo::Tray tray;
    tray.width = run.width;
    tray.height = run.height;
    tray.septums = run.septums;
    tray.cover = run.cover;
    tray.packing = "gravity";
    tray.lengthM = lengthM > 0.0 ? lengthM : 1.0;
    if (run.kind == RunKind::Duct) {
        tray.trayType = "solid";
        tray.support = "none";
    } else {
        tray.brand = run.brand;
        tray.seriesId = run.seriesId;
        tray.trayType = "perforated";
        for (const canalo::Series& s : canalo::seriesOf(run.brand))
            if (s.id == run.seriesId)
                tray.trayType = s.type;
        tray.support = run.support;
        tray.spanM = run.spanM;
    }
    tray.cables = cables;
    rc.tray = canalo::recalculate(tray);
    rc.fillPct = rc.tray.fillPct;
    bool overflow = false;
    for (const canalo::Cable& c : tray.cables)
        overflow = overflow || c.y + c.d / 2.0 > tray.height + 0.5;
    const std::string what = std::string(runKindName(run.kind)) + " " + fmt(run.width, 0) + "×" + fmt(run.height, 0) + " mm";
    const std::string fill = what + ": " + std::to_string(rc.cableCount) + " cavi, " + fmt(rc.cablesArea, 0) + " mm² su " +
                             fmt(run.width * run.height, 0) + " mm² (" + fmt(rc.fillPct) + "%)";
    if (run.kind == RunKind::Duct) {
        if (rc.fillPct <= 50.0)
            add(Level::Ok, fill + ", al massimo il 50% della sezione (CEI 64-8): OK");
        else
            add(Level::Error, fill + ": oltre il 50% della sezione (CEI 64-8): NON VERIFICATO");
    } else if (rc.fillPct <= 50.0) {
        add(Level::Ok, fill + ": OK");
    } else {
        add(Level::Warning, fill + ": riserva sotto il 50%");
    }
    if (overflow)
        add(Level::Error, "i cavi non stanno in un solo strato sotto il bordo: sezione insufficiente");
    if (rc.tray.segConform)
        add(Level::Ok, "segregazione: " + rc.tray.segDetail);
    else
        add(Level::Error, "segregazione: " + rc.tray.segDetail + " (CEI 64-8 art. 528.1)");
    if (run.kind == RunKind::Tray && run.support != "none") {
        const canalo::Support* s = canalo::support(run.support);
        const std::string name = s ? s->name : run.support;
        const std::string load = "staffaggio " + name + " ogni " + fmt(run.spanM, 2) + " m: " + fmt(rc.tray.pointLoad) + " kg su " +
                                 fmt(rc.tray.maxPoint, 0) + " kg (" + fmt(rc.tray.loadPct) + "%), freccia " + fmt(rc.tray.deflectionMm, 2) +
                                 " mm su " + fmt(rc.tray.maxDeflectionMm) + " mm";
        if (rc.tray.loadPct <= 100.0 && rc.tray.deflectionMm <= rc.tray.maxDeflectionMm + 1e-9)
            add(Level::Ok, load + ": OK");
        else
            add(Level::Error, load + ": NON VERIFICATO");
    } else if (run.kind == RunKind::Tray) {
        add(Level::Warning, "staffaggio non indicato: carico non verificato");
    }
    return rc;
}

// ------------------------------------------------------------------ circuiti: portata e caduta di tensione

int phasesOf(const Circuit& circuit)
{
    std::string type, formation = circuit.formation;
    if (formation.empty())
        splitCableText(circuit.cableType, type, formation);
    const auto parts = parseFormation(formation);
    if (parts.empty())
        return 1;
    const FormationPart& p = parts.front();
    if (p.cores >= 4)
        return 3;
    if (p.cores == 1 && p.count >= 3)
        return 3;
    return 1;
}

double groupingFactor(int circuits)
{
    static const std::pair<int, double> table[] = {{1, 1.00}, {2, 0.80}, {3, 0.70}, {4, 0.65}, {5, 0.60}, {6, 0.57}, {7, 0.54},
                                                   {8, 0.52}, {9, 0.50}, {12, 0.45}, {16, 0.41}, {20, 0.38}};
    double k = 1.0;
    for (const auto& [n, value] : table)
        if (circuits >= n)
            k = value;
    return k;
}

CircuitCheck checkCircuit(const Project& project, const Circuit& circuit, const std::map<std::string, double>& runLengthsM)
{
    CircuitCheck cc;
    auto add = [&cc](Level level, std::string text) {
        cc.findings.push_back({level, std::move(text)});
        if (static_cast<int>(level) > static_cast<int>(cc.level))
            cc.level = level;
    };
    const auto cables = cablesOf(project, circuit);
    if (cables.empty() || circuit.system == System::Signal)   // segnali e dati: niente portata né caduta di tensione
        return cc;
    std::string type, formation = circuit.formation;
    if (formation.empty())
        splitCableText(circuit.cableType, type, formation);
    const auto parts = parseFormation(formation);
    const int phases = phasesOf(circuit);
    if (!parts.empty() && parts.front().cores == 1 && !parts.front().pe) {
        const int perSet = phases == 3 ? (parts.front().count % 4 == 0 ? 4 : 3) : 2;
        cc.parallel = std::max(1, parts.front().count / perSet);
    } else if (!parts.empty()) {
        cc.parallel = std::max(1, parts.front().count);
    }
    const bool unipolar = !parts.empty() && parts.front().cores == 1;
    const Insulation insulation = insulationOf(cables.front().type);
    const int loaded = phases == 3 ? 3 : 2;
    const double section = parts.empty() ? 0.0 : parts.front().section;
    // Portata di un cavo per un metodo di posa: imposta, dalle tabelle o (fuori tabella) dal catalogo.
    auto base = [&](Method m, bool& fromTable) {
        fromTable = false;
        if (circuit.izRef > 0.0)
            return circuit.izRef;
        if (const auto v = tableAmpacity(insulation, m, loaded, section)) {
            fromTable = true;
            return *v;
        }
        return cables.front().iz0;
    };

    // Tratte che contengono il circuito: vale la portata più bassa.
    double measured = 0.0;
    bool first = true;
    for (const Run& r : project.runs) {
        if (std::find(r.circuits.begin(), r.circuits.end(), circuit.code) == r.circuits.end())
            continue;
        cc.runs.push_back(r.id);
        if (const auto it = runLengthsM.find(r.id); it != runLengthsM.end())
            measured += it->second;
        Method m;
        double kg = 1.0;
        const int n = static_cast<int>(r.circuits.size());
        if (r.kind == RunKind::Tray) {
            // Multipolari: aria libera (E); unipolari a contatto: C, cautelativo. Più strati: riduzione di CanaloCAD.
            m = unipolar ? Method::C : Method::E;
            kg = trayGroupingFactor(n) * checkRun(project, r).tray.kStrati;
        } else {
            const bool embedded = r.kind == RunKind::Tube && r.laying == "incassato";
            m = embedded ? (unipolar ? Method::A1 : Method::A2) : (unipolar ? Method::B1 : Method::B2);
            kg = groupingFactor(n);
        }
        bool table = false;
        const double b = base(m, table);
        const double kt = temperatureFactor(insulation, r.ambientC);
        const double iz = b * cc.parallel * kg * kt;
        if (first || iz < cc.iz) {
            first = false;
            cc.iz = iz;
            cc.iz0 = b;
            cc.method = methodName(m);
            cc.fromTable = table;
            cc.kGroup = kg;
            cc.kTemp = kt;
        }
    }
    if (first) {   // in nessuna tratta: aria libera, 30 °C
        const Method m = unipolar ? Method::C : Method::E;
        bool table = false;
        cc.iz0 = base(m, table);
        cc.method = methodName(m);
        cc.fromTable = table;
        cc.iz = cc.iz0 * cc.parallel;
    }
    cc.iz = canalo::fixed(cc.iz, 1);
    const std::string source = circuit.izRef > 0.0 ? "imposta"
                               : cc.fromTable ? cc.method + ", " + (insulation == Insulation::PVC ? "PVC" : "EPR") + ", " + std::to_string(loaded) + " cond."
                                              : "catalogo, fuori tabella";
    const std::string izText = "Iz' = " + fmt(cc.iz0) + " A (" + source + ")" + (cc.parallel > 1 ? " × " + std::to_string(cc.parallel) : std::string()) +
                               " × " + fmt(cc.kGroup, 2) + (cc.kTemp != 1.0 ? " × " + fmt(cc.kTemp, 2) : std::string()) + " = " + fmt(cc.iz) + " A";
    if (circuit.ib <= 0.0) {
        add(Level::Warning, "portata: " + izText + ", corrente d'impiego non indicata");
    } else if (cc.iz + 1e-9 < circuit.ib) {
        add(Level::Error, "portata: Ib " + fmt(circuit.ib) + " A oltre " + izText + ": NON VERIFICATA");
    } else if (circuit.breakerIn > 0.0 && (circuit.ib > circuit.breakerIn + 1e-9 || circuit.breakerIn > cc.iz + 1e-9)) {
        add(Level::Error, "protezione: Ib " + fmt(circuit.ib) + " ≤ In " + fmt(circuit.breakerIn) + " ≤ " + izText +
                              " non rispettata (CEI 64-8 art. 433.2)");
    } else {
        add(Level::Ok, "portata: Ib " + fmt(circuit.ib) + (circuit.breakerIn > 0.0 ? " ≤ In " + fmt(circuit.breakerIn) : std::string()) +
                           " ≤ " + izText + ": OK");
    }
    if (cc.runs.empty())
        add(Level::Warning, "il circuito non è in nessuna tratta: portata in aria libera, senza raggruppamento");

    // Caduta di tensione (resistiva, cosφ 0,9, rame a regime): lunghezza dello schema, altrimenti delle tratte.
    cc.lengthM = circuit.lengthM > 0.0 ? circuit.lengthM : measured;
    cc.measured = circuit.lengthM <= 0.0 && measured > 0.0;
    if (cc.lengthM > 0.0 && circuit.ib > 0.0 && !parts.empty()) {
        const double sec = std::max(0.5, parts.front().section);
        const double kP = phases == 3 ? 1.732 : 2.0;
        const double vN = phases == 3 ? 400.0 : 230.0;
        cc.dvPct = canalo::fixed(((kP * cc.lengthM * circuit.ib * (0.0214 / sec) * 0.9) / vN) * 100.0 / cc.parallel, 2);
        const std::string text = "caduta di tensione " + fmt(cc.dvPct, 2) + "% su " + fmt(cc.lengthM) + " m" +
                                 (cc.measured ? " (misurati sul disegno)" : "");
        if (cc.dvPct <= 4.0)
            add(Level::Ok, text + ": OK");
        else
            add(Level::Warning, text + ": oltre il 4% raccomandato (CEI 64-8 art. 525)");
    } else if (circuit.ib > 0.0) {
        add(Level::Warning, "caduta di tensione: lunghezza non nota");
    }
    return cc;
}

// ------------------------------------------------------------------ distinta materiali

std::vector<BomItem> plantBom(const Project& project, const std::map<std::string, RunGeometry>& geometry)
{
    std::vector<BomItem> items;
    auto add = [&items](const std::string& category, const std::string& description, double qty, const std::string& unit,
                        const std::string& note = {}) {
        if (qty <= 0.0)
            return;
        for (BomItem& it : items)
            if (it.category == category && it.description == description && it.unit == unit) {
                it.qty += qty;
                return;
            }
        items.push_back({category, description, qty, unit, note});
    };
    auto geo = [&geometry](const std::string& id) {
        const auto it = geometry.find(id);
        return it == geometry.end() ? RunGeometry{} : it->second;
    };
    for (const Run& r : project.runs) {
        const RunGeometry g = geo(r.id);
        if (g.lengthM <= 0.0)
            continue;
        const std::string size = fmt(r.width, 0) + "×" + fmt(r.height, 0);
        if (r.kind == RunKind::Tube) {
            const bool rigid = upper(r.tubeType).rfind("RIG", 0) == 0;
            const std::string d = "Ø" + fmt(r.tubeOuter, 0);
            add("Tubi", "Tubo " + r.tubeType + " " + d, g.lengthM, "m", rigid ? "barre da 3 m" : "");
            if (rigid) {
                add("Tubi", "Curva per tubo rigido " + d, g.bends, "cad");
                add("Tubi", "Manicotto di giunzione " + d, std::ceil(g.lengthM / 3.0), "cad");
            }
            add("Tubi", "Cassetta di derivazione per tubo " + d, g.tees, "cad");
        } else if (r.kind == RunKind::Duct) {
            add("Canali", "Canale " + size, g.lengthM, "m", "barre da 2 m");
            if (r.cover != "none")
                add("Canali", "Coperchio per canale L=" + fmt(r.width, 0), g.lengthM, "m");
            add("Canali", "Setto separatore per canale H=" + fmt(r.height, 0), g.lengthM * r.septums, "m");
            add("Canali", "Curva piana per canale " + size, g.bends, "cad");
            add("Canali", "Derivazione a T per canale " + size, g.tees, "cad");
            add("Canali", "Giunzione per canale " + size, std::ceil(g.lengthM / 2.0), "cad");
        } else {
            const std::vector<canalo::Series>& list = canalo::seriesOf(r.brand);
            const canalo::Series* series = &list.front();
            for (const canalo::Series& s : list)
                if (s.id == r.seriesId)
                    series = &s;
            const double bars = std::ceil(g.lengthM / 3.0);
            add("Passerelle", series->name + " " + size, bars * 3.0, "m", "verghe da 3 m");
            if (r.cover != "none")
                add("Passerelle", "Coperchio per passerella L=" + fmt(r.width, 0), bars * 3.0, "m");
            add("Passerelle", "Setto separatore H=" + fmt(r.height, 0), bars * 3.0 * r.septums, "m");
            add("Passerelle", "Kit di giunzione per passerella " + size, bars, "cad");
            add("Passerelle", "Curva piana 90° per passerella " + size, g.bends, "cad");
            add("Passerelle", "Derivazione a T per passerella " + size, g.tees, "cad");
            if (r.support != "none") {
                const canalo::Support* s = canalo::support(r.support);
                add("Passerelle", (s ? s->name : r.support) + " L=" + fmt(r.width + 50.0, 0), std::ceil(g.lengthM / r.spanM) + 1.0, "cad",
                    "passo " + fmt(r.spanM, 2) + " m");
            }
        }
    }
    // Cavi: metri per tipo e formazione.
    int withoutLength = 0;
    for (const Circuit& c : project.circuits) {
        double length = c.lengthM;
        bool inRun = false;
        if (length <= 0.0)
            for (const Run& r : project.runs)
                if (std::find(r.circuits.begin(), r.circuits.end(), c.code) != r.circuits.end()) {
                    inRun = true;
                    // Un circuito ripetuto nella tratta sono più linee uguali.
                    length += geo(r.id).lengthM * static_cast<double>(std::count(r.circuits.begin(), r.circuits.end(), c.code));
                }
        const auto cables = cablesOf(project, c);
        if (cables.empty())
            continue;
        if (length <= 0.0) {
            withoutLength += inRun;
            continue;
        }
        for (const canalo::Cable& cable : cables)
            add("Cavi", (cable.type.empty() ? std::string("Cavo") : cable.type) + " " + cable.form, length, "m");
    }
    if (withoutLength > 0)
        items.push_back({"Cavi", "Circuiti nelle tratte senza lunghezza (non contati)", static_cast<double>(withoutLength), "cad",
                         "tratte non disegnate in questo disegno"});
    for (BomItem& it : items)
        it.qty = it.unit == "m" ? canalo::fixed(it.qty, 1) : it.qty;
    // Ordine: per categoria nell'ordine di comparsa, poi per descrizione.
    std::vector<std::string> categories;
    for (const BomItem& it : items)
        if (std::find(categories.begin(), categories.end(), it.category) == categories.end())
            categories.push_back(it.category);
    std::stable_sort(items.begin(), items.end(), [&](const BomItem& a, const BomItem& b) {
        const auto ia = std::find(categories.begin(), categories.end(), a.category) - categories.begin();
        const auto ib = std::find(categories.begin(), categories.end(), b.category) - categories.begin();
        return ia != ib ? ia < ib : a.description < b.description;
    });
    return items;
}

std::string bomCsv(const std::vector<BomItem>& items)
{
    auto cell = [](const std::string& s) {
        if (s.find_first_of(";\"\n") == std::string::npos)
            return s;
        std::string out = "\"";
        for (char c : s)
            out += c == '"' ? std::string("\"\"") : std::string(1, c);
        return out + "\"";
    };
    std::string out = "Categoria;Descrizione;Quantità;UM;Note\r\n";
    for (const BomItem& it : items)
        out += cell(it.category) + ";" + cell(it.description) + ";" + fmt(it.qty, 1) + ";" + it.unit + ";" + cell(it.note) + "\r\n";
    return out;
}

std::unique_ptr<TableEntity> bomTable(const std::vector<BomItem>& items, Vec2 topLeft, double textHeight)
{
    auto t = std::make_unique<TableEntity>();
    t->position = topLeft;
    t->textHeight = textHeight;
    t->resize(static_cast<int>(items.size()) + 1, 5, textHeight * 10.0, textHeight * 1.9);
    const double widths[] = {10.0, 38.0, 7.0, 4.5, 20.0};
    for (int c = 0; c < 5; ++c)
        t->columnWidths[static_cast<std::size_t>(c)] = widths[c] * textHeight;
    const char* head[] = {"Categoria", "Descrizione", "Q.tà", "UM", "Note"};
    for (int c = 0; c < 5; ++c)
        t->cell(0, c) = head[c];
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int r = static_cast<int>(i) + 1;
        t->cell(r, 0) = items[i].category;
        t->cell(r, 1) = items[i].description;
        t->cell(r, 2) = fmt(items[i].qty, 1);
        t->cell(r, 3) = items[i].unit;
        t->cell(r, 4) = items[i].note;
    }
    return t;
}

// ------------------------------------------------------------------ sezione e etichetta

std::string runLabel(const Run& run, const RunCheck& check)
{
    std::string size = run.kind == RunKind::Tube ? "Ø" + fmt(run.tubeOuter, 0) : fmt(run.width, 0) + "×" + fmt(run.height, 0);
    std::string esito = check.level == Level::Error ? "NON VERIFICATA" : check.level == Level::Warning ? "DA CONTROLLARE" : "VERIFICATA";
    return run.id + " " + runKindName(run.kind) + " " + size + " - " + std::to_string(run.circuits.size()) + " circuiti" +
           (check.cableCount > 0 ? " - " + fmt(check.fillPct, 0) + "%" : std::string()) + " - " + esito;
}

std::vector<std::unique_ptr<Entity>> drawRunSection(const Project& project, const Run& run, const SectionStyle& st)
{
    std::vector<std::unique_ptr<Entity>> out;
    const double s = st.scale, th = st.textHeight;
    auto keep = [&](std::unique_ptr<Entity> e, int color = -2) {
        e->setLayer(st.layer);
        if (color != -2)
            e->setColor(color);
        e->setPlantRun(st.tag);
        out.push_back(std::move(e));
    };
    auto at = [&](double x, double y) { return Vec2{st.origin.x + x * s, st.origin.y + y * s}; };
    auto colorOf = [](const std::string& sys) { return sys == "BT_FIRE" ? 1 : sys == "SPECIAL_ELV" ? 5 : 256; };   // 256 = DaLayer

    // Cavi e numero del circuito di ognuno.
    std::vector<canalo::Cable> cables;
    std::vector<int> circuitNumber;
    std::vector<std::string> order;
    for (const std::string& code : run.circuits) {
        const Circuit* c = project.circuit(code);
        if (!c)
            continue;
        auto it = std::find(order.begin(), order.end(), code);
        if (it == order.end()) {
            order.push_back(code);
            it = order.end() - 1;
        }
        for (const canalo::Cable& cable : cablesOf(project, *c)) {
            cables.push_back(cable);
            circuitNumber.push_back(static_cast<int>(it - order.begin()) + 1);
        }
    }
    const RunCheck rc = checkRun(project, run);
    double top = 0.0, bottom = 0.0, left = 0.0;
    auto number = [&](Vec2 center, double d, int n) {
        const double h = std::min(th * 0.8, d * s * 0.55);
        auto t = std::make_unique<TextEntity>(center, h, std::to_string(n));
        t->hAlign = TextEntity::Middle;
        t->alignPoint = center;
        keep(std::move(t));
    };

    if (run.kind == RunKind::Tube) {
        const double inner = tubeInnerDiameter(run);
        keep(std::make_unique<CircleEntity>(at(0, 0), run.tubeOuter / 2.0 * s));
        keep(std::make_unique<CircleEntity>(at(0, 0), inner / 2.0 * s));
        std::vector<double> ds;
        for (const canalo::Cable& c : cables)
            ds.push_back(c.d);
        double B = 0.0;
        const auto centers = bundleLayout(ds, &B);
        const double drop = B <= inner ? -(inner / 2.0 - B / 2.0) : 0.0;   // il fascio appoggia sul fondo
        for (std::size_t i = 0; i < cables.size(); ++i) {
            const Vec2 c = at(centers[i].x, centers[i].y + drop);
            keep(std::make_unique<CircleEntity>(c, cables[i].d / 2.0 * s), colorOf(cables[i].sys));
            number(c, cables[i].d, circuitNumber[i]);
        }
        top = run.tubeOuter / 2.0 * s;
        bottom = -top;
        left = -top;
    } else {
        canalo::Tray tray;
        tray.width = run.width;
        tray.height = run.height;
        tray.septums = run.septums;
        tray.cover = run.cover;
        tray.cables = cables;
        canalo::recalculate(tray);
        const double W = run.width, H = run.height;
        const bool closed = run.kind == RunKind::Duct && run.cover != "none";
        std::vector<PolyVertex> v = closed ? std::vector<PolyVertex>{{at(0, 0)}, {at(W, 0)}, {at(W, H)}, {at(0, H)}}
                                           : std::vector<PolyVertex>{{at(0, H)}, {at(0, 0)}, {at(W, 0)}, {at(W, H)}};
        keep(std::make_unique<PolylineEntity>(v, closed));
        if (run.kind == RunKind::Tray && run.cover != "none")   // coperchio della passerella, staccato di 2 mm
            keep(std::make_unique<LineEntity>(at(-2, H + 2), at(W + 2, H + 2)));
        for (double x : canalo::septumX(tray))
            keep(std::make_unique<LineEntity>(at(x, 0), at(x, H)));
        for (std::size_t i = 0; i < tray.cables.size(); ++i) {
            const Vec2 c = at(tray.cables[i].x, tray.cables[i].y);
            keep(std::make_unique<CircleEntity>(c, tray.cables[i].d / 2.0 * s), colorOf(tray.cables[i].sys));
            number(c, tray.cables[i].d, circuitNumber[i]);
        }
        top = (H + 2) * s;
        bottom = 0.0;
        left = 0.0;
    }

    // Titolo con l'esito (colore: verde, arancione, rosso).
    const std::string what = run.kind == RunKind::Tube
                                 ? "tubo " + run.tubeType + " Ø" + fmt(run.tubeOuter, 0) + " (interno " + fmt(tubeInnerDiameter(run)) + " mm)"
                                 : std::string(runKindName(run.kind)) + " " + fmt(run.width, 0) + "×" + fmt(run.height, 0) + " mm";
    std::string title = "SEZIONE " + run.id + (run.name.empty() ? "" : " - " + run.name) + "\\P" + what +
                        (st.scaleText.empty() ? "" : " - scala " + st.scaleText);
    const std::string esito = rc.level == Level::Error ? "NON VERIFICATA" : rc.level == Level::Warning ? "DA CONTROLLARE" : "VERIFICATA";
    title += "\\PRiempimento " + fmt(rc.fillPct) + "% - " + esito;
    auto t = std::make_unique<MTextEntity>(Vec2{st.origin.x + left, st.origin.y + top + th * 5.2}, th, title);
    keep(std::move(t), rc.level == Level::Error ? 1 : rc.level == Level::Warning ? 30 : 3);

    // Legenda dei circuiti.
    std::string legend;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Circuit* c = project.circuit(order[i]);
        int count = 0;
        double d = 0.0;
        for (std::size_t k = 0; k < cables.size(); ++k)
            if (circuitNumber[k] == static_cast<int>(i) + 1) {
                ++count;
                d = cables[k].d;
            }
        const auto times = std::count(run.circuits.begin(), run.circuits.end(), order[i]);
        legend += (legend.empty() ? "" : "\\P") + std::to_string(i + 1) + "  " + order[i] + (times > 1 ? " (×" + std::to_string(times) + ")" : "") +
                  "  " + c->cableType + " " + c->formation + "  " + std::to_string(count / std::max<long>(1, times)) + " × Ø" + fmt(d) +
                  (c->description.empty() ? "" : "  " + c->description);
    }
    if (!legend.empty())
        keep(std::make_unique<MTextEntity>(Vec2{st.origin.x + left, st.origin.y + bottom - th * 1.5}, th * 0.8, legend));
    return out;
}

} // namespace cartesio::plant
