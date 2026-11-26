#include "core/DataExport.h"

#include "core/QuickSelect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <unordered_set>

namespace cartesio {

namespace {

constexpr double kPiValue = 3.14159265358979323846;

class Writer {
public:
    explicit Writer(char separator) : sep_(separator) {}

    // Testo del disegno: tra virgolette se serve, e mai interpretabile come formula.
    void text(const std::string& value)
    {
        std::string v = value;
        if (!v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '-' || v[0] == '@' || v[0] == '\t' || v[0] == '\r'))
            v.insert(v.begin(), '\'');
        raw(v);
    }
    // Numero con al massimo 4 decimali (senza zeri finali); con il separatore ';' la virgola decimale.
    void number(double value)
    {
        if (!std::isfinite(value)) {
            raw(std::string());
            return;
        }
        if (std::abs(value) < 5e-5)
            value = 0.0;
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.4f", value);
        std::string s = buffer;
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
        if (sep_ == ';')
            std::replace(s.begin(), s.end(), '.', ',');
        raw(s);
    }
    void empty() { raw(std::string()); }
    void integer(long long value) { raw(std::to_string(value)); }
    void endRow()
    {
        out_ += "\r\n";
        first_ = true;
    }
    std::string take() { return std::move(out_); }

private:
    void raw(const std::string& v)
    {
        if (!first_)
            out_ += sep_;
        first_ = false;
        const bool quote = v.find_first_of(std::string(1, sep_) + "\"\r\n") != std::string::npos;
        if (!quote) {
            out_ += v;
            return;
        }
        out_ += '"';
        for (char c : v) {
            if (c == '"')
                out_ += '"';
            out_ += c;
        }
        out_ += '"';
    }

    char sep_;
    std::string out_;
    bool first_ = true;
};

std::string upper(std::string s)
{
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string layerName(const Document& doc, const Entity& e)
{
    const auto& layers = doc.layers();
    const int li = e.layer();
    return li >= 0 && li < static_cast<int>(layers.size()) ? layers[static_cast<std::size_t>(li)].name : std::string("0");
}

std::string spaceName(const Document& doc, int space)
{
    if (space <= 0)
        return "Modello";
    for (const Layout& l : doc.layouts())
        if (l.id == space)
            return l.name;
    return "Layout " + std::to_string(space);
}

// Lunghezza delle curve (NaN per gli oggetti che non ne hanno una).
double lengthOf(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Line:
    case EntityType::Arc:
    case EntityType::Circle:
    case EntityType::Polyline:
    case EntityType::Ellipse:
    case EntityType::Spline: {
        if (e.type() == EntityType::Polyline)
            return static_cast<const PolylineEntity&>(e).length();
        double total = 0.0;
        for (const Prim& p : e.prims())
            total += p.kind == Prim::Kind::Arc ? p.radius * p.sweep : distance(p.a, p.b);
        return total;
    }
    default:
        return std::nan("");
    }
}

// Area delle figure chiuse (NaN per le altre).
double areaOf(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Circle: {
        const double r = static_cast<const CircleEntity&>(e).radius;
        return kPiValue * r * r;
    }
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        return p.closed ? std::abs(p.area()) : std::nan("");
    }
    case EntityType::Ellipse: {
        const auto& el = static_cast<const EllipseEntity&>(e);
        if (!el.isFull())
            return std::nan("");
        const double a = el.majorAxis.length();
        return kPiValue * a * a * el.ratio;
    }
    default:
        return std::nan("");
    }
}

std::string textOf(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Text: return static_cast<const TextEntity&>(e).text;
    case EntityType::MText: return mtextPlainText(static_cast<const MTextEntity&>(e).text);
    case EntityType::Insert: return static_cast<const InsertEntity&>(e).effectiveName();
    default: return std::string();
    }
}

// Punto di riferimento: inizio, centro o punto di inserimento; per gli altri il centro dell'ingombro.
Vec2 referencePoint(const Entity& e, bool* ok)
{
    *ok = true;
    switch (e.type()) {
    case EntityType::Line: return static_cast<const LineEntity&>(e).start;
    case EntityType::Circle: return static_cast<const CircleEntity&>(e).center;
    case EntityType::Arc: return static_cast<const ArcEntity&>(e).center;
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        if (!p.vertices.empty())
            return p.vertices.front().point;
        break;
    }
    case EntityType::Text: return static_cast<const TextEntity&>(e).position;
    case EntityType::MText: return static_cast<const MTextEntity&>(e).position;
    case EntityType::Insert: return static_cast<const InsertEntity&>(e).position;
    default: break;
    }
    const BBox b = e.bounds();
    if (!b.isValid()) {
        *ok = false;
        return {};
    }
    return (b.min + b.max) * 0.5;
}

} // namespace

const char* dataExportModeName(DataExportOptions::Mode mode)
{
    switch (mode) {
    case DataExportOptions::Mode::Count: return "conteggio";
    case DataExportOptions::Mode::Objects: return "oggetti";
    default: return "blocchi";
    }
}

bool parseDataExportMode(const std::string& text, DataExportOptions::Mode* mode)
{
    const std::string t = upper(text);
    if (t == "BLOCCHI" || t == "BLOCKS" || t == "B")
        *mode = DataExportOptions::Mode::Blocks;
    else if (t == "CONTEGGIO" || t == "COUNT" || t == "C" || t == "DISTINTA")
        *mode = DataExportOptions::Mode::Count;
    else if (t == "OGGETTI" || t == "OBJECTS" || t == "O")
        *mode = DataExportOptions::Mode::Objects;
    else
        return false;
    return true;
}

DataExportResult exportData(const Document& doc, const DataExportOptions& options)
{
    const std::unordered_set<EntityId> only(options.only.begin(), options.only.end());
    std::vector<const Entity*> chosen;
    for (const auto& e : doc.entities()) {
        if (!e || e->type() == EntityType::Viewport)
            continue;
        if (e->space() > 0 && !options.layouts)
            continue;
        if (!only.empty() && !only.count(e->id()))
            continue;
        chosen.push_back(e.get());
    }

    Writer w(options.separator);
    DataExportResult result;

    if (options.mode == DataExportOptions::Mode::Objects) {
        for (const char* h : {"Tipo", "Layer", "Colore", "Tipo di linea", "Spazio", "X", "Y", "Lunghezza", "Area", "Testo o blocco"})
            w.text(h);
        w.endRow();
        for (const Entity* e : chosen) {
            w.text(entityTypeName(e->type()));
            w.text(layerName(doc, *e));
            w.text(propertyValue(doc, *e, "color").value_or(std::string()));
            w.text(propertyValue(doc, *e, "linetype").value_or(std::string()));
            w.text(spaceName(doc, e->space()));
            bool ok = false;
            const Vec2 p = referencePoint(*e, &ok);
            if (ok) {
                w.number(p.x);
                w.number(p.y);
            } else {
                w.empty();
                w.empty();
            }
            w.number(lengthOf(*e));
            w.number(areaOf(*e));
            w.text(textOf(*e));
            w.endRow();
            ++result.rows;
        }
        result.csv = w.take();
        return result;
    }

    // Blocchi: le etichette degli attributi in ordine di comparsa diventano le colonne.
    std::vector<const InsertEntity*> inserts;
    std::vector<std::string> tags;
    std::set<std::string> seen;
    for (const Entity* e : chosen) {
        if (e->type() != EntityType::Insert)
            continue;
        const auto* in = static_cast<const InsertEntity*>(e);
        inserts.push_back(in);
        for (const Attribute& a : in->attributes)
            if (seen.insert(upper(a.tag)).second)
                tags.push_back(upper(a.tag));
    }
    const auto values = [&tags](const InsertEntity& in) {
        std::vector<std::string> v(tags.size());
        for (const Attribute& a : in.attributes) {
            const auto it = std::find(tags.begin(), tags.end(), upper(a.tag));
            if (it != tags.end() && v[static_cast<std::size_t>(it - tags.begin())].empty())
                v[static_cast<std::size_t>(it - tags.begin())] = a.text.text;
        }
        return v;
    };

    if (options.mode == DataExportOptions::Mode::Count) {
        w.text("Blocco");
        for (const std::string& t : tags)
            w.text(t);
        w.text("Quantità");
        w.endRow();
        // Gruppi nell'ordine in cui compaiono la prima volta.
        std::map<std::vector<std::string>, std::size_t> index;
        std::vector<std::pair<std::vector<std::string>, long long>> groups;
        for (const InsertEntity* in : inserts) {
            std::vector<std::string> key = values(*in);
            key.insert(key.begin(), in->effectiveName());
            const auto [it, added] = index.emplace(key, groups.size());
            if (added)
                groups.push_back({std::move(key), 0});
            ++groups[it->second].second;
        }
        for (const auto& [key, count] : groups) {
            for (const std::string& v : key)
                w.text(v);
            w.integer(count);
            w.endRow();
            ++result.rows;
        }
        result.csv = w.take();
        return result;
    }

    for (const char* h : {"Blocco", "Layer", "Spazio", "X", "Y", "Rotazione", "Scala X", "Scala Y"})
        w.text(h);
    for (const std::string& t : tags)
        w.text(t);
    w.endRow();
    for (const InsertEntity* in : inserts) {
        w.text(in->effectiveName());
        w.text(layerName(doc, *in));
        w.text(spaceName(doc, in->space()));
        w.number(in->position.x);
        w.number(in->position.y);
        double degrees = std::fmod(in->rotation * 180.0 / kPiValue, 360.0);
        if (degrees < 0.0)
            degrees += 360.0;
        w.number(degrees);
        w.number(in->scaleX);
        w.number(in->scaleY);
        for (const std::string& v : values(*in))
            w.text(v);
        w.endRow();
        ++result.rows;
    }
    result.csv = w.take();
    return result;
}

} // namespace cartesio
