#include "core/Plot.h"
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

namespace cartesio {

// ---------------------------------------------------------------------- scale standard

const std::vector<StandardScale>& standardScales()
{
    // Le scale in pollici sono unità carta (pollici) : unità disegno (pollici).
    static const std::vector<StandardScale> scales = {
        {0, "Adatta al foglio", 0.0, 0.0},
        {1, "1/128\" = 1'-0\"", 1.0 / 128.0, 12.0},
        {2, "1/64\" = 1'-0\"", 1.0 / 64.0, 12.0},
        {3, "1/32\" = 1'-0\"", 1.0 / 32.0, 12.0},
        {4, "1/16\" = 1'-0\"", 1.0 / 16.0, 12.0},
        {5, "3/32\" = 1'-0\"", 3.0 / 32.0, 12.0},
        {6, "1/8\" = 1'-0\"", 1.0 / 8.0, 12.0},
        {7, "3/16\" = 1'-0\"", 3.0 / 16.0, 12.0},
        {8, "1/4\" = 1'-0\"", 1.0 / 4.0, 12.0},
        {9, "3/8\" = 1'-0\"", 3.0 / 8.0, 12.0},
        {10, "1/2\" = 1'-0\"", 1.0 / 2.0, 12.0},
        {11, "3/4\" = 1'-0\"", 3.0 / 4.0, 12.0},
        {12, "1\" = 1'-0\"", 1.0, 12.0},
        {13, "3\" = 1'-0\"", 3.0, 12.0},
        {14, "6\" = 1'-0\"", 6.0, 12.0},
        {15, "1'-0\" = 1'-0\"", 12.0, 12.0},
        {16, "1:1", 1.0, 1.0},
        {17, "1:2", 1.0, 2.0},
        {18, "1:4", 1.0, 4.0},
        {19, "1:8", 1.0, 8.0},
        {20, "1:10", 1.0, 10.0},
        {21, "1:16", 1.0, 16.0},
        {22, "1:20", 1.0, 20.0},
        {23, "1:30", 1.0, 30.0},
        {24, "1:40", 1.0, 40.0},
        {25, "1:50", 1.0, 50.0},
        {26, "1:100", 1.0, 100.0},
        {27, "2:1", 2.0, 1.0},
        {28, "4:1", 4.0, 1.0},
        {29, "8:1", 8.0, 1.0},
        {30, "10:1", 10.0, 1.0},
        {31, "100:1", 100.0, 1.0},
        {32, "1000:1", 1000.0, 1.0},
    };
    return scales;
}

const StandardScale* findStandardScale(int type)
{
    for (const auto& s : standardScales())
        if (s.type == type)
            return &s;
    return nullptr;
}

int standardScaleTypeFor(double paper, double drawing)
{
    if (paper <= 0.0 || drawing <= 0.0)
        return -1;
    const double ratio = paper / drawing;
    for (const auto& s : standardScales())
        if (s.type >= 16 && std::abs(s.paper / s.drawing - ratio) <= 1e-9 * ratio)
            return s.type;
    return -1;
}

// ---------------------------------------------------------------------- formati carta

const std::vector<MediaFormat>& plotMediaFormats()
{
    constexpr double in = 25.4;
    static const std::vector<MediaFormat> formats = {
        {"ISO A0", 1189.0, 841.0, false},   {"ISO A1", 841.0, 594.0, false},    {"ISO A2", 594.0, 420.0, false},
        {"ISO A3", 420.0, 297.0, false},    {"ISO A4", 297.0, 210.0, false},    {"ISO A5", 210.0, 148.0, false},
        {"ANSI A", 11.0 * in, 8.5 * in, true}, {"ANSI B", 17.0 * in, 11.0 * in, true},
        {"ANSI C", 22.0 * in, 17.0 * in, true}, {"ANSI D", 34.0 * in, 22.0 * in, true},
        {"ANSI E", 44.0 * in, 34.0 * in, true}, {"ARCH A", 12.0 * in, 9.0 * in, true},
        {"ARCH B", 18.0 * in, 12.0 * in, true}, {"ARCH C", 24.0 * in, 18.0 * in, true},
        {"ARCH D", 36.0 * in, 24.0 * in, true}, {"ARCH E", 48.0 * in, 36.0 * in, true},
        {"ARCH E1", 42.0 * in, 30.0 * in, true},
    };
    return formats;
}

namespace {

std::string format2(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

// Numero senza zeri inutili: 100, 12.5.
std::string formatShort(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

bool equalsNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && equalsNoCase(s.substr(0, prefix.size()), prefix);
}

} // namespace

std::string canonicalMediaName(const MediaFormat& m)
{
    const std::string_view name = m.name;
    const auto space = name.find(' ');
    const std::string family(name.substr(0, space));
    const std::string size(name.substr(space + 1));
    if (m.inches)
        return family + "_full_bleed_" + size + "_(" + format2(m.width / 25.4) + "_x_" + format2(m.height / 25.4)
               + "_Inches)";
    return family + "_full_bleed_" + size + "_(" + format2(m.width) + "_x_" + format2(m.height) + "_MM)";
}

const MediaFormat* findMediaBySize(double width, double height)
{
    const double w = std::max(width, height), h = std::min(width, height);
    for (const auto& m : plotMediaFormats())
        if (std::abs(m.width - w) <= 0.5 && std::abs(m.height - h) <= 0.5)
            return &m;
    return nullptr;
}

const MediaFormat* findMediaByName(std::string_view name)
{
    for (const auto& m : plotMediaFormats()) {
        const std::string_view full = m.name;
        const std::string_view family = full.substr(0, full.find(' '));
        const std::string_view size = full.substr(full.find(' ') + 1);
        if (equalsNoCase(name, full) || equalsNoCase(name, canonicalMediaName(m)))
            return &m;
        if (family == "ISO" && equalsNoCase(name, size))   // "A3"
            return &m;
        // Altri nomi canonici: "ISO_A3_(420.00_x_297.00_MM)", "ISO_expand_A3_(...)".
        if (startsWithNoCase(name, std::string(family) + "_")
            && name.find("_" + std::string(size) + "_(") != std::string_view::npos)
            return &m;
    }
    return nullptr;
}

std::string mediaDisplayName(double width, double height)
{
    if (const MediaFormat* m = findMediaBySize(width, height))
        return m->name;
    return formatShort(width) + " x " + formatShort(height) + " mm";
}

// ---------------------------------------------------------------------- impostazioni di stampa

PlotSettings PlotSettings::forModel()
{
    PlotSettings s;
    s.flags = kPlotPlotStyles | kPlotPrintLineweights | kPlotDrawViewportsFirst | kPlotModelType
              | kPlotUseStandardScale | kPlotCentered;
    s.plotType = PlotType::Extents;
    s.standardScaleType = 0;
    s.paperWidth = 420.0;
    s.paperHeight = 297.0;
    s.marginLeft = s.marginBottom = s.marginRight = s.marginTop = 10.0;
    s.mediaName = canonicalMediaName(*findMediaByName("A3"));
    return s;
}

PlotSettings PlotSettings::forLayout(double sheetWidth, double sheetHeight, double margin)
{
    PlotSettings s;
    s.flags = kPlotPlotStyles | kPlotPrintLineweights | kPlotDrawViewportsFirst | kPlotUseStandardScale;
    s.plotType = PlotType::Layout;
    s.standardScaleType = 16;
    s.paperWidth = sheetWidth;
    s.paperHeight = sheetHeight;
    s.marginLeft = s.marginBottom = s.marginRight = s.marginTop = margin;
    if (const MediaFormat* m = findMediaBySize(sheetWidth, sheetHeight))
        s.mediaName = canonicalMediaName(*m);
    return s;
}

SheetSize sheetSize(const PlotSettings& s)
{
    if (s.rotation % 2 != 0)
        return {s.paperHeight, s.paperWidth};
    return {s.paperWidth, s.paperHeight};
}

std::array<double, 4> sheetMargins(const PlotSettings& s)
{
    std::array<double, 4> m = {s.marginLeft, s.marginBottom, s.marginRight, s.marginTop};
    if (s.rotation % 2 != 0)   // supporto ruotato di 90°: il bordo inferiore diventa il sinistro
        m = {s.marginBottom, s.marginRight, s.marginTop, s.marginLeft};
    if (s.rotation >= 2) {     // capovolta
        std::swap(m[0], m[2]);
        std::swap(m[1], m[3]);
    }
    return m;
}

BBox printableArea(const PlotSettings& s)
{
    const SheetSize sheet = sheetSize(s);
    const auto m = sheetMargins(s);
    BBox box;
    box.min = {m[0], m[1]};
    box.max = {sheet.width - m[2], sheet.height - m[3]};
    return box;
}

bool isLandscape(const PlotSettings& s)
{
    const SheetSize sheet = sheetSize(s);
    return sheet.width >= sheet.height;
}

bool isUpsideDown(const PlotSettings& s)
{
    return s.rotation >= 2;
}

void setOrientation(PlotSettings& s, bool landscape, bool upsideDown)
{
    const bool mediaLandscape = s.paperWidth >= s.paperHeight;
    const bool rotate = landscape != mediaLandscape;
    s.rotation = (rotate ? 1 : 0) + (upsideDown ? 2 : 0);
}

void setSheet(PlotSettings& s, double sheetWidth, double sheetHeight)
{
    const bool upsideDown = isUpsideDown(s);
    s.paperWidth = sheetWidth;
    s.paperHeight = sheetHeight;
    s.rotation = upsideDown ? 2 : 0;
}

bool isFitToPaper(const PlotSettings& s)
{
    return s.flag(kPlotUseStandardScale) && s.standardScaleType == 0;
}

double fixedPlotScale(const PlotSettings& s)
{
    const double unit = s.paperUnits == 0 ? 25.4 : 1.0;
    if (s.flag(kPlotUseStandardScale)) {
        const StandardScale* scale = findStandardScale(s.standardScaleType);
        if (!scale || scale->type == 0)
            return 0.0;
        return scale->paper / scale->drawing * unit;
    }
    if (s.customPaper <= 0.0 || s.customDrawing <= 0.0)
        return 0.0;
    return s.customPaper / s.customDrawing * unit;
}

std::string plotScaleText(const PlotSettings& s)
{
    if (isFitToPaper(s))
        return "Adatta al foglio";
    if (s.flag(kPlotUseStandardScale))
        if (const StandardScale* scale = findStandardScale(s.standardScaleType))
            return scale->name;
    const double paper = s.customPaper, drawing = s.customDrawing;
    if (paper <= 0.0 || drawing <= 0.0)
        return "Adatta al foglio";
    if (s.paperUnits == 0)
        return formatShort(paper) + "\" = " + formatShort(drawing) + " unità";
    if (std::abs(paper - 1.0) < 1e-12)
        return "1:" + formatShort(drawing);
    if (std::abs(drawing - 1.0) < 1e-12)
        return formatShort(paper) + ":1";
    return formatShort(paper) + " mm = " + formatShort(drawing) + " unità";
}

PlotFrame computePlotFrame(const PlotSettings& s, const BBox& area)
{
    PlotFrame frame;
    frame.sheet = sheetSize(s);
    frame.printable = printableArea(s);
    if (!area.isValid() || frame.sheet.width <= 0.0 || frame.sheet.height <= 0.0)
        return frame;
    const double aw = area.width(), ah = area.height();
    if (aw <= 0.0 && ah <= 0.0)
        return frame;

    double scale = fixedPlotScale(s);
    if (scale <= 0.0) {   // adatta all'area stampabile
        const double pw = std::max(frame.printable.width(), 0.0), ph = std::max(frame.printable.height(), 0.0);
        const double sx = aw > 0.0 ? pw / aw : std::numeric_limits<double>::infinity();
        const double sy = ah > 0.0 ? ph / ah : std::numeric_limits<double>::infinity();
        scale = std::min(sx, sy);
        if (!(scale > 0.0) || !std::isfinite(scale))
            return frame;
    }
    const Vec2 size{aw * scale, ah * scale};
    Vec2 pos;
    if (s.flag(kPlotCentered))
        pos = frame.printable.center() - size / 2.0;
    else if (s.plotType == PlotType::Layout)
        pos = {};   // il foglio del layout coincide con il foglio stampato
    else
        pos = frame.printable.min + s.plotOrigin;

    frame.scale = scale;
    frame.a = scale;
    frame.d = scale;
    frame.e = pos.x - area.min.x * scale;
    frame.f = pos.y - area.min.y * scale;
    if (isUpsideDown(s)) {   // capovolta: ruota di 180° sul foglio
        frame.a = -frame.a;
        frame.d = -frame.d;
        frame.e = frame.sheet.width - frame.e;
        frame.f = frame.sheet.height - frame.f;
    }
    frame.placed = BBox{};
    frame.placed.expand(frame.toPaper(area.min));
    frame.placed.expand(frame.toPaper(area.max));
    frame.valid = true;
    return frame;
}

// ---------------------------------------------------------------------- stili di stampa

namespace {

constexpr int kObjectColor2 = -1006632961;   // 0xC3FFFFFF

int packRgb(std::uint32_t rgb)
{
    const std::uint32_t v = 0xC2000000u | (rgb & 0xFFFFFFu);
    return static_cast<int>(v);
}

} // namespace

bool PlotStyle::hasObjectColor() const
{
    return color == kPlotStyleObjectColor || color == kObjectColor2;
}

std::uint32_t PlotStyle::rgb() const
{
    const int v = modeColor != kPlotStyleObjectColor ? modeColor : color;
    return static_cast<std::uint32_t>(v) & 0xFFFFFFu;
}

void PlotStyle::setRgb(std::uint32_t value)
{
    color = modeColor = packRgb(value);
}

void PlotStyle::setObjectColor()
{
    color = modeColor = kPlotStyleObjectColor;
}

const std::vector<double>& defaultPlotLineweights()
{
    static const std::vector<double> weights = {0.00, 0.05, 0.09, 0.10, 0.13, 0.15, 0.18, 0.20, 0.25,
                                                0.30, 0.35, 0.40, 0.45, 0.50, 0.53, 0.60, 0.65, 0.70,
                                                0.80, 0.90, 1.00, 1.06, 1.20, 1.40, 1.58, 2.00, 2.11};
    return weights;
}

PlotStyleTable::PlotStyleTable()
{
    for (std::size_t i = 0; i < styles.size(); ++i) {
        styles[i].name = "Color_" + std::to_string(i + 1);
        styles[i].localizedName = styles[i].name;
    }
}

const PlotStyle& PlotStyleTable::forAci(int aci) const
{
    if (aci < 1 || aci > 255)
        aci = 7;
    return styles[static_cast<std::size_t>(aci - 1)];
}

PlotStyle& PlotStyleTable::forAci(int aci)
{
    if (aci < 1 || aci > 255)
        aci = 7;
    return styles[static_cast<std::size_t>(aci - 1)];
}

double PlotStyleTable::lineweightMm(const PlotStyle& s) const
{
    if (s.lineweight <= 0 || s.lineweight >= static_cast<int>(lineweights.size()))
        return 0.0;
    return lineweights[static_cast<std::size_t>(s.lineweight)];
}

int PlotStyleTable::lineweightIndex(double mm) const
{
    int best = 0;
    double bestDiff = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < lineweights.size(); ++i) {
        const double diff = std::abs(lineweights[i] - mm);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = static_cast<int>(i);
        }
    }
    return best;
}

namespace {

std::string formatDouble(double v)
{
    // Come la rappresentazione più breve di Python: 0.5, 1.0.
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.15g", v);
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos)
        s += ".0";
    return s;
}

const char* boolText(bool b)
{
    return b ? "TRUE" : "FALSE";
}

// Albero del testo di un file .ctb: "nome=valore" e "nome{ ... }".
struct CtbNode {
    std::string value;
    std::vector<std::pair<std::string, std::unique_ptr<CtbNode>>> children;
    bool list = false;

    const CtbNode* child(std::string_view name) const
    {
        for (const auto& [n, c] : children)
            if (n == name)
                return c.get();
        return nullptr;
    }
};

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\0'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\0'))
        s.remove_suffix(1);
    return s;
}

std::string sanitizedValue(std::string_view value)
{
    value = trim(value);
    if (!value.empty() && value.front() == '"')
        return std::string(value.substr(1));
    // Valori come "0.0076200000000 (+7.Z+"8V?S_LC )": si tiene il numero.
    if (!value.empty() && value.back() == ')')
        value = value.substr(0, value.find(' '));
    return std::string(value);
}

bool parseList(const std::vector<std::string_view>& lines, std::size_t& i, CtbNode& node, bool topLevel)
{
    while (i < lines.size()) {
        const std::string_view line = lines[i];
        if (line.empty()) {
            ++i;
            continue;
        }
        if (line == "}") {
            ++i;
            return !topLevel;
        }
        if (line.back() == '{') {
            auto child = std::make_unique<CtbNode>();
            child->list = true;
            const std::string name(trim(line.substr(0, line.size() - 1)));
            ++i;
            if (!parseList(lines, i, *child, false))
                return false;
            node.children.emplace_back(name, std::move(child));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos)
            return false;
        auto child = std::make_unique<CtbNode>();
        child->value = sanitizedValue(line.substr(eq + 1));
        node.children.emplace_back(std::string(trim(line.substr(0, eq))), std::move(child));
        ++i;
    }
    return topLevel;
}

std::optional<double> number(const CtbNode* node)
{
    if (!node || node->list)
        return std::nullopt;
    double v = 0.0;
    const char* first = node->value.data();
    const char* last = first + node->value.size();
    const auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc{} || ptr == first)
        return std::nullopt;
    return v;
}

int integer(const CtbNode* node, int fallback)
{
    const auto v = number(node);
    if (!v || !std::isfinite(*v))
        return fallback;
    // I colori sono interi a 32 bit con segno, a volte scritti senza segno.
    if (*v > 2147483647.0 && *v <= 4294967295.0)
        return static_cast<int>(static_cast<std::uint32_t>(*v));
    return static_cast<int>(*v);
}

bool boolean(const CtbNode* node, bool fallback)
{
    if (!node || node->list)
        return fallback;
    if (equalsNoCase(node->value, "TRUE"))
        return true;
    if (equalsNoCase(node->value, "FALSE"))
        return false;
    return fallback;
}

std::string text(const CtbNode* node, const std::string& fallback = {})
{
    return node && !node->list ? node->value : fallback;
}

} // namespace

std::string PlotStyleTable::toText() const
{
    std::string out;
    char buf[64];
    out += "description=\"" + description + "\n";
    out += "aci_table_available=TRUE\n";
    std::snprintf(buf, sizeof buf, "scale_factor=%.1f\n", scaleFactor);
    out += buf;
    out += std::string("apply_factor=") + boolText(applyFactor) + "\n";
    out += "custom_lineweight_display_units=" + std::to_string(customLineweightDisplayUnits) + "\n";
    out += "aci_table{\n";
    for (std::size_t i = 0; i < styles.size(); ++i)
        out += " " + std::to_string(i) + "=\"Color_" + std::to_string(i + 1) + "\n";
    out += "}\n";
    out += "plot_style{\n";
    for (std::size_t i = 0; i < styles.size(); ++i) {
        const PlotStyle& s = styles[i];
        out += " " + std::to_string(i) + "{\n";
        out += "  name=\"" + s.name + "\n";
        out += "  localized_name=\"" + s.localizedName + "\n";
        out += "  description=\"" + s.description + "\n";
        out += "  color=" + std::to_string(s.color) + "\n";
        if (s.color != kPlotStyleObjectColor)
            out += "  mode_color=" + std::to_string(s.modeColor) + "\n";
        out += "  color_policy=" + std::to_string(s.colorPolicy) + "\n";
        out += "  physical_pen_number=" + std::to_string(s.physicalPen) + "\n";
        out += "  virtual_pen_number=" + std::to_string(s.virtualPen) + "\n";
        out += "  screen=" + std::to_string(s.screen) + "\n";
        out += "  linepattern_size=" + formatDouble(s.linepatternSize) + "\n";
        out += "  linetype=" + std::to_string(s.linetype) + "\n";
        out += std::string("  adaptive_linetype=") + boolText(s.adaptiveLinetype) + "\n";
        out += "  lineweight=" + std::to_string(s.lineweight) + "\n";
        out += "  fill_style=" + std::to_string(s.fillStyle) + "\n";
        out += "  end_style=" + std::to_string(s.endStyle) + "\n";
        out += "  join_style=" + std::to_string(s.joinStyle) + "\n";
        out += " }\n";
    }
    out += "}\n";
    out += "custom_lineweight_table{\n";
    for (std::size_t i = 0; i < lineweights.size(); ++i) {
        std::snprintf(buf, sizeof buf, " %zu=%.2f\n", i, lineweights[i]);
        out += buf;
    }
    out += "}\n";
    return out;
}

std::optional<PlotStyleTable> PlotStyleTable::fromText(std::string_view input, std::string* error)
{
    const auto fail = [error](const char* message) -> std::optional<PlotStyleTable> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= input.size()) {
        const auto end = input.find('\n', start);
        const auto line = input.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        lines.push_back(trim(line));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    CtbNode root;
    root.list = true;
    std::size_t i = 0;
    if (!parseList(lines, i, root, true))
        return fail("Testo della tabella stili non valido (parentesi graffe non bilanciate).");
    const CtbNode* styles = root.child("plot_style");
    if (!styles || !styles->list)
        return fail("La tabella stili non contiene \"plot_style\".");

    PlotStyleTable table;
    table.description = text(root.child("description"));
    table.scaleFactor = number(root.child("scale_factor")).value_or(1.0);
    table.applyFactor = boolean(root.child("apply_factor"), false);
    table.customLineweightDisplayUnits = integer(root.child("custom_lineweight_display_units"), 0);
    if (const CtbNode* weights = root.child("custom_lineweight_table"); weights && weights->list) {
        std::map<int, double> values;
        for (const auto& [key, node] : weights->children) {
            int index = -1;
            std::from_chars(key.data(), key.data() + key.size(), index);
            if (const auto v = number(node.get()); v && index >= 0 && index < 1000)
                values[index] = *v;
        }
        if (!values.empty()) {
            table.lineweights.assign(static_cast<std::size_t>(values.rbegin()->first + 1), 0.0);
            for (const auto& [index, v] : values)
                table.lineweights[static_cast<std::size_t>(index)] = v;
        }
    }
    for (const auto& [key, node] : styles->children) {
        int index = -1;
        std::from_chars(key.data(), key.data() + key.size(), index);
        if (index < 0 || index >= static_cast<int>(table.styles.size()) || !node->list)
            continue;
        PlotStyle& s = table.styles[static_cast<std::size_t>(index)];
        s.name = text(node->child("name"), s.name);
        s.localizedName = text(node->child("localized_name"), s.name);
        s.description = text(node->child("description"));
        s.color = integer(node->child("color"), kPlotStyleObjectColor);
        if (s.color == kObjectColor2)
            s.color = kPlotStyleObjectColor;
        s.modeColor = integer(node->child("mode_color"), s.color);
        s.colorPolicy = integer(node->child("color_policy"), 1);
        s.physicalPen = integer(node->child("physical_pen_number"), 0);
        s.virtualPen = integer(node->child("virtual_pen_number"), 0);
        s.screen = std::clamp(integer(node->child("screen"), 100), 0, 100);
        s.linepatternSize = number(node->child("linepattern_size")).value_or(0.5);
        s.linetype = integer(node->child("linetype"), kPlotStyleObjectLinetype);
        s.adaptiveLinetype = boolean(node->child("adaptive_linetype"), true);
        s.lineweight = integer(node->child("lineweight"), kPlotStyleObjectLineweight);
        s.fillStyle = integer(node->child("fill_style"), kPlotStyleObjectFill);
        s.endStyle = integer(node->child("end_style"), kPlotStyleObjectEnd);
        s.joinStyle = integer(node->child("join_style"), kPlotStyleObjectJoin);
    }
    return table;
}

const std::vector<std::string>& builtinPlotStyleTableNames()
{
    static const std::vector<std::string> names = {"Colori.ctb",     "Nero.ctb",       "Grigi.ctb",      "Retino 25%.ctb",
                                                   "Retino 50%.ctb", "Retino 75%.ctb", "Retino 100%.ctb"};
    return names;
}

std::optional<PlotStyleTable> builtinPlotStyleTable(std::string_view name)
{
    PlotStyleTable t;
    // Solo i nomi di CartesioCAD. Una tabella chiesta da un disegno con un altro nome si cerca come file .ctb nella
    // cartella degli stili; se non c'è, si stampa con i colori degli oggetti.
    if (equalsNoCase(name, "Colori.ctb")) {
        t.description = "Stili di stampa predefiniti: colori, tipi di linea e spessori degli oggetti";
    } else if (equalsNoCase(name, "Nero.ctb")) {
        t.description = "Tutti i colori in nero";
        for (auto& s : t.styles)
            s.setRgb(0x000000);
    } else if (equalsNoCase(name, "Grigi.ctb")) {
        t.description = "Colori convertiti in scala di grigi";
        for (auto& s : t.styles)
            s.setGrayscale(true);
    } else {
        int screen = 0;
        for (int v : {25, 50, 75, 100})
            if (equalsNoCase(name, "Retino " + std::to_string(v) + "%.ctb"))
                screen = v;
        if (screen == 0)
            return std::nullopt;
        t.description = "Intensità dei colori al " + std::to_string(screen) + "%";
        for (auto& s : t.styles)
            s.screen = screen;
    }
    return t;
}

// ---------------------------------------------------------------------- file .ctb

namespace {

constexpr std::string_view kCtbHeader = "PIAFILEVERSION_2.0,CTBVER1,compress\r\npmzlibcodec";

void putU32(std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

std::uint32_t getU32(std::string_view s, std::size_t at)
{
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(i)]);
    return v;
}

} // namespace

std::uint32_t adler32(std::string_view data)
{
    std::uint32_t a = 1, b = 0;
    for (const char ch : data) {
        a = (a + static_cast<unsigned char>(ch)) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::string ctbFileBytes(std::string_view zlibData, std::size_t textLength)
{
    std::string out(kCtbHeader);
    putU32(out, adler32(zlibData));
    putU32(out, static_cast<std::uint32_t>(textLength));
    putU32(out, static_cast<std::uint32_t>(zlibData.size()));
    out += zlibData;
    return out;
}

std::optional<CtbPayload> ctbPayload(std::string_view bytes, std::string* error)
{
    if (bytes.size() < 60 || bytes.substr(0, 19) != "PIAFILEVERSION_2.0,") {
        if (error)
            *error = "Il file non è una tabella stili di stampa (.ctb).";
        return std::nullopt;
    }
    if (bytes.substr(19, 8) != "CTBVER1,") {
        if (error)
            *error = "Sono supportate solo le tabelle dipendenti dal colore (.ctb), non quelle con nome (.stb).";
        return std::nullopt;
    }
    CtbPayload p;
    p.textLength = getU32(bytes, 52);
    const std::size_t compressed = getU32(bytes, 56);
    p.zlibData = bytes.substr(60, std::min(compressed, bytes.size() - 60));
    return p;
}

std::optional<std::string> ctbText(std::string_view bytes, std::string* error)
{
    const auto payload = ctbPayload(bytes, error);
    if (!payload)
        return std::nullopt;
    // Si decomprime a pezzi con un tetto: una tabella vera è sotto i 100 kB, mentre un file costruito apposta
    // (pochi kB che si espandono in gigabyte) non deve poter riempire la memoria.
    constexpr std::size_t kMaxText = 4u << 20;
    const auto damaged = [error]() -> std::optional<std::string> {
        if (error)
            *error = "Il file .ctb è danneggiato (dati compressi non validi).";
        return std::nullopt;
    };
    z_stream z{};
    if (inflateInit(&z) != Z_OK)
        return damaged();
    std::string text;
    char buffer[16384];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(payload->zlibData.data()));
    z.avail_in = static_cast<uInt>(payload->zlibData.size());
    int status = Z_OK;
    while (status == Z_OK) {
        z.next_out = reinterpret_cast<Bytef*>(buffer);
        z.avail_out = sizeof buffer;
        status = inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
            break;
        text.append(buffer, sizeof buffer - z.avail_out);
        if (text.size() > kMaxText) {
            status = Z_MEM_ERROR;
            break;
        }
        if (status == Z_OK && z.avail_in == 0 && z.avail_out != 0)
            break;   // dati finiti prima della fine del flusso
    }
    inflateEnd(&z);
    if (status != Z_STREAM_END)
        return damaged();
    return text;
}

// ---------------------------------------------------------------------- timbro

std::string plotStampText(const PlotStampSettings& settings, const PlotStampValues& values)
{
    std::vector<std::string> parts;
    const auto add = [&parts](bool on, const std::string& v) {
        if (on && !v.empty())
            parts.push_back(v);
    };
    add(settings.drawingName, values.drawingName);
    add(settings.layoutName, values.layoutName);
    add(settings.dateTime, values.dateTime);
    add(settings.loginName, values.loginName);
    add(settings.deviceName, values.deviceName);
    add(settings.paperSize, values.paperSize);
    add(settings.plotScale, values.plotScale);
    add(true, settings.user1);
    add(true, settings.user2);
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty())
            out += ", ";
        out += p;
    }
    return out;
}

} // namespace cartesio
