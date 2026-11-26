#include "core/SwitchboardBlocks.h"

#include "core/Entity.h"
#include "core/Layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>
#include <memory>

namespace cartesio {

namespace {

constexpr double kMod = 18.0;     // modulo DIN
constexpr double kModH = 90.0;    // altezza del corpo modulare
constexpr double kPlateW = 500.0; // piastra per colonna da 600 mm (700 per 800)

std::string num(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// Disegno di un blocco: oggetti sul layer 0, colore DaBlocco (prendono il colore dell'inserimento).
struct Pen {
    std::vector<std::unique_ptr<Entity>>& out;
    template <typename T>
    void add(std::unique_ptr<T> e)
    {
        e->setLayer(0);
        e->setColor(kColorByBlock);
        out.push_back(std::move(e));
    }
    void line(Vec2 a, Vec2 b) { add(std::make_unique<LineEntity>(a, b)); }
    void rect(double x, double y, double w, double h)
    {
        add(std::make_unique<PolylineEntity>(
            std::vector<PolyVertex>{{{x, y}}, {{x + w, y}}, {{x + w, y + h}}, {{x, y + h}}}, true));
    }
    void circle(Vec2 c, double r) { add(std::make_unique<CircleEntity>(c, r)); }
    void text(Vec2 p, double h, const std::string& s)
    {
        auto t = std::make_unique<TextEntity>(p, h, s);
        t->hAlign = TextEntity::Middle;
        t->alignPoint = p;
        add(std::move(t));
    }
    void screws(double x, double y, double w, double h, double r = 4.0)
    {
        const double m = std::max(r + 4.0, std::min(14.0, std::min(w, h) * 0.12));
        for (const Vec2 c : {Vec2{x + m, y + m}, Vec2{x + w - m, y + m}, Vec2{x + m, y + h - m}, Vec2{x + w - m, y + h - m}})
            circle(c, r);
    }
};

// ---------------------------------------------------------------- modulari (larghezza w, alti 90 mm)

void modularFrame(Pen& p, double w, int poles)
{
    p.rect(0.0, 0.0, w, kModH);
    p.rect(0.0, 22.5, w, 45.0);   // parte che si vede dal pannello finestrato
    for (int i = 0; i < std::max(1, poles); ++i) {
        const double cx = (i + 0.5) * w / std::max(1, poles);
        p.circle({cx, 9.0}, 3.0);
        p.circle({cx, kModH - 9.0}, 3.0);
    }
}

void toggle(Pen& p, double cx, double width = 7.0)
{
    p.rect(cx - width / 2.0, 34.0, width, 22.0);
    p.rect(cx - width / 2.0 + 1.5, 46.0, width - 3.0, 8.0);   // leva in alto (chiuso)
}

void drawBreaker(Pen& p, int poles, double mpp)
{
    const double w = poles * mpp * kMod;
    modularFrame(p, w, poles);
    for (int i = 0; i < poles; ++i)
        toggle(p, (i + 0.5) * w / poles);
    p.rect(2.0, 58.0, w - 4.0, 6.0);   // targhetta
}

void drawResidual(Pen& p, int poles, double modules)
{
    const double w = modules * kMod;
    modularFrame(p, w, poles);
    const double split = w * 0.55;   // a sinistra l'interruttore, a destra il blocco differenziale
    p.line({split, 22.5}, {split, 67.5});
    for (int i = 0; i < poles; ++i)
        toggle(p, (i + 0.5) * split / poles, std::min(7.0, split / poles * 0.6));
    const double rx = split + (w - split) / 2.0;
    p.circle({rx, 56.0}, 3.5);              // tasto di prova
    p.rect(rx - 6.0, 30.0, 12.0, 8.0);      // indicatore di intervento
    p.rect(split + 3.0, 41.0, w - split - 6.0, 6.0);
}

void drawSwitch(Pen& p, int poles, double mpp)
{
    const double w = poles * mpp * kMod;
    modularFrame(p, w, poles);
    const double tw = std::min(w - 4.0, 12.0 + 4.0 * poles);
    p.rect((w - tw) / 2.0, 33.0, tw, 24.0);   // leva larga di manovra
    p.rect((w - tw) / 2.0 + 2.0, 46.0, tw - 4.0, 9.0);
}

void drawFuse(Pen& p, int poles, double mpp)
{
    const double w = poles * mpp * kMod;
    modularFrame(p, w, poles);
    const double pw = w / poles;
    for (int i = 0; i < poles; ++i) {
        const double x = i * pw;
        p.rect(x + 2.0, 26.0, pw - 4.0, 38.0);         // portello
        p.rect(x + 5.0, 36.0, pw - 10.0, 16.0);        // finestra del fusibile
        p.circle({x + pw / 2.0, 59.0}, 2.0);
    }
}

void drawSpd(Pen& p, int poles)
{
    const double w = std::max(1, poles) * kMod;
    modularFrame(p, w, poles);
    for (int i = 0; i < std::max(1, poles); ++i) {
        const double x = i * kMod;
        p.rect(x + 2.0, 25.0, kMod - 4.0, 40.0);        // cartuccia
        p.rect(x + 5.0, 50.0, kMod - 10.0, 8.0);        // indicatore di fine vita
    }
}

void drawMeter(Pen& p, double modules)
{
    const double w = modules * kMod;
    modularFrame(p, w, 0);
    p.rect(6.0, 44.0, w - 12.0, 18.0);   // display
    for (int i = 0; i < 3; ++i)
        p.circle({w * (i + 1) / 4.0, 32.0}, 3.0);
}

void drawSupply(Pen& p, double modules)
{
    const double w = modules * kMod;
    modularFrame(p, w, 0);
    // simbolo alternata/continua
    std::vector<PolyVertex> wave;
    for (int i = 0; i <= 16; ++i) {
        const double t = i / 16.0;
        wave.push_back({{w * 0.2 + t * w * 0.25, 52.0 + 5.0 * std::sin(t * 2.0 * 3.14159265358979)}});
    }
    p.add(std::make_unique<PolylineEntity>(std::move(wave), false));
    p.line({w * 0.55, 54.0}, {w * 0.8, 54.0});
    p.line({w * 0.55, 49.0}, {w * 0.8, 49.0});
    p.circle({w * 0.3, 32.0}, 2.5);   // spie
    p.circle({w * 0.7, 32.0}, 2.5);
}

void drawRelay(Pen& p, double modules)
{
    const double w = modules * kMod;
    modularFrame(p, w, 0);
    p.rect(4.0, 50.0, w - 8.0, 12.0);   // display
    p.circle({w * 0.3, 36.0}, 5.0);     // regolazioni
    p.circle({w * 0.7, 36.0}, 5.0);
    p.circle({w * 0.5, 28.0}, 2.5);     // prova
}

void drawAux(Pen& p, double modules)
{
    const double w = modules * kMod;
    modularFrame(p, w, 0);
    toggle(p, w / 2.0, std::min(7.0, w * 0.4));
}

void drawTerminals(Pen& p)
{
    const double w = 6.0 * kMod;   // un tratto di guida con 12 morsetti
    p.rect(0.0, 22.5, w, 45.0);
    for (int i = 0; i < 12; ++i) {
        const double x = i * w / 12.0;
        if (i > 0)
            p.line({x, 22.5}, {x, 67.5});
        p.circle({x + w / 24.0, 32.0}, 2.5);
        p.circle({x + w / 24.0, 58.0}, 2.5);
    }
}

// ---------------------------------------------------------------- scatolati su piastra, aperti

struct Frame {
    double from, to;   // A
    double w3, w4, h;  // mm del corpo, 3 e 4 poli
};
constexpr Frame kFrames[] = {
    {0.0, 160.0, 90.0, 120.0, 130.0},   {161.0, 250.0, 105.0, 140.0, 160.0},  {251.0, 630.0, 140.0, 185.0, 255.0},
    {631.0, 1600.0, 210.0, 280.0, 330.0}, {1601.0, 3200.0, 280.0, 370.0, 350.0},
};
constexpr Frame kAirFrames[] = {
    {0.0, 1600.0, 300.0, 380.0, 440.0}, {1601.0, 4000.0, 420.0, 530.0, 480.0}, {4001.0, 6300.0, 780.0, 1000.0, 480.0},
};

double plateHeight(double bodyH, bool residual)
{
    return std::max(150.0, std::ceil((bodyH + (residual ? 160.0 : 90.0)) / 50.0) * 50.0);
}

// Scatolato (o sezionatore) su una piastra larga 500 mm: variante N normale, R con blocco differenziale, S sezionatore.
void drawMoulded(Pen& p, const Frame& f, int poles, char variant)
{
    const double bw = poles == 4 ? f.w4 : f.w3, bh = f.h;
    const bool residual = variant == 'R';
    const double ph = plateHeight(bh, residual);
    p.rect(0.0, 0.0, kPlateW, ph);
    p.screws(0.0, 0.0, kPlateW, ph);
    const double extra = residual ? 70.0 : 0.0;
    const double bx = (kPlateW - bw) / 2.0, by = (ph - bh - extra) / 2.0 + extra;
    p.rect(bx, by, bw, bh);                                  // corpo
    p.rect(bx + 6.0, by + bh * 0.18, bw - 12.0, bh * 0.64);  // coperchio frontale
    const double pw = bw / poles;
    for (int i = 0; i < poles; ++i) {                        // attacchi
        const double cx = bx + (i + 0.5) * pw;
        p.circle({cx, by + bh * 0.08}, std::min(6.0, pw * 0.18));
        p.circle({cx, by + bh * 0.92}, std::min(6.0, pw * 0.18));
    }
    if (variant == 'S') {                                     // manovra rotativa
        const Vec2 c{bx + bw / 2.0, by + bh / 2.0};
        const double r = std::min(bw, bh) * 0.24;
        p.circle(c, r);
        p.rect(c.x - r * 0.25, c.y - r * 0.95, r * 0.5, r * 1.9);
    } else {
        p.rect(bx + bw / 2.0 - 9.0, by + bh * 0.52, 18.0, bh * 0.26);   // leva
        p.rect(bx + bw / 2.0 - 6.0, by + bh * 0.62, 12.0, bh * 0.13);
        p.rect(bx + bw * 0.18, by + bh * 0.22, bw * 0.64, bh * 0.22);   // sganciatore
        p.rect(bx + bw * 0.24, by + bh * 0.30, bw * 0.22, bh * 0.08);   // regolazioni
        p.circle({bx + bw * 0.66, by + bh * 0.33}, std::min(6.0, bh * 0.04));
    }
    if (residual) {                                            // blocco differenziale sotto
        const double ay = by - extra + 6.0;
        p.rect(bx, ay, bw, extra - 10.0);
        p.circle({bx + bw * 0.3, ay + (extra - 10.0) / 2.0}, 5.0);
        p.rect(bx + bw * 0.55, ay + 12.0, bw * 0.3, extra - 34.0);
    }
}

// Aperto estraibile: telaio, fronte, sganciatore con display, pulsanti, leva di carica, finestre di stato.
void drawAir(Pen& p, const Frame& f, int poles)
{
    const double w = poles == 4 ? f.w4 : f.w3, h = f.h;
    p.rect(0.0, 0.0, w, h);                          // parte fissa
    p.rect(15.0, 15.0, w - 30.0, h - 30.0);          // parte mobile
    for (int i = 0; i < 6; ++i)                      // camere d'arco
        p.line({30.0 + i * (w - 60.0) / 5.0, h - 15.0}, {30.0 + i * (w - 60.0) / 5.0, h - 60.0});
    p.line({15.0, h - 60.0}, {w - 15.0, h - 60.0});
    p.rect(35.0, h * 0.42, w * 0.32, h * 0.32);      // sganciatore
    p.rect(45.0, h * 0.60, w * 0.32 - 20.0, h * 0.09);   // display
    for (int i = 0; i < 3; ++i)
        p.circle({55.0 + i * (w * 0.32 - 40.0) / 2.0, h * 0.50}, 5.0);
    p.circle({w * 0.55, h * 0.62}, 12.0);            // apertura e chiusura
    p.circle({w * 0.55, h * 0.48}, 12.0);
    p.rect(w * 0.72, h * 0.30, 26.0, h * 0.38);      // leva di carica
    p.rect(w * 0.62, h * 0.30, 30.0, 14.0);          // stato
    p.rect(w * 0.62, h * 0.36, 30.0, 14.0);
    p.circle({w / 2.0, 38.0}, 9.0);                  // manovella di estrazione
    p.rect(w / 2.0 - 40.0, 22.0, 24.0, 12.0);        // posizione
}

// ---------------------------------------------------------------- piastre, sbarre, carpenterie

void drawDinPlate(Pen& p, double w)
{
    p.rect(0.0, 0.0, w, 150.0);
    p.screws(0.0, 0.0, w, 150.0, 3.5);
    p.rect(30.0, 52.5, w - 60.0, 45.0);   // finestra degli apparecchi
}

void drawBlankPlate(Pen& p, double w, double h)
{
    p.rect(0.0, 0.0, w, h);
    p.screws(0.0, 0.0, w, h, 3.5);
}

void drawBusbars(Pen& p, double w)
{
    constexpr double h = 1650.0;
    const double bar = w >= 140.0 ? 14.0 : 8.0;
    for (int k = 0; k < 4; ++k) {
        const double x = (k + 0.5) * w / 4.0 - bar / 2.0;
        p.rect(x, 0.0, bar, h);
    }
    for (double y = 100.0; y < h; y += 400.0)   // supporti isolanti
        p.rect(0.0, y, w, 30.0);
}

void drawEnclosure(Pen& p, double w, double h)
{
    p.rect(0.0, 0.0, w, h);
    p.rect(0.0, 0.0, 20.0, h);              // montanti
    p.rect(w - 20.0, 0.0, 20.0, h);
    p.rect(20.0, h - 50.0, w - 40.0, 50.0); // traverse
    p.rect(20.0, 0.0, w - 40.0, 40.0);
    p.rect(25.0, 45.0, w - 50.0, h - 100.0);   // porta
    p.rect(w - 48.0, h / 2.0 - 60.0, 12.0, 120.0);   // maniglia
    p.rect(26.0, h * 0.2, 6.0, 40.0);       // cerniere
    p.rect(26.0, h * 0.8 - 40.0, 6.0, 40.0);
}

// ---------------------------------------------------------------- tabella dei blocchi

struct Generic {
    std::function<void(Pen&)> draw;
};

std::map<std::string, Generic>& registry()
{
    static std::map<std::string, Generic> r;
    return r;
}

DeviceType make(const std::string& id, BoardDevice::Kind kind, int pMin, int pMax, double from, double to,
                const std::string& text)
{
    DeviceType t;
    t.id = id;
    t.kind = kind;
    t.polesMin = pMin;
    t.polesMax = pMax;
    t.ratedMin = from;
    t.ratedMax = to;
    t.block = id;
    t.reference = text;
    t.source = "blocco generico CartesioCAD";
    t.generic = true;
    return t;
}

const std::vector<DeviceType>& table()
{
    static const std::vector<DeviceType> types = [] {
        using K = BoardDevice::Kind;
        std::vector<DeviceType> out;
        auto& reg = registry();
        const auto add = [&](DeviceType t, std::function<void(Pen&)> draw) {
            reg[t.block] = {std::move(draw)};
            out.push_back(std::move(t));
        };
        const auto modular = [&](DeviceType t, double modules, std::function<void(Pen&)> draw) {
            t.modules = modules;
            add(std::move(t), std::move(draw));
        };
        for (int n = 1; n <= 4; ++n) {
            const std::string pn = std::to_string(n) + "P";
            modular(make("FQG_MCB_" + pn, K::Modular, n, n, 0.0, 63.0, "Magnetotermico modulare fino a 63 A " + pn), n,
                    [n](Pen& p) { drawBreaker(p, n, 1.0); });
            modular(make("FQG_MCB125_" + pn, K::Modular, n, n, 64.0, 125.0, "Magnetotermico modulare 80-125 A " + pn),
                    n * 1.5, [n](Pen& p) { drawBreaker(p, n, 1.5); });
            const double rm = n <= 2 ? 4.0 : n == 3 ? 6.5 : 7.5;
            DeviceType r = make("FQG_RCBO_" + pn, K::Modular, n, n, 0.0, 63.0, "Magnetotermico differenziale fino a 63 A " + pn);
            r.residual = true;
            modular(r, rm, [n, rm](Pen& p) { drawResidual(p, n, rm); });
            const double rm2 = n <= 2 ? 6.0 : n == 3 ? 9.0 : 11.0;
            DeviceType r2 = make("FQG_RCBO125_" + pn, K::Modular, n, n, 64.0, 125.0,
                                 "Magnetotermico differenziale 80-125 A " + pn);
            r2.residual = true;
            modular(r2, rm2, [n, rm2](Pen& p) { drawResidual(p, n, rm2); });
            modular(make("FQG_IMS_" + pn, K::Switch, n, n, 0.0, 125.0, "Interruttore di manovra-sezionatore modulare " + pn), n,
                    [n](Pen& p) { drawSwitch(p, n, 1.0); });
            modular(make("FQG_IMS250_" + pn, K::Switch, n, n, 126.0, 250.0, "Sezionatore modulare 125-250 A " + pn), n * 2.0,
                    [n](Pen& p) { drawSwitch(p, n, 2.0); });
            modular(make("FQG_FUSE_" + pn, K::Fuse, n, n, 0.0, 32.0, "Portafusibili sezionatore fino a 32 A " + pn), n,
                    [n](Pen& p) { drawFuse(p, n, 1.0); });
            modular(make("FQG_FUSE125_" + pn, K::Fuse, n, n, 33.0, 125.0, "Portafusibili sezionatore fino a 125 A " + pn),
                    n * 1.5, [n](Pen& p) { drawFuse(p, n, 1.5); });
            modular(make("FQG_SPD_" + pn, K::Spd, n, n, 0.0, 1e9, "Limitatore di sovratensione " + pn), n,
                    [n](Pen& p) { drawSpd(p, n); });
        }
        modular(make("FQG_METER", K::Meter, 0, 99, 0.0, 1e9, "Strumento di misura modulare"), 4.0,
                [](Pen& p) { drawMeter(p, 4.0); });
        modular(make("FQG_SUPPLY", K::Supply, 0, 99, 0.0, 1e9, "Alimentatore modulare"), 4.0,
                [](Pen& p) { drawSupply(p, 4.0); });
        DeviceType relay = make("FQG_RELAY", K::Modular, 0, 99, 0.0, 1e9, "Relè differenziale su guida");
        relay.plate = DeviceType::Plate::Accessory;
        relay.residual = true;
        modular(relay, 2.0, [](Pen& p) { drawRelay(p, 2.0); });
        DeviceType aux = make("FQG_AUX", K::Modular, 0, 99, 0.0, 1e9, "Accessorio su guida");
        aux.plate = DeviceType::Plate::Accessory;
        modular(aux, 2.0, [](Pen& p) { drawAux(p, 2.0); });
        DeviceType term = make("FQG_TERM", K::Modular, 0, 99, 0.0, 1e9, "Morsettiera");
        term.plate = DeviceType::Plate::Terminal;
        term.width = 6.0 * kMod;
        term.height = kModH;
        add(term, [](Pen& p) { drawTerminals(p); });

        // Scatolati, sezionatori scatolati e aperti: tre e quattro poli.
        for (std::size_t k = 0; k < std::size(kFrames); ++k) {
            const Frame& f = kFrames[k];
            for (const int poles : {3, 4}) {
                const std::string tag = num(f.to) + "_" + std::to_string(poles) + "P";
                const int pMin = poles == 3 ? 1 : 4, pMax = poles;
                for (const char v : {'N', 'R', 'S'}) {
                    if (v == 'R' && f.to > 630.0)
                        continue;
                    const K kind = v == 'S' ? K::Switch : K::MouldedCase;
                    const std::string what = v == 'S' ? "Sezionatore scatolato" : v == 'R' ? "Scatolato con differenziale"
                                                                                           : "Scatolato";
                    DeviceType t = make(std::string("FQG_") + (v == 'S' ? "SS_" : v == 'R' ? "SCD_" : "SC_") + tag, kind,
                                        pMin, pMax, f.from, f.to,
                                        what + " " + num(f.from) + "-" + num(f.to) + " A " + std::to_string(poles)
                                            + "P su piastra");
                    t.residual = v == 'R';
                    t.plate = DeviceType::Plate::Device;
                    t.width = kPlateW;
                    t.height = plateHeight(f.h, v == 'R');
                    add(t, [f, poles, v](Pen& p) { drawMoulded(p, f, poles, v); });
                }
            }
        }
        for (const Frame& f : kAirFrames)
            for (const int poles : {3, 4}) {
                DeviceType t = make("FQG_ACB_" + num(f.to) + "_" + std::to_string(poles) + "P", K::AirCircuit,
                                    poles == 3 ? 1 : 4, poles, f.from, f.to,
                                    "Aperto estraibile " + num(f.from) + "-" + num(f.to) + " A " + std::to_string(poles) + "P");
                t.width = poles == 4 ? f.w4 : f.w3;
                t.height = f.h;
                add(t, [f, poles](Pen& p) { drawAir(p, f, poles); });
            }

        // Piastre (colonne da 600 e 800 mm) e sistemi di sbarre.
        for (const double w : {kPlateW, 700.0}) {
            DeviceType din = make("FQG_DIN_" + num(w), K::Modular, 0, 99, 0.0, 1e9, "Piastra modulare " + num(w) + " mm");
            din.plate = DeviceType::Plate::Din;
            din.width = w;
            din.height = 150.0;
            add(din, [w](Pen& p) { drawDinPlate(p, w); });
            for (double h = 50.0; h <= 800.0 + 1e-6; h += 50.0) {
                DeviceType blank = make("FQG_BLANK_" + num(w) + "_" + num(h), K::Modular, 0, 99, 0.0, 1e9,
                                        "Piastra cieca " + num(w) + "x" + num(h) + " mm");
                blank.plate = DeviceType::Plate::Blank;
                blank.width = w;
                blank.height = h;
                add(blank, [w, h](Pen& p) { drawBlankPlate(p, w, h); });
            }
        }
        for (const auto& [to, w] : {std::pair{1600.0, 100.0}, std::pair{6300.0, 160.0}}) {
            DeviceType bus = make("FQG_BUS_" + num(to), K::Modular, 0, 99, to > 1600.0 ? 1601.0 : 0.0, to,
                                  "Sistema di sbarre verticali fino a " + num(to) + " A");
            bus.plate = DeviceType::Plate::Busbar;
            bus.width = w;
            bus.height = 1650.0;
            const double bw = w;
            add(bus, [bw](Pen& p) { drawBusbars(p, bw); });
        }
        return out;
    }();
    return types;
}

} // namespace

std::vector<DeviceType> genericDeviceTypes()
{
    return table();
}

std::string genericEnclosureBlock(double width, double height)
{
    return "FQG_ENC_" + num(width) + "x" + num(height);
}

bool addGenericBlock(Document& doc, const std::string& name)
{
    if (name.rfind("FQG_", 0) != 0)
        return false;
    if (doc.findBlock(name))
        return true;
    std::function<void(Pen&)> draw;
    double w = 0.0, h = 0.0;
    if (std::sscanf(name.c_str(), "FQG_ENC_%lfx%lf", &w, &h) == 2 && w > 0.0 && h > 0.0) {
        draw = [w, h](Pen& p) { drawEnclosure(p, w, h); };
    } else {
        table();   // riempie il registro
        const auto it = registry().find(name);
        if (it == registry().end())
            return false;
        draw = it->second.draw;
    }
    auto block = std::make_shared<BlockDef>();
    block->name = name;
    Pen pen{block->entities};
    draw(pen);
    doc.addBlock(block);
    return true;
}

} // namespace cartesio
