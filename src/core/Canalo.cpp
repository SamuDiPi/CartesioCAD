#include "core/Canalo.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <regex>

namespace cartesio::canalo {

namespace {
#include "core/CanaloData.inc"

constexpr double kPi = 3.14159265358979323846;

// Math.round di JavaScript: la metà va verso +infinito.
double jsRound(double v) { return std::floor(v + 0.5); }

bool test(const char* pattern, const std::string& s, bool icase = true)
{
    const std::regex re(pattern, icase ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript);
    return std::regex_search(s, re);
}

std::string upper(std::string s)
{
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string replaceAll(std::string s, const char* pattern, const std::string& with, bool icase = true)
{
    const std::regex re(pattern, icase ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript);
    return std::regex_replace(s, re, with);
}

double parseNumber(std::string s)
{
    std::replace(s.begin(), s.end(), ',', '.');
    return std::strtod(s.c_str(), nullptr);
}
} // namespace

// ------------------------------------------------------------------ numeri come JavaScript

double fixed(double v, int decimals)
{
    if (!std::isfinite(v))
        return v;
    const bool negative = v < 0.0;
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*f", decimals + 40, std::abs(v));
    std::string s = buf;
    const std::size_t dot = s.find('.');
    std::string digits = s.substr(0, dot) + s.substr(dot + 1, static_cast<std::size_t>(decimals));
    // toFixed: il più vicino; a pari distanza il maggiore (in valore assoluto, il segno si aggiunge dopo).
    if (s[dot + 1 + static_cast<std::size_t>(decimals)] >= '5') {
        int i = static_cast<int>(digits.size()) - 1;
        while (i >= 0 && digits[static_cast<std::size_t>(i)] == '9')
            digits[static_cast<std::size_t>(i--)] = '0';
        if (i < 0)
            digits.insert(digits.begin(), '1');
        else
            ++digits[static_cast<std::size_t>(i)];
    }
    if (decimals > 0)
        digits.insert(digits.size() - static_cast<std::size_t>(decimals), ".");
    const double r = std::strtod(digits.c_str(), nullptr);
    return negative ? -r : r;
}

std::string jsNumber(double v)
{
    if (v == 0.0)
        return "0";
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, res.ptr);
}

// ------------------------------------------------------------------ dati

const std::vector<CableType>& catalog() { return kCatalog; }

const CableType* catalogItem(const std::string& id)
{
    for (const CableType& c : kCatalog)
        if (c.id == id)
            return &c;
    return nullptr;
}

const std::vector<std::pair<std::string, std::vector<Series>>>& brands() { return kBrands; }

const std::vector<Series>& seriesOf(const std::string& brand)
{
    for (const auto& [name, list] : kBrands)
        if (name == brand)
            return list;
    return kBrands.front().second;   // MARCA_A
}

const std::vector<Support>& supports() { return kSupports; }

const Support* support(const std::string& key)
{
    for (const Support& s : kSupports)
        if (s.key == key)
            return &s;
    return nullptr;
}

Cable cableFrom(const CableType& t, std::string code)
{
    Cable c;
    c.code = code.empty() ? t.code : std::move(code);
    c.desc = t.desc;
    c.type = t.type;
    c.form = t.form;
    c.sys = t.sys;
    c.d = t.d;
    c.w = t.w;
    c.sec = t.sec;
    c.cores = t.cores;
    c.iz0 = t.iz0;
    c.bendK = t.bendK;
    c.x = 18.0;
    c.y = t.d / 2.0;
    return c;
}

bool isUnipolar(const std::string& form, const std::string& type, int cores)
{
    return test(R"(\b1X\d+|\(\s*1X\d+)", upper(form)) || test(R"(\bFG16M16\b|\bFS17\b|\bFG17\b|\bH07Z1-K\b)", upper(type))
           || cores == 1;
}

double sectionFromForm(const std::string& form, double fallback)
{
    if (test("AWG", form))
        return fallback;
    static const std::regex re(R"([xXGg](\d+(?:[.,]\d+)?))");
    std::smatch m;
    if (!std::regex_search(form, m, re))
        return fallback;
    const double val = parseNumber(m[1].str());
    static const double standard[] = {0.5, 1, 1.5, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120, 150, 185, 240, 300, 400, 500, 630};
    double closest = standard[0];
    for (double s : standard)
        if (std::abs(s - val) < std::abs(closest - val))
            closest = s;
    return closest;
}

Inferred inferByDiameter(double d, const std::string& code, const std::string& layer)
{
    const std::string combined = upper(code + " " + layer);
    auto make = [](const char* type, const char* form, double sec, int cores, double iz0, double bendK, const char* sys) {
        return Inferred{type, form, sec, cores, iz0, bendK, sys};
    };
    if (test(R"((?:IRAI|FUMI|LOOP|EVAC|SPK|DIFFUS|CAT\s*[567]|DATI|LAN|UTP|FTP|BUS|KNX|FONIA|FIBRA|ELV|SEGNAL|SONOR))", combined)) {
        if (test("IRAI|FUMI|LOOP", combined))
            return make("FTG18(O)M16", "1(2x1,5)", 1.5, 2, 15, 10, "SPECIAL_ELV");
        if (test("EVAC|SPK|DIFFUS|SONOR", combined))
            return make("FTG18(O)M16", "1(2x2,5)", 2.5, 2, 20, 10, "SPECIAL_ELV");
        if (test("BUS|KNX", combined))
            return make("KNX Bus TP1", "2x2x0,8", 0.5, 2, 6, 10, "SPECIAL_ELV");
        if (test("FIBRA", combined))
            return make("Fibra Ottica", "8 FO OM4", 0.2, 2, 1, 12, "SPECIAL_ELV");
        return make("F/UTP Cat.6A", "4x2x23AWG", 0.5, 2, 5, 10, "SPECIAL_ELV");
    }
    if (test("(?:FTG10|FTG18|RESIST|INCEND|TAGLIAFUOCO|REI)", combined)) {
        if (d <= 14.5)
            return make("FTG18OM16", "1(3G2,5)", 2.5, 2, 27, 10, "BT_FIRE");
        if (d <= 18.0)
            return make("FTG18OM16", "1(4G4)", 4.0, 3, 36, 10, "BT_FIRE");
        return make("FTG18OM16", "1(4G10)", 10.0, 3, 60, 10, "BT_FIRE");
    }
    struct Row { double maxD; const char* type; const char* form; double sec; int cores; double iz0, bendK; };
    static const Row rows[] = {
        {6.8, "FS17", "1(1x2,5)", 2.5, 1, 24, 8},       {7.8, "FS17", "1(1x4)", 4.0, 1, 32, 8},
        {8.8, "FS17", "1(1x6)", 6.0, 1, 42, 8},         {9.8, "FG16M16", "1(1x10)", 10.0, 1, 75, 10},
        {10.8, "FG16M16", "1(1x16)", 16.0, 1, 100, 10}, {11.6, "FG16OM16", "1(3G1,5)", 1.5, 2, 22, 8},
        {12.8, "FG16M16", "1(1x25)", 25.0, 1, 135, 10}, {13.5, "FG16OM16", "1(4G1,5)", 1.5, 3, 19, 8},
        {14.4, "FG16OM16", "1(3G2,5)", 2.5, 2, 30, 8},  {15.2, "FG16OM16", "1(5G2,5)", 2.5, 3, 26, 8},
        {16.0, "FG16OM16", "1(3G4)", 4.0, 2, 40, 8},    {17.2, "FG16OM16", "1(5G4)", 4.0, 3, 35, 8},
        {18.2, "FG16OM16", "1(5G6)", 6.0, 3, 45, 8},    {20.5, "FG16M16", "1(1x95)", 95.0, 1, 328, 12},
        {21.8, "FG16OM16", "1(5G10)", 10.0, 3, 63, 10}, {23.5, "FG16M16", "1(1x120)", 120.0, 1, 383, 12},
        {25.0, "FG16OM16", "1(5G16)", 16.0, 3, 85, 10}, {27.5, "FG16M16", "1(1x185)", 185.0, 1, 510, 12},
        {31.0, "FG16M16", "1(1x240)", 240.0, 1, 607, 12}, {35.0, "FG16M16", "1(1x300)", 300.0, 1, 703, 12},
    };
    for (const Row& r : rows)
        if (d <= r.maxD)
            return make(r.type, r.form, r.sec, r.cores, r.iz0, r.bendK, "BT_POWER");
    return make("FG16M16", "1(1x400)", 400.0, 1, 823, 12, "BT_POWER");
}

// ------------------------------------------------------------------ staffaggio

double allowedLinearLoadKgM(const Tray& tray, const std::string& supportKey, double spanM)
{
    if (supportKey.empty() || supportKey == "none")
        return 0.0;
    const Support* spec = support(supportKey);
    if (!spec)
        spec = support("wall_med");
    const double baseKgMAt15 = (spec->swl[1] != 0.0 ? spec->swl[1] : 75.0) / 1.5;
    const double hFactor = std::pow(tray.height / 75.0, 0.35) * std::pow(tray.thk / 1.5, 0.5);
    return fixed(baseKgMAt15 * std::pow(1.5 / spanM, 1.55) * hFactor, 1);
}

double elasticDeflectionMm(const Tray& tray, double qTotKgM, const std::string& supportKey, double spanM)
{
    if (supportKey.empty() || supportKey == "none")
        return 0.0;
    const double qAmm = allowedLinearLoadKgM(tray, supportKey, spanM);
    const double fLim = std::min(10.0, (spanM * 1000.0) / 200.0);
    return fixed(std::min(25.0, fLim * (qTotKgM / std::max(1.0, qAmm))), 2);
}

// ------------------------------------------------------------------ disposizione dei cavi

std::vector<double> septumX(Tray& tray)
{
    const int n = tray.septums;
    if (n <= 0) {
        tray.septumPositions.clear();
        return {};
    }
    const double W = tray.width;
    auto& pos = tray.septumPositions;
    if (static_cast<int>(pos.size()) != n) {
        pos.clear();
        if (n == 1)
            pos = {jsRound(W * 0.6)};
        else if (n == 2)
            pos = {jsRound(W * 0.34), jsRound(W * 0.68)};
        else
            for (int i = 1; i <= n; ++i)
                pos.push_back(jsRound((W * i) / (n + 1)));
    }
    for (std::size_t i = 0; i < pos.size(); ++i) {
        const double minX = i == 0 ? 15.0 : pos[i - 1] + 15.0;
        const double maxX = i == pos.size() - 1 ? W - 15.0 : pos[i + 1] - 15.0;
        if (pos[i] < minX)
            pos[i] = minX;
        if (pos[i] > maxX)
            pos[i] = maxX;
        pos[i] = jsRound(pos[i]);
    }
    return pos;
}

namespace {

bool isSpotFree(Tray& tray, std::size_t self, double x, double y)
{
    const double W = tray.width, H = tray.height, r = tray.cables[self].d / 2.0;
    if (x < r + 0.3 || x > W - r - 0.3)
        return false;
    if (y < r - 0.1 || y > H - r - 0.3)
        return false;
    for (double sx : septumX(tray))
        if (std::abs(x - sx) < r + tray.thk / 2.0 + 0.3)
            return false;
    for (std::size_t i = 0; i < tray.cables.size(); ++i) {
        const Cable& other = tray.cables[i];
        if (i == self || other.x < 0.0)
            continue;
        if (std::hypot(x - other.x, y - other.y) < r + other.d / 2.0 + 0.2)
            return false;
    }
    return true;
}

Vec2 findFreePositionNear(Tray& tray, std::size_t self, double targetX, double targetY)
{
    const Cable& cable = tray.cables[self];
    const double W = tray.width, H = tray.height, r = cable.d / 2.0;
    const double defX = std::isfinite(targetX) ? targetX : (std::isfinite(cable.x) && cable.x >= 0.0 ? cable.x : W / 2.0);
    const double defY = std::isfinite(targetY) ? targetY : (std::isfinite(cable.y) && cable.y >= 0.0 ? cable.y : r + 1.0);
    const double cx = std::max(r + 0.4, std::min(W - r - 0.4, defX));
    const double cy = std::max(r, std::min(H - r - 0.4, defY));
    if (isSpotFree(tray, self, cx, cy))
        return {fixed(cx, 1), fixed(cy, 1)};
    for (double rad = 0.8; rad <= std::max(W, H); rad += 0.8)
        for (double a = 0.0; a < kPi * 2.0; a += kPi / 10.0) {
            const double tx = cx + std::cos(a) * rad;
            const double ty = cy + std::sin(a) * rad;
            if (isSpotFree(tray, self, tx, ty))
                return {fixed(tx, 1), fixed(ty, 1)};
        }
    return {fixed(cx, 1), fixed(cy, 1)};
}

void packByGravity(Tray& tray, const std::vector<std::size_t>& list, double xMin, double xMax)
{
    struct Unit {
        bool trefoil = false;
        double d = 0.0, baseD = 0.0;
        std::vector<std::size_t> members;
    };
    std::vector<std::pair<std::string, std::vector<std::size_t>>> buckets;   // in ordine di comparsa
    std::vector<std::size_t> singles;
    for (std::size_t i : list) {
        const Cable& c = tray.cables[i];
        if (c.trefoil && !c.trefoilGroup.empty()) {
            auto it = std::find_if(buckets.begin(), buckets.end(), [&](const auto& b) { return b.first == c.trefoilGroup; });
            if (it == buckets.end())
                buckets.push_back({c.trefoilGroup, {i}});
            else
                it->second.push_back(i);
        } else {
            singles.push_back(i);
        }
    }
    std::vector<Unit> units;
    for (const auto& [id, group] : buckets) {
        if (group.size() == 3) {
            const double d0 = tray.cables[group[0]].d;
            units.push_back({true, d0 * 2.02, d0, group});
        } else {
            for (std::size_t i : group)
                units.push_back({false, tray.cables[i].d, 0.0, {i}});
        }
    }
    for (std::size_t i : singles)
        units.push_back({false, tray.cables[i].d, 0.0, {i}});
    std::stable_sort(units.begin(), units.end(), [](const Unit& a, const Unit& b) { return b.d - a.d < 0.0; });

    std::vector<std::size_t> placed;
    for (const Unit& u : units) {
        if (u.trefoil) {
            const double r = u.baseD / 2.0;
            const double spanX = 2.0 * r;
            const double startX = xMin + spanX + 0.6;
            const double endX = std::max(startX, xMax - spanX - 0.6);
            double bestX = startX, bestY = r, bestScore = 1e9;
            const double tcs[3][3] = {{-r, 0.0, r}, {+r, 0.0, r}, {0.0, r * 1.73205, r}};
            for (double tx = startX; tx <= endX; tx += 0.6) {
                double ty = r;
                for (std::size_t pi : placed) {
                    const Cable& p = tray.cables[pi];
                    for (const auto& tc : tcs) {
                        const double distX = std::abs((tx + tc[0]) - p.x);
                        const double minD = tc[2] + p.d / 2.0 + 0.35;
                        if (distX < minD) {
                            const double reqBaseY = p.y + std::sqrt(minD * minD - distX * distX) - tc[1];
                            if (reqBaseY > ty)
                                ty = reqBaseY;
                        }
                    }
                }
                const double score = ty * 100.0 + tx;
                if (score < bestScore) {
                    bestScore = score;
                    bestX = tx;
                    bestY = ty;
                }
            }
            Cable& c0 = tray.cables[u.members[0]];
            Cable& c1 = tray.cables[u.members[1]];
            Cable& c2 = tray.cables[u.members[2]];
            c0.x = fixed(bestX - r, 1);
            c0.y = fixed(bestY, 1);
            c1.x = fixed(bestX + r, 1);
            c1.y = fixed(bestY, 1);
            c2.x = fixed(bestX, 1);
            c2.y = fixed(bestY + r * 1.73205, 1);
            placed.insert(placed.end(), u.members.begin(), u.members.end());
        } else {
            Cable& c = tray.cables[u.members[0]];
            const double r = c.d / 2.0;
            const double startX = xMin + r + 0.5;
            const double endX = std::max(startX, xMax - r - 0.5);
            double bestX = startX, bestY = r, bestScore = 1e9;
            for (double tx = startX; tx <= endX; tx += 0.6) {
                double ty = r;
                for (std::size_t pi : placed) {
                    const Cable& p = tray.cables[pi];
                    const double dx = std::abs(tx - p.x);
                    const double minD = r + p.d / 2.0 + 0.4;
                    if (dx < minD)
                        ty = std::max(ty, p.y + std::sqrt(minD * minD - dx * dx));
                }
                const double score = ty * 100.0 + tx;
                if (score < bestScore) {
                    bestScore = score;
                    bestX = tx;
                    bestY = ty;
                }
            }
            c.x = fixed(bestX, 1);
            c.y = fixed(bestY, 1);
            placed.push_back(u.members[0]);
        }
    }
}

template <typename Pred>
std::vector<std::size_t> cablesWhere(const Tray& tray, Pred pred)
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < tray.cables.size(); ++i)
        if (pred(tray.cables[i]))
            out.push_back(i);
    return out;
}

std::vector<std::size_t> allCables(const Tray& tray)
{
    return cablesWhere(tray, [](const Cable&) { return true; });
}

} // namespace

// ------------------------------------------------------------------ calcolo

Metrics recalculate(Tray& tray)
{
    const double W = tray.width, H = tray.height;
    if (tray.packing == "input") {
        for (Cable& c : tray.cables) {
            c.x = -999.0;
            c.y = -999.0;
        }
        for (std::size_t i = 0; i < tray.cables.size(); ++i) {
            const Vec2 p = findFreePositionNear(tray, i, tray.cables[i].origX, tray.cables[i].origY);
            tray.cables[i].x = p.x;
            tray.cables[i].y = p.y;
        }
    } else {
        const std::vector<double> seps = septumX(tray);
        const bool hasSpecialElv = std::any_of(tray.cables.begin(), tray.cables.end(), [](const Cable& c) { return c.sys == "SPECIAL_ELV"; });
        const double tHalf = (tray.thk != 0.0 ? tray.thk : 1.5) / 2.0;
        auto sys = [&](const char* s) { return cablesWhere(tray, [s](const Cable& c) { return c.sys == s; }); };
        if (seps.empty()) {
            packByGravity(tray, allCables(tray), 0.0, W);
        } else if (hasSpecialElv) {
            if (seps.size() == 1) {
                packByGravity(tray, cablesWhere(tray, [](const Cable& c) { return c.sys != "SPECIAL_ELV"; }), 0.0, seps[0] - tHalf);
                packByGravity(tray, sys("SPECIAL_ELV"), seps[0] + tHalf, W);
            } else {
                packByGravity(tray, sys("BT_POWER"), 0.0, seps[0] - tHalf);
                packByGravity(tray, sys("BT_FIRE"), seps[0] + tHalf, seps[1] - tHalf);
                packByGravity(tray, sys("SPECIAL_ELV"), seps[1] + tHalf, W);
            }
        } else if (seps.size() == 1) {
            // Setti con soli cavi BT: ogni cavo resta nello scomparto in cui si trova.
            const auto left = cablesWhere(tray, [&](const Cable& c) { return c.x >= 0.0 && c.x < seps[0]; });
            const auto right = cablesWhere(tray, [&](const Cable& c) { return c.x >= seps[0]; });
            if (left.empty() && right.empty()) {
                packByGravity(tray, allCables(tray), 0.0, seps[0] - tHalf);
            } else {
                packByGravity(tray, left, 0.0, seps[0] - tHalf);
                packByGravity(tray, right, seps[0] + tHalf, W);
            }
        } else {
            const auto c1 = cablesWhere(tray, [&](const Cable& c) { return c.x >= 0.0 && c.x < seps[0]; });
            const auto c2 = cablesWhere(tray, [&](const Cable& c) { return c.x >= seps[0] && c.x < seps[1]; });
            const auto c3 = cablesWhere(tray, [&](const Cable& c) { return c.x >= seps[1]; });
            if (c1.empty() && c2.empty() && c3.empty()) {
                packByGravity(tray, allCables(tray), 0.0, seps[0] - tHalf);
            } else {
                packByGravity(tray, c1, 0.0, seps[0] - tHalf);
                packByGravity(tray, c2, seps[0] + tHalf, seps[1] - tHalf);
                packByGravity(tray, c3, seps[1] + tHalf, W);
            }
        }
    }

    Metrics m;
    const double trayArea = W * H;
    double usedArea = 0.0, cablesKgM = 0.0, cuArea = 0.0, maxBendR = 0.0, jouleWm = 0.0, maxDv = 0.0;
    std::string critCode = "-";

    // Strati verticali (CEI UNEL 35024/1).
    std::vector<double> yLayers;
    for (const Cable& c : tray.cables)
        if (std::none_of(yLayers.begin(), yLayers.end(), [&](double yl) { return std::abs(yl - c.y) < c.d * 0.72; }))
            yLayers.push_back(c.y);
    const int nLayers = std::max(1, static_cast<int>(yLayers.size()));
    const std::size_t nCirc = tray.cables.size();
    const double kFascio = nCirc <= 1 ? 1.0 : nCirc <= 2 ? 0.85 : nCirc <= 3 ? 0.79 : nCirc <= 6 ? 0.75 : nCirc <= 9 ? 0.72 : 0.68;
    const double kStrati = nLayers == 1 ? 1.0 : nLayers == 2 ? 0.80 : 0.73;
    const double kPosa = (tray.trayType == "solid" && tray.cover != "none")          ? 0.85
                         : (tray.cover == "snap" || tray.cover == "clamped")          ? 0.90
                         : (tray.trayType == "mesh" || tray.trayType == "ladder")     ? 1.00
                                                                                      : 0.96;
    const double kr = fixed(kFascio * kStrati * kPosa, 2);

    std::string deratingFailCode;
    for (const Cable& c : tray.cables) {
        const double sec = c.sec != 0.0 ? c.sec : 2.5;
        const int cores = c.cores != 0 ? c.cores : 2;
        usedArea += kPi * std::pow(c.d / 2.0, 2);
        cablesKgM += c.w;
        cuArea += sec * cores;
        const double rMin = jsRound(c.d * (c.bendK != 0.0 ? c.bendK : 10.0));
        if (rMin > maxBendR) {
            maxBendR = rMin;
            critCode = c.code;
        }
        if (tray.pdfLoaded && c.ib > 0.0) {
            jouleWm += cores * (0.0214 / std::max(0.5, sec)) * std::pow(c.ib, 2);
            const double kP = cores == 3 ? 1.732 : 2.0;
            const double vN = cores == 3 ? 400.0 : 230.0;
            const double dv = ((kP * tray.lengthM * c.ib * (0.0214 / std::max(0.5, sec)) * 0.9) / vN) * 100.0;
            if (dv > maxDv)
                maxDv = dv;
            if ((c.iz0 != 0.0 ? c.iz0 : 27.0) * kr < c.ib && deratingFailCode.empty())
                deratingFailCode = c.code;
        }
    }

    // Segregazione CEI 64-8 art. 528.1, scomparto per scomparto.
    std::vector<double> bounds{0.0};
    for (double s : septumX(tray))
        bounds.push_back(s);
    bounds.push_back(W);
    bool segConform = true;
    std::string segDetail = "Art. 528.1 OK (Kr=" + jsNumber(kr) + " su " + std::to_string(nLayers) + " str.)";
    const bool hasAnySignal = std::any_of(tray.cables.begin(), tray.cables.end(), [](const Cable& c) { return c.sys == "SPECIAL_ELV"; });
    if (!hasAnySignal) {
        segDetail = "Solo Cavi BT Potenza (Kr=" + jsNumber(kr) + " su " + std::to_string(nLayers) + " str.)";
    } else {
        for (std::size_t s = 0; s + 1 < bounds.size(); ++s) {
            const double x0 = bounds[s], x1 = bounds[s + 1];
            bool hasPower = false;
            const Cable* unshielded = nullptr;
            for (const Cable& c : tray.cables) {
                if (!(c.x >= x0 - 0.5 && c.x <= x1 + 0.5))
                    continue;
                if (c.sys == "BT_POWER" || c.sys == "BT_FIRE")
                    hasPower = true;
                if (!unshielded && c.sys == "SPECIAL_ELV" && !test(R"(FTG18|FG16|0,6\/1|1kV|C-4|SCHERM|OH2)", c.type))
                    unshielded = &c;
            }
            if (hasPower && unshielded) {
                segConform = false;
                segDetail = "Conflitto " + unshielded->code + ": inserire Setto!";
                break;
            }
        }
    }

    const double fillPct = trayArea > 0.0 ? fixed((usedArea / trayArea) * 100.0, 1) : 0.0;
    const double sparePct = fixed(std::max(0.0, 100.0 - fillPct), 1);
    const double unfolded = W + 2.0 * H + 16.0 + (tray.cover != "none" ? W + 20.0 : 0.0);
    const double trayKgM = fixed(unfolded * tray.thk * 0.00785 * (tray.trayType == "perforated" ? 0.85 : 1.0), 2);
    const double totalKgM = fixed(cablesKgM + trayKgM, 2);

    const bool hasSupport = !tray.support.empty() && tray.support != "none";
    const Support* supSpec = support(tray.support);
    if (!supSpec)
        supSpec = support("none");
    const double qMaxKgM = hasSupport ? allowedLinearLoadKgM(tray, tray.support, tray.spanM) : 999.0;
    const double pointLoad = fixed(totalKgM * tray.spanM, 1);
    const double maxPoint = hasSupport ? jsRound(qMaxKgM * tray.spanM) : 0.0;
    const double loadPct = hasSupport && qMaxKgM > 0.0 ? fixed((totalKgM / qMaxKgM) * 100.0, 1) : 0.0;
    const double maxDeflectionMm = fixed(std::min(10.0, (tray.spanM * 1000.0) / 200.0), 1);
    const double deflectionMm = hasSupport ? elasticDeflectionMm(tray, totalKgM, tray.support, tray.spanM) : 0.0;

    const double pullDaN = fixed(cablesKgM * tray.lengthM * 0.35 * std::exp(0.35 * tray.bends90 * (kPi / 2.0)), 1);
    const double maxPullDaN = jsRound(std::max(15.0, cuArea * 5.0));
    const double fitR = maxBendR <= 150 ? 150 : maxBendR <= 300 ? 300 : 600;

    const double nVerghe = std::ceil(tray.lengthM / 3.0);
    const double nStaffe = std::ceil(tray.lengthM / tray.spanM) + 1.0;
    const std::vector<Series>& serList = seriesOf(tray.brand);
    const Series* serObj = &serList.front();
    for (const Series& s : serList)
        if (s.id == tray.seriesId) {
            serObj = &s;
            break;
        }
    const std::string& pCode = serObj->code;
    const double freeAreaCm2 = std::max(0.0, jsRound((trayArea - usedArea) / 100.0));
    const double nSacchettiRei = std::max(2.0, std::ceil(freeAreaCm2 / 35.0)) * tray.reiCount;
    const std::string w = jsNumber(W), h = jsNumber(H);
    const int seismicKits = tray.seismic != "none" ? static_cast<int>(std::max(1.0, std::ceil(tray.lengthM / (tray.seismic == "cable_9m" ? 9.0 : 6.0)))) : 0;

    auto& bom = m.bom;
    bom.push_back({pCode + "-" + w + "x" + h, serObj->name + " L=" + w + "xH=" + h + "mm sp." + jsNumber(tray.thk) + "mm", nVerghe * 3, "m",
                   jsNumber(nVerghe) + " verghe da 3m"});
    if (tray.cover != "none")
        bom.push_back({pCode + "-COP-" + w, "Coperchio superiore (" + tray.cover + ") L=" + w + "mm", nVerghe * 3, "m", "Inclusi fermacoperchi"});
    if (tray.septums > 0)
        bom.push_back({pCode + "-SET-H" + h, "Setto separatore longitudinale H=" + h + "mm (" + std::to_string(tray.septums) + " file)",
                       nVerghe * 3 * tray.septums, "m", "Segregazione CEI 64-8"});
    bom.push_back({pCode + "-GIU-M6", "Kit giunzione lineare rinforzata con bulloneria M6", nVerghe, "cad", "1 ogni 3m"});
    if (hasSupport)
        bom.push_back({pCode + "-STF-" + w, supSpec->name + " L=" + jsNumber(W + 50) + "mm (Passo Ls=" + jsNumber(tray.spanM) + "m)", nStaffe, "cad",
                       "Inclusi " + jsNumber(nStaffe * 2) + " tasselli M10"});
    if (tray.bends90 > 0)
        bom.push_back({pCode + "-CRV90-" + w + "-R" + jsNumber(fitR),
                       "Curva piana 90° L=" + w + "mm H=" + h + "mm (R_acc=" + jsNumber(fitR) + "mm >= R_min=" + jsNumber(maxBendR) + "mm)",
                       static_cast<double>(tray.bends90), "cad", "Cavo critico: " + critCode});
    if (tray.tees > 0)
        bom.push_back({pCode + "-DEVT-" + w, "Deviazione piana a T L=" + w + "mm H=" + h + "mm completa di giunti", static_cast<double>(tray.tees), "cad",
                       "Raggio R=" + jsNumber(fitR) + "mm"});
    if (tray.reducers > 0)
        bom.push_back({pCode + "-RID-" + w, "Riduzione simmetrica/asimmetrica di larghezza L=" + w + "mm", static_cast<double>(tray.reducers), "cad",
                       "Acciaio zincato"});
    if (tray.seismic != "none")
        bom.push_back({pCode + "-SISM-" + upper(tray.seismic),
                       std::string("Kit controvento antisismico NTC 2018 (") + (tray.seismic == "cable_9m" ? "Funi 45° ogni 9m" : "Strut rigido 45° ogni 6m") + ")",
                       static_cast<double>(seismicKits), "kit", "Rif. NTC 2018"});
    if (tray.reiCount > 0)
        bom.push_back({"FIRE-EI120-" + w + "x" + h,
                       "Barriera tagliafuoco intumescente EI120 (cuscini + mastice endotermico per " + jsNumber(freeAreaCm2) + " cm² netti/varco)",
                       static_cast<double>(tray.reiCount), "varco", jsNumber(nSacchettiRei) + " cuscini intumescenti tot."});
    if (tray.earth != "none")
        bom.push_back({pCode + "-PE-16", "Morsetto equipotenziale + capocorda M6 + conduttore PE (" + upper(tray.earth) + ")",
                       std::ceil(tray.lengthM / 12.0) + 1.0, "cad", "1 ogni 12m"});

    m.trayArea = trayArea;
    m.usedArea = usedArea;
    m.fillPct = fillPct;
    m.sparePct = sparePct;
    m.kr = kr;
    m.kFascio = kFascio;
    m.kStrati = kStrati;
    m.kPosa = kPosa;
    m.nLayers = nLayers;
    m.segConform = segConform;
    m.segDetail = segDetail;
    m.totalKgM = totalKgM;
    m.trayKgM = trayKgM;
    m.qMaxKgM = qMaxKgM;
    m.pointLoad = pointLoad;
    m.maxPoint = maxPoint;
    m.loadPct = loadPct;
    m.deflectionMm = deflectionMm;
    m.maxDeflectionMm = maxDeflectionMm;
    m.pullDaN = pullDaN;
    m.maxPullDaN = maxPullDaN;
    m.maxBendR = maxBendR;
    m.critCode = critCode;
    m.fitR = fitR;
    m.jouleWm = fixed(jouleWm, 1);
    m.maxDv = fixed(maxDv, 2);
    m.deratingFailCode = deratingFailCode;
    m.seismicKits = seismicKits;
    m.reiCushions = static_cast<int>(nSacchettiRei);
    return m;
}

// ------------------------------------------------------------------ sezione disegnata

namespace {

// Toglie i codici di formattazione del testo multilinea.
std::string cleanMText(const std::string& raw)
{
    std::string s = raw;
    // \U+XXXX: solo il caso più comune (Ø) e lettere latine; il resto diventa uno spazio.
    {
        static const std::regex re(R"(\\U\+([0-9A-Fa-f]{4}))");
        std::string out;
        auto begin = std::sregex_iterator(s.begin(), s.end(), re);
        std::size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            out += s.substr(last, static_cast<std::size_t>(it->position()) - last);
            const unsigned cp = static_cast<unsigned>(std::strtoul(it->str(1).c_str(), nullptr, 16));
            if (cp < 0x80)
                out += static_cast<char>(cp);
            else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            last = static_cast<std::size_t>(it->position() + it->length());
        }
        s = out + s.substr(last);
    }
    s = replaceAll(s, R"(\\P)", " ");
    s = replaceAll(s, R"(\\~|\^J)", " ", false);
    s = replaceAll(s, R"(\\[A-Za-z][^;]*;)", "", false);
    s = replaceAll(s, R"([{}])", "", false);
    s = replaceAll(s, R"(\s+)", " ", false);
    return trim(s);
}

struct Label {
    std::string code, form, desc;
};

Label parseLabel(const std::string& raw)
{
    std::string s = trim(raw);
    s = trim(replaceAll(s, R"(^#\d+\s+)", "", false));
    Label out;
    std::smatch paren;
    static const std::regex parenRe(R"(\(([^)]+)\)\s*$)");
    if (std::regex_search(s, paren, parenRe) && test(R"([xXGg]\d+|Ø)", paren[1].str())) {
        static const std::regex formRe(R"(\d*\(\d+[xXGg]\d+(?:[.,]\d+)?\)|\b\d+[xXGg]\d+(?:[.,]\d+)?\b)", std::regex::ECMAScript | std::regex::icase);
        std::smatch fm;
        const std::string inner = paren[1].str();
        if (std::regex_search(inner, fm, formRe))
            out.form = fm[0].str();
        s = trim(s.substr(0, static_cast<std::size_t>(paren.position())));
    }
    out.code = s;
    static const std::regex codeRe(R"(^([A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+C-?\d+|C-?\d+|[A-Z0-9_-]+-C\d+)\b)",
                                   std::regex::ECMAScript | std::regex::icase);
    std::smatch mc;
    if (std::regex_search(s, mc, codeRe)) {
        out.code = trim(mc[1].str());
        const std::string rem = trim(s.substr(static_cast<std::size_t>(mc.length(0))));
        if (!rem.empty())
            out.desc = rem;
    }
    return out;
}

std::string baseCircuitCode(const std::string& str)
{
    std::string s = trim(str);
    s = trim(replaceAll(s, R"(\s*\(\s*\d*\(?\d+[xXGg]\d+(?:[.,]\d+)?\)?\s*\)\s*$)", ""));
    if (test(R"(^CAVO-\d+$)", s))
        return s;
    s = trim(replaceAll(s, R"((?:[-_/\s]+(?:(?:FASE|COND|POLO)\s*)?(?:L[123]|F[123]|R|S|T|N|PE|TERRA)|[./]\d+|\(\s*(?:L[123]|F[123]|R|S|T|N|PE|TERRA|[1-9])\s*\))$)", ""));
    return s;
}

bool isCodeLike(const std::string& s)
{
    return !test(R"(^CANALINA\b|^SCALA|^DATA|^DISEGNATORE|^VISTO|^DESCRIZIONE|^REV\b|^POS\b|^SIGLA\b|^Base\s+L|^H\s*=)", s)
           && (test(R"([A-Z0-9]+.*C-\d+)", s) || test(R"(^(?:QGBT|QP|QE|UPS|IRAI|EVAC|TD|LINEA|CAVO))", s) || test(R"(^#\d+\s+)", s));
}

double snapTo(double v, const std::vector<double>& list)
{
    double best = list.front();
    for (double c : list)
        if (std::abs(c - v) < std::abs(best - v))
            best = c;
    return best;
}

} // namespace

Tray readSection(const Drawn& drawn, Vec2* originOut)
{
    struct Seg { double x1, y1, x2, y2; std::string lay; };
    struct Cir { double cx, cy, r, d; std::string lay; };
    struct Txt { std::string str; double x, y; std::string lay; };
    std::vector<Seg> traySegments, leaderSegments;
    std::vector<Cir> rawCircles;
    std::vector<Txt> rawTexts;
    auto isTrayLay = [](const std::string& l) { return test("CANAL|PASSERELL|TRAY|PROFIL|SEZION|CONDOTT|BORDO|CONTORN", l); };
    auto isLeaderLay = [](const std::string& l) { return test("BANDIERINE|LEADER|RICHIAM", l); };

    for (const auto& s : drawn.segments) {
        const std::string lay = trim(s.layer.empty() ? "0" : s.layer);
        if (isTrayLay(lay))
            traySegments.push_back({s.a.x, s.a.y, s.b.x, s.b.y, lay});
        else if (isLeaderLay(lay))
            leaderSegments.push_back({s.a.x, s.a.y, s.b.x, s.b.y, lay});
    }
    for (const auto& c : drawn.circles) {
        const std::string lay = trim(c.layer.empty() ? "0" : c.layer);
        const double d = fixed(c.radius * 2.0, 1);
        if (d >= 4.5 && d <= 110.0 && (test("CAVI", lay) || !test("ANIME|CORNICE|CARTIGLIO|SIMB", lay)))
            rawCircles.push_back({fixed(c.center.x, 2), fixed(c.center.y, 2), fixed(c.radius, 2), d, lay});
    }
    for (const auto& t : drawn.texts) {
        const std::string str = cleanMText(t.text);
        if (!str.empty())
            rawTexts.push_back({str, fixed(t.position.x, 2), fixed(t.position.y, 2), trim(t.layer.empty() ? "0" : t.layer)});
    }
    if (traySegments.empty())
        for (const auto& s : drawn.segments) {
            const std::string lay = trim(s.layer.empty() ? "0" : s.layer);
            if (!test("CAVI|ANIME|BANDIERINE|LEADER|RICHIAM|QUOTE|DIM|CORNICE|CARTIGLIO|TABELLA", lay))
                traySegments.push_back({s.a.x, s.a.y, s.b.x, s.b.y, lay});
        }

    // Cerchi concentrici (guaina interna): resta l'esterno.
    std::stable_sort(rawCircles.begin(), rawCircles.end(), [](const Cir& a, const Cir& b) { return b.r - a.r < 0.0; });
    std::vector<Cir> outer;
    for (const Cir& c : rawCircles)
        if (std::none_of(outer.begin(), outer.end(), [&](const Cir& o) { return std::hypot(c.cx - o.cx, c.cy - o.cy) < o.r * 0.85; }))
            outer.push_back(c);

    // Misure della canalina: dai testi (L=.. H=.., "CANALINA 75x300", "75x300"), dal nome del file, dalla geometria.
    std::string corpus;
    for (std::size_t i = 0; i < rawTexts.size(); ++i)
        corpus += (i ? " | " : "") + rawTexts[i].str;
    corpus = replaceAll(corpus, "\xC3\x97", "x", false);   // ×
    double detW = 0.0, detH = 0.0;
    auto assign = [&](double a, double b) {
        detW = std::max(a, b);
        detH = std::min(a, b);
    };
    std::smatch mm;
    const std::regex lh1(R"((?:L|B|W|BASE|LARGHEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?\s*[,;\s|/-]+\s*(?:H|ALT|ALTEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?)",
                         std::regex::ECMAScript | std::regex::icase);
    const std::regex lh2(R"((?:H|ALT|ALTEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?\s*[,;\s|/-]+\s*(?:L|B|W|BASE|LARGHEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?)",
                         std::regex::ECMAScript | std::regex::icase);
    if (std::regex_search(corpus, mm, lh1) || std::regex_search(corpus, mm, lh2))
        assign(std::stod(mm[1].str()), std::stod(mm[2].str()));
    if (detW == 0.0 || detH == 0.0) {
        const std::regex kw(R"((?:CANALINA|CANALE|PASSERELLA|TRAY|A1|CF|RKS|MKS)\b[^\d\n\r]*(\d{2,4})\s*[xX*]\s*(\d{2,4}))", std::regex::ECMAScript | std::regex::icase);
        if (std::regex_search(corpus, mm, kw))
            assign(std::stod(mm[1].str()), std::stod(mm[2].str()));
    }
    if (detW == 0.0 || detH == 0.0) {
        const std::regex any(R"(\b(\d{2,4})\s*[xX*]\s*(\d{2,4})\s*(?:mm)?\b)", std::regex::ECMAScript | std::regex::icase);
        for (auto it = std::sregex_iterator(corpus.begin(), corpus.end(), any); it != std::sregex_iterator(); ++it) {
            const double d1 = std::stod((*it)[1].str()), d2 = std::stod((*it)[2].str());
            const double maxD = std::max(d1, d2), minD = std::min(d1, d2);
            if (maxD >= 40 && maxD <= 1200 && minD >= 20 && minD <= 300) {
                assign(d1, d2);
                break;
            }
        }
    }
    if (detW == 0.0 || detH == 0.0) {
        const std::string name = replaceAll(drawn.fileName, "\xC3\x97", "x", false);
        const std::regex fr(R"((\d{2,4})\s*[xX*_-]\s*(\d{2,4}))");
        if (std::regex_search(name, mm, fr)) {
            const double d1 = std::stod(mm[1].str()), d2 = std::stod(mm[2].str());
            const double maxD = std::max(d1, d2), minD = std::min(d1, d2);
            if (maxD >= 40 && maxD <= 1200 && minD >= 20 && minD <= 300)
                assign(d1, d2);
        }
    }
    static const std::vector<double> validW = {50, 60, 75, 100, 150, 200, 250, 300, 400, 500, 600, 700, 800, 900, 1000, 1200};
    static const std::vector<double> validH = {25, 30, 35, 40, 50, 60, 75, 80, 85, 100, 110, 120, 150, 200};
    double origX = 0.0, origY = 0.0;
    if (!traySegments.empty()) {
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (const Seg& s : traySegments) {
            minX = std::min({minX, s.x1, s.x2});
            maxX = std::max({maxX, s.x1, s.x2});
            minY = std::min({minY, s.y1, s.y2});
            maxY = std::max({maxY, s.y1, s.y2});
        }
        origX = minX;
        origY = minY;
        const double spanX = maxX - origX, spanY = maxY - origY;
        if (detW == 0.0 && spanX >= 40 && spanX <= 1200)
            detW = snapTo(spanX - 3.0, validW);
        if (detH == 0.0 && spanY >= 20 && spanY <= 400)
            detH = snapTo(spanY - 1.5, validH);
    } else if (!outer.empty()) {
        origX = 1e300;
        origY = 1e300;
        for (const Cir& c : outer) {
            origX = std::min(origX, c.cx - c.r);
            origY = std::min(origY, c.cy - c.r);
        }
        origX -= 12.0;
    }
    if (detW != 0.0 && detH != 0.0 && detW < detH)
        std::swap(detW, detH);

    Tray tray;
    tray.width = detW != 0.0 ? detW : 200.0;
    tray.height = detH != 0.0 ? detH : 75.0;
    tray.packing = "gravity";
    if (originOut)
        *originOut = {origX, origY};
    const double geomW = tray.width;

    // Sigle: testi delle bandierine vicino alla sezione.
    std::vector<const Txt*> callouts;
    for (const Txt& t : rawTexts)
        if ((isCodeLike(t.str) || test("Testi|Bandierine", t.lay)) && t.x >= origX - 350 && t.x <= origX + geomW + 350
            && !test(R"(^CANALINA\b|^Base\s+L|^H\s*=)", t.str) && !test(R"(^\d+$)", trim(t.str)))
            callouts.push_back(&t);

    auto traceLeader = [&](const Cir& cir) -> const Txt* {
        const Txt* best = nullptr;
        double bestDist = 9999.0;
        // Le bandierine che toccano il cerchio, dalla più vicina al centro: a parità di distanza dalla sigla vince
        // quella che parte da questo cavo e non da un cavo accostato (la pagina prende la prima disegnata).
        std::vector<std::pair<double, const Seg*>> touching;
        for (const Seg& s0 : leaderSegments) {
            const double d1 = std::hypot(s0.x1 - cir.cx, s0.y1 - cir.cy);
            const double d2 = std::hypot(s0.x2 - cir.cx, s0.y2 - cir.cy);
            if (d1 <= cir.r + 4.5 || d2 <= cir.r + 4.5)
                touching.push_back({std::min(d1, d2), &s0});
        }
        std::stable_sort(touching.begin(), touching.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [startDist, seg0] : touching) {
            const Seg& s0 = *seg0;
            const double d1 = std::hypot(s0.x1 - cir.cx, s0.y1 - cir.cy);
            const double d2 = std::hypot(s0.x2 - cir.cx, s0.y2 - cir.cy);
            double ex = d1 < d2 ? s0.x2 : s0.x1, ey = d1 < d2 ? s0.y2 : s0.y1;
            std::vector<const Seg*> visited{&s0};
            for (int hop = 0; hop < 3; ++hop) {
                const Seg* next = nullptr;
                for (const Seg& s : leaderSegments) {
                    if (std::find(visited.begin(), visited.end(), &s) != visited.end())
                        continue;
                    if (std::hypot(s.x1 - ex, s.y1 - ey) < 3.5 || std::hypot(s.x2 - ex, s.y2 - ey) < 3.5) {
                        next = &s;
                        break;
                    }
                }
                if (!next)
                    break;
                visited.push_back(next);
                const double nd1 = std::hypot(next->x1 - ex, next->y1 - ey);
                if (nd1 < 3.5) {
                    ex = next->x2;
                    ey = next->y2;
                } else {
                    ex = next->x1;
                    ey = next->y1;
                }
            }
            for (const Txt* lbl : callouts) {
                const double dy = std::abs(lbl->y - ey), dx = std::abs(lbl->x - ex);
                const double dist = std::hypot(dx * 0.45, dy * 2.2);
                if (dy < 12 && dx < 120 && dist < bestDist) {
                    bestDist = dist;
                    best = lbl;
                }
            }
        }
        return best;
    };

    std::stable_sort(outer.begin(), outer.end(), [](const Cir& a, const Cir& b) {
        const double k = (a.cy - b.cy) != 0.0 ? (a.cy - b.cy) : (a.cx - b.cx);
        return k < 0.0;
    });
    std::vector<const Txt*> used;
    for (std::size_t idx = 0; idx < outer.size(); ++idx) {
        const Cir& cir = outer[idx];
        const Txt* lbl = traceLeader(cir);
        if (!lbl) {
            std::vector<const Txt*> unused;
            for (const Txt* t : callouts)
                if (std::find(used.begin(), used.end(), t) == used.end())
                    unused.push_back(t);
            if (!unused.empty()) {
                std::stable_sort(unused.begin(), unused.end(), [&](const Txt* a, const Txt* b) {
                    return std::hypot(a->x - cir.cx, a->y - cir.cy) - std::hypot(b->x - cir.cx, b->y - cir.cy) < 0.0;
                });
                lbl = unused.front();
            }
        }
        if (lbl)
            used.push_back(lbl);
        const Label parsed = parseLabel(lbl ? lbl->str : "CAVO-" + std::to_string(idx + 1));
        const std::string code = !parsed.code.empty() ? parsed.code : "CAVO-" + std::to_string(idx + 1);
        const std::string base = baseCircuitCode(code);
        const Inferred spec = inferByDiameter(cir.d, code, lbl ? lbl->lay : std::string());
        Cable c;
        c.code = code;
        c.form = !parsed.form.empty() ? parsed.form : spec.form;
        c.type = spec.type;
        c.desc = !parsed.desc.empty() ? parsed.desc
                 : code.rfind("CAVO-", 0) == 0 ? "Utenza " + code.substr(5)
                                               : "Alimentazione " + base;
        c.sec = sectionFromForm(c.form, spec.sec);
        const bool uni = test(R"(\b1X\d+|\(\s*1X\d+)", c.form) || test(R"(\bFG16M16\b|\bFS17\b|\bFG17\b)", c.type);
        c.cores = uni ? 1 : (c.form.find("5G") != std::string::npos || c.form.find("4G") != std::string::npos ? 3 : 2);
        c.d = cir.d;
        c.w = fixed(std::pow(cir.d / 10.0, 2) * 0.145, 2);
        c.iz0 = spec.iz0;
        c.bendK = spec.bendK;
        c.sys = spec.sys;
        c.origX = c.x = fixed(cir.cx - origX, 1);
        c.origY = c.y = fixed(cir.cy - origY, 1);
        tray.cables.push_back(c);
    }

    // Circuiti unipolari (es. 4(1x185)): i cerchi senza sigla vicini e dello stesso diametro diventano gli altri
    // conduttori del circuito.
    std::vector<std::pair<std::string, std::vector<std::size_t>>> groups;
    for (std::size_t i = 0; i < tray.cables.size(); ++i) {
        const std::string base = baseCircuitCode(tray.cables[i].code);
        auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == base; });
        if (it == groups.end())
            groups.push_back({base, {i}});
        else
            it->second.push_back(i);
    }
    for (auto& [base, group] : groups) {
        if (base.empty() || base.rfind("CAVO-", 0) == 0)
            continue;
        const Cable main = tray.cables[group[0]];
        int expected = 1;
        std::smatch cm;
        static const std::regex c1(R"(^(\d+)\s*\(?\s*1[xX]\d+)", std::regex::ECMAScript | std::regex::icase);
        static const std::regex c2(R"(^(\d+)[xX]\s*1[xX]\d+)", std::regex::ECMAScript | std::regex::icase);
        if (std::regex_search(main.form, cm, c1) || std::regex_search(main.form, cm, c2))
            expected = std::stoi(cm[1].str());
        else if (test(R"(3\(1x.*PE)", main.form))
            expected = 5;
        else if (test(R"(3\(1x)", main.form))
            expected = 4;
        if (expected <= static_cast<int>(group.size()))
            continue;
        const std::size_t missing = static_cast<std::size_t>(expected) - group.size();
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < tray.cables.size(); ++i)
            if (tray.cables[i].code.rfind("CAVO-", 0) == 0 && std::abs(tray.cables[i].d - main.d) <= 3.0)
                candidates.push_back(i);
        std::stable_sort(candidates.begin(), candidates.end(), [&](std::size_t a, std::size_t b) {
            const Cable& ca = tray.cables[a];
            const Cable& cb = tray.cables[b];
            return std::hypot(ca.x - main.x, ca.y - main.y) - std::hypot(cb.x - main.x, cb.y - main.y) < 0.0;
        });
        static const char* suffixes[] = {"L2", "L3", "N", "PE"};
        const std::size_t start = group.size();
        for (std::size_t k = 0; k < std::min(missing, candidates.size()); ++k) {
            Cable& tc = tray.cables[candidates[k]];
            // Come la pagina: l'indice usa la lunghezza del gruppo che cresce a ogni conduttore aggiunto.
            const std::size_t s = start - 1 + 2 * k;
            tc.code = base + " - " + (s < 4 ? std::string(suffixes[s]) : std::to_string(start + 2 * k + 1));
            tc.type = main.type;
            tc.form = main.form;
            tc.cores = 1;
            tc.sys = main.sys;
            group.push_back(candidates[k]);
        }
    }
    return tray;
}

int matchCatalog(Tray& tray, const Drawn& drawn, const std::vector<CableType>& extra)
{
    // Etichette "SIGLA (TIPO FORMAZIONE)" del catalogo, ognuna usabile una volta.
    // Un testo può corrispondere a più tipi con lo stesso nome (un cavo proprio con un altro diametro): decide il
    // diametro del cerchio.
    struct Known {
        std::string code, full;   // sigla e testo intero (letto come sigla se la parentesi non è una formazione)
        const CableType* type;
        std::size_t text;
    };
    std::vector<Known> known;
    std::vector<const CableType*> types;
    for (const CableType& c : catalog())
        types.push_back(&c);
    for (const CableType& c : extra)
        types.push_back(&c);
    for (std::size_t ti = 0; ti < drawn.texts.size(); ++ti) {
        const std::string str = cleanMText(drawn.texts[ti].text);
        for (const CableType* c : types) {
            const std::string suffix = sectionLabel(cableFrom(*c, " ")).substr(1);   // " (TIPO FORMAZIONE)"
            if (suffix.size() > 1 && str.size() > suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0)
                known.push_back({trim(str.substr(0, str.size() - suffix.size())), str, c, ti});
        }
    }
    std::vector<bool> usedText(drawn.texts.size(), false);
    int matched = 0;
    for (Cable& cable : tray.cables) {
        // Prima la sigla uguale, poi una sigla che la estende (QE1 C-3/2 letta come QE1 C-3); sempre con il diametro.
        Known* hit = nullptr;
        for (int pass = 0; pass < 2 && !hit; ++pass)
            for (Known& k : known)
                if (!usedText[k.text] && std::abs(k.type->d - cable.d) <= 0.11
                    && (pass == 0 ? k.code == cable.code || k.full == cable.code : k.code.rfind(cable.code, 0) == 0)) {
                    hit = &k;
                    break;
                }
        if (!hit)
            continue;
        usedText[hit->text] = true;
        Cable c = cableFrom(*hit->type, hit->code);
        c.x = cable.x;
        c.y = cable.y;
        c.origX = cable.origX;
        c.origY = cable.origY;
        c.ib = cable.ib;
        c.trefoil = cable.trefoil;
        c.trefoilGroup = cable.trefoilGroup;
        cable = c;
        ++matched;
    }
    return matched;
}

} // namespace cartesio::canalo
