#include "core/SingleLine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <regex>
#include <set>

namespace cartesio {

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && lower(s.substr(0, prefix.size())) == lower(prefix);
}

// Parole vicine sulla stessa riga, unite in un testo.
struct Run {
    double x = 0.0, y = 0.0, right = 0.0, bottom = 0.0;
    double height = 0.0;
    std::string text;
    double cx() const { return (x + right) / 2.0; }
    double cy() const { return (y + bottom) / 2.0; }
};

std::vector<Run> makeRuns(std::vector<const PdfWord*> words)
{
    std::sort(words.begin(), words.end(), [](const PdfWord* a, const PdfWord* b) {
        const double ya = a->y + a->height / 2.0, yb = b->y + b->height / 2.0;
        if (std::abs(ya - yb) > 0.4 * std::max(a->height, b->height))
            return ya < yb;
        return a->x < b->x;
    });
    std::vector<Run> runs;
    for (const PdfWord* w : words) {
        const double cy = w->y + w->height / 2.0;
        if (!runs.empty()) {
            Run& r = runs.back();
            const double h = std::max(r.height, w->height);
            if (std::abs(r.cy() - cy) < 0.4 * h && w->x >= r.right - 0.2 * h && w->x - r.right < 0.9 * h) {
                r.text += ' ';
                r.text += w->text;
                r.right = std::max(r.right, w->x + w->width);
                r.bottom = std::max(r.bottom, w->y + w->height);
                r.y = std::min(r.y, w->y);
                r.height = h;
                continue;
            }
        }
        runs.push_back(Run{w->x, w->y, w->x + w->width, w->y + w->height, w->height, w->text});
    }
    return runs;
}

enum class Field { Code, Description, Power, Current, Phase, Brand, Model, Curve, Rated, Differential, CableType,
                   CableName, Length, Section, Other };

// Voci a sinistra dello schema (si confrontano gli inizi, senza maiuscole).
const std::vector<std::pair<const char*, Field>>& labels()
{
    static const std::vector<std::pair<const char*, Field>> table = {
        {"Sigla utenza", Field::Code},
        {"Descrizione", Field::Description},
        {"POTENZA CONTEMPORANEA", Field::Power},
        {"CORRENTE", Field::Current},
        {"ESECUZIONE LINEA", Field::Phase},
        {"Kc / Ku", Field::Other},
        {"IccMAX", Field::Other},
        {"Marca", Field::Brand},
        {"Modello", Field::Model},
        {"Esecuzione / Curva", Field::Curve},
        {"In (Max", Field::Rated},
        {"Reg. L1", Field::Other},
        {"Im (Max", Field::Other},
        {"Reg. S", Field::Other},
        {"Reg. Ig", Field::Other},
        {"I differenziale", Field::Differential},
        {"P.d.I.", Field::Other},
        {"TENSIONE LINEA", Field::Other},
        {"Tipologia", Field::CableType},
        {"Lunghezza", Field::Length},
        {"Posa", Field::Other},
        {"Sezione", Field::Section},
        {"Portata", Field::Other},
    };
    return table;
}

std::optional<Field> labelOf(const std::string& text)
{
    // "Sigla" da sola è la sigla del cavo; "Sigla utenza" è la riga dei circuiti.
    if (lower(text) == "sigla")
        return Field::CableName;
    for (const auto& [prefix, field] : labels())
        if (startsWithNoCase(text, prefix))
            return field;
    return std::nullopt;
}

double firstNumber(const std::string& s)
{
    static const std::regex number(R"((\d+(?:[.,]\d+)?))");
    std::smatch m;
    if (!std::regex_search(s, m, number))
        return 0.0;
    std::string v = m[1];
    std::replace(v.begin(), v.end(), ',', '.');
    try {
        return std::stod(v);
    } catch (...) {
        return 0.0;
    }
}

std::string join(const std::vector<std::string>& parts, const char* sep)
{
    std::string out;
    for (const auto& p : parts)
        if (!p.empty() && p != "---" && p != "--- / ---" && p != "---/---") {
            if (!out.empty())
                out += sep;
            out += p;
        }
    return out;
}

void parsePage(const std::vector<Run>& runs, int page, std::vector<Circuit>& out, std::set<std::string>& seen)
{
    const Run* sigla = nullptr;
    for (const Run& r : runs)
        if (startsWithNoCase(r.text, "Sigla utenza")) {
            sigla = &r;
            break;
        }
    if (!sigla)
        return;
    const double ySig = sigla->cy();
    const double h = std::max(sigla->height, 1.0);

    // Colonne: le sigle dei circuiti sulla riga "Sigla utenza".
    struct Column {
        double cx;
        std::string code;
        std::map<Field, std::vector<const Run*>> values;
    };
    std::vector<Column> columns;
    for (const Run& r : runs)
        if (&r != sigla && std::abs(r.cy() - ySig) < 0.6 * h && r.x > sigla->right)
            if (const auto code = normalizeCircuitCode(r.text))
                columns.push_back(Column{r.cx(), *code, {}});
    if (columns.empty())
        return;
    std::sort(columns.begin(), columns.end(), [](const Column& a, const Column& b) { return a.cx < b.cx; });
    double spacing = 1e9;
    for (std::size_t i = 1; i < columns.size(); ++i)
        spacing = std::min(spacing, columns[i].cx - columns[i - 1].cx);
    const double half = columns.size() > 1 ? spacing / 2.0 : 45.0;
    const double valuesLeft = columns.front().cx - half;

    // Voci a sinistra, sotto la riga delle sigle.
    struct Label {
        Field field;
        double cy;
    };
    std::vector<Label> rows{{Field::Code, ySig}};
    double lastRow = ySig;
    for (const Run& r : runs)
        if (r.right <= valuesLeft + 0.5 * h && r.cy() > ySig + 0.5 * h)
            if (const auto field = labelOf(r.text); field && *field != Field::Code) {
                rows.push_back(Label{*field, r.cy()});
                lastRow = std::max(lastRow, r.cy());
            }

    for (const Run& r : runs) {
        if (r.cx() < valuesLeft || r.cy() <= ySig + 0.5 * h || r.cy() > lastRow + 1.5 * h)
            continue;
        Column* col = nullptr;
        for (Column& c : columns)
            if (std::abs(r.cx() - c.cx) < half)
                col = &c;
        if (!col)
            continue;
        const Label* best = nullptr;
        for (const Label& l : rows)
            if (!best || std::abs(l.cy - r.cy()) < std::abs(best->cy - r.cy()))
                best = &l;
        if (best && best->field != Field::Code && best->field != Field::Other)
            col->values[best->field].push_back(&r);
    }

    for (Column& c : columns) {
        if (!seen.insert(c.code).second)
            continue;
        const auto text = [&c](Field f) {
            auto& list = c.values[f];
            std::sort(list.begin(), list.end(), [](const Run* a, const Run* b) {
                return std::abs(a->cy() - b->cy()) > 0.4 * a->height ? a->cy() < b->cy() : a->x < b->x;
            });
            std::vector<std::string> parts;
            for (const Run* r : list)
                parts.push_back(r->text);
            return join(parts, " ");
        };
        Circuit circuit;
        circuit.code = c.code;
        circuit.board = c.code.substr(0, c.code.rfind(" C-"));
        circuit.number = std::atoi(c.code.substr(c.code.rfind(" C-") + 3).c_str());
        circuit.description = text(Field::Description);
        circuit.power = text(Field::Power);
        circuit.powerKw = firstNumber(circuit.power);
        circuit.current = text(Field::Current);
        circuit.phase = text(Field::Phase);
        circuit.breaker = join({text(Field::Brand), text(Field::Model), text(Field::Curve), text(Field::Rated)}, " - ");
        circuit.differential = text(Field::Differential);
        circuit.breakerPoles = text(Field::Brand);
        circuit.breakerModel = text(Field::Model);
        circuit.breakerExecution = text(Field::Curve);
        circuit.breakerRated = text(Field::Rated);
        circuit.cable = join({text(Field::CableType), text(Field::CableName), text(Field::Section)}, " - ");
        circuit.length = text(Field::Length);
        circuit.page = page;
        circuit.kind = classifyCircuit(circuit.description);
        out.push_back(std::move(circuit));
    }
}

} // namespace

std::optional<std::string> normalizeCircuitCode(std::string_view text)
{
    static const std::regex code(R"(^\s*([A-Za-z0-9][A-Za-z0-9._/-]*)\s+[Cc]\s*-?\s*(\d+)\s*$)");
    std::cmatch m;
    if (!std::regex_match(text.data(), text.data() + text.size(), m, code))
        return std::nullopt;
    std::string board = m[1];
    for (char& ch : board)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    // Il quadro deve avere almeno una lettera e un trattino o essere una sigla "Q..." (non un numero).
    if (std::none_of(board.begin(), board.end(), [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)); }))
        return std::nullopt;
    return board + " C-" + std::to_string(std::atoi(std::string(m[2]).c_str()));
}

CircuitKind classifyCircuit(std::string_view description)
{
    const std::string d = " " + lower(description) + " ";
    const auto has = [&d](const char* w) { return d.find(w) != std::string::npos; };
    if (has("luce") || has("luci") || has("illumin") || has("lampad") || has(" emergenza") || has("segnapasso"))
        return CircuitKind::Lighting;
    if (has(" fm ") || has("forza motrice") || has("prese") || has("presa") || has("schuko") || has(" cee")
        || has("torrett") || has("bipasso"))
        return CircuitKind::Power;
    return CircuitKind::Other;
}

const char* circuitKindName(CircuitKind kind)
{
    switch (kind) {
    case CircuitKind::Lighting: return "Luce";
    case CircuitKind::Power: return "FM";
    case CircuitKind::Other: return "Utenza";
    }
    return "";
}

std::vector<Circuit> parseSingleLineDiagram(const std::vector<PdfWord>& words)
{
    std::map<int, std::vector<const PdfWord*>> pages;
    for (const PdfWord& w : words)
        pages[w.page].push_back(&w);
    std::vector<Circuit> out;
    std::set<std::string> seen;
    for (auto& [page, list] : pages)
        parsePage(makeRuns(list), page, out, seen);
    return out;
}

} // namespace cartesio
