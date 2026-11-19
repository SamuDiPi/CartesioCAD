// Tavola di CanaloCAD disegnata nel disegno (A3 orizzontale): sezione in scala con i cavi numerati, risultati dei
// calcoli, tabella dei cavi, distinta e cartiglio. Le misure del foglio sono in millimetri di carta, moltiplicati per
// SheetOptions::unit (unità di disegno per millimetro di carta).
#include "core/Canalo.h"

#include "core/Entity.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cartesio::canalo {

namespace {

std::string num(double v, int decimals = 1)
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

} // namespace

std::vector<std::unique_ptr<Entity>> drawSheet(const Tray& tray, const Metrics& m, const SheetOptions& o)
{
    std::vector<std::unique_ptr<Entity>> out;
    const double u = o.unit;
    auto P = [&](double x, double y) { return Vec2{o.origin.x + x * u, o.origin.y + y * u}; };
    auto keep = [&](std::unique_ptr<Entity> e, int color = -2) {
        e->setLayer(o.layer);
        if (color != -2)
            e->setColor(color);
        out.push_back(std::move(e));
    };
    auto line = [&](double x1, double y1, double x2, double y2) { keep(std::make_unique<LineEntity>(P(x1, y1), P(x2, y2))); };
    auto rect = [&](double x1, double y1, double x2, double y2) {
        keep(std::make_unique<PolylineEntity>(std::vector<PolyVertex>{{P(x1, y1)}, {P(x2, y1)}, {P(x2, y2)}, {P(x1, y2)}}, true));
    };
    auto text = [&](double x, double y, double h, const std::string& s, int color = -2) {
        keep(std::make_unique<MTextEntity>(P(x, y), h * u, s), color);
    };

    // Foglio e cornice.
    rect(0, 0, 420, 297);
    rect(10, 10, 410, 287);
    text(20, 280, 5.0, o.title.empty() ? "SEZIONE CANALINA " + num(tray.width, 0) + "x" + num(tray.height, 0) : o.title);

    // Sezione in scala: la più grande che sta in 170 × 110 mm.
    double s = 1.0;
    for (double k : {2.0, 1.0, 0.5, 0.4, 0.25, 0.2, 0.1, 0.05})
        if (tray.width * k <= 170.0 && (tray.height + 10.0) * k <= 110.0) {
            s = k;
            break;
        }
    const double W = tray.width * s, H = tray.height * s;
    const double x0 = 20.0 + (180.0 - W) / 2.0, y0 = 150.0;
    auto S = [&](double x, double y) { return P(x0 + x * s, y0 + y * s); };
    keep(std::make_unique<PolylineEntity>(std::vector<PolyVertex>{{S(0, tray.height)}, {S(0, 0)}, {S(tray.width, 0)}, {S(tray.width, tray.height)}}, false));
    if (tray.cover != "none")
        line(x0 - 2.0, y0 + H + 2.0, x0 + W + 2.0, y0 + H + 2.0);
    Tray copy = tray;
    for (double x : septumX(copy))
        keep(std::make_unique<LineEntity>(S(x, 0), S(x, tray.height)));
    for (std::size_t i = 0; i < tray.cables.size(); ++i) {
        const Cable& c = tray.cables[i];
        const int color = c.sys == "BT_FIRE" ? 1 : c.sys == "SPECIAL_ELV" ? 5 : -2;
        keep(std::make_unique<CircleEntity>(S(c.x, c.y), c.d / 2.0 * s * u), color);
        const double h = std::min(2.5, c.d * s * 0.5);
        if (h >= 0.8) {
            auto t = std::make_unique<TextEntity>(S(c.x, c.y), h * u, std::to_string(i + 1));
            t->hAlign = TextEntity::Middle;
            t->alignPoint = t->position;
            keep(std::move(t));
        }
    }
    const double scale = 1.0 / s;
    text(x0, y0 - 4.0, 2.5, "L = " + num(tray.width, 0) + " mm   H = " + num(tray.height, 0) + " mm   scala " +
                                (s >= 1.0 ? num(s, 1) + ":1" : "1:" + num(scale, 0)));

    // Risultati.
    std::string r = "RISULTATI\\P";
    r += "Riempimento " + num(m.fillPct) + "% (riserva " + num(m.sparePct) + "%)\\P";
    r += "Coefficiente di riduzione Kr = " + num(m.kr, 2) + " (" + std::to_string(m.nLayers) + " strati)\\P";
    r += std::string("Segregazione CEI 64-8 art. 528.1: ") + (m.segConform ? "CONFORME" : "NON CONFORME") + "\\P";
    r += "Peso " + num(m.totalKgM, 2) + " kg/m (canalina " + num(m.trayKgM, 2) + ")\\P";
    if (tray.support != "none") {
        const Support* sp = support(tray.support);
        r += "Staffaggio " + (sp ? sp->name : tray.support) + " passo " + num(tray.spanM, 2) + " m: " + num(m.pointLoad) + " su " +
             num(m.maxPoint, 0) + " kg (" + num(m.loadPct) + "%), freccia " + num(m.deflectionMm, 2) + " / " + num(m.maxDeflectionMm) + " mm\\P";
    }
    r += "Tiro di posa " + num(m.pullDaN) + " daN (max " + num(m.maxPullDaN, 0) + "), raggio minimo " + num(m.maxBendR, 0) + " mm\\P";
    r += "Valori indicativi: verificare con le norme e i dati dei costruttori.";
    text(20, 128, 2.5, r, m.segConform && (tray.support == "none" || m.loadPct <= 100.0) ? -2 : 1);

    // Tabella dei cavi (in alto a destra).
    const double rowH = tray.cables.size() > 18 ? 4.0 : 5.0;
    const double th = rowH > 4.5 ? 2.2 : 1.8;
    auto cables = std::make_unique<TableEntity>();
    cables->position = P(215, 270);
    cables->textHeight = th * u;
    cables->resize(static_cast<int>(tray.cables.size()) + 1, 6, 20.0 * u, rowH * u);
    const double cw[] = {8, 50, 34, 30, 14, 18};
    for (int c = 0; c < 6; ++c)
        cables->columnWidths[static_cast<std::size_t>(c)] = cw[c] * u;
    const char* head[] = {"N.", "Sigla", "Tipo", "Formazione", "Ø mm", "kg/m"};
    for (int c = 0; c < 6; ++c)
        cables->cell(0, c) = head[c];
    for (std::size_t i = 0; i < tray.cables.size(); ++i) {
        const Cable& c = tray.cables[i];
        const int row = static_cast<int>(i) + 1;
        cables->cell(row, 0) = std::to_string(i + 1);
        cables->cell(row, 1) = c.code;
        cables->cell(row, 2) = c.type;
        cables->cell(row, 3) = c.form;
        cables->cell(row, 4) = num(c.d);
        cables->cell(row, 5) = num(c.w, 2);
    }
    keep(std::move(cables));
    text(215, 276, 3.0, "CAVI");

    // Distinta (sotto la tabella dei cavi).
    const double bomTop = 270.0 - (static_cast<double>(tray.cables.size()) + 1.0) * rowH - 12.0;
    auto bom = std::make_unique<TableEntity>();
    bom->position = P(215, bomTop);
    bom->textHeight = 1.8 * u;
    bom->resize(static_cast<int>(m.bom.size()) + 1, 4, 20.0 * u, 4.5 * u);
    const double bw[] = {34, 120, 12, 8};
    for (int c = 0; c < 4; ++c)
        bom->columnWidths[static_cast<std::size_t>(c)] = bw[c] * u;
    const char* bomHead[] = {"Codice", "Descrizione", "Q.tà", "UM"};
    for (int c = 0; c < 4; ++c)
        bom->cell(0, c) = bomHead[c];
    for (std::size_t i = 0; i < m.bom.size(); ++i) {
        const int row = static_cast<int>(i) + 1;
        bom->cell(row, 0) = m.bom[i].code;
        bom->cell(row, 1) = m.bom[i].desc;
        bom->cell(row, 2) = num(m.bom[i].qty, 0);
        bom->cell(row, 3) = m.bom[i].um;
    }
    keep(std::move(bom));
    text(215, bomTop + 6.0, 3.0, "DISTINTA (codici indicativi, non dei costruttori)");

    // Cartiglio.
    rect(250, 10, 410, 48);
    line(250, 29, 410, 29);
    line(330, 10, 330, 29);
    text(253, 46, 2.0, "TITOLO");
    text(253, 41, 3.5, o.title.empty() ? "Sezione canalina " + num(tray.width, 0) + "x" + num(tray.height, 0) : o.title);
    text(253, 27, 2.0, "PROGETTO");
    text(253, 22, 2.5, o.project);
    text(333, 27, 2.0, "DATA / SCALA");
    text(333, 22, 2.5, o.date + "   sezione " + (s >= 1.0 ? num(s, 1) + ":1" : "1:" + num(scale, 0)));
    text(253, 15, 2.0, "CartesioCAD - calcoli CanaloCAD");
    return out;
}

} // namespace cartesio::canalo
