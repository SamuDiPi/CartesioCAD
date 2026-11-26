// Test del nucleo di CartesioCAD. Nessuna dipendenza esterna: basta compilare ed eseguire.
#include "core/Document.h"
#include "core/Geometry.h"
#include "core/InputParser.h"
#include "core/HatchBoundary.h"
#include "core/Import.h"
#include "core/Attributes.h"
#include "core/FindReplace.h"
#include "core/QuickSelect.h"
#include "core/Osnap.h"
#include "core/Plot.h"
#include "core/Version.h"
#include "core/Routing.h"
#include "core/RunImport.h"
#include "core/SingleLine.h"
#include "core/CircuitTags.h"
#include "core/PointLegend.h"
#include "core/TitleBlock.h"
#include "core/Canalo.h"
#include "core/Plant.h"
#include "core/Ampacity.h"
#include "core/DataExport.h"
#include "core/commands/CommandManager.h"
#include "core/io/Dxf.h"

#include <zlib.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cartesio;

namespace {

int failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

bool approx(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
bool approx(Vec2 a, Vec2 b, double eps = 1e-9) { return approx(a.x, b.x, eps) && approx(a.y, b.y, eps); }

void testInputParser()
{
    auto p = parseInput("10,20", std::nullopt);
    CHECK(p && p->kind == ParsedInput::Kind::Point && approx(p->point, {10, 20}));

    p = parseInput("@5,-3", Vec2{10, 20});
    CHECK(p && p->kind == ParsedInput::Kind::Point && approx(p->point, {15, 17}));

    p = parseInput("@10<90", Vec2{1, 1});
    CHECK(p && p->kind == ParsedInput::Kind::Point && approx(p->point, {1, 11}));

    p = parseInput("  2.5 ", std::nullopt);
    CHECK(p && p->kind == ParsedInput::Kind::Value && approx(p->value, 2.5));

    p = parseInput("_line", std::nullopt);
    CHECK(p && p->kind == ParsedInput::Kind::Word && p->word == "LINE");

    p = parseInput("", std::nullopt);
    CHECK(p && p->kind == ParsedInput::Kind::Empty);

    CHECK(!parseInput("1,2,3", std::nullopt));
    CHECK(!parseInput("10,abc", std::nullopt));
}

void testUndoRedo()
{
    Document doc;
    const EntityId a = doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    doc.add(std::make_unique<CircleEntity>(Vec2{0, 0}, 5));
    CHECK(doc.entities().size() == 2);

    doc.erase({a});
    CHECK(doc.entities().size() == 1);
    CHECK(doc.find(a) == nullptr);

    CHECK(doc.undo());
    CHECK(doc.entities().size() == 2);
    CHECK(doc.find(a) != nullptr);

    CHECK(doc.undo());
    CHECK(doc.undo());
    CHECK(doc.entities().empty());
    CHECK(!doc.undo());

    CHECK(doc.redo());
    CHECK(doc.redo());
    CHECK(doc.redo());
    CHECK(doc.entities().size() == 1);
    CHECK(!doc.redo());
}

void testPick()
{
    Document doc;
    const EntityId line = doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    const Entity* e = doc.pick({5, 0.2}, 0.5);
    CHECK(e && e->id() == line);
    CHECK(doc.pick({5, 2}, 0.5) == nullptr);
}

void testLineCommand()
{
    Document doc;
    std::vector<EntityId> selection;
    CommandContext ctx{doc, selection};
    CommandManager commands;
    registerBuiltinCommands(commands);

    CHECK(commands.execute("l", ctx));
    commands.point(ctx, {0, 0});
    commands.point(ctx, {10, 0});
    ctx.cursor = {10, 50};
    commands.value(ctx, 10);           // 10 unità verso l'alto
    commands.keyword(ctx, "C");        // chiude il triangolo
    CHECK(!commands.isActive());
    CHECK(doc.entities().size() == 3);

    const auto& second = static_cast<const LineEntity&>(*doc.entities()[1]);
    CHECK(approx(second.end, {10, 10}));
}

void testCircleAndErase()
{
    Document doc;
    std::vector<EntityId> selection;
    CommandContext ctx{doc, selection};
    CommandManager commands;
    registerBuiltinCommands(commands);

    commands.execute("CIRCLE", ctx);
    commands.point(ctx, {0, 0});
    commands.value(ctx, 5);
    CHECK(!commands.isActive());
    CHECK(doc.entities().size() == 1);

    commands.execute("E", ctx);
    commands.point(ctx, {5, 0});       // sul bordo del cerchio
    commands.enter(ctx);
    CHECK(doc.entities().empty());
    CHECK(selection.empty());

    commands.execute("U", ctx);
    CHECK(doc.entities().size() == 1);
}

void testDxfRoundTrip()
{
    Document doc;
    const int red = doc.addLayer(Layer{"Muri", 1, true});
    doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100.5, -20}));
    doc.setCurrentLayer(red);
    doc.add(std::make_unique<CircleEntity>(Vec2{3, 4}, 12.25));

    const auto path = std::filesystem::temp_directory_path() / "cartesio_test.dxf";
    std::string error;
    CHECK(writeDxf(doc, path, &error));

    Document loaded;
    const DxfReadResult result = readDxf(path, loaded);
    std::filesystem::remove(path);

    CHECK(result.ok);
    CHECK(result.imported == 2);
    CHECK(result.skippedTypes.empty());
    CHECK(loaded.findLayer("Muri") >= 0);
    CHECK(loaded.entities().size() == 2);
    if (loaded.entities().size() == 2) {
        const auto& line = static_cast<const LineEntity&>(*loaded.entities()[0]);
        CHECK(approx(line.end, {100.5, -20}));
        const auto& circle = static_cast<const CircleEntity&>(*loaded.entities()[1]);
        CHECK(approx(circle.radius, 12.25));
        CHECK(loaded.layers()[static_cast<std::size_t>(circle.layer())].name == "Muri");
    }
}

struct Fixture {
    Document doc;
    std::vector<EntityId> selection;
    CommandContext ctx{doc, selection};
    CommandManager commands;

    Fixture() { registerBuiltinCommands(commands); }

    template <typename T>
    const T& at(std::size_t i) const { return static_cast<const T&>(*doc.entities().at(i)); }
};

void testIntersections()
{
    const LineEntity h({0, 0}, {10, 0});
    const LineEntity v({5, -5}, {5, 5});
    auto pts = intersections(h, v);
    CHECK(pts.size() == 1 && approx(pts[0], {5, 0}));

    const CircleEntity c({0, 0}, 5);
    const LineEntity through({-10, 0}, {10, 0});
    CHECK(intersections(c, through).size() == 2);

    const ArcEntity upper({0, 0}, 5, 0.0, kPi);    // semicerchio superiore
    const LineEntity vertical({0, -10}, {0, 10});
    pts = intersections(upper, vertical);
    CHECK(pts.size() == 1 && approx(pts[0], {0, 5}));

    const CircleEntity c2({8, 0}, 5);
    CHECK(intersections(c, c2).size() == 2);
}

void testArcThroughPoints()
{
    auto arc = arcThroughPoints({1, 0}, {0, 1}, {-1, 0});
    CHECK(arc != nullptr);
    if (arc) {
        CHECK(approx(arc->center, {0, 0}));
        CHECK(approx(arc->radius, 1));
        CHECK(approx(arc->sweep(), kPi));
        CHECK(arc->containsAngle(kPi / 2));
    }
    // Stessi punti in senso orario: deve passare comunque per (0,-1).
    arc = arcThroughPoints({-1, 0}, {0, -1}, {1, 0});
    CHECK(arc && arc->containsAngle(3 * kPi / 2) && !arc->containsAngle(kPi / 2));

    CHECK(arcThroughPoints({0, 0}, {1, 1}, {2, 2}) == nullptr);
}

void testWindowSelection()
{
    Document doc;
    const EntityId line = doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    const EntityId circle = doc.add(std::make_unique<CircleEntity>(Vec2{20, 0}, 2));

    BBox window;
    window.expand({-1, -1});
    window.expand({11, 1});
    auto ids = doc.selectInRect(window, false);
    CHECK(ids.size() == 1 && ids[0] == line);

    BBox crossing;
    crossing.expand({5, -1});
    crossing.expand({19, 1});
    CHECK(doc.selectInRect(crossing, false).empty());
    ids = doc.selectInRect(crossing, true);
    CHECK(ids.size() == 2);

    // Rettangolo interamente dentro il cerchio: non lo tocca.
    BBox innerBox;
    innerBox.expand({19.5, -0.5});
    innerBox.expand({20.5, 0.5});
    CHECK(doc.selectInRect(innerBox, true).empty());
    (void)circle;
}

void testSelectionPicker()
{
    Fixture f;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 5}, Vec2{10, 5}));

    SelectionPicker picker;
    CHECK(picker.click(f.ctx, {-1, -1}) == -1);         // apre una finestra
    CHECK(picker.corner().has_value());
    CHECK(picker.click(f.ctx, {11, 6}) == 2);           // la chiude
    CHECK(f.selection.size() == 2);

    f.ctx.shiftPressed = true;                           // Maiusc+clic toglie
    CHECK(picker.click(f.ctx, {5, 0}) == 1);
    CHECK(f.selection.size() == 1);
}

// Selezione: oggetti sovrapposti (ciclo), lazo, opzioni Tutto/Ultimo/Precedente/Rimuovi,
// isolamento, maniglie multifunzione, ADDSELECTED, COPYBASE, MEASUREGEOM.
void testSelectionUx()
{
    {   // pickAll: stesso ordine di pick; il suggerimento del ciclo vince sul più vicino
        Fixture f;
        const EntityId a = f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        const EntityId b = f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0.1}, Vec2{10, 0.1}));
        const auto all = f.doc.pickAll({5, 0.05}, 0.5);
        CHECK(all.size() == 2);
        CHECK(!all.empty() && all.front() == f.doc.pick({5, 0.05}, 0.5));
        f.ctx.pickTolerance = 0.5;
        f.ctx.pickHint = all.size() == 2 ? all[1]->id() : 0;
        SelectionPicker picker;
        CHECK(picker.click(f.ctx, {5, 0.05}) == 1);
        CHECK(f.selection.size() == 1 && f.selection.front() == f.ctx.pickHint);
        CHECK((a == f.ctx.pickHint) != (b == f.ctx.pickHint));
    }
    {   // lazo: finestra (interamente dentro) e intersezione (anche toccati)
        Fixture f;
        const EntityId in = f.doc.add(std::make_unique<LineEntity>(Vec2{1, 1}, Vec2{2, 2}));
        const EntityId half = f.doc.add(std::make_unique<LineEntity>(Vec2{2, 2}, Vec2{20, 2}));
        f.doc.add(std::make_unique<CircleEntity>(Vec2{50, 50}, 1.0));
        const std::vector<Vec2> tri{{0, 0}, {10, 0}, {0, 10}};
        auto ids = f.doc.selectInPolygon(tri, false);
        CHECK(ids.size() == 1 && ids.front() == in);
        ids = f.doc.selectInPolygon(tri, true);
        CHECK(ids.size() == 2 && std::find(ids.begin(), ids.end(), half) != ids.end());
        // Linea dentro il riquadro del lazo ma fuori dal triangolo: non va presa.
        f.doc.add(std::make_unique<LineEntity>(Vec2{8, 8}, Vec2{9, 9}));
        CHECK(f.doc.selectInPolygon(tri, true).size() == 2);
    }
    {   // opzioni della selezione nei comandi
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 5}, Vec2{10, 5}));
        const EntityId last = f.doc.add(std::make_unique<CircleEntity>(Vec2{30, 30}, 2.0));
        f.commands.execute("ERASE", f.ctx);
        f.commands.keyword(f.ctx, "T");
        CHECK(f.selection.size() == 3);
        f.commands.keyword(f.ctx, "R");
        f.commands.keyword(f.ctx, "U");             // toglie l'ultimo
        CHECK(f.selection.size() == 2);
        f.commands.cancel(f.ctx);
        f.commands.execute("MOVE", f.ctx);
        f.commands.keyword(f.ctx, "U");
        CHECK(f.selection.size() == 1 && f.selection.front() == last);
        f.commands.enter(f.ctx);
        f.commands.point(f.ctx, {0, 0});
        f.commands.point(f.ctx, {1, 0});
        CHECK(!f.commands.isActive());
        f.selection.clear();
        f.commands.execute("SELECTPREVIOUS", f.ctx);   // la selezione dell'ultimo comando
        CHECK(f.selection.size() == 1 && f.selection.front() == last);
    }
    {   // isolamento: nascosti non si vedono, non si selezionano; non cambia la revisione (niente da salvare)
        Fixture f;
        const EntityId a = f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        const EntityId b = f.doc.add(std::make_unique<LineEntity>(Vec2{0, 5}, Vec2{10, 5}));
        const auto revision = f.doc.revision();
        f.selection = {a};
        f.commands.execute("ISOLATEOBJECTS", f.ctx);
        CHECK(f.doc.isHidden(b) && !f.doc.isHidden(a));
        CHECK(f.doc.pick({5, 5}, 0.5) == nullptr);
        CHECK(f.doc.entitiesNear({5, 5}, 0.5).empty());
        CHECK(f.doc.revision() == revision);
        f.commands.execute("UNISOLATEOBJECTS", f.ctx);
        CHECK(f.doc.hiddenCount() == 0 && f.doc.pick({5, 5}, 0.5) != nullptr);
        f.selection = {b};
        f.commands.execute("HIDEOBJECTS", f.ctx);
        CHECK(f.doc.isHidden(b) && f.selection.empty());
    }
    {   // maniglie multifunzione
        PolylineEntity p({{{0, 0}}, {{10, 0}}, {{10, 10}}}, false);
        CHECK(gripActions(p, 0).size() == 3);                       // stira, aggiungi, rimuovi
        auto added = gripApply(p, 3, GripAction::AddVertex, {5, -2});   // punto medio del primo tratto
        CHECK(added && static_cast<PolylineEntity&>(*added).vertices.size() == 4);
        CHECK(added && approx(static_cast<PolylineEntity&>(*added).vertices[1].point, {5, -2}));
        auto removed = gripApply(p, 1, GripAction::RemoveVertex, {});
        CHECK(removed && static_cast<PolylineEntity&>(*removed).vertices.size() == 2);
        auto arc = gripApply(p, 3, GripAction::ToArc, {5, -5});        // semicerchio sotto il primo tratto
        CHECK(arc && std::abs(std::abs(static_cast<PolylineEntity&>(*arc).vertices[0].bulge) - 1.0) < 1e-9);
        CHECK(arc && static_cast<PolylineEntity&>(*arc).vertices[0].bulge > 0);   // a destra = antiorario
        CHECK(arc && gripActions(*arc, 3).back() == GripAction::ToLine);
        LineEntity l({0, 0}, {10, 0});
        auto longer = gripApply(l, 1, GripAction::Lengthen, {15, 3});    // resta sulla stessa direzione
        CHECK(longer && approx(static_cast<LineEntity&>(*longer).end, {15, 0}));
    }
    {   // ADDSELECTED: il comando di creazione con le proprietà dell'oggetto, poi si torna a quelle di prima
        Fixture f;
        const int red = f.doc.addLayer(Layer{"ROSSO"});
        auto c = std::make_unique<CircleEntity>(Vec2{0, 0}, 5.0);
        c->setLayer(red);
        c->setColor(1);
        f.selection = {f.doc.add(std::move(c))};
        f.commands.execute("ADDSELECTED", f.ctx);
        CHECK(f.ctx.followUp.size() == 1 && f.ctx.followUp.front() == "CIRCLE");
        CHECK(f.doc.currentLayer() == red && f.doc.currentColor() == 1);
        CHECK(f.ctx.afterFollowUp != nullptr);
        if (f.ctx.afterFollowUp)
            f.ctx.afterFollowUp();
        CHECK(f.doc.currentLayer() != red && f.doc.currentColor() == kColorByLayer);
    }
    {   // COPYBASE: incollando, il punto base va dove si clicca
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{10, 10}, Vec2{20, 10}));
        f.commands.execute("COPYBASE", f.ctx);
        f.commands.point(f.ctx, {15, 10});      // selezione
        f.commands.enter(f.ctx);
        f.commands.point(f.ctx, {20, 10});      // punto base
        CHECK(!f.commands.isActive());
        f.commands.execute("PASTECLIP", f.ctx);
        f.commands.point(f.ctx, {0, 0});
        CHECK(f.doc.entities().size() == 2);
        CHECK(f.doc.entities().size() == 2 && approx(f.at<LineEntity>(1).end, {0, 0}));
    }
    {   // MEASUREGEOM: rapido di default, Distanza passa per DIST e torna al rapido
        Fixture f;
        f.commands.execute("MEASUREGEOM", f.ctx);
        CHECK(f.commands.active() && f.commands.active()->quickMeasure());
        f.commands.keyword(f.ctx, "D");
        CHECK(f.commands.active() && !f.commands.active()->quickMeasure());
        f.commands.point(f.ctx, {0, 0});
        f.commands.point(f.ctx, {3, 4});
        CHECK(f.commands.active() && f.commands.active()->quickMeasure());
        f.commands.enter(f.ctx);
        CHECK(!f.commands.isActive());
    }
}

// Testo dei MTEXT senza codici, direttrici selezionabili dal testo, Taglia / Incolla come blocco / sulle
// coordinate originali.
void testTextAndClipboardUx()
{
    {   // testo semplice e formattazione d'insieme conservata
        const std::string original = "{\\W0.7;COLONNA MONTANTE\\P-N.1 CAVO}";
        CHECK(mtextPlainText(original) == "COLONNA MONTANTE\n-N.1 CAVO");
        CHECK(mtextWithPlainText(original, "UNA\nDUE") == "{\\W0.7;UNA\\PDUE}");
        CHECK(mtextWithPlainText("\\A1;testo", "nuovo") == "\\A1;nuovo");
        CHECK(mtextWithPlainText("semplice", "a{b}") == "a\\{b\\}");
        // Gruppo che non chiude in fondo: niente parentesi aggiunte.
        CHECK(mtextWithPlainText("{\\C1;rosso} nero", "x") == "x");
    }
    {   // multidirettrice: un clic sul testo prende la direttrice
        Fixture f;
        auto leader = std::make_unique<LeaderEntity>();
        leader->points = {{0, 0}, {10, 10}, {15, 10}};
        leader->text = "TESTO DELLA DIRETTRICE";
        leader->textHeight = 2.0;
        const EntityId id = f.doc.add(std::move(leader));
        const Entity* hit = f.doc.pick({25, 10}, 0.2);   // in mezzo al testo, lontano dalle linee
        CHECK(hit && hit->id() == id);
    }
    {   // CUTCLIP, PASTEORIG, PASTEBLOCK
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{10, 10}, Vec2{20, 10}));
        f.commands.execute("CUTCLIP", f.ctx);
        f.commands.point(f.ctx, {15, 10});
        f.commands.enter(f.ctx);
        CHECK(f.doc.entities().empty());
        f.commands.execute("PASTEORIG", f.ctx);
        CHECK(f.doc.entities().size() == 1 && approx(f.at<LineEntity>(0).start, {10, 10}));
        f.commands.execute("PASTEBLOCK", f.ctx);
        f.commands.point(f.ctx, {100, 100});
        CHECK(f.doc.entities().size() == 2 && f.doc.entities().back()->type() == EntityType::Insert);
        if (f.doc.entities().size() == 2) {
            const BBox b = f.doc.entities().back()->bounds();   // il punto base (in basso a sinistra) va in 100,100
            CHECK(approx(b.min, {100, 100}) && approx(b.max, {110, 100}));
        }
    }
}

// SPLINE, quote (DIMTEDIT, DIMEDIT, maniglie), stili delle direttrici e di testo, AUDIT.
void testDraftingTools()
{
    {   // la spline passa per i punti e ha punti di controllo e nodi validi
        Fixture f;
        f.commands.execute("SPLINE", f.ctx);
        for (Vec2 p : {Vec2{0, 0}, Vec2{10, 5}, Vec2{20, -5}, Vec2{30, 0}})
            f.commands.point(f.ctx, p);
        f.commands.enter(f.ctx);
        CHECK(f.doc.entities().size() == 1);
        if (f.doc.entities().size() == 1) {
            const auto& s = f.at<SplineEntity>(0);
            CHECK(s.degree == 3 && s.control.size() == 4 && s.knots.size() == 8);
            const auto pts = s.evaluate();
            const auto passes = [&](Vec2 q) {
                double best = 1e9;
                for (Vec2 p : pts)
                    best = std::min(best, distance(p, q));
                return best < 0.5;
            };
            CHECK(passes({10, 5}) && passes({20, -5}));
            CHECK(!pts.empty() && approx(pts.front(), {0, 0}) && approx(pts.back(), {30, 0}));
        }
    }
    {   // quota: testo spostato e ruotato, linee oblique; si salvano nel DXF
        Document doc;
        auto d = std::make_unique<DimensionEntity>();
        d->kind = DimensionEntity::Linear;
        d->p1 = {0, 0};
        d->p2 = {40, 0};
        d->dimLine = {20, 10};
        const EntityId id = doc.add(std::move(d));
        const auto& dim = static_cast<const DimensionEntity&>(*doc.find(id));
        // Maniglia del testo (ultima): trascinandola il testo si sposta.
        const auto grips = dim.snapPoints();
        CHECK(grips.size() == 5 && grips.back().kind == SnapKind::Insertion);
        auto moved = gripEdit(dim, grips.size() - 1, {50, 20});
        CHECK(moved && static_cast<DimensionEntity&>(*moved).textMoved);
        CHECK(moved && distance(static_cast<DimensionEntity&>(*moved).textMidpoint(), {50, 20}) < 1.5);   // centro del riquadro del testo
        auto& m = static_cast<DimensionEntity&>(*moved);
        m.textRotation = degToRad(30.0);
        m.oblique = degToRad(60.0);
        doc.commit([&] { std::vector<std::unique_ptr<Entity>> v; v.push_back(std::move(moved)); return v; }(), {id});
        const auto path = std::filesystem::temp_directory_path() / "cartesio_dim.dxf";
        CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
        Document again;
        CHECK(readDxf(path, again).ok);
        std::filesystem::remove(path);
        const DimensionEntity* back = nullptr;
        for (const auto& e : again.entities())
            if (e->type() == EntityType::Dimension)
                back = static_cast<const DimensionEntity*>(e.get());
        CHECK(back && back->textMoved && approx(back->textPos, {50, 20}));
        CHECK(back && back->textRotation && std::abs(*back->textRotation - degToRad(30.0)) < 1e-6);
        CHECK(back && std::abs(back->oblique - degToRad(60.0)) < 1e-6);
        // Linee di estensione oblique: gli estremi della linea di quota si spostano lungo i 60 gradi.
        if (back) {
            const auto [q1, q2] = back->dimensionLineEnds();
            CHECK(std::abs(q1.y - 10.0) < 1e-6 && std::abs(q1.x - 10.0 / std::tan(degToRad(60.0))) < 1e-6);
        }
    }
    {   // DIMEDIT Obliqua e Nuovo; DIMTEDIT Origine
        Fixture f;
        auto d = std::make_unique<DimensionEntity>();
        d->kind = DimensionEntity::Aligned;
        d->p1 = {0, 0};
        d->p2 = {40, 0};
        d->dimLine = {20, 10};
        d->textMoved = true;
        d->textPos = {5, 30};
        const EntityId id = f.doc.add(std::move(d));
        f.commands.execute("DIMEDIT", f.ctx);
        f.commands.keyword(f.ctx, "N");
        f.commands.text(f.ctx, "<> mm");
        f.commands.point(f.ctx, {20, 10});
        f.commands.enter(f.ctx);
        CHECK(static_cast<const DimensionEntity&>(*f.doc.find(id)).textOverride == "<> mm");
        f.selection = {id};
        f.commands.execute("DIMTEDIT", f.ctx);
        f.commands.keyword(f.ctx, "O");
        CHECK(!static_cast<const DimensionEntity&>(*f.doc.find(id)).textMoved);
    }
    {   // stili delle direttrici: la direttrice nuova li usa, cambiandoli si aggiornano; si salvano
        Fixture f;
        f.doc.setMLeaderStyle({"Note", 5.0, 3.0, "Standard", 1.0});
        f.doc.setCurrentMLeaderStyle("Note");
        f.commands.execute("LEADER", f.ctx);
        f.commands.point(f.ctx, {0, 0});
        f.commands.point(f.ctx, {10, 10});
        f.commands.enter(f.ctx);
        f.commands.text(f.ctx, "NOTA");
        f.commands.enter(f.ctx);
        CHECK(f.doc.entities().size() == 1);
        if (f.doc.entities().size() == 1) {
            const auto& l = f.at<LeaderEntity>(0);
            CHECK(l.style == "Note" && approx(l.textHeight, 5.0) && approx(l.arrowSize, 3.0));
        }
        f.doc.setMLeaderStyle({"Note", 7.0, 3.0, "Standard", 1.0});
        CHECK(f.doc.restyleLeaders("Note") == 1);
        CHECK(f.doc.entities().size() == 1 && approx(f.at<LeaderEntity>(0).textHeight, 7.0));
        const auto path = std::filesystem::temp_directory_path() / "cartesio_mleader.dxf";
        CHECK(writeDxf(f.doc, path, nullptr, nullptr, DxfVersion::R2000));
        Document again;
        CHECK(readDxf(path, again).ok);
        std::filesystem::remove(path);
        const auto* style = again.findMLeaderStyle("Note");
        CHECK(style && approx(style->textHeight, 7.0) && again.currentMLeaderStyle() == "Note");
        const LeaderEntity* leader = nullptr;
        for (const auto& e : again.entities())
            if (e->type() == EntityType::Leader)
                leader = static_cast<const LeaderEntity*>(e.get());
        CHECK(leader && leader->style == "Note" && leader->text == "NOTA");
        std::string error;
        CHECK(!again.removeMLeaderStyle("Note", &error));   // corrente e usato
    }
    {   // stili di testo: rinomina (anche nei testi) ed eliminazione solo se non usati
        Document doc;
        TextStyle s;
        s.name = "Note";
        s.font = "romans.shx";
        doc.setTextStyle(s);
        auto t = std::make_unique<TextEntity>(Vec2{0, 0}, 2.5, "x");
        t->style = "Note";
        const EntityId id = doc.add(std::move(t));
        std::string error;
        CHECK(!doc.removeTextStyle("Note", &error));
        CHECK(doc.renameTextStyle("Note", "Appunti", &error));
        CHECK(static_cast<const TextEntity&>(*doc.find(id)).style == "Appunti");
        CHECK(doc.findTextStyle("Appunti") && !doc.findTextStyle("Note"));
        doc.erase({id});
        CHECK(doc.removeTextStyle("Appunti", &error));
    }
    {   // AUDIT: trova, e con la correzione sistema (annullabile)
        Document doc;
        doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        auto bad = std::make_unique<CircleEntity>(Vec2{0, 0}, 5.0);
        bad->radius = -1.0;
        doc.add(std::move(bad));
        auto lost = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 1});
        lost->setLayer(42);   // layer inesistente
        doc.add(std::move(lost));
        auto t = std::make_unique<TextEntity>(Vec2{0, 0}, 2.5, "x");
        t->style = "Manca";
        doc.add(std::move(t));
        auto report = doc.audit(false);
        CHECK(report.found == 3 && report.fixed == 0 && doc.entities().size() == 4);
        report = doc.audit(true);
        CHECK(report.found == 3 && report.fixed == 3);
        CHECK(doc.entities().size() == 3);
        CHECK(doc.audit(false).found == 0);
        CHECK(doc.undo());
        CHECK(doc.entities().size() == 4);
    }
}

void testMoveAndCopy()
{
    Fixture f;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));

    f.commands.execute("M", f.ctx);
    f.commands.point(f.ctx, {5, 0});            // selezione
    f.commands.enter(f.ctx);
    f.commands.point(f.ctx, {0, 0});            // punto base
    f.commands.point(f.ctx, {3, 4});            // secondo punto
    CHECK(!f.commands.isActive());
    CHECK(f.doc.entities().size() == 1);
    CHECK(approx(f.at<LineEntity>(0).start, {3, 4}));
    CHECK(approx(f.at<LineEntity>(0).end, {13, 4}));

    f.commands.execute("U", f.ctx);
    CHECK(approx(f.at<LineEntity>(0).start, {0, 0}));

    // COPY con preselezione e copie multiple; la seconda con distanza diretta.
    f.selection = {f.doc.entities()[0]->id()};
    f.commands.execute("COPY", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.point(f.ctx, {0, 10});
    f.ctx.cursor = {0, -100};
    f.commands.value(f.ctx, 20);
    f.commands.enter(f.ctx);
    CHECK(!f.commands.isActive());
    CHECK(f.doc.entities().size() == 3);
    CHECK(approx(f.at<LineEntity>(1).start, {0, 10}));
    CHECK(approx(f.at<LineEntity>(2).start, {0, -20}));
    CHECK(f.at<LineEntity>(1).id() != f.at<LineEntity>(0).id());
}

void testOffset()
{
    Fixture f;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    f.doc.add(std::make_unique<CircleEntity>(Vec2{50, 0}, 5));

    f.commands.execute("OFFSET", f.ctx);
    f.commands.value(f.ctx, 2);
    f.commands.point(f.ctx, {5, 0});     // la linea
    f.commands.point(f.ctx, {5, -7});    // verso il basso
    f.commands.point(f.ctx, {55, 0});    // il cerchio
    f.commands.point(f.ctx, {50, 1});    // verso l'interno
    f.commands.enter(f.ctx);
    CHECK(!f.commands.isActive());
    CHECK(f.doc.entities().size() == 4);
    CHECK(approx(f.at<LineEntity>(2).start, {0, -2}));
    CHECK(approx(f.at<CircleEntity>(3).radius, 3));

    // Invio alla prima domanda riusa l'ultima distanza.
    f.commands.execute("O", f.ctx);
    f.commands.enter(f.ctx);
    f.commands.point(f.ctx, {5, 0});
    f.commands.point(f.ctx, {5, 1});
    CHECK(approx(f.at<LineEntity>(4).start, {0, 2}));
}

void testTrim()
{
    Fixture f;
    // Croce: taglio il braccio destro dell'orizzontale.
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    f.doc.add(std::make_unique<LineEntity>(Vec2{5, -5}, Vec2{5, 5}));
    f.ctx.pickTolerance = 0.1;

    f.commands.execute("TR", f.ctx);
    f.commands.point(f.ctx, {8, 0});
    f.commands.enter(f.ctx);
    CHECK(f.doc.entities().size() == 2);
    const auto& trimmed = f.at<LineEntity>(1);
    CHECK(approx(trimmed.start, {0, 0}) && approx(trimmed.end, {5, 0}));

    f.commands.execute("U", f.ctx);
    CHECK(f.doc.entities().size() == 2);

    // Cerchio tagliato da un diametro: resta un semicerchio.
    Fixture g;
    g.doc.add(std::make_unique<CircleEntity>(Vec2{0, 0}, 5));
    g.doc.add(std::make_unique<LineEntity>(Vec2{-10, 0}, Vec2{10, 0}));
    g.ctx.pickTolerance = 0.1;
    g.commands.execute("TRIM", g.ctx);
    g.commands.point(g.ctx, {0, 5});        // la metà superiore
    CHECK(g.doc.entities().size() == 2);
    const auto& arc = g.at<ArcEntity>(1);
    CHECK(arc.type() == EntityType::Arc);
    CHECK(approx(arc.sweep(), kPi));
    CHECK(arc.containsAngle(3 * kPi / 2) && !arc.containsAngle(kPi / 2));

    // Taglio del tratto centrale di un arco attraversato da due linee.
    Fixture h;
    h.doc.add(std::make_unique<ArcEntity>(Vec2{0, 0}, 5, 0.0, kPi));
    h.doc.add(std::make_unique<LineEntity>(Vec2{-2, -10}, Vec2{-2, 10}));
    h.doc.add(std::make_unique<LineEntity>(Vec2{2, -10}, Vec2{2, 10}));
    h.ctx.pickTolerance = 0.1;
    h.commands.execute("TRIM", h.ctx);
    h.commands.point(h.ctx, {0, 5});
    CHECK(h.doc.entities().size() == 4);    // l'arco diventa due archi

    // Oggetto isolato: viene cancellato.
    Fixture k;
    k.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    k.commands.execute("TRIM", k.ctx);
    k.commands.point(k.ctx, {5, 0});
    CHECK(k.doc.entities().empty());
}

void testPolyline()
{
    // Quadrato 10x10 con il lato superiore sostituito da un semicerchio verso l'alto.
    PolylineEntity p({{{0, 0}, 0}, {{10, 0}, 0}, {{10, 10}, 1.0}, {{0, 10}, 0}}, true);
    const auto prims = p.prims();
    CHECK(prims.size() == 4);
    CHECK(prims[2].kind == Prim::Kind::Arc);
    CHECK(approx(prims[2].radius, 5.0));
    CHECK(approx(prims[2].center, {5, 10}));
    // L'arco da (10,10) a (0,10) in senso antiorario passa per (5,15).
    CHECK(approx(primDistance(prims[2], {5, 15}), 0.0, 1e-9));
    CHECK(approx(p.area(), 100.0 + kPi * 25.0 / 2.0, 1e-9));
    CHECK(approx(p.length(), 30.0 + kPi * 5.0, 1e-9));
}

void testPolylineTrim()
{
    Fixture f;
    // Rettangolo attraversato da una linea verticale: il lato destro sporge oltre la linea.
    f.doc.add(std::make_unique<PolylineEntity>(
        std::vector<PolyVertex>{{{0, 0}, 0}, {{20, 0}, 0}, {{20, 10}, 0}, {{0, 10}, 0}}, true));
    f.doc.add(std::make_unique<LineEntity>(Vec2{15, -5}, Vec2{15, 15}));
    f.ctx.pickTolerance = 0.1;
    f.commands.execute("TRIM", f.ctx);
    f.commands.point(f.ctx, {20, 5});        // il lato destro, oltre la linea
    CHECK(f.doc.entities().size() == 2);
    const auto& rest = f.at<PolylineEntity>(1);
    CHECK(!rest.closed);
    CHECK(rest.vertices.size() == 4);
    if (rest.vertices.size() == 4) {
        CHECK(approx(rest.vertices.front().point, {15, 10}));
        CHECK(approx(rest.vertices.back().point, {15, 0}));
    }
}

void testRectangleAndOffset()
{
    Fixture f;
    f.commands.execute("REC", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.point(f.ctx, {10, 5});
    CHECK(f.doc.entities().size() == 1);
    f.commands.execute("O", f.ctx);
    f.commands.value(f.ctx, 1);
    f.commands.point(f.ctx, {5, 0});
    f.commands.point(f.ctx, {5, -3});         // all'esterno
    f.commands.enter(f.ctx);
    CHECK(f.doc.entities().size() == 2);
    if (f.doc.entities().size() == 2) {
        const BBox b = f.doc.entities()[1]->bounds();
        CHECK(approx(b.min, {-1, -1}) && approx(b.max, {11, 6}));
    }
}

void testBlocksAndExplode()
{
    Fixture f;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0}));
    f.doc.add(std::make_unique<CircleEntity>(Vec2{0, 0}, 1));

    f.commands.execute("BLOCK", f.ctx);
    f.commands.text(f.ctx, "Simbolo");
    f.commands.point(f.ctx, {0, 0});
    f.commands.point(f.ctx, {-5, -5});        // finestra
    f.commands.point(f.ctx, {5, 5});
    f.commands.enter(f.ctx);
    CHECK(f.doc.findBlock("Simbolo") != nullptr);
    CHECK(f.doc.entities().size() == 1 && f.doc.entities()[0]->type() == EntityType::Insert);

    f.commands.execute("INSERT", f.ctx);
    f.commands.text(f.ctx, "simbolo");        // senza distinguere maiuscole
    f.commands.point(f.ctx, {10, 0});
    f.commands.value(f.ctx, 2);
    f.commands.value(f.ctx, 90);
    CHECK(f.doc.entities().size() == 2);
    const BBox b = f.doc.entities()[1]->bounds();
    CHECK(approx(b.min, {8, -2}, 1e-9) && approx(b.max, {12, 2}, 1e-9));

    f.selection = {f.doc.entities()[1]->id()};
    f.commands.execute("X", f.ctx);
    CHECK(f.doc.entities().size() == 3);
    const auto& line = f.at<LineEntity>(1);
    CHECK(approx(line.start, {10, 0}) && approx(line.end, {10, 2}));   // ruotata di 90° e scalata
}

void testDimension()
{
    DimensionEntity d;
    d.kind = DimensionEntity::Linear;
    d.p1 = {0, 0};
    d.p2 = {12.5, 3};
    d.dimLine = {0, 10};
    CHECK(d.displayText() == "12.5");
    const auto parts = d.geometry();
    int texts = 0, solids = 0;
    for (const auto& e : parts) {
        texts += e->type() == EntityType::Text;
        solids += e->type() == EntityType::Solid;
    }
    CHECK(texts == 1 && solids == 2);
    d.kind = DimensionEntity::Radius;
    d.dimLine = {0, 0};
    d.curvePoint = {0, 4};
    CHECK(d.displayText() == "R4");
}

// Contorni "textbox" (flag 8): buchi attorno ai testi. Quello fuori dal contorno vero non va disegnato
// (i "retini a caso" di una pianta di prova), quello dentro resta un buco.
void testHatchTextBoxes()
{
    const char* content =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nHATCH\n8\n0\n2\nSOLID\n70\n1\n91\n3\n"
        "92\n3\n72\n0\n73\n1\n93\n4\n10\n0\n20\n0\n10\n10\n20\n0\n10\n10\n20\n10\n10\n0\n20\n10\n97\n0\n"
        "92\n11\n72\n0\n73\n1\n93\n4\n10\n4\n20\n4\n10\n6\n20\n4\n10\n6\n20\n6\n10\n4\n20\n6\n97\n0\n"
        "92\n11\n72\n0\n73\n1\n93\n4\n10\n20\n20\n20\n10\n22\n20\n20\n10\n22\n20\n22\n10\n20\n20\n22\n97\n0\n"
        "75\n0\n76\n1\n98\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto path = std::filesystem::temp_directory_path() / "cartesio_textbox.dxf";
    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        std::fputs(content, f);
        std::fclose(f);
    }
    Document doc;
    const auto result = readDxf(path, doc);
    std::filesystem::remove(path);
    CHECK(result.ok && doc.entities().size() == 1);
    if (doc.entities().size() == 1) {
        const auto& h = static_cast<const HatchEntity&>(*doc.entities()[0]);
        CHECK(h.loops.size() == 2);
        CHECK(h.contains({1, 1}) && !h.contains({5, 5}) && !h.contains({21, 21}));
    }
}

// Stati dei layer: salva, ripristina (annullabile), DXF 2000 andata e ritorno. Ritaglio XCLIP: contorno nelle
// coordinate del blocco, ingombro limitato, SPATIAL_FILTER scritto e riletto.
void testLayerStatesAndXClip()
{
    Document doc;
    Layer muri;
    muri.name = "Muri";
    muri.color = 3;
    const int m = doc.addLayer(muri);
    doc.saveLayerState("Tutto acceso", "prima");
    doc.layer(m).visible = false;
    doc.layer(m).color = 1;
    doc.checkpoint();
    CHECK(doc.restoreLayerState("Tutto acceso") == 2);
    CHECK(doc.layers()[static_cast<std::size_t>(m)].visible && doc.layers()[static_cast<std::size_t>(m)].color == 3);
    doc.checkpoint();
    CHECK(doc.undo() && !doc.layers()[static_cast<std::size_t>(m)].visible);
    CHECK(doc.restoreLayerState("Inesistente") == -1);

    auto def = std::make_shared<BlockDef>();
    def->name = "PIANTA";
    def->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100, 0}));
    def->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{0, 100}));
    doc.addBlock(def);
    auto insert = std::make_unique<InsertEntity>("PIANTA", Vec2{1000, 0});
    insert->scaleX = insert->scaleY = 2.0;
    insert->clip = {{10, -5}, {40, 50}};   // rettangolo nelle coordinate del blocco
    const EntityId id = doc.add(std::move(insert));
    const auto& in = static_cast<const InsertEntity&>(*doc.find(id));
    CHECK(in.clipped() && in.clipPolygon().size() == 4);
    const BBox b = in.bounds();
    CHECK(approx(b.min.x, 1020.0) && approx(b.max.x, 1080.0) && approx(b.max.y, 100.0));

    const auto path = std::filesystem::temp_directory_path() / "cartesio_states_clip.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document again;
    CHECK(readDxf(path, again).ok);
    std::filesystem::remove(path);
    CHECK(again.layerStates().size() == 1 && again.layerStates().front().name == "Tutto acceso"
          && again.layerStates().front().description == "prima");
    bool clipBack = false;
    for (const auto& e : again.entities())
        if (e->type() == EntityType::Insert) {
            const auto& r = static_cast<const InsertEntity&>(*e);
            clipBack = r.clip.size() == 2 && approx(r.clip[0].x, 10.0) && approx(r.clip[1].y, 50.0);
        }
    CHECK(clipBack);
}

// Selezione con un clic: una linea sopra un tratteggio vince sull'interno del tratteggio; dentro il tratteggio,
// lontano dalle linee, si prende il tratteggio.
void testPickPriority()
{
    Document doc;
    auto hatch = std::make_unique<HatchEntity>();
    hatch->loops = {{{{0, 0}}, {{100, 0}}, {{100, 100}}, {{0, 100}}}};
    const EntityId h = doc.add(std::move(hatch));
    const EntityId line = doc.add(std::make_unique<LineEntity>(Vec2{0, 50}, Vec2{100, 50}));
    // Il tratteggio aggiunto dopo la linea (sopra nell'ordine) non deve comunque rubare il clic alla linea.
    doc.reorder({h}, Document::DrawOrder::Front);
    const Entity* a = doc.pick({50, 50.5}, 1.0);
    CHECK(a && a->id() == line);
    const Entity* b = doc.pick({20, 20}, 1.0);
    CHECK(b && b->id() == h);
    CHECK(doc.pick({200, 200}, 1.0) == nullptr);
}

// Annulla completo: layer, layer corrente e definizioni dei blocchi.
void testUndoTables()
{
    Document doc;
    Layer muri;
    muri.name = "Muri";
    const int m = doc.addLayer(muri);
    doc.checkpoint();
    // Proprietà di un layer (gestore layer): un passo da solo.
    doc.layer(m).visible = false;
    doc.checkpoint();
    CHECK(doc.undo() && doc.layers()[static_cast<std::size_t>(m)].visible);
    CHECK(doc.redo() && !doc.layers()[static_cast<std::size_t>(m)].visible);
    // Layer nuovo e oggetto sopra nello stesso comando: un solo Annulla toglie entrambi.
    Layer nuovo;
    nuovo.name = "Nuovo";
    const int n = doc.addLayer(nuovo);
    auto line = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0});
    line->setLayer(n);
    doc.add(std::move(line));
    doc.checkpoint();
    CHECK(doc.undo() && doc.entities().empty() && doc.findLayer("Nuovo") < 0);
    CHECK(doc.redo() && doc.entities().size() == 1 && doc.findLayer("Nuovo") >= 0);

    // Ridefinizione di un blocco (BSAVE) e modifica sul posto (BATTMAN).
    auto def = std::make_shared<BlockDef>();
    def->name = "B";
    def->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0}));
    doc.addBlock(def);
    doc.add(std::make_unique<InsertEntity>("B", Vec2{5, 5}));
    doc.checkpoint();
    Document edit;
    CHECK(copyBlockForEditing(doc, "B", edit));
    edit.add(std::make_unique<CircleEntity>(Vec2{0, 0}, 1));
    CHECK(saveEditedBlock(doc, "B", edit) == 1);
    doc.checkpoint();
    CHECK(doc.findBlock("B")->entities.size() == 2);
    CHECK(doc.undo() && doc.findBlock("B")->entities.size() == 1);
    const auto& insert = static_cast<const InsertEntity&>(*doc.entities().back());
    CHECK(insert.block == doc.findBlock("B"));
    CHECK(doc.redo() && doc.findBlock("B")->entities.size() == 2);
    const auto before = doc.findBlock("B");
    auto editable = doc.editableBlock("B");
    CHECK(editable && editable != before);   // copia: la definizione di prima resta per Annulla
    editable->entities.pop_back();
    doc.checkpoint();
    CHECK(doc.findBlock("B")->entities.size() == 1);
    CHECK(doc.undo() && doc.findBlock("B")->entities.size() == 2);
}

// Editor blocchi e libreria: copia per la modifica, salvataggio che ridefinisce il blocco, importazione da un
// altro disegno, elenco dei blocchi utente.
void testBlockEditAndImport()
{
    Document host;
    Layer quadri;
    quadri.name = "Quadri";
    const int layer = host.addLayer(quadri);
    auto def = std::make_shared<BlockDef>();
    def->name = "QUADRO";
    def->base = {1, 1};
    auto rect = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0});
    rect->setLayer(layer);
    def->entities.push_back(std::move(rect));
    host.addBlock(def);
    host.add(std::make_unique<InsertEntity>("QUADRO", Vec2{20, 0}));
    host.add(std::make_unique<InsertEntity>("QUADRO", Vec2{40, 0}));

    Document edit;
    CHECK(copyBlockForEditing(host, "QUADRO", edit));
    CHECK(edit.entities().size() == 1 && edit.findLayer("Quadri") >= 0);
    CHECK(!copyBlockForEditing(host, "NONC'E", edit));
    edit.add(std::make_unique<CircleEntity>(Vec2{5, 5}, 3));
    std::string error;
    CHECK(saveEditedBlock(host, "QUADRO", edit, &error) == 2);
    const auto redefined = host.findBlock("QUADRO");
    CHECK(redefined && redefined->entities.size() == 2 && approx(redefined->base.x, 1.0));
    bool linked = true;
    for (const auto& e : host.entities())
        if (e->type() == EntityType::Insert)
            linked = linked && static_cast<const InsertEntity&>(*e).block == redefined;
    CHECK(linked);
    edit.add(std::make_unique<InsertEntity>("QUADRO", Vec2{0, 0}));   // sé stesso: rifiutato
    CHECK(saveEditedBlock(host, "QUADRO", edit, &error) < 0);

    Document target;
    CHECK(importBlock(target, host, "QUADRO") && target.findBlock("QUADRO") && target.findLayer("Quadri") >= 0);
    CHECK(!importBlock(target, host, "ALTRO"));
    Document scaled;   // libreria in cm, disegno in mm: il blocco importato si scala (punto base compreso)
    CHECK(importBlock(scaled, host, "QUADRO", 10.0));
    const auto big = scaled.findBlock("QUADRO");
    CHECK(big && approx(big->base.x, 10.0) && !big->entities.empty() && approx(big->entities.front()->bounds().max.x, 100.0));
    auto anon = std::make_shared<BlockDef>();
    anon->name = "*U12";
    host.addBlock(anon);
    const auto names = userBlockNames(host);
    CHECK(names.size() == 1 && names.front() == "QUADRO");
}

// Associa (BIND) un riferimento esterno: Associa = NOME$0$x, Inserisci = nomi uniti a quelli del disegno.
void testBindXref()
{
    const auto makeHost = [](Document& host) {
        Layer muri;
        muri.name = "Muri";
        muri.color = 3;
        host.addLayer(muri);
        Document source;
        Layer srcMuri;
        srcMuri.name = "Muri";
        const int srcLayer = source.addLayer(srcMuri);
        auto porta = std::make_shared<BlockDef>();
        porta->name = "Porta";
        auto line = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0});
        line->setLayer(srcLayer);
        porta->entities.push_back(std::move(line));
        source.addBlock(porta);
        auto wall = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0});
        wall->setLayer(srcLayer);
        source.add(std::move(wall));
        source.add(std::make_unique<InsertEntity>("Porta", Vec2{5, 0}));
        auto xref = std::make_shared<BlockDef>();
        xref->name = "EXT";
        xref->xrefPath = "ext.dwg";
        host.addBlock(xref);
        loadXref(host, *xref, source);
        host.add(std::make_unique<InsertEntity>("EXT", Vec2{0, 0}));
    };
    {
        Document host;
        makeHost(host);
        CHECK(host.findLayer("EXT|Muri") >= 0 && host.findBlock("EXT|Porta") != nullptr);
        std::string error;
        CHECK(bindXref(host, "EXT", false, &error));
        CHECK(host.findLayer("EXT|Muri") < 0 && host.findLayer("EXT$0$Muri") >= 0 && host.findLayer("Muri") >= 0);
        CHECK(host.findBlock("EXT$0$Porta") != nullptr && host.findBlock("EXT|Porta") == nullptr);
        const auto ext = host.findBlock("EXT");
        CHECK(ext && !ext->isXref());
        bool insertRenamed = false;
        for (const auto& e : ext->entities)
            if (e->type() == EntityType::Insert)
                insertRenamed = static_cast<const InsertEntity&>(*e).blockName == "EXT$0$Porta";
        CHECK(insertRenamed);
        CHECK(!bindXref(host, "EXT", false, &error));   // ormai non è più un riferimento
    }
    {
        Document host;
        makeHost(host);
        CHECK(bindXref(host, "EXT", true));
        CHECK(host.findLayer("EXT|Muri") < 0 && host.findLayer("EXT$0$Muri") < 0);
        const int muri = host.findLayer("Muri");
        const auto ext = host.findBlock("EXT");
        bool onMuri = true;
        for (const auto& e : ext->entities)
            if (e->type() == EntityType::Line)
                onMuri = onMuri && e->layer() == muri;
        CHECK(onMuri && host.findBlock("Porta") != nullptr);
    }
}

// HATCHEDIT: motivo standard ricostruito, motivo del file scalato e ruotato, nomi sconosciuti rifiutati.
void testHatchSetPattern()
{
    HatchEntity h;
    h.loops = {{{{0, 0}}, {{10, 0}}, {{10, 10}}, {{0, 10}}}};
    CHECK(h.setPattern("ANSI31", 2.0, 0.0) && !h.solid && !h.patternLines.empty());
    CHECK(!h.setPattern("NONESISTE", 1.0, 0.0) && h.pattern == "ANSI31");
    HatchEntity custom = h;
    custom.pattern = "MIO";
    custom.patternScale = 1.0;
    custom.patternAngle = 0.0;
    custom.patternLines = {{0.0, {0, 0}, {0, 1}, {2.0, -1.0}}};
    CHECK(custom.setPattern("MIO", 3.0, kPi / 2.0));
    CHECK(approx(custom.patternLines[0].angle, kPi / 2.0) && approx(custom.patternLines[0].offset.x, -3.0)
          && approx(custom.patternLines[0].dashes[0], 6.0));
    CHECK(h.setPattern("SOLID", 1.0, 0.0) && h.solid && h.patternLines.empty());
}

// Polilinee con spessore: aree piene, scala, lettura (43 e 40/41) e scrittura R12/R2000.
void testPolylineWidth()
{
    PolylineEntity zig({{{0, 0}, 0.0, 2.0, 2.0}, {{10, 0}, 0.0, 2.0, 2.0}, {{10, 10}, 0.0, 2.0, 2.0}}, false);
    CHECK(zig.hasWidth() && approx(zig.maxWidth(), 2.0));
    const auto polys = zig.widthPolygons();
    CHECK(polys.size() == 3);   // due tratti e il raccordo dello spigolo
    CHECK(zig.bounds().min.y < -0.99 && zig.bounds().max.x > 10.99);
    CHECK(approx(zig.distanceTo({5, 0.5}), 0.0));
    PolylineEntity mixed({{{0, 0}, 0.0, 1.0, 1.0}, {{10, 0}, 0.0, 0.0, 0.0}, {{20, 0}, 0.0, 1.0, 1.0}, {{30, 0}}}, false);
    CHECK(mixed.thinParts().size() == 1 && mixed.thinParts().front()->vertices.size() == 2);
    PolylineEntity scaled = zig;
    scaled.transform(Affine::scaling({0, 0}, 3.0));
    CHECK(approx(scaled.maxWidth(), 6.0));

    const char* content =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n43\n1.5\n10\n0\n20\n0\n10\n10\n20\n0\n"
        "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n10\n0\n20\n5\n40\n4\n41\n0\n10\n10\n20\n5\n"
        "0\nPOLYLINE\n8\n0\n66\n1\n70\n0\n40\n2\n41\n2\n0\nVERTEX\n8\n0\n10\n0\n20\n9\n0\nVERTEX\n8\n0\n10\n5\n20\n9\n40\n1\n41\n3\n"
        "0\nVERTEX\n8\n0\n10\n9\n20\n9\n0\nSEQEND\n8\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto path = std::filesystem::temp_directory_path() / "cartesio_width.dxf";
    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        std::fputs(content, f);
        std::fclose(f);
    }
    const auto widths = [](const Document& d, std::size_t i) {
        std::vector<double> w;
        if (i < d.entities().size() && d.entities()[i]->type() == EntityType::Polyline)
            for (const PolyVertex& v : static_cast<const PolylineEntity&>(*d.entities()[i]).vertices) {
                w.push_back(v.startWidth);
                w.push_back(v.endWidth);
            }
        return w;
    };
    Document doc;
    CHECK(readDxf(path, doc).ok && doc.entities().size() == 3);
    CHECK((widths(doc, 0) == std::vector<double>{1.5, 1.5, 1.5, 1.5}));
    CHECK((widths(doc, 1) == std::vector<double>{4, 0, 0, 0}));
    CHECK((widths(doc, 2) == std::vector<double>{2, 2, 1, 3, 2, 2}));
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000}) {
        CHECK(writeDxf(doc, path, nullptr, nullptr, version));
        Document again;
        CHECK(readDxf(path, again).ok && again.entities().size() == 3);
        CHECK(widths(again, 0) == widths(doc, 0));
        CHECK(widths(again, 1) == widths(doc, 1));
        CHECK(widths(again, 2) == widths(doc, 2));
    }
    std::filesystem::remove(path);
}

void testModernDxf()
{
    // Estratto di un DXF R2000 come quelli prodotti dagli altri CAD.
    const char* content =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLWPOLYLINE\n8\nMuri\n90\n3\n70\n1\n10\n0\n20\n0\n10\n10\n20\n0\n42\n0.5\n10\n10\n20\n10\n"
        "0\nMTEXT\n8\n0\n10\n0\n20\n20\n40\n2.5\n41\n50\n71\n1\n1\n{\\fArial|b1;Prima}\\Pseconda riga\n"
        "0\nHATCH\n8\n0\n2\nSOLID\n70\n1\n91\n1\n92\n2\n72\n0\n73\n1\n93\n3\n10\n0\n20\n0\n10\n5\n20\n0\n10\n5\n20\n5\n"
        "97\n0\n75\n0\n76\n1\n98\n0\n"
        "0\nTEXT\n8\n0\n10\n1\n20\n2\n40\n3\n1\nDiametro %%c20\n"
        "0\nCIRCLE\n8\n0\n67\n1\n10\n0\n20\n0\n40\n1\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto path = std::filesystem::temp_directory_path() / "cartesio_modern.dxf";
    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        std::fputs(content, f);
        std::fclose(f);
    }
    Document doc;
    const auto result = readDxf(path, doc);
    std::filesystem::remove(path);
    CHECK(result.ok);
    CHECK(result.imported == 5);
    // Il cerchio con codice 67 = 1 va nello spazio carta del primo layout.
    CHECK(doc.entities().size() == 5 && doc.entities()[4]->space() == doc.layouts().front().id);
    CHECK(doc.extents(0).isValid() && doc.extents(0).min.x >= -1e-9);   // il cerchio (x da -1) non è nel modello
    if (doc.entities().size() == 5) {
        const auto& poly = static_cast<const PolylineEntity&>(*doc.entities()[0]);
        CHECK(poly.closed && poly.vertices.size() == 3 && approx(poly.vertices[1].bulge, 0.5));
        CHECK(doc.layers()[static_cast<std::size_t>(poly.layer())].name == "Muri");
        const auto& m = static_cast<const MTextEntity&>(*doc.entities()[1]);
        const auto lines = m.lines();
        CHECK(lines.size() == 2 && lines[0] == "Prima" && lines[1] == "seconda riga");
        const auto& h = static_cast<const HatchEntity&>(*doc.entities()[2]);
        CHECK(h.solid && h.loops.size() == 1 && h.contains({4, 1}) && !h.contains({1, 4}));
        const auto& t = static_cast<const TextEntity&>(*doc.entities()[3]);
        CHECK(t.displayText() == "Diametro Ø20");
    }
}

void testFullRoundTrip()
{
    Fixture f;
    f.doc.add(std::make_unique<PolylineEntity>(
        std::vector<PolyVertex>{{{0, 0}, 0.3}, {{10, 0}, 0}, {{10, 5}, 0}}, false));
    f.doc.add(std::make_unique<TextEntity>(Vec2{1, 1}, 2.5, "Ciao"));
    f.doc.add(std::make_unique<PointEntity>(Vec2{3, 3}));
    auto dim = std::make_unique<DimensionEntity>();
    dim->kind = DimensionEntity::Aligned;
    dim->p1 = {0, 0};
    dim->p2 = {10, 0};
    dim->dimLine = {5, -5};
    f.doc.add(std::move(dim));
    auto block = std::make_shared<BlockDef>();
    block->name = "B1";
    block->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 1}));
    f.doc.addBlock(block);
    auto insert = std::make_unique<InsertEntity>("B1", Vec2{20, 20});
    f.doc.add(std::move(insert));
    f.doc.linkBlocks();

    const auto path = std::filesystem::temp_directory_path() / "cartesio_full.dxf";
    CHECK(writeDxf(f.doc, path));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok);
    CHECK(result.skippedTypes.empty());
    CHECK(loaded.entities().size() == 5);
    int types[16] = {};
    for (const auto& e : loaded.entities())
        ++types[static_cast<int>(e->type())];
    CHECK(types[static_cast<int>(EntityType::Polyline)] == 1);
    CHECK(types[static_cast<int>(EntityType::Text)] == 1);
    CHECK(types[static_cast<int>(EntityType::Point)] == 1);
    CHECK(types[static_cast<int>(EntityType::Dimension)] == 1);
    CHECK(types[static_cast<int>(EntityType::Insert)] == 1);
    for (const auto& e : loaded.entities()) {
        if (e->type() == EntityType::Insert)
            CHECK(static_cast<const InsertEntity&>(*e).block != nullptr);
        if (e->type() == EntityType::Dimension)
            CHECK(static_cast<const DimensionEntity&>(*e).block != nullptr);
        if (e->type() == EntityType::Polyline)
            CHECK(approx(static_cast<const PolylineEntity&>(*e).vertices[0].bulge, 0.3));
    }
}

void testDxfEncoding()
{
    // "Città" in Windows-1252 (byte E0) e "\U+00B0" (simbolo di grado), come nei DXF 2000-2004.
    const std::string content = std::string("0\nSECTION\n2\nENTITIES\n0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n1\n1\nCitt\xE0\n")
                                + "0\nTEXT\n8\n0\n10\n0\n20\n5\n40\n1\n1\n90\\U+00B0\n0\nENDSEC\n0\nEOF\n";
    const auto path = std::filesystem::temp_directory_path() / "cartesio_encoding.dxf";
    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        std::fwrite(content.data(), 1, content.size(), f);
        std::fclose(f);
    }
    Document doc;
    const auto result = readDxf(path, doc);
    std::filesystem::remove(path);
    CHECK(result.ok && doc.entities().size() == 2);
    if (doc.entities().size() == 2) {
        CHECK(static_cast<const TextEntity&>(*doc.entities()[0]).text == "Città");
        CHECK(static_cast<const TextEntity&>(*doc.entities()[1]).text == "90°");
    }
}

void testTextStylesRoundTrip()
{
    // Stili di testo (SHX con larghezza e obliquità, TrueType con famiglia e grassetto) e stile/obliquità dei
    // testi: si conservano in DXF R12 e R2000 (da cui passa anche il DWG).
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000}) {
        Document doc;
        TextStyle romand;
        romand.name = "ROMAND";
        romand.font = "romand.shx";
        romand.widthFactor = 0.7;
        romand.oblique = degToRad(15.0);
        doc.setTextStyle(romand);
        TextStyle narrow;
        narrow.name = "Narrow_B";
        narrow.font = "arialn.ttf";
        narrow.family = "Arial Narrow";
        narrow.bold = true;
        doc.setTextStyle(narrow);
        CHECK(romand.isShx() && !narrow.isShx());
        auto t = std::make_unique<TextEntity>(Vec2{1, 2}, 2.5, "Prova");
        t->style = "ROMAND";
        t->oblique = degToRad(10.0);
        t->widthFactor = 0.8;
        doc.add(std::move(t));
        auto m = std::make_unique<MTextEntity>(Vec2{0, 10}, 3.0, "Riga 1\\PRiga 2");
        m->style = "Narrow_B";
        doc.add(std::move(m));

        const auto path = std::filesystem::temp_directory_path() / "cartesio_styles.dxf";
        std::string error;
        CHECK(writeDxf(doc, path, &error, nullptr, version));
        Document loaded;
        const DxfReadResult result = readDxf(path, loaded);
        std::filesystem::remove(path);
        CHECK(result.ok);
        const TextStyle* s = loaded.findTextStyle("romand");
        CHECK(s && s->font == "romand.shx" && approx(s->widthFactor, 0.7, 1e-9) && approx(s->oblique, degToRad(15.0), 1e-9));
        if (version == DxfVersion::R2000) {
            const TextStyle* n = loaded.findTextStyle("Narrow_B");
            CHECK(n && n->family == "Arial Narrow" && n->bold && !n->italic);
        }
        bool text = false, mtext = false;
        for (const auto& e : loaded.entities()) {
            if (e->type() == EntityType::Text) {
                const auto& lt = static_cast<const TextEntity&>(*e);
                text = text || (lt.style == "ROMAND" && approx(lt.oblique, degToRad(10.0), 1e-9) && approx(lt.widthFactor, 0.8, 1e-9));
            }
            if (e->type() == EntityType::MText)
                mtext = static_cast<const MTextEntity&>(*e).style == "Narrow_B";
        }
        CHECK(text);
        if (version == DxfVersion::R2000)
            CHECK(mtext);   // in R12 il testo multilinea diventa più TEXT
    }
}

void testDxf2000RoundTrip()
{
    Document doc;
    auto hatch = std::make_unique<HatchEntity>();
    hatch->solid = false;
    hatch->pattern = "ANSI31";
    hatch->loops.push_back({{{0, 0}, 0}, {{10, 0}, 0}, {{10, 10}, 0.5}, {{0, 10}, 0}});
    doc.add(std::move(hatch));
    doc.add(std::make_unique<EllipseEntity>(Vec2{20, 0}, Vec2{5, 0}, 0.5));
    auto spline = std::make_unique<SplineEntity>();
    spline->fit = {{0, 20}, {5, 25}, {10, 20}, {15, 25}};
    doc.add(std::move(spline));
    doc.add(std::make_unique<MTextEntity>(Vec2{0, 40}, 2.5, "Prima riga\\PSeconda città", 50.0));

    const auto path = std::filesystem::temp_directory_path() / "cartesio_2000.dxf";
    std::string error;
    CHECK(writeDxf(doc, path, &error, nullptr, DxfVersion::R2000));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok && result.skippedTypes.empty());
    CHECK(loaded.entities().size() == 4);
    if (loaded.entities().size() != 4)
        return;
    const auto& h = static_cast<const HatchEntity&>(*loaded.entities()[0]);
    CHECK(h.type() == EntityType::Hatch && !h.solid && h.pattern == "ANSI31" && h.loops.size() == 1);
    CHECK(h.loops[0].size() == 4 && approx(h.loops[0][2].bulge, 0.5));
    const auto& el = static_cast<const EllipseEntity&>(*loaded.entities()[1]);
    CHECK(el.type() == EntityType::Ellipse && approx(el.ratio, 0.5) && el.isFull());
    const auto& sp = static_cast<const SplineEntity&>(*loaded.entities()[2]);
    CHECK(sp.type() == EntityType::Spline);
    const auto pts = sp.evaluate();
    CHECK(!pts.empty() && approx(pts.front(), {0, 20}, 1e-9) && approx(pts.back(), {15, 25}, 1e-9));
    const auto& m = static_cast<const MTextEntity&>(*loaded.entities()[3]);
    const auto lines = m.lines();
    CHECK(lines.size() == 2 && lines[1] == "Seconda città");
}

void testAttributesAndLayouts()
{
    Document doc;
    // Blocco con una definizione di attributo e un inserimento con il suo valore.
    auto block = std::make_shared<BlockDef>();
    block->name = "PORTA";
    block->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0}));
    TextEntity def({0, 0.2}, 0.25, "1");
    block->entities.push_back(std::make_unique<AttDefEntity>("NUMERO", "Numero porta", def));
    doc.addBlock(block);
    auto insert = std::make_unique<InsertEntity>("PORTA", Vec2{10, 10});
    TextEntity value({10, 10.2}, 0.25, "12");
    insert->attributes.push_back({"NUMERO", value, 0});
    doc.add(std::move(insert));
    doc.linkBlocks();

    // Secondo layout A4 con una finestra in scala 1:100 e una linea sul foglio.
    const int second = doc.addLayout("Tavola 2");
    doc.findLayout(second)->paperWidth = 297;
    doc.findLayout(second)->paperHeight = 210;
    doc.setActiveSpace(second);
    auto vp = std::make_unique<ViewportEntity>(Vec2{148.5, 105}, 250, 180);
    vp->viewCenter = {10, 10};
    vp->setScale(0.01);
    doc.add(std::move(vp));
    doc.add(std::make_unique<LineEntity>(Vec2{10, 10}, Vec2{100, 10}));
    doc.setActiveSpace(0);

    const auto path = std::filesystem::temp_directory_path() / "cartesio_layouts.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok);

    // Layout: Layout1 (predefinito) e "Tavola 2", con il loro contenuto.
    CHECK(loaded.layouts().size() == 2);
    const Layout* tavola = nullptr;
    for (const Layout& l : loaded.layouts())
        if (l.name == "Tavola 2")
            tavola = &l;
    CHECK(tavola && approx(tavola->paperWidth, 297) && approx(tavola->paperHeight, 210));
    int paperLines = 0, viewports = 0, inserts = 0;
    for (const auto& e : loaded.entities()) {
        if (tavola && e->space() == tavola->id) {
            paperLines += e->type() == EntityType::Line;
            if (e->type() == EntityType::Viewport) {
                ++viewports;
                const auto& v = static_cast<const ViewportEntity&>(*e);
                CHECK(approx(v.scale(), 0.01, 1e-9) && approx(v.viewCenter, {10, 10}));
            }
        }
        if (e->type() == EntityType::Insert && e->space() == 0) {
            ++inserts;
            const auto& in = static_cast<const InsertEntity&>(*e);
            CHECK(in.attributes.size() == 1 && in.attributes[0].tag == "NUMERO" && in.attributes[0].text.text == "12");
        }
    }
    CHECK(paperLines == 1 && viewports == 1 && inserts == 1);
    const auto def2 = loaded.findBlock("PORTA");
    CHECK(def2 && def2->entities.size() == 2 && def2->entities[1]->type() == EntityType::AttDef);
}

void testTransformCommands()
{
    Fixture f;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    f.selection = {f.doc.entities()[0]->id()};
    f.commands.execute("RO", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.value(f.ctx, 90);
    CHECK(approx(f.at<LineEntity>(0).end, {0, 10}, 1e-9));

    f.selection = {f.doc.entities()[0]->id()};
    f.commands.execute("SC", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.value(f.ctx, 2);
    CHECK(approx(f.at<LineEntity>(0).end, {0, 20}, 1e-9));

    // Specchio con copia: il testo resta leggibile.
    f.doc.add(std::make_unique<TextEntity>(Vec2{5, 5}, 1.0, "ABC"));
    f.selection = {f.doc.entities()[1]->id()};
    f.commands.execute("MI", f.ctx);
    f.commands.point(f.ctx, {10, 0});
    f.commands.point(f.ctx, {10, 10});
    f.commands.enter(f.ctx);   // non cancellare l'originale
    CHECK(f.doc.entities().size() == 3);
    const auto& mirrored = f.at<TextEntity>(2);
    CHECK(approx(mirrored.position, {15, 5}) && approx(std::cos(mirrored.rotation), 1.0, 1e-9));

    // Serie rettangolare 2x3 e polare da 4.
    f.selection = {f.doc.entities()[0]->id()};
    f.commands.execute("AR", f.ctx);
    f.commands.keyword(f.ctx, "R");
    f.commands.value(f.ctx, 2);
    f.commands.value(f.ctx, 3);
    f.commands.value(f.ctx, 50);
    f.commands.value(f.ctx, 30);
    CHECK(f.doc.entities().size() == 3 + 5);
    f.selection = {f.doc.entities()[0]->id()};
    f.commands.execute("AR", f.ctx);
    f.commands.keyword(f.ctx, "P");
    f.commands.point(f.ctx, {0, 0});
    f.commands.value(f.ctx, 4);
    f.commands.enter(f.ctx);
    CHECK(f.doc.entities().size() == 8 + 3);
}

void testExtendFilletGrips()
{
    Fixture f;
    f.ctx.pickTolerance = 0.1;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{5, 0}));
    f.doc.add(std::make_unique<LineEntity>(Vec2{10, -5}, Vec2{10, 5}));
    f.commands.execute("EX", f.ctx);
    f.commands.point(f.ctx, {4, 0});
    f.commands.enter(f.ctx);
    CHECK(approx(f.at<LineEntity>(0).end, {10, 0}));

    // Raccordo con raggio 0 fra due linee che non si toccano: spigolo vivo.
    Fixture g;
    g.ctx.pickTolerance = 0.1;
    g.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{8, 0}));
    g.doc.add(std::make_unique<LineEntity>(Vec2{10, 2}, Vec2{10, 10}));
    g.commands.execute("F", g.ctx);
    g.commands.point(g.ctx, {2, 0});
    g.commands.point(g.ctx, {10, 8});
    CHECK(g.doc.entities().size() == 2);
    CHECK(approx(g.at<LineEntity>(0).end, {10, 0}) && approx(g.at<LineEntity>(1).end, {10, 0}));

    // Raccordo con raggio 2: arco tangente fra (8,0) e (10,2).
    g.commands.execute("F", g.ctx);
    g.commands.keyword(g.ctx, "R");
    g.commands.value(g.ctx, 2);
    g.commands.point(g.ctx, {2, 0});
    g.commands.point(g.ctx, {10, 8});
    CHECK(g.doc.entities().size() == 3);
    if (g.doc.entities().size() == 3) {
        const auto& arc = g.at<ArcEntity>(2);
        CHECK(approx(arc.center, {8, 2}, 1e-9) && approx(arc.radius, 2));
    }

    // Maniglia dell'estremo di una linea.
    const auto edited = gripEdit(g.at<LineEntity>(0), 0, {-5, -5});
    CHECK(edited && approx(static_cast<const LineEntity&>(*edited).start, {-5, -5}));
}

void testClipboardBetweenDrawings()
{
    Fixture a;
    const int red = a.doc.addLayer(Layer{"Rosso", 1, true});
    a.doc.setCurrentLayer(red);
    a.doc.add(std::make_unique<CircleEntity>(Vec2{10, 10}, 2));
    a.selection = {a.doc.entities()[0]->id()};
    a.commands.execute("COPYCLIP", a.ctx);

    Fixture b;
    b.commands.execute("PASTECLIP", b.ctx);
    b.commands.point(b.ctx, {100, 100});
    CHECK(b.doc.entities().size() == 1);
    if (b.doc.entities().size() == 1) {
        const auto& c = b.at<CircleEntity>(0);
        CHECK(approx(c.center, {102, 102}));   // base = angolo in basso a sinistra (8,8)
        CHECK(b.doc.layers()[static_cast<std::size_t>(c.layer())].name == "Rosso");
    }
}

void testScriptCommands()
{
    Fixture f;
    // -LAYER: crea, colora e rende corrente.
    f.commands.execute("-LAYER", f.ctx);
    for (const char* t : {"N", "Muri", "C", "1", "Muri", "S", "Muri"})
        f.commands.text(f.ctx, t);
    f.commands.enter(f.ctx);
    CHECK(!f.commands.isActive());
    const int muri = f.doc.findLayer("Muri");
    CHECK(muri >= 0 && f.doc.currentLayer() == muri && f.doc.layers()[static_cast<std::size_t>(muri)].color == 1);

    f.commands.execute("CLAYER", f.ctx);
    f.commands.text(f.ctx, "0");
    CHECK(f.doc.currentLayer() == 0);

    // ATTDEF, BLOCK, INSERT con valore e con ATTREQ = 0 (valore predefinito).
    f.commands.execute("ATTDEF", f.ctx);
    f.commands.text(f.ctx, "num");
    f.commands.text(f.ctx, "Numero");
    f.commands.text(f.ctx, "0");
    f.commands.point(f.ctx, {0, 0});
    f.commands.value(f.ctx, 1);
    CHECK(f.doc.entities().size() == 1 && f.doc.entities()[0]->type() == EntityType::AttDef);
    f.selection = {f.doc.entities()[0]->id()};
    f.commands.execute("BLOCK", f.ctx);
    f.commands.text(f.ctx, "B");
    f.commands.point(f.ctx, {0, 0});
    f.commands.enter(f.ctx);
    f.commands.execute("INSERT", f.ctx);
    f.commands.text(f.ctx, "B");
    f.commands.point(f.ctx, {5, 5});
    f.commands.enter(f.ctx);
    f.commands.enter(f.ctx);
    CHECK(f.commands.isActive());          // chiede il valore dell'attributo
    f.commands.text(f.ctx, "42");
    CHECK(!f.commands.isActive());
    const auto& insert = static_cast<const InsertEntity&>(*f.doc.entities().back());
    CHECK(insert.attributes.size() == 1 && insert.attributes[0].tag == "NUM" && insert.attributes[0].text.text == "42");
    CHECK(approx(insert.attributes[0].text.position, {5, 5}));

    f.ctx.getVar = [](const std::string& name) { return name == "ATTREQ" ? std::string("0") : std::string(); };
    f.commands.execute("INSERT", f.ctx);
    f.commands.text(f.ctx, "B");
    f.commands.point(f.ctx, {9, 9});
    f.commands.enter(f.ctx);
    f.commands.enter(f.ctx);
    CHECK(!f.commands.isActive());
    CHECK(static_cast<const InsertEntity&>(*f.doc.entities().back()).attributes[0].text.text == "0");
}

void testArcDxf()
{
    Document doc;
    doc.add(std::make_unique<ArcEntity>(Vec2{1, 2}, 3, 0.0, kPi / 2));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_arc_test.dxf";
    CHECK(writeDxf(doc, path));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok && result.imported == 1);
    if (loaded.entities().size() == 1) {
        const auto& arc = static_cast<const ArcEntity&>(*loaded.entities()[0]);
        CHECK(arc.type() == EntityType::Arc);
        CHECK(approx(arc.radius, 3) && approx(arc.sweep(), kPi / 2, 1e-9));
    }
}

} // namespace

void testOsnap()
{
    const LineEntity h({0, 0}, {10, 0});
    const LineEntity v({4, -5}, {4, 3});
    const CircleEntity c({20, 0}, 5);
    const std::vector<const Entity*> all = {&h, &v, &c};

    // Intersezione delle due linee.
    auto s = findSnap(all, {4.3, 0.2}, 1.0, Osnap::Default);
    CHECK(s && s->kind == SnapKind::Intersection && approx(s->position, {4, 0}));
    // Senza la modalità intersezione non c'è nulla da agganciare lì.
    CHECK(!findSnap(all, {4.3, 0.2}, 0.5, Osnap::Endpoint | Osnap::Midpoint));
    // Il punto finale ha la precedenza a pari distanza.
    s = findSnap(all, {10.1, 0.1}, 1.0, Osnap::Default);
    CHECK(s && s->kind == SnapKind::Endpoint && approx(s->position, {10, 0}));
    // Perpendicolare dal punto base (7,6) sulla linea orizzontale.
    s = findSnap(all, {7.2, 0.3}, 1.0, Osnap::Perpendicular, Vec2{7, 6});
    CHECK(s && s->kind == SnapKind::Perpendicular && approx(s->position, {7, 0}));
    // Tangente al cerchio da (20, 10): punti (20 ± 4.33, 2.5).
    s = findSnap(all, {24.5, 2.0}, 1.0, Osnap::Tangent, Vec2{20, 10});
    CHECK(s && s->kind == SnapKind::Tangent && approx(s->position, {20 + 5 * std::cos(kPi / 6), 2.5}, 1e-9));
    // Vicino: solo se non ci sono altri punti.
    s = findSnap(all, {2.5, 0.4}, 1.0, Osnap::Nearest);
    CHECK(s && s->kind == SnapKind::Nearest && approx(s->position, {2.5, 0}));
    s = findSnap(all, {2.5, 0.4}, 1.0, Osnap::Nearest | Osnap::Endpoint);
    CHECK(s && s->kind == SnapKind::Nearest);
    CHECK(snapBit(SnapKind::Tangent) == 256);
}

void testAdvancedViewports()
{
    Fixture f;
    const int hidden = f.doc.addLayer(Layer{"Arredi", 3, true});
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1000, 1000}));
    const int layout = f.doc.layouts().front().id;
    f.doc.setActiveSpace(layout);

    // MVIEW Poligonale: un triangolo.
    f.commands.execute("MVIEW", f.ctx);
    f.commands.keyword(f.ctx, "P");
    f.commands.point(f.ctx, {20, 20});
    f.commands.point(f.ctx, {200, 20});
    f.commands.point(f.ctx, {110, 180});
    f.commands.keyword(f.ctx, "C");
    CHECK(!f.commands.isActive());
    const ViewportEntity* vp = nullptr;
    for (const auto& e : f.doc.entities())
        if (e->type() == EntityType::Viewport)
            vp = static_cast<const ViewportEntity*>(e.get());
    CHECK(vp && vp->clip.size() == 3 && approx(vp->width, 180) && approx(vp->height, 160));
    CHECK(vp && vp->containsPaperPoint({110, 60}) && !vp->containsPaperPoint({25, 170}));
    if (!vp)
        return;
    const EntityId firstId = vp->id();   // le finestre vengono sostituite dai comandi: si tiene l'id

    // MVIEW Oggetto: un cerchio diventa il contorno e sparisce come oggetto a sé.
    f.doc.add(std::make_unique<CircleEntity>(Vec2{250, 100}, 30));
    const std::size_t before = f.doc.entities().size();
    f.commands.execute("MVIEW", f.ctx);
    f.commands.keyword(f.ctx, "O");
    f.commands.point(f.ctx, {280, 100});
    CHECK(!f.commands.isActive() && f.doc.entities().size() == before);

    // VPLAYER: congela "Arredi" in tutte le finestre.
    f.commands.execute("VPLAYER", f.ctx);
    f.commands.keyword(f.ctx, "C");
    f.commands.text(f.ctx, "arredi");
    f.commands.keyword(f.ctx, "T");
    int frozenIn = 0;
    for (const auto& e : f.doc.entities())
        if (e->type() == EntityType::Viewport)
            frozenIn += static_cast<const ViewportEntity&>(*e).isLayerFrozen(hidden);
    CHECK(frozenIn == 2);

    // VPCLIP Elimina sulla prima finestra: torna rettangolare.
    f.commands.execute("VPCLIP", f.ctx);
    f.commands.point(f.ctx, {110, 60});
    f.commands.keyword(f.ctx, "E");
    const Entity* first = f.doc.find(firstId);
    CHECK(first && static_cast<const ViewportEntity*>(first)->clip.empty());
    // ... e di nuovo poligonale con VPCLIP.
    f.commands.execute("VPCLIP", f.ctx);
    f.commands.point(f.ctx, {110, 60});
    f.commands.enter(f.ctx);
    for (Vec2 p : {Vec2{30, 30}, Vec2{190, 30}, Vec2{190, 150}, Vec2{30, 150}})
        f.commands.point(f.ctx, p);
    f.commands.enter(f.ctx);
    first = f.doc.find(firstId);
    CHECK(first && static_cast<const ViewportEntity*>(first)->clip.size() == 4);
    f.doc.setActiveSpace(0);

    // Salvataggio DXF 2000 e rilettura: contorni e layer congelati restano, senza oggetti in più.
    const auto path = std::filesystem::temp_directory_path() / "cartesio_vpclip.dxf";
    CHECK(writeDxf(f.doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok);
    int viewports = 0, polylines = 0, circles = 0, clipped = 0, frozen = 0;
    const int arredi = loaded.findLayer("Arredi");
    for (const auto& e : loaded.entities()) {
        if (e->space() == 0)
            continue;
        polylines += e->type() == EntityType::Polyline;
        circles += e->type() == EntityType::Circle;
        if (e->type() == EntityType::Viewport) {
            const auto& v = static_cast<const ViewportEntity&>(*e);
            ++viewports;
            clipped += v.clip.size() >= 3;
            frozen += v.isLayerFrozen(arredi);
        }
    }
    CHECK(viewports == 2 && clipped == 2 && frozen == 2 && polylines == 0 && circles == 0);
}

void testTitleBlock()
{
    Fixture f;
    f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10000, 5000}));
    const int layout = f.doc.layouts().front().id;
    f.doc.setActiveSpace(layout);
    f.commands.execute("MVIEW", f.ctx);
    f.commands.keyword(f.ctx, "A");

    // CARTIGLIO A1: cambia il foglio e inserisce cornice e cartiglio compilato.
    f.commands.execute("CARTIGLIO", f.ctx);
    f.commands.keyword(f.ctx, "A1");
    f.commands.text(f.ctx, "Pianta piano terra");
    f.commands.text(f.ctx, "Ufficio tecnico");
    CHECK(!f.commands.isActive());
    const Layout* l = f.doc.findLayout(layout);
    CHECK(l && approx(l->paperWidth, 841) && approx(l->paperHeight, 594) && l->paperName == "A1");
    CHECK(f.doc.findBlock(kTitleBlockName) != nullptr);

    const auto titleBlock = [&f]() -> const InsertEntity* {
        for (const auto& e : f.doc.entities())
            if (e->type() == EntityType::Insert && static_cast<const InsertEntity&>(*e).blockName == kTitleBlockName)
                return static_cast<const InsertEntity*>(e.get());
        return nullptr;
    };
    const auto value = [](const InsertEntity* in, const std::string& tag) {
        for (const Attribute& a : in->attributes)
            if (a.tag == tag)
                return a.text.text;
        return std::string("?");
    };
    const InsertEntity* tb = titleBlock();
    CHECK(tb && tb->attributes.size() == 8);
    if (!tb)
        return;
    CHECK(value(tb, "TITOLO") == "Pianta piano terra" && value(tb, "AUTORE") == "Ufficio tecnico");
    CHECK(value(tb, "FOGLIO") == "A1" && value(tb, "DATA") == todayText());
    CHECK(approx(tb->position, {831, 10}));

    // Scala 1:100 nella finestra, poi AGGCARTIGLIO.
    for (const auto& e : f.doc.entities())
        if (e->type() == EntityType::Viewport) {
            auto copy = e->clone();
            static_cast<ViewportEntity&>(*copy).setScale(0.01);
            std::vector<std::unique_ptr<Entity>> added;
            added.push_back(std::move(copy));
            f.doc.commit(std::move(added), {e->id()});
            break;
        }
    f.commands.execute("AGGCARTIGLIO", f.ctx);
    tb = titleBlock();
    CHECK(tb && value(tb, "SCALA") == "1:100" && value(tb, "TITOLO") == "Pianta piano terra");

    // Di nuovo CARTIGLIO: sostituisce il precedente (un solo cartiglio e una sola cornice).
    f.commands.execute("CARTIGLIO", f.ctx);
    f.commands.enter(f.ctx);
    f.commands.enter(f.ctx);
    f.commands.enter(f.ctx);
    int inserts = 0, frames = 0;
    for (const auto& e : f.doc.entities()) {
        inserts += e->type() == EntityType::Insert;
        frames += e->type() == EntityType::Polyline;
    }
    CHECK(inserts == 1 && frames == 1);
    tb = titleBlock();
    CHECK(tb && value(tb, "TITOLO") == "Pianta piano terra" && value(tb, "AUTORE") == "Ufficio tecnico");
    CHECK(scaleText(0.02) == "1:50" && scaleText(2.0) == "2:1" && paperFormatName(210, 297) == "A4");
    f.doc.setActiveSpace(0);
}

void testLinetypesAndLineweights()
{
    Document doc;
    CHECK(doc.loadStandardLinetype("dashed") && doc.loadStandardLinetype("HIDDEN") && !doc.loadStandardLinetype("XYZ"));
    const int walls = doc.addLayer(Layer{"Assi", 1, true});
    doc.layer(walls).linetype = "DASHED";
    doc.layer(walls).lineweight = 50;
    doc.setCurrentLayer(walls);
    doc.setLtscale(10.0);
    doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100, 0}));            // DaLayer
    auto hidden = std::make_unique<LineEntity>(Vec2{0, 10}, Vec2{100, 10});
    hidden->setLinetype("HIDDEN");
    hidden->setLineweight(35);
    hidden->setLinetypeScale(2.0);
    doc.add(std::move(hidden));

    const Entity& first = *doc.entities()[0];
    const Entity& second = *doc.entities()[1];
    CHECK(first.linetype() == "ByLayer" && first.lineweight() == kLineweightByLayer);
    CHECK(doc.effectiveLinetype(first) && doc.effectiveLinetype(first)->name == "DASHED");
    CHECK(doc.effectiveLineweight(first) == 50 && doc.effectiveLineweight(second) == 35);
    CHECK(doc.effectiveLinetype(second)->name == "HIDDEN");

    for (const DxfVersion version : {DxfVersion::R2000, DxfVersion::R12}) {
        const auto path = std::filesystem::temp_directory_path() / "cartesio_linetypes.dxf";
        CHECK(writeDxf(doc, path, nullptr, nullptr, version));
        Document loaded;
        const auto result = readDxf(path, loaded);
        std::filesystem::remove(path);
        CHECK(result.ok && loaded.entities().size() == 2);
        if (loaded.entities().size() != 2)
            continue;
        const int assi = loaded.findLayer("Assi");
        CHECK(assi >= 0 && loaded.findLinetype("DASHED") && loaded.findLinetype("hidden"));
        CHECK(loaded.findLinetype("DASHED") && loaded.findLinetype("DASHED")->pattern.size() == 2);
        const Entity& a = *loaded.entities()[0];
        const Entity& b = *loaded.entities()[1];
        CHECK(loaded.effectiveLinetype(a) && loaded.effectiveLinetype(b));
        CHECK(loaded.effectiveLinetype(b) && loaded.effectiveLinetype(b)->name != loaded.effectiveLinetype(a)->name);
        if (version == DxfVersion::R2000) {
            CHECK(approx(loaded.ltscale(), 10.0));
            CHECK(loaded.effectiveLineweight(a) == 50 && loaded.effectiveLineweight(b) == 35);
            CHECK(approx(b.linetypeScale(), 2.0));
        }
    }
}

void testHatchBoundary()
{
    // Rettangolo 100x60 fatto di quattro linee che sporgono, diviso da una verticale a x = 40,
    // con un cerchio (isola) nella parte destra e un quadrato dentro il cerchio (isola nell'isola).
    std::vector<std::unique_ptr<Entity>> owned;
    owned.push_back(std::make_unique<LineEntity>(Vec2{-5, 0}, Vec2{105, 0}));
    owned.push_back(std::make_unique<LineEntity>(Vec2{100, -5}, Vec2{100, 65}));
    owned.push_back(std::make_unique<LineEntity>(Vec2{105, 60}, Vec2{-5, 60}));
    owned.push_back(std::make_unique<LineEntity>(Vec2{0, 65}, Vec2{0, -5}));
    owned.push_back(std::make_unique<LineEntity>(Vec2{40, 0}, Vec2{40, 60}));
    owned.push_back(std::make_unique<CircleEntity>(Vec2{70, 30}, 15));
    owned.push_back(std::make_unique<PolylineEntity>(
        std::vector<PolyVertex>{{{65, 25}, 0}, {{75, 25}, 0}, {{75, 35}, 0}, {{65, 35}, 0}}, true));
    std::vector<const Entity*> all;
    for (const auto& e : owned)
        all.push_back(e.get());

    // A sinistra: solo il rettangolo 40x60, senza isole.
    auto left = findHatchBoundary(all, {20, 30});
    CHECK(left.size() == 1 && left.size() >= 1 && approx(std::abs(signedArea(left[0])), 2400.0, 1e-6));

    // A destra: rettangolo 60x60 con il cerchio e il quadrato come isole.
    auto right = findHatchBoundary(all, {90, 30});
    CHECK(right.size() == 3);
    if (!right.empty())
        CHECK(approx(std::abs(signedArea(right[0])), 3600.0, 1e-6));

    // Dentro il cerchio (fra cerchio e quadrato): il contorno è il cerchio, con il quadrato come isola.
    auto ring = findHatchBoundary(all, {70, 42});
    CHECK(ring.size() == 2);
    if (!ring.empty())
        CHECK(std::abs(std::abs(signedArea(ring[0])) - kPi * 225.0) < 5.0);

    // Fuori da tutto: nessun contorno.
    CHECK(findHatchBoundary(all, {200, 30}).empty());

    // Il comando: due punti interni, poi Invio, un solo tratteggio con tre contorni + quattro.
    Fixture f;
    for (auto& e : owned)
        f.doc.add(e->clone());
    f.commands.execute("HATCH", f.ctx);
    f.commands.point(f.ctx, {20, 30});
    f.commands.point(f.ctx, {90, 30});
    f.commands.enter(f.ctx);
    CHECK(!f.commands.isActive());
    const auto& last = *f.doc.entities().back();
    CHECK(last.type() == EntityType::Hatch && static_cast<const HatchEntity&>(last).loops.size() == 4);
}

void testChamferBreakJoinStretch()
{
    // CHAMFER 10/5 fra due linee che si incontrano in (0,0).
    {
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{-50, 0}, Vec2{5, 0}));
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, -5}, Vec2{0, 50}));
        f.commands.execute("CHAMFER", f.ctx);
        f.commands.keyword(f.ctx, "D");
        f.commands.value(f.ctx, 10);
        f.commands.value(f.ctx, 5);
        f.commands.point(f.ctx, {-30, 0});
        f.commands.point(f.ctx, {0, 30});
        CHECK(!f.commands.isActive() && f.doc.entities().size() == 3);
        if (f.doc.entities().size() == 3) {
            const auto& a = f.at<LineEntity>(0);
            const auto& b = f.at<LineEntity>(1);
            const auto& c = f.at<LineEntity>(2);
            CHECK(approx(a.start, {-50, 0}) && approx(a.end, {-10, 0}));
            CHECK(approx(b.start, {0, 50}) && approx(b.end, {0, 5}));
            CHECK(approx(c.start, {-10, 0}) && approx(c.end, {0, 5}));
        }
    }
    // BREAK: linea interrotta fra due punti; "@" spezza in un punto; cerchio -> arco.
    {
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100, 0}));
        f.commands.execute("BREAK", f.ctx);
        f.commands.point(f.ctx, {30, 0});
        f.commands.point(f.ctx, {60, 0});
        CHECK(f.doc.entities().size() == 2);
        CHECK(approx(f.at<LineEntity>(0).end, {30, 0}) && approx(f.at<LineEntity>(1).start, {60, 0}));

        f.commands.execute("BREAKATPOINT", f.ctx);
        f.commands.point(f.ctx, {80, 0});
        f.commands.point(f.ctx, {80, 0});
        CHECK(f.doc.entities().size() == 3);

        const CircleEntity circle({0, 0}, 10);
        auto arc = breakEntity(circle, {10, 0}, {0, 10});   // si toglie il quarto da 0° a 90°
        CHECK(arc && arc->size() == 1 && (*arc)[0]->type() == EntityType::Arc);
        if (arc && arc->size() == 1) {
            const auto& a = static_cast<const ArcEntity&>(*(*arc)[0]);
            CHECK(approx(a.sweep(), 1.5 * kPi, 1e-9));
        }
        PolylineEntity square({{{0, 0}, 0}, {{10, 0}, 0}, {{10, 10}, 0}, {{0, 10}, 0}}, true);
        auto open = breakEntity(square, {5, 0}, {5, 0});
        CHECK(open && open->size() == 1 && static_cast<const PolylineEntity&>(*(*open)[0]).vertices.size() == 6);
    }
    // JOIN: due linee allineate -> una; tre tratti che si toccano -> polilinea chiusa; due archi -> cerchio.
    {
        std::vector<std::unique_ptr<Entity>> owned;
        owned.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        owned.push_back(std::make_unique<LineEntity>(Vec2{10, 0}, Vec2{25, 0}));
        owned.push_back(std::make_unique<LineEntity>(Vec2{0, 20}, Vec2{10, 20}));
        owned.push_back(std::make_unique<LineEntity>(Vec2{10, 20}, Vec2{5, 30}));
        owned.push_back(std::make_unique<LineEntity>(Vec2{0, 20}, Vec2{5, 30}));
        owned.push_back(std::make_unique<ArcEntity>(Vec2{100, 0}, 5, 0.0, kPi));
        owned.push_back(std::make_unique<ArcEntity>(Vec2{100, 0}, 5, kPi, 2.0 * kPi));
        std::vector<const Entity*> all;
        for (std::size_t i = 0; i < owned.size(); ++i) {
            owned[i]->setId(i + 1);
            all.push_back(owned[i].get());
        }
        std::vector<EntityId> consumed;
        auto joined = joinEntities(all, 1e-6, consumed);
        CHECK(consumed.size() == 7 && joined.size() == 3);
        int lines = 0, closedPolys = 0, circles = 0;
        for (const auto& e : joined) {
            if (e->type() == EntityType::Line) {
                ++lines;
                CHECK(approx(distance(static_cast<const LineEntity&>(*e).start, static_cast<const LineEntity&>(*e).end), 25));
            }
            if (e->type() == EntityType::Polyline && static_cast<const PolylineEntity&>(*e).closed
                && static_cast<const PolylineEntity&>(*e).vertices.size() == 3)
                ++closedPolys;
            circles += e->type() == EntityType::Circle;
        }
        CHECK(lines == 1 && closedPolys == 1 && circles == 1);
    }
    // STRETCH: rettangolo (polilinea) stirato a destra; la linea interamente nella finestra si sposta.
    {
        Fixture f;
        f.doc.add(std::make_unique<PolylineEntity>(
            std::vector<PolyVertex>{{{0, 0}, 0}, {{10, 0}, 0}, {{10, 5}, 0}, {{0, 5}, 0}}, true));
        f.doc.add(std::make_unique<LineEntity>(Vec2{8, 1}, Vec2{9, 4}));
        f.commands.execute("STRETCH", f.ctx);
        f.commands.point(f.ctx, {12, 6});
        f.commands.point(f.ctx, {7, -1});
        f.commands.point(f.ctx, {0, 0});
        f.commands.point(f.ctx, {5, 0});
        CHECK(!f.commands.isActive());
        const auto& rect = f.at<PolylineEntity>(0);
        CHECK(approx(rect.vertices[1].point, {15, 0}) && approx(rect.vertices[2].point, {15, 5}));
        CHECK(approx(rect.vertices[0].point, {0, 0}));
        CHECK(approx(f.at<LineEntity>(1).start, {13, 1}));
    }
}

void testDimStylesAndLeaders()
{
    Fixture f;
    // Stile "Architettonico": testo 3.5, trattini obliqui, suffisso, misure in cm.
    DimStyle arch;
    arch.name = "Architettonico";
    arch.textHeight = 3.5;
    arch.tickSize = 1.5;
    arch.linearFactor = 0.1;
    arch.decimals = 1;
    arch.post = "<> cm";
    f.doc.setDimStyle(arch);
    f.doc.setCurrentDimStyle("Architettonico");

    f.commands.execute("DIMLINEAR", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.point(f.ctx, {1234, 0});
    f.commands.point(f.ctx, {600, 100});
    CHECK(f.doc.entities().size() == 1);
    const auto& dim = f.at<DimensionEntity>(0);
    CHECK(dim.style.name == "Architettonico" && dim.displayText() == "123.4 cm");
    bool ticks = false;
    for (const auto& part : dim.geometry())
        ticks = ticks || (part->type() == EntityType::Line && part->bounds().width() > 0 && part->bounds().height() > 0);
    CHECK(ticks);

    // Modificare lo stile aggiorna le quote (passo annullabile).
    arch.decimals = 0;
    f.doc.setDimStyle(arch);
    CHECK(f.doc.restyleDimensions("Architettonico") == 1);
    CHECK(f.at<DimensionEntity>(0).displayText() == "123 cm");
    f.doc.undo();
    CHECK(f.at<DimensionEntity>(0).displayText() == "123.4 cm");

    // Direttrice con due righe di testo.
    f.commands.execute("MLEADER", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.point(f.ctx, {20, 20});
    f.commands.enter(f.ctx);
    f.commands.text(f.ctx, "Muro portante");
    f.commands.text(f.ctx, "sp. 30 cm");
    f.commands.enter(f.ctx);
    CHECK(!f.commands.isActive() && f.doc.entities().size() == 2);
    const auto& leader = f.at<LeaderEntity>(1);
    CHECK(leader.points.size() == 3 && leader.text == "Muro portante\\Psp. 30 cm" && approx(leader.textHeight, 3.5));

    // DXF 2000: stili, quota con il suo stile, direttrice con il testo (LEADER + MTEXT collegato).
    const auto path = std::filesystem::temp_directory_path() / "cartesio_dimstyle.dxf";
    CHECK(writeDxf(f.doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok);
    const DimStyle* back = loaded.findDimStyle("Architettonico");
    CHECK(back && approx(back->textHeight, 3.5) && approx(back->linearFactor, 0.1) && back->post == "<> cm");
    CHECK(loaded.currentDimStyle().name == "Architettonico");
    int dims = 0, leaders = 0, mtexts = 0;
    for (const auto& e : loaded.entities()) {
        if (e->type() == EntityType::Dimension) {
            ++dims;
            CHECK(static_cast<const DimensionEntity&>(*e).style.name == "Architettonico");
        }
        if (e->type() == EntityType::Leader) {
            ++leaders;
            const auto& l = static_cast<const LeaderEntity&>(*e);
            CHECK(l.points.size() == 3 && l.text == "Muro portante\\Psp. 30 cm");
        }
        mtexts += e->type() == EntityType::MText;
    }
    CHECK(dims == 1 && leaders == 1 && mtexts == 0);
}

void testHatchPatternsPreserved()
{
    Document doc;
    // Tratteggio con un motivo "personalizzato" (come quelli dei DWG), colore RGB e trasparenza.
    auto hatch = std::make_unique<HatchEntity>();
    hatch->loops.push_back({{{0, 0}, 0}, {{100, 0}, 0}, {{100, 50}, 0}, {{0, 50}, 0}});
    hatch->solid = false;
    hatch->pattern = "MIO-MOTIVO";
    hatch->patternType = 2;
    hatch->patternScale = 2.0;
    hatch->patternLines.push_back({degToRad(30.0), {1, 2}, {-3, 5}, {6, -2, 0, -2}});
    hatch->patternLines.push_back({degToRad(120.0), {0, 0}, {4, 4}, {}});
    hatch->setTrueColor(0x8040C0);
    hatch->setTransparency(0x02000000 | 128);
    doc.add(std::move(hatch));
    // Sfumatura: riempimento pieno con i dati 450... conservati.
    auto grad = std::make_unique<HatchEntity>();
    grad->loops.push_back({{{200, 0}, 0}, {{250, 0}, 0}, {{250, 50}, 0}});
    grad->gradient = {{450, "1"}, {451, "0"}, {452, "0"}, {453, "2"}, {460, "0.0"}, {461, "0.0"},
                      {463, "0.0"}, {63, "5"}, {421, "255"}, {463, "1.0"}, {63, "2"}, {421, "16776960"},
                      {470, "LINEAR"}};
    doc.add(std::move(grad));

    const auto path = std::filesystem::temp_directory_path() / "cartesio_hatch_patterns.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok && loaded.entities().size() == 2);
    if (loaded.entities().size() != 2)
        return;
    const auto& h = static_cast<const HatchEntity&>(*loaded.entities()[0]);
    CHECK(h.pattern == "MIO-MOTIVO" && h.patternType == 2 && h.patternLines.size() == 2);
    if (h.patternLines.size() == 2) {
        const auto& l = h.patternLines[0];
        CHECK(approx(l.angle, degToRad(30.0), 1e-9) && approx(l.base, {1, 2}) && approx(l.offset, {-3, 5}));
        CHECK(l.dashes.size() == 4 && approx(l.dashes[0], 6) && approx(l.dashes[3], -2));
        CHECK(h.patternLines[1].dashes.empty());
    }
    CHECK(h.trueColor() == 0x8040C0 && h.transparency() == (0x02000000 | 128));
    const auto& g = static_cast<const HatchEntity&>(*loaded.entities()[1]);
    CHECK(g.gradient.size() == 13 && g.gradient.back().second == "LINEAR");

    // Spostare e scalare il tratteggio sposta e scala anche il motivo.
    auto moved = h.clone();
    moved->transform(Affine::scaling({0, 0}, 2.0));
    const auto& m = static_cast<const HatchEntity&>(*moved);
    CHECK(approx(m.patternLines[0].base, {2, 4}) && approx(m.patternLines[0].dashes[0], 12));
    // Motivi standard: ANSI31 a 45° con passo 3.
    const auto ansi = standardHatchPattern("ANSI31", 1.0, 0.0);
    CHECK(ansi.size() == 1 && approx(ansi[0].angle, kPi / 4) && approx(ansi[0].offset.length(), 3.0, 1e-9));
}

void testExtraCommands()
{
    // POLYGON: esagono inscritto di raggio 10.
    {
        Fixture f;
        f.commands.execute("POLYGON", f.ctx);
        f.commands.value(f.ctx, 6);
        f.commands.point(f.ctx, {0, 0});
        f.commands.keyword(f.ctx, "I");
        f.commands.point(f.ctx, {10, 0});
        CHECK(f.doc.entities().size() == 1);
        const auto& p = f.at<PolylineEntity>(0);
        CHECK(p.closed && p.vertices.size() == 6 && approx(distance(p.vertices[3].point, {0, 0}), 10.0));
    }
    // DIVIDE in 4 e MEASURE ogni 30 su una linea lunga 100.
    {
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100, 0}));
        f.commands.execute("DIVIDE", f.ctx);
        f.commands.point(f.ctx, {50, 0});
        f.commands.value(f.ctx, 4);
        CHECK(f.doc.entities().size() == 4);
        CHECK(approx(f.at<PointEntity>(2).position, {50, 0}));
        f.commands.execute("MEASURE", f.ctx);
        f.commands.point(f.ctx, {1, 0});
        f.commands.value(f.ctx, 30);
        CHECK(f.doc.entities().size() == 7);
        CHECK(approx(f.at<PointEntity>(6).position, {90, 0}));
    }
    // LENGTHEN Totale e Delta; ALIGN con rotazione.
    {
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        f.commands.execute("LENGTHEN", f.ctx);
        f.commands.keyword(f.ctx, "T");
        f.commands.value(f.ctx, 25);
        f.commands.point(f.ctx, {9, 0});
        f.commands.enter(f.ctx);
        CHECK(approx(f.at<LineEntity>(0).end, {25, 0}));
        f.commands.execute("LENGTHEN", f.ctx);
        f.commands.keyword(f.ctx, "DE");
        f.commands.value(f.ctx, -5);
        f.commands.point(f.ctx, {1, 0});
        f.commands.enter(f.ctx);
        CHECK(approx(f.at<LineEntity>(0).start, {5, 0}));

        f.selection = {f.doc.entities()[0]->id()};
        f.commands.execute("ALIGN", f.ctx);
        f.commands.point(f.ctx, {5, 0});
        f.commands.point(f.ctx, {100, 100});
        f.commands.point(f.ctx, {25, 0});
        f.commands.point(f.ctx, {100, 200});
        f.commands.keyword(f.ctx, "N");
        const auto& l = f.at<LineEntity>(0);
        CHECK(approx(l.start, {100, 100}, 1e-9) && approx(l.end, {100, 120}, 1e-9));
    }
    // OVERKILL: una linea doppia (invertita) e una contenuta in un'altra; PURGE.
    {
        Fixture f;
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
        f.doc.add(std::make_unique<LineEntity>(Vec2{10, 0}, Vec2{0, 0}));
        f.doc.add(std::make_unique<LineEntity>(Vec2{2, 0}, Vec2{5, 0}));
        f.doc.add(std::make_unique<LineEntity>(Vec2{0, 5}, Vec2{10, 5}));
        for (const auto& e : f.doc.entities())
            f.selection.push_back(e->id());
        f.commands.execute("OVERKILL", f.ctx);
        CHECK(f.doc.entities().size() == 2);

        f.doc.addLayer(Layer{"Vuoto", 7, true});
        auto block = std::make_shared<BlockDef>();
        block->name = "INUTILE";
        block->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 1}));
        f.doc.addBlock(block);
        f.doc.loadStandardLinetype("DASHED");
        f.commands.execute("PURGE", f.ctx);
        f.commands.enter(f.ctx);
        CHECK(f.doc.findLayer("Vuoto") < 0 && !f.doc.findBlock("INUTILE") && !f.doc.findLinetype("DASHED"));
    }
    // REVCLOUD rettangolare e XLINE: salvataggio DXF e estensioni.
    {
        Fixture f;
        f.commands.execute("REVCLOUD", f.ctx);
        f.commands.point(f.ctx, {0, 0});
        f.commands.point(f.ctx, {100, 50});
        CHECK(f.doc.entities().size() == 1);
        const auto& cloud = f.at<PolylineEntity>(0);
        CHECK(cloud.closed && cloud.vertices.size() >= 30 && cloud.vertices[0].bulge < 0.0);
        f.commands.execute("XLINE", f.ctx);
        f.commands.point(f.ctx, {0, 0});
        f.commands.point(f.ctx, {1, 1});
        f.commands.enter(f.ctx);
        CHECK(f.doc.entities().size() == 2);
        const BBox ext = f.doc.extents(0);
        CHECK(ext.max.x < 200 && ext.min.x > -50);   // la linea infinita non conta
        const auto path = std::filesystem::temp_directory_path() / "cartesio_xline.dxf";
        CHECK(writeDxf(f.doc, path, nullptr, nullptr, DxfVersion::R2000));
        Document loaded;
        CHECK(readDxf(path, loaded).ok);
        std::filesystem::remove(path);
        bool found = false;
        for (const auto& e : loaded.entities())
            if (e->type() == EntityType::XLine) {
                const auto& x = static_cast<const XLineEntity&>(*e);
                found = !x.ray && approx(x.direction, Vec2{std::sqrt(0.5), std::sqrt(0.5)}, 1e-9);
            }
        CHECK(found);
    }
}

void testWipeoutMlineTableGroup()
{
    Fixture f;
    // WIPEOUT con tre punti.
    f.commands.execute("WIPEOUT", f.ctx);
    f.commands.point(f.ctx, {0, 0});
    f.commands.point(f.ctx, {40, 0});
    f.commands.point(f.ctx, {20, 30});
    f.commands.enter(f.ctx);
    // MLINE chiusa a tre vertici, giustificazione Zero, scala 10.
    f.commands.execute("MLINE", f.ctx);
    f.commands.keyword(f.ctx, "G");
    f.commands.keyword(f.ctx, "Z");
    f.commands.keyword(f.ctx, "S");
    f.commands.value(f.ctx, 10);
    f.commands.point(f.ctx, {100, 0});
    f.commands.point(f.ctx, {200, 0});
    f.commands.point(f.ctx, {200, 100});
    f.commands.keyword(f.ctx, "C");
    // TABLE 3 colonne x 2 righe, poi due celle.
    f.commands.execute("TABLE", f.ctx);
    f.commands.value(f.ctx, 3);
    f.commands.value(f.ctx, 2);
    f.commands.value(f.ctx, 30);
    f.commands.point(f.ctx, {0, 200});
    f.commands.execute("TABLEDIT", f.ctx);
    f.commands.point(f.ctx, {5, 198});
    f.commands.text(f.ctx, "Codice");
    f.commands.text(f.ctx, "Descrizione");
    f.commands.enter(f.ctx);   // (0,2) resta vuota
    f.commands.enter(f.ctx);   // (1,0) resta vuota
    f.commands.text(f.ctx, "A1");
    CHECK(f.doc.entities().size() == 3);
    if (f.doc.entities().size() != 3)
        return;
    const auto& m = f.at<MlineEntity>(1);
    CHECK(m.closed && approx(m.offsets[0], 5.0) && approx(m.offsets[1], -5.0));
    const auto lines = m.elementLines();
    CHECK(approx(lines[0][1], {195, 5}, 1e-9));   // giunzione interna allo spigolo
    const auto& t = f.at<TableEntity>(2);
    CHECK(t.cell(0, 0) == "Codice" && t.cell(0, 1) == "Descrizione" && t.cell(1, 1) == "A1");
    CHECK(t.cellAt({65, 195}) == std::make_pair(0, 2));

    // GROUP: selezionando un oggetto si seleziona tutto il gruppo.
    f.selection = {f.doc.entities()[0]->id(), f.doc.entities()[1]->id()};
    f.commands.execute("GROUP", f.ctx);
    f.commands.text(f.ctx, "Pianta");
    CHECK(f.doc.groups().size() == 1 && f.doc.groups()[0].name == "Pianta");
    std::vector<EntityId> picked{f.doc.entities()[1]->id()};
    f.doc.expandGroups(picked);
    CHECK(picked.size() == 2);

    // Salvataggio DXF 2000 e rilettura: copertura, multilinea, tabella e gruppo tornano uguali.
    const auto path = std::filesystem::temp_directory_path() / "cartesio_objects.dxf";
    CHECK(writeDxf(f.doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    int wipeouts = 0, mlines = 0, tables = 0;
    for (const auto& e : loaded.entities()) {
        if (e->type() == EntityType::Wipeout) {
            ++wipeouts;
            const auto& w = static_cast<const WipeoutEntity&>(*e);
            CHECK(w.outline.size() == 3 && approx(w.outline[2], {20, 30}, 1e-9));
        }
        if (e->type() == EntityType::Mline) {
            ++mlines;
            const auto& ml = static_cast<const MlineEntity&>(*e);
            CHECK(ml.closed && ml.vertices.size() == 3 && approx(ml.offsets[0], 5.0, 1e-9));
        }
        if (e->type() == EntityType::Table) {
            ++tables;
            const auto& tb = static_cast<const TableEntity&>(*e);
            CHECK(tb.rows == 2 && tb.columns == 3 && tb.cell(1, 1) == "A1" && approx(tb.position, {0, 200}));
        }
    }
    CHECK(wipeouts == 1 && mlines == 1 && tables == 1);
    CHECK(loaded.groups().size() == 1 && loaded.groups()[0].name == "Pianta" && loaded.groups()[0].ids.size() == 2);
    for (const auto& b : loaded.blocks())
        CHECK(b->name.rfind("*U9", 0) != 0);   // il blocco della grafica della tabella non resta
}

void testPlotSettingsAndFrame()
{
    // Scale standard.
    CHECK(standardScaleTypeFor(1, 100) == 26);
    CHECK(standardScaleTypeFor(2, 1) == 27);
    CHECK(standardScaleTypeFor(1, 25) == -1);

    // Modello: estensioni adattate all'A3 con 10 mm di margine, centrate.
    PlotSettings model = PlotSettings::forModel();
    CHECK(isFitToPaper(model) && isLandscape(model));
    BBox area;
    area.expand(Vec2{0, 0});
    area.expand(Vec2{2000, 1000});
    PlotFrame f = computePlotFrame(model, area);
    CHECK(f.valid && approx(f.scale, 0.2));   // 400 / 2000 contro 277 / 1000
    CHECK(approx(f.placed.min, {10, 48.5}) && approx(f.placed.max, {410, 248.5}));

    // Scala 1:100, non centrata, con spostamento: l'angolo dell'area va nell'origine di stampa.
    model.standardScaleType = 26;
    model.setFlag(kPlotCentered, false);
    model.plotOrigin = {5, 7};
    f = computePlotFrame(model, area);
    CHECK(f.valid && approx(f.scale, 0.01));
    CHECK(approx(f.toPaper({0, 0}), {15, 17}) && approx(f.toPaper({2000, 1000}), {35, 27}));
    CHECK(plotScaleText(model) == "1:100");

    // Scala personalizzata 1 mm = 25 unità; in pollici 1" = 25 unità.
    model.setFlag(kPlotUseStandardScale, false);
    model.customPaper = 1;
    model.customDrawing = 25;
    CHECK(approx(fixedPlotScale(model), 0.04) && plotScaleText(model) == "1:25");
    model.paperUnits = 0;
    CHECK(approx(fixedPlotScale(model), 25.4 / 25));
    model.paperUnits = 1;

    // Verticale e capovolta.
    setOrientation(model, false, false);
    CHECK(!isLandscape(model) && approx(sheetSize(model).width, 297) && approx(sheetSize(model).height, 420));
    setOrientation(model, true, true);
    CHECK(isLandscape(model) && isUpsideDown(model));
    f = computePlotFrame(model, area);
    CHECK(approx(f.toPaper({0, 0}), {420 - 15, 297 - 17}));

    // Margini diversi: in verticale il margine inferiore del supporto diventa il sinistro.
    PlotSettings m = PlotSettings::forModel();
    m.marginLeft = 1;
    m.marginBottom = 2;
    m.marginRight = 3;
    m.marginTop = 4;
    m.rotation = 1;
    const BBox printable = printableArea(m);
    CHECK(approx(printable.min, {2, 3}) && approx(printable.max, {297 - 4, 420 - 1}));

    // Layout: il foglio va sul foglio a 1:1, qualunque sia la sua origine.
    Layout layout;
    layout.origin = {-10, -10};
    PlotSettings ls = layoutPlotSettings(layout);
    BBox sheetArea;
    sheetArea.expand(layout.origin);
    sheetArea.expand(layout.origin + Vec2{layout.paperWidth, layout.paperHeight});
    f = computePlotFrame(ls, sheetArea);
    CHECK(f.valid && approx(f.scale, 1) && approx(f.toPaper({-10, -10}), {0, 0}));

    // Formati.
    const MediaFormat* a3 = findMediaByName("A3");
    CHECK(a3 && canonicalMediaName(*a3) == "ISO_full_bleed_A3_(420.00_x_297.00_MM)");
    CHECK(findMediaByName("ISO_A1_(841.00_x_594.00_MM)") == findMediaByName("ISO A1"));
    const MediaFormat* ansiB = findMediaByName("ANSI_full_bleed_B_(17.00_x_11.00_Inches)");
    CHECK(ansiB && std::string(ansiB->name) == "ANSI B");
    CHECK(findMediaBySize(297, 420) == a3 && mediaDisplayName(300, 200) == "300 x 200 mm");

    // Layout in verticale: applyPlotSettings cambia foglio e formato.
    PlotSettings portrait = layoutPlotSettings(layout);
    portrait.paperWidth = 297;
    portrait.paperHeight = 210;
    portrait.rotation = 1;
    applyPlotSettings(layout, portrait);
    CHECK(approx(layout.paperWidth, 210) && approx(layout.paperHeight, 297) && layout.paperName == "A4");
    CHECK(approx(layoutPlotSettings(layout).paperWidth, 297));

    // Timbro.
    PlotStampSettings stamp;
    PlotStampValues values{"pianta.dxf", "Layout1", "16/11/2026 10:00", "utente", "PDF", "A3", "1:100"};
    CHECK(plotStampText(stamp, values) == "pianta.dxf, Layout1, 16/11/2026 10:00, PDF, 1:100");
    stamp.user1 = "Rev. B";
    stamp.layoutName = false;
    CHECK(plotStampText(stamp, values) == "pianta.dxf, 16/11/2026 10:00, PDF, 1:100, Rev. B");
}

void testPlotStyleTables()
{
    const auto mono = builtinPlotStyleTable("Nero.ctb");
    CHECK(mono && !mono->forAci(1).hasObjectColor() && mono->forAci(1).rgb() == 0);
    CHECK(mono && mono->forAci(1).color == -1040187392);   // 0xC2000000 con segno
    const auto gray = builtinPlotStyleTable("Grigi.ctb");
    CHECK(gray && gray->forAci(3).grayscale() && gray->forAci(3).hasObjectColor());
    const auto screen = builtinPlotStyleTable("Retino 50%.ctb");
    CHECK(screen && screen->forAci(200).screen == 50);
    // Solo i nomi di CartesioCAD sono predefiniti.
    CHECK(!builtinPlotStyleTable("monochrome.ctb"));
    CHECK(!builtinPlotStyleTable("inesistente.ctb"));

    // Andata e ritorno del testo, con colore, spessore e retinatura personalizzati.
    PlotStyleTable t;
    t.description = "Prova";
    t.forAci(1).setRgb(0x112233);
    t.forAci(1).setDithering(false);
    t.forAci(1).lineweight = t.lineweightIndex(0.5);
    t.forAci(2).screen = 30;
    t.forAci(2).description = "Linee sottili";
    const std::string text = t.toText();
    CHECK(text.find("description=\"Prova\n") == 0);
    CHECK(text.find(" 0{\n  name=\"Color_1\n") != std::string::npos);
    std::string error;
    const auto back = PlotStyleTable::fromText(text, &error);
    CHECK(back.has_value());
    if (back) {
        CHECK(back->description == "Prova");
        CHECK(back->forAci(1).rgb() == 0x112233 && !back->forAci(1).dithering());
        CHECK(approx(back->lineweightMm(back->forAci(1)), 0.5));
        CHECK(back->forAci(2).screen == 30 && back->forAci(2).description == "Linee sottili");
        CHECK(back->forAci(3).hasObjectColor() && back->lineweightMm(back->forAci(3)) == 0.0);
        CHECK(back->lineweights.size() == 27);
        CHECK(back->toText() == text);
    }

    // Testo scritto da un altro CAD: a capo \r\n, valori con appendice, colore 0xC3FFFFFF, NUL finale.
    const std::string acad =
        "description=\"\r\naci_table_available=TRUE\r\nscale_factor=1.0\r\napply_factor=FALSE\r\n"
        "custom_lineweight_display_units=0\r\nplot_style{\r\n 0{\r\n  name=\"Color_1\r\n"
        "  color=-1006632961\r\n  color_policy=3\r\n  screen=100\r\n  lineweight=8\r\n"
        "  linepattern_size=0.5000000000000 (+7.Z+\"8V?S_LC )\r\n }\r\n 6{\r\n  name=\"Color_7\r\n"
        "  color=-1023410176\r\n  mode_color=-1040187392\r\n  color_policy=1\r\n }\r\n}\r\n"
        "custom_lineweight_table{\r\n 0=0.00\r\n 8=0.25\r\n}\r\n";
    const auto parsed = PlotStyleTable::fromText(acad + std::string(1, '\0'), &error);
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->forAci(1).hasObjectColor() && parsed->forAci(1).grayscale());
        CHECK(approx(parsed->lineweightMm(parsed->forAci(1)), 0.25));
        CHECK(approx(parsed->forAci(1).linepatternSize, 0.5));
        CHECK(!parsed->forAci(7).hasObjectColor() && parsed->forAci(7).rgb() == 0);
    }
    CHECK(!PlotStyleTable::fromText("plot_style{\n 0{\n", &error) && !error.empty());

    // Intestazione del file .ctb.
    CHECK(adler32("Wikipedia") == 0x11E60398u);
    const std::string bytes = ctbFileBytes("zlibdata", 1234);
    CHECK(bytes.size() == 68 && bytes.substr(0, 11) == "PIAFILEVERS");
    const auto payload = ctbPayload(bytes);
    CHECK(payload && payload->zlibData == "zlibdata" && payload->textLength == 1234);
    CHECK(!ctbPayload("PIAFILEVERSION_2.0,STBVER1,compress\r\npmzlibcodec" + std::string(20, 'x'), &error));
}

void testPlotSettingsInDxf()
{
    Document doc;
    PlotSettings model = PlotSettings::forModel();
    model.plotType = PlotType::Window;
    model.windowMin = {10, 20};
    model.windowMax = {500, 300};
    model.standardScaleType = 25;   // 1:50
    model.styleSheet = "Nero.ctb";
    model.rotation = 2;
    doc.setModelPlot(model);
    doc.setLimits({-5, -6}, {841, 594});

    Layout* layout = doc.findLayout(doc.layouts().front().id);
    PlotSettings ls = layoutPlotSettings(*layout);
    ls.styleSheet = "Grigi.ctb";
    ls.setFlag(kPlotScaleLineweights, true);
    ls.setFlag(kPlotCentered, true);
    ls.marginLeft = 5;
    ls.marginBottom = 6;
    ls.marginRight = 7;
    ls.marginTop = 8;
    applyPlotSettings(*layout, ls);

    PlotSettings setup = PlotSettings::forModel();
    setup.pageSetupName = "A1 pianta";
    setup.paperWidth = 841;
    setup.paperHeight = 594;
    setup.setFlag(kPlotUseStandardScale, false);
    setup.customPaper = 1;
    setup.customDrawing = 200;
    doc.setPageSetup(setup);
    PlotSettings setup2 = setup;
    setup2.pageSetupName = "Bozza";
    setup2.plotType = PlotType::Limits;
    doc.setPageSetup(setup2);
    setup2.styleSheet = "Colori.ctb";
    doc.setPageSetup(setup2);   // sostituisce, non aggiunge
    CHECK(doc.pageSetups().size() == 2 && doc.findPageSetup("bozza")->styleSheet == "Colori.ctb");

    const auto path = std::filesystem::temp_directory_path() / "cartesio_plot.dxf";
    std::string error;
    CHECK(writeDxf(doc, path, &error, nullptr, DxfVersion::R2000));
    Document loaded;
    const auto result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok);

    const PlotSettings& m = loaded.modelPlot();
    CHECK(m.plotType == PlotType::Window && approx(m.windowMin, {10, 20}) && approx(m.windowMax, {500, 300}));
    CHECK(m.standardScaleType == 25 && m.styleSheet == "Nero.ctb" && m.rotation == 2);
    CHECK(m.flag(kPlotModelType) && m.flag(kPlotCentered) && approx(m.marginLeft, 10));
    CHECK(approx(loaded.limitsMin(), {-5, -6}) && approx(loaded.limitsMax(), {841, 594}));

    CHECK(loaded.layouts().size() == 1);
    if (!loaded.layouts().empty()) {
        const Layout& l = loaded.layouts().front();
        const PlotSettings p = layoutPlotSettings(l);
        CHECK(p.styleSheet == "Grigi.ctb" && p.flag(kPlotScaleLineweights) && p.flag(kPlotCentered));
        CHECK(approx(p.marginLeft, 5) && approx(p.marginTop, 8) && approx(l.margin, 8));
        CHECK(approx(l.paperWidth, 420) && approx(l.paperHeight, 297) && l.paperName == "A3");
        CHECK(p.mediaName == "ISO_full_bleed_A3_(420.00_x_297.00_MM)");
    }

    CHECK(loaded.pageSetups().size() == 2);
    if (const PlotSettings* s = loaded.findPageSetup("A1 pianta")) {
        CHECK(approx(s->paperWidth, 841) && !s->flag(kPlotUseStandardScale));
        CHECK(approx(fixedPlotScale(*s), 1.0 / 200));
    } else {
        CHECK(false);
    }
    const PlotSettings* bozza = loaded.findPageSetup("Bozza");
    CHECK(bozza && bozza->plotType == PlotType::Limits && bozza->styleSheet == "Colori.ctb");
    CHECK(loaded.removePageSetup("BOZZA") && loaded.pageSetups().size() == 1);
}

void testInsUnitsAndScaledImport()
{
    // $INSUNITS: letto e scritto; senza unità si scrive mm (come prima).
    Document doc;
    doc.setInsUnits(5);   // cm
    doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_units.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    CHECK(loaded.insUnits() == 5);
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R12));
    Document loaded12;
    CHECK(readDxf(path, loaded12).ok && loaded12.insUnits() == 5);
    std::filesystem::remove(path);

    // Un file in cm inserito in un disegno in mm: il blocco viene scalato di 10.
    Document target;
    importDrawingAsBlock(target, doc, "PEZZO", 10.0);
    const auto block = target.findBlock("PEZZO");
    CHECK(block && block->entities.size() == 1);
    if (block && !block->entities.empty()) {
        const auto& line = static_cast<const LineEntity&>(*block->entities.front());
        CHECK(approx(line.end, {100, 0}));
    }
}

void testCullingAndLayoutBlocks()
{
    // entitiesInRect: solo ciò che tocca il rettangolo, senza ciò che è più piccolo di minSize;
    // i punti (ingombro nullo) restano.
    Document doc;
    doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100, 0}));
    doc.add(std::make_unique<LineEntity>(Vec2{50, 50}, Vec2{50.001, 50}));
    doc.add(std::make_unique<LineEntity>(Vec2{500, 500}, Vec2{600, 500}));
    doc.add(std::make_unique<PointEntity>(Vec2{10, 10}));
    BBox view;
    view.expand(Vec2{-10, -10});
    view.expand(Vec2{200, 200});
    CHECK(doc.entitiesInRect(view).size() == 3);
    CHECK(doc.entitiesInRect(view, 0.01).size() == 2);   // la linea minuscola no, il punto sì

    // Ingombri degli oggetti di un blocco, calcolati una volta.
    auto block = std::make_shared<BlockDef>();
    block->name = "B";
    block->entities.push_back(std::make_unique<CircleEntity>(Vec2{0, 0}, 5));
    block->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{20, 0}));
    const auto& b = block->childBounds();
    CHECK(b.size() == 2 && approx(b[0].min, {-5, -5}) && approx(b[1].max, {20, 0}));

    // Un blocco inserito in un layout non attivo (blocco *Paper_SpaceN) deve essere collegato.
    Document src;
    auto symbol = std::make_shared<BlockDef>();
    symbol->name = "SIMBOLO";
    symbol->entities.push_back(std::make_unique<CircleEntity>(Vec2{0, 0}, 2));
    src.addBlock(symbol);
    const int second = src.addLayout("Tavola2");
    src.setActiveSpace(second);
    auto insert = std::make_unique<InsertEntity>("SIMBOLO", Vec2{10, 10});
    src.add(std::move(insert));
    src.setActiveSpace(0);
    const auto path = std::filesystem::temp_directory_path() / "cartesio_layout_blocks.dxf";
    CHECK(writeDxf(src, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    int linked = 0, inserts = 0;
    for (const auto& e : loaded.entities())
        if (e->type() == EntityType::Insert) {
            ++inserts;
            linked += static_cast<const InsertEntity&>(*e).block != nullptr;
        }
    CHECK(inserts == 1 && linked == 1);
}

void testMergeAlternativeConversion()
{
    // `base`: blocco svuotato e layout vuoto; `alt`: blocco completo e layout con oggetti.
    Document base, alt;
    for (Document* d : {&base, &alt}) {
        d->clearLayouts();
        d->addLayout("Tavola");
        d->addLayer(Layer{"Prese", 3, true});
    }
    auto empty = std::make_shared<BlockDef>();
    empty->name = "PUNTO";
    base.addBlock(empty);
    base.add(std::make_unique<InsertEntity>("PUNTO", Vec2{100, 100}));

    auto full = std::make_shared<BlockDef>();
    full->name = "PUNTO";
    full->entities.push_back(std::make_unique<CircleEntity>(Vec2{0, 0}, 3));
    full->entities.push_back(std::make_unique<LineEntity>(Vec2{-3, 0}, Vec2{3, 0}));
    alt.addBlock(full);
    const int altLayout = alt.layouts().front().id;
    auto onSheet = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{420, 297});
    onSheet->setSpace(altLayout);
    onSheet->setLayer(alt.findLayer("Prese"));
    alt.insertLoaded(std::move(onSheet));

    const DwgMergeReport r = mergeAlternativeConversion(base, alt);
    CHECK(r.blocks == 1 && r.layouts == 1 && r.entities == 1);
    CHECK(base.findBlock("PUNTO")->entities.size() == 2);
    int onLayout = 0;
    for (const auto& e : base.entities())
        if (e->space() == base.layouts().front().id) {
            ++onLayout;
            CHECK(base.layers()[static_cast<std::size_t>(e->layer())].name == "Prese");
        }
    CHECK(onLayout == 1);
    // L'inserimento ora usa il blocco completo.
    for (const auto& e : base.entities())
        if (e->type() == EntityType::Insert)
            CHECK(static_cast<const InsertEntity&>(*e).block && static_cast<const InsertEntity&>(*e).block->entities.size() == 2);
    // Una seconda fusione non cambia niente.
    const DwgMergeReport again = mergeAlternativeConversion(base, alt);
    CHECK(again.blocks == 0 && again.layouts == 0);
}

// Immagini raster e riferimenti esterni: scrittura e rilettura in DXF 2000 (solo i percorsi, non i contenuti).
void testImageAndXrefRoundTrip()
{
    Document doc;
    auto image = std::make_unique<ImageEntity>();
    image->insertion = {10, 20};
    image->u = {0.5, 0};
    image->v = {0, 0.5};
    image->width = 200;
    image->height = 100;
    image->path = "foto\logo.png";
    image->fade = 30;
    doc.add(std::move(image));

    auto xref = std::make_shared<BlockDef>();
    xref->name = "pianta";
    xref->xrefPath = "sub\pianta.dwg";
    xref->xrefLoaded = true;
    xref->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 1}));   // non va salvato
    doc.addBlock(xref);
    doc.add(std::make_unique<InsertEntity>("pianta", Vec2{5, 5}));
    doc.addLayer(Layer{"pianta|MURI", 1, true});

    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_xref.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    const DxfReadResult result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok);
    const ImageEntity* im = nullptr;
    for (const auto& e : loaded.entities())
        if (e->type() == EntityType::Image)
            im = static_cast<const ImageEntity*>(e.get());
    CHECK(im != nullptr);
    if (im) {
        CHECK(im->path == "foto\logo.png");
        CHECK(std::abs(im->width - 200) < 1e-9 && std::abs(im->height - 100) < 1e-9);
        CHECK(std::abs(im->u.x - 0.5) < 1e-9 && std::abs(im->insertion.y - 20) < 1e-9);
        CHECK(im->fade == 30);
    }
    const auto block = loaded.findBlock("pianta");
    CHECK(block && block->isXref() && block->xrefPath == "sub\pianta.dwg");
    CHECK(block && block->entities.empty());
    CHECK(loaded.findLayer("pianta|MURI") >= 0);

    // Caricamento: layer e blocchi del file esterno con il prefisso "pianta|", layer 0 invariato.
    Document source;
    const int walls = source.addLayer(Layer{"MURI", 1, true});
    auto door = std::make_shared<BlockDef>();
    door->name = "PORTA";
    door->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{0, 90}));
    source.addBlock(door);
    source.setCurrentLayer(walls);
    source.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{400, 0}));
    source.setCurrentLayer(0);
    source.add(std::make_unique<InsertEntity>("PORTA", Vec2{100, 0}));
    if (block) {
        loadXref(loaded, *block, source, 10.0);
        const auto filled = loaded.findBlock("pianta");
        CHECK(filled && filled->xrefLoaded && filled->isXref() && filled->entities.size() == 2);
        CHECK(loaded.findBlock("pianta|PORTA") != nullptr);
        CHECK(loaded.findLayer("MURI") < 0);
        if (filled && filled->entities.size() == 2) {
            const auto& line = *filled->entities[0];
            CHECK(loaded.layers()[static_cast<std::size_t>(line.layer())].name == "pianta|MURI");
            CHECK(std::abs(static_cast<const LineEntity&>(line).end.x - 4000) < 1e-9);   // scala delle unità
            CHECK(filled->entities[1]->layer() == 0);
            CHECK(static_cast<const InsertEntity&>(*filled->entities[1]).blockName == "pianta|PORTA");
        }
    }
}

// Oggetti OLE (OLE2FRAME): dati riscritti uguali, vertici aggiornati nell'intestazione.
void testOleRoundTrip()
{
    Document doc;
    auto ole = std::make_unique<ImageEntity>();
    std::string data(2 + 12 * 8, '\0');
    data[0] = static_cast<char>(0x80);
    data[1] = 0x55;
    data += std::string("\xD0\xCF\x11\xE0 dati dell'oggetto \x00\x01\xFF", 27);
    ole->insertion = {10, 20};
    ole->width = 1000;
    ole->height = 500;
    ole->u = {0.04, 0};
    ole->v = {0, 0.04};
    ole->ole = std::make_shared<const std::string>(data);
    ole->oleType = "Paintbrush Picture";
    doc.add(std::move(ole));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_ole.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    const DxfReadResult result = readDxf(path, loaded);
    std::filesystem::remove(path);
    CHECK(result.ok && result.skippedTypes.empty());
    const ImageEntity* im = nullptr;
    for (const auto& e : loaded.entities())
        if (e->type() == EntityType::Image)
            im = static_cast<const ImageEntity*>(e.get());
    CHECK(im && im->isOle());
    if (im && im->isOle()) {
        CHECK(im->ole->size() == data.size());
        CHECK(im->ole->substr(98) == data.substr(98));   // il contenuto resta uguale
        CHECK(im->oleType == "Paintbrush Picture");
        const auto c = im->corners();
        CHECK(std::abs(c[0].x - 10) < 1e-9 && std::abs(c[0].y - 20) < 1e-9);
        CHECK(std::abs(c[2].x - 50) < 1e-9 && std::abs(c[2].y - 40) < 1e-9);
    }
}

// DRAWORDER: riordino annullabile e ordine conservato nel file.
// find() usa un indice id -> posizione: deve restare giusto dopo aggiunte, riordini, cancellazioni e annulla.
void testFindIndex()
{
    Document doc;
    std::vector<EntityId> ids;
    for (int i = 0; i < 50; ++i)
        ids.push_back(doc.add(std::make_unique<LineEntity>(Vec2{0, double(i)}, Vec2{1, double(i)})));
    const auto findsAll = [&doc](const std::vector<EntityId>& list) {
        for (EntityId id : list) {
            const Entity* e = doc.find(id);
            if (!e || e->id() != id)
                return false;
        }
        return true;
    };
    CHECK(findsAll(ids));
    doc.reorder({ids[40], ids[3]}, Document::DrawOrder::Back);
    CHECK(findsAll(ids));
    doc.erase({ids[10], ids[11]});
    CHECK(doc.find(ids[10]) == nullptr && doc.find(ids[11]) == nullptr);
    std::vector<EntityId> rest = ids;
    rest.erase(rest.begin() + 10, rest.begin() + 12);
    CHECK(findsAll(rest));
    CHECK(doc.undo());
    CHECK(findsAll(ids));
    CHECK(doc.find(987654321) == nullptr);
}

void testDrawOrder()
{
    Document doc;
    const EntityId a = doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0}));
    const EntityId b = doc.add(std::make_unique<LineEntity>(Vec2{0, 1}, Vec2{1, 1}));
    const EntityId c = doc.add(std::make_unique<CircleEntity>(Vec2{0, 0}, 5));
    const auto order = [&doc] {
        std::vector<EntityId> ids;
        for (const auto& e : doc.entities())
            ids.push_back(e->id());
        return ids;
    };
    doc.reorder({c}, Document::DrawOrder::Back);
    CHECK((order() == std::vector<EntityId>{c, a, b}));
    doc.reorder({c}, Document::DrawOrder::Above, a);
    CHECK((order() == std::vector<EntityId>{a, c, b}));
    doc.reorder({a}, Document::DrawOrder::Front);
    CHECK((order() == std::vector<EntityId>{c, b, a}));
    CHECK(doc.undo());
    CHECK((order() == std::vector<EntityId>{a, c, b}));
    CHECK(doc.redo());
    CHECK((order() == std::vector<EntityId>{c, b, a}));
    // Nel file l'ordine è quello di scrittura: riaperto, il cerchio è ancora il primo.
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_order.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    CHECK(!loaded.entities().empty() && loaded.entities().front()->type() == EntityType::Circle);
}

// ATTSYNC: i valori restano per etichetta, i nuovi attributi prendono il predefinito, quelli tolti spariscono.
void testAttributeSync()
{
    Document doc;
    auto block = std::make_shared<BlockDef>();
    block->name = "CARTIGLIO";
    block->entities.push_back(std::make_unique<AttDefEntity>("TITOLO", "Titolo", TextEntity({0, 0}, 2.5, "-")));
    block->entities.push_back(std::make_unique<AttDefEntity>("VECCHIO", "Vecchio", TextEntity({0, 5}, 2.5, "x")));
    doc.addBlock(block);
    auto insert = std::make_unique<InsertEntity>("CARTIGLIO", Vec2{100, 50});
    insert->attributes.push_back({"TITOLO", TextEntity({100, 50}, 2.5, "Pianta piano terra"), 0});
    insert->attributes.push_back({"VECCHIO", TextEntity({100, 55}, 2.5, "da togliere"), 0});
    const EntityId id = doc.add(std::move(insert));

    // Nuova definizione: VECCHIO tolto, REVISIONE aggiunto, TITOLO più alto.
    auto def = std::make_shared<BlockDef>();
    def->name = "CARTIGLIO";
    def->entities.push_back(std::make_unique<AttDefEntity>("TITOLO", "Titolo", TextEntity({0, 0}, 5.0, "-")));
    def->entities.push_back(std::make_unique<AttDefEntity>("REVISIONE", "Revisione", TextEntity({0, -10}, 2.5, "00")));
    doc.addBlock(def);
    CHECK(syncBlockAttributes(doc, "CARTIGLIO") == 1);
    const auto& synced = static_cast<const InsertEntity&>(*doc.find(id));
    CHECK(synced.attributes.size() == 2);
    if (synced.attributes.size() == 2) {
        CHECK(synced.attributes[0].tag == "TITOLO" && synced.attributes[0].text.text == "Pianta piano terra");
        CHECK(std::abs(synced.attributes[0].text.height - 5.0) < 1e-9);
        CHECK(synced.attributes[1].tag == "REVISIONE" && synced.attributes[1].text.text == "00");
        CHECK(std::abs(synced.attributes[1].text.position.y - 40.0) < 1e-9);   // sotto l'inserimento
    }
    CHECK(setAttributeValues(doc, id, {{"REVISIONE", "01"}}));
    CHECK(static_cast<const InsertEntity&>(*doc.find(id)).attributes[1].text.text == "01");
    CHECK(doc.undo());
    CHECK(static_cast<const InsertEntity&>(*doc.find(id)).attributes[1].text.text == "00");
}

// FIND: ricerca (maiuscole, parole intere) e sostituzione annullabile in testi e attributi.
void testFindReplace()
{
    Document doc;
    const EntityId t1 = doc.add(std::make_unique<TextEntity>(Vec2{0, 0}, 2.5, "QF1 - quadro qf1"));
    auto insert = std::make_unique<InsertEntity>("SIMBOLO", Vec2{10, 0});
    insert->attributes.push_back({"SIGLA", TextEntity({10, 0}, 2.5, "QF1/C2"), 0});
    const EntityId i1 = doc.add(std::move(insert));
    doc.add(std::make_unique<TextEntity>(Vec2{0, 5}, 2.5, "QF10"));
    FindOptions options;
    CHECK(findText(doc, "qf1", options).size() == 3);   // senza maiuscole: anche QF10
    options.wholeWord = true;
    CHECK(findText(doc, "qf1", options).size() == 2);   // QF1 (testo e attributo), non QF10
    options.matchCase = true;
    const auto matches = findText(doc, "QF1", options);
    CHECK(matches.size() == 2);
    CHECK(replaceText(doc, matches, "QF1", "QG1", options) == 2);
    CHECK(static_cast<const TextEntity&>(*doc.find(t1)).text == "QG1 - quadro qf1");
    CHECK(static_cast<const InsertEntity&>(*doc.find(i1)).attributes[0].text.text == "QG1/C2");
    CHECK(doc.undo());
    CHECK(static_cast<const TextEntity&>(*doc.find(t1)).text == "QF1 - quadro qf1");
}

// QSELECT: tipo, layer, attributo con caratteri jolly, esclusione.
void testQuickSelect()
{
    Document doc;
    const int fm = doc.addLayer(Layer{"EL-FM", 3, true});
    doc.setCurrentLayer(fm);
    const EntityId line = doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0}));
    doc.setCurrentLayer(0);
    doc.add(std::make_unique<CircleEntity>(Vec2{0, 0}, 1));
    auto presa = std::make_unique<InsertEntity>("PRESA", Vec2{5, 5});
    presa->attributes.push_back({"QUADRO", TextEntity({5, 5}, 2, "QD1/C3"), 0});
    const EntityId p1 = doc.add(std::move(presa));
    auto altra = std::make_unique<InsertEntity>("PRESA", Vec2{9, 5});
    altra->attributes.push_back({"QUADRO", TextEntity({9, 5}, 2, "QGBT/C1"), 0});
    doc.add(std::move(altra));
    QuickSelectQuery q;
    q.type = EntityType::Line;
    CHECK((quickSelect(doc, q) == std::vector<EntityId>{line}));
    q = {};
    q.property = "layer";
    q.value = "el-fm";
    CHECK((quickSelect(doc, q) == std::vector<EntityId>{line}));
    q = {};
    q.property = "attribute:QUADRO";
    q.op = QuickSelectQuery::Op::Wildcard;
    q.value = "QD*";
    CHECK((quickSelect(doc, q) == std::vector<EntityId>{p1}));
    q.exclude = true;
    CHECK(quickSelect(doc, q).size() == 3);
    CHECK(wildcardMatches("EL-?M", "el-fm") && !wildcardMatches("QD*", "QGBT"));
}

// ESTRAIDATI: blocchi con attributi, conteggio per distinta, oggetti con misure; separatori, virgolette e formule.
void testDataExport()
{
    Document doc;
    const int fm = doc.addLayer(Layer{"EL-FM", 3, true});
    doc.setCurrentLayer(fm);
    const EntityId line = doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{3, 4}));
    doc.add(std::make_unique<CircleEntity>(Vec2{10, 10}, 1));
    doc.add(std::make_unique<PolylineEntity>(std::vector<PolyVertex>{{{0, 0}}, {{2, 0}}, {{2, 1}}, {{0, 1}}}, true));
    doc.setCurrentLayer(0);
    auto a = std::make_unique<InsertEntity>("PRESA", Vec2{5.5, 5});
    a->rotation = kPi / 2.0;
    a->attributes.push_back({"quadro", TextEntity({5, 5}, 2, "QD1"), 0});
    a->attributes.push_back({"CIRCUITO", TextEntity({5, 5}, 2, "C3; FM"), 0});
    const EntityId p1 = doc.add(std::move(a));
    auto b = std::make_unique<InsertEntity>("PRESA", Vec2{9, 5});
    b->attributes.push_back({"QUADRO", TextEntity({9, 5}, 2, "QD1"), 0});
    b->attributes.push_back({"CIRCUITO", TextEntity({9, 5}, 2, "C3; FM"), 0});
    doc.add(std::move(b));
    auto c = std::make_unique<InsertEntity>("LUCE", Vec2{1, 1});
    c->attributes.push_back({"NOTE", TextEntity({1, 1}, 2, "=CMD()"), 0});
    doc.add(std::move(c));

    DataExportOptions o;
    DataExportResult r = exportData(doc, o);
    CHECK(r.rows == 3);
    CHECK(r.csv.rfind("Blocco;Layer;Spazio;X;Y;Rotazione;Scala X;Scala Y;QUADRO;CIRCUITO;NOTE\r\n", 0) == 0);
    CHECK(r.csv.find("PRESA;0;Modello;5,5;5;90;1;1;QD1;\"C3; FM\";\r\n") != std::string::npos);
    CHECK(r.csv.find("'=CMD()") != std::string::npos);   // mai una formula nel foglio di calcolo

    o.mode = DataExportOptions::Mode::Count;
    r = exportData(doc, o);
    CHECK(r.rows == 2);
    CHECK(r.csv.find("PRESA;QD1;\"C3; FM\";;2\r\n") != std::string::npos);
    CHECK(r.csv.find("LUCE;;;'=CMD();1\r\n") != std::string::npos);

    o.mode = DataExportOptions::Mode::Objects;
    o.separator = ',';
    r = exportData(doc, o);
    CHECK(r.rows == 6);
    CHECK(r.csv.find("Linea,EL-FM,") != std::string::npos);
    CHECK(r.csv.find(",0,0,5,,") != std::string::npos);                 // lunghezza della linea 3-4-5, senza area
    CHECK(r.csv.find(",10,10,6.2832,3.1416,") != std::string::npos);    // cerchio: circonferenza e area col punto
    CHECK(r.csv.find(",0,0,6,2,") != std::string::npos);                // rettangolo chiuso 2x1

    o.mode = DataExportOptions::Mode::Blocks;
    o.only = {p1, line};
    r = exportData(doc, o);
    CHECK(r.rows == 1);
    DataExportOptions::Mode m;
    CHECK(parseDataExportMode("conteggio", &m) && m == DataExportOptions::Mode::Count && !parseDataExportMode("x", &m));
}

// Blocchi dinamici: lo stato salvato (*U...) conserva il nome del blocco dinamico, anche dopo il salvataggio.
void testDynamicBlockName()
{
    Document doc;
    auto dynamic = std::make_shared<BlockDef>();
    dynamic->name = "PRESA";
    dynamic->entities.push_back(std::make_unique<CircleEntity>(Vec2{0, 0}, 1));
    doc.addBlock(dynamic);
    auto state = std::make_shared<BlockDef>();
    state->name = "*U12";
    state->dynamicName = "PRESA";
    state->entities.push_back(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 0}));
    doc.addBlock(state);
    doc.add(std::make_unique<InsertEntity>("*U12", Vec2{5, 5}));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_dynamic.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    const InsertEntity* insert = nullptr;
    for (const auto& e : loaded.entities())
        if (e->type() == EntityType::Insert)
            insert = static_cast<const InsertEntity*>(e.get());
    CHECK(insert && insert->effectiveName() == "PRESA");
}

// Quote angolari e a coordinata: misura, testo e DXF (tipo 5 e 6, bit X).
void testAngularOrdinateDims()
{
    DimensionEntity ang;
    ang.kind = DimensionEntity::Angular3;
    ang.curvePoint = {0, 0};
    ang.p1 = {10, 0};
    ang.p2 = {0, 10};
    ang.dimLine = {5, 5};
    CHECK(std::abs(ang.measurement() - 90.0) < 1e-9);
    CHECK(ang.displayText() == "90°");
    ang.dimLine = {-5, -5};
    CHECK(std::abs(ang.measurement() - 270.0) < 1e-9);
    ang.dimLine = {5, 5};
    CHECK(!ang.geometry().empty());
    DimensionEntity ord;
    ord.kind = DimensionEntity::Ordinate;
    ord.p1 = {12.5, 3};
    ord.p2 = {12.5, 20};
    ord.dimLine = {0, 0};
    ord.ordinateX = true;
    CHECK(std::abs(ord.measurement() - 12.5) < 1e-9);
    ord.ordinateX = false;
    CHECK(std::abs(ord.measurement() - 3.0) < 1e-9);
    ord.ordinateX = true;

    Document doc;
    doc.add(std::make_unique<DimensionEntity>(ang));
    doc.add(std::make_unique<DimensionEntity>(ord));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_dims.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    int angular = 0, ordinate = 0;
    for (const auto& e : loaded.entities())
        if (e->type() == EntityType::Dimension) {
            const auto& d = static_cast<const DimensionEntity&>(*e);
            if (d.kind == DimensionEntity::Angular3 && std::abs(d.measurement() - 90.0) < 1e-6)
                ++angular;
            if (d.kind == DimensionEntity::Ordinate && d.ordinateX && std::abs(d.measurement() - 12.5) < 1e-6)
                ++ordinate;
        }
    CHECK(angular == 1 && ordinate == 1);
}

void testSingleLineDiagram()
{
    CHECK(normalizeCircuitCode("QPT-SX-N C-14") == std::optional<std::string>("QPT-SX-N C-14"));
    CHECK(normalizeCircuitCode(" qpt-sx-n  c14 ") == std::optional<std::string>("QPT-SX-N C-14"));
    CHECK(normalizeCircuitCode("QPT-SX-N C-07") == std::optional<std::string>("QPT-SX-N C-7"));
    CHECK(!normalizeCircuitCode("Sigla utenza"));
    CHECK(!normalizeCircuitCode("12 C-3"));
    CHECK(classifyCircuit("FM Servizio Lato SX") == CircuitKind::Power);
    CHECK(classifyCircuit("Torretta 1 Lato DX") == CircuitKind::Power);
    CHECK(classifyCircuit("Illuminazione corridoio") == CircuitKind::Lighting);
    CHECK(classifyCircuit("UE Barriera aria Varco EST 1") == CircuitKind::Other);

    // Una pagina di schema unifilare (y verso il basso): voci a sinistra, una colonna per circuito.
    std::vector<PdfWord> w;
    // Una frase: parole larghe 4 punti per lettera, separate da 2 punti (come nel PDF vero).
    const auto phrase = [&w](double x, double y, const std::string& text) {
        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t end = std::min(text.find(' ', start), text.size());
            const std::string t = text.substr(start, end - start);
            w.push_back(PdfWord{3, x, y, 4.0 * t.size(), 6.0, t});
            x += 4.0 * t.size() + 2.0;
            start = end + 1;
        }
    };
    phrase(48, 297, "Sigla utenza");
    phrase(49, 312, "Descrizione");
    phrase(49, 325, "POTENZA CONTEMPORANEA");
    phrase(49, 345, "ESECUZIONE LINEA");
    phrase(49, 600, "Tipologia");   // voce lontana: fine dell'area dei valori
    const char* numbers[] = {"C-14", "C-15", "C-17"};
    for (int i = 0; i < 3; ++i) {
        const double x = 230 + 85.0 * i;
        phrase(x, 280, "Utenza");   // sopra la riga delle sigle: da ignorare
        phrase(x, 297, std::string("QPT-SX-N ") + numbers[i]);
    }
    phrase(236, 306, "Serranda");
    phrase(231, 316, "Varco OVEST");
    phrase(320, 306, "FM Servizio");
    phrase(320, 316, "Lato SX");
    phrase(402, 306, "Luce corridoio");
    phrase(237, 326, "1 / 1,053");
    phrase(320, 326, "1,5");
    phrase(225, 346, "Monofase L1+N");
    const auto circuits = parseSingleLineDiagram(w);
    CHECK(circuits.size() == 3);
    if (circuits.size() == 3) {
        CHECK(circuits[0].code == "QPT-SX-N C-14" && circuits[0].board == "QPT-SX-N" && circuits[0].number == 14);
        CHECK(circuits[0].description == "Serranda Varco OVEST");
        CHECK(circuits[0].power == "1 / 1,053" && approx(circuits[0].powerKw, 1.0));
        CHECK(circuits[0].phase == "Monofase L1+N" && circuits[0].page == 3);
        CHECK(circuits[1].description == "FM Servizio Lato SX" && circuits[1].kind == CircuitKind::Power);
        CHECK(approx(circuits[1].powerKw, 1.5));
        CHECK(circuits[2].code == "QPT-SX-N C-17" && circuits[2].kind == CircuitKind::Lighting);
    }
}

void testCircuitTags()
{
    Document doc;
    const int fixt = doc.addLayer(Layer{"E-ELEC-FIXT", 3, true});
    const int iden = doc.addLayer(Layer{"E-ELEC-FIXT-IDEN", 7, true});
    const int circ = doc.addLayer(Layer{"E-ELEC-CIRC", 6, true});
    // Un simbolo come quelli esportati dai programmi BIM: inserimento nell'origine, geometria lontana.
    const auto symbol = [&doc](const std::string& name, Vec2 at) {
        auto b = std::make_shared<BlockDef>();
        b->name = name;
        b->entities.push_back(std::make_unique<CircleEntity>(at, 100));
        doc.addBlock(b);
    };
    symbol("STUDIO_Punto di alimentazione - Punto-1", {10000, 5000});
    symbol("STUDIO_Punto di alimentazione - Punto-2", {20000, 5000});
    symbol("STUDIO_FM_Torretta da incasso - 3", {30000, 5000});
    for (const char* name : {"STUDIO_Punto di alimentazione - Punto-1", "STUDIO_Punto di alimentazione - Punto-2",
                             "STUDIO_FM_Torretta da incasso - 3"}) {
        auto in = std::make_unique<InsertEntity>(name, Vec2{0, 0});
        in->setLayer(fixt);
        doc.add(std::move(in));
    }
    doc.linkBlocks();
    const auto text = [&doc](Vec2 p, const char* s, int layer, double h) {
        auto t = std::make_unique<TextEntity>(p, h, s);
        t->setLayer(layer);
        return doc.add(std::move(t));
    };
    text({9900, 5300}, "D", iden, 200);
    text({10300, 5000}, "QPT-SX-N C-4", circ, 120);
    text({30300, 5000}, "QPT-SX-N C-99", circ, 120);   // non nello schema
    text({50000, 5000}, "QPT-SX-N C-5", 0, 120);       // su un altro layer: ignorato

    std::vector<Circuit> circuits(4);
    circuits[0].code = "QPT-SX-N C-4";
    circuits[1].code = "QPT-SX-N C-5";
    circuits[2].code = "QPT-SX-N C-6";
    circuits[2].description = "Riserva";
    circuits[3].code = "QPT-DX-N C-1";   // quadro assente dalla pianta
    for (Circuit& c : circuits) {
        c.board = c.code.substr(0, c.code.rfind(" C-"));
        c.number = std::atoi(c.code.substr(c.code.rfind(" C-") + 3).c_str());
    }

    const auto points = findCircuitPoints(doc);
    CHECK(points.size() == 3);
    if (points.size() == 3) {
        CHECK(points[0].family == "STUDIO_Punto di alimentazione" && points[0].letter == "D");
        CHECK(approx(points[0].position, {10000, 5000}, 1e-6));
        CHECK(points[1].letter.empty() && points[2].family == "STUDIO_FM_Torretta da incasso");
    }
    const auto tags = findCircuitTags(doc);
    CHECK(tags.size() == 2);

    const CircuitCheck check = checkCircuits(doc, circuits);
    CHECK(check.matchedTags == 1);
    CHECK(check.unknownTags.size() == 1 && check.unknownTags.front().code == "QPT-SX-N C-99");
    CHECK(check.circuitsWithoutTags == std::vector<std::string>{"QPT-SX-N C-5"});   // non la riserva né l'altro quadro
    CHECK(check.pointsWithoutTags.size() == 1 && approx(check.pointsWithoutTags.front().position, {20000, 5000}, 1e-6));

    // Sigle nuove: stesso stile, sul layer delle sigle, annullabili in un passo.
    CircuitTagStyle style;
    style.height = usualTagHeight(tags, 100.0);
    CHECK(approx(style.height, 120.0));
    doc.commit(makeCircuitTags(doc, {check.pointsWithoutTags.front()}, "QPT-SX-N C-5", style));
    const CircuitCheck after = checkCircuits(doc, circuits);
    CHECK(after.matchedTags == 2 && after.circuitsWithoutTags.empty() && after.pointsWithoutTags.empty());
    CHECK(doc.undo());
    CHECK(checkCircuits(doc, circuits).matchedTags == 1);
}

void testPointLegend()
{
    // Righe di legenda: con "Tipo" o virgolette sempre, le forme brevi solo se non rigorose.
    const auto e = parseLegendLine("Tipo \xE2\x80\x9C" "E\xE2\x80\x9D: alimentazione porta automatica;");
    CHECK(e && e->letter == "E" && e->description == "alimentazione porta automatica");
    CHECK(!parseLegendLine("N.1 Presa CEE (16A - 230V - 2P+T) con interblocco;", false));
    CHECK(!parseLegendLine("QPT-SX-N C-4", false));
    CHECK(!parseLegendLine("Tipo di posa: incasso"));
    CHECK(!parseLegendLine("D - alimentazione pompa"));
    const auto shortForm = parseLegendLine("D - alimentazione pompa", false);
    CHECK(shortForm && shortForm->letter == "D");
    CHECK(descriptionMatches("Alimentazione Porte Automatiche", "porta automatica"));
    CHECK(descriptionMatches("UE Barriera aria Varco EST 1", "unità esterna lama d'aria"));
    CHECK(!descriptionMatches("UI Barriera aria Varco EST 1", "unità esterna lama d'aria"));

    Document doc;
    const int fixt = doc.addLayer(Layer{"E-ELEC-FIXT", 3, true});
    const int iden = doc.addLayer(Layer{"E-ELEC-FIXT-IDEN", 7, true});
    const int circ = doc.addLayer(Layer{"E-ELEC-CIRC", 6, true});
    const auto text = [&doc](Vec2 p, const std::string& s, int layer, int space = 0) {
        auto t = std::make_unique<TextEntity>(p, 100, s);
        t->setLayer(layer);
        t->setSpace(space);
        return doc.add(std::move(t));
    };
    // Punti: D (con sigla), D, L, G, C e X (senza legenda).
    const std::vector<std::pair<std::string, Vec2>> pts = {{"D", {10000, 5000}}, {"D", {20000, 5000}},
                                                           {"L", {30000, 5000}}, {"G", {40000, 5000}},
                                                           {"C", {50000, 5000}}, {"X", {60000, 5000}}};
    for (std::size_t i = 0; i < pts.size(); ++i) {
        auto b = std::make_shared<BlockDef>();
        b->name = "STUDIO_Punto di alimentazione - P-" + std::to_string(i);
        b->entities.push_back(std::make_unique<CircleEntity>(pts[i].second, 100));
        doc.addBlock(b);
        auto in = std::make_unique<InsertEntity>(b->name, Vec2{0, 0});
        in->setLayer(fixt);
        doc.add(std::move(in));
        text(pts[i].second + Vec2{-100, 300}, pts[i].first, iden);
    }
    doc.linkBlocks();
    text({10300, 4950}, "QPT-SX-N C-4", circ);
    // Legenda nel modello (testo su più righe, con una riga di continuazione), in un blocco e in un
    // layout (lettera e descrizione in due testi affiancati).
    auto mtext = std::make_unique<MTextEntity>(Vec2{100000, 0}, 100,
                                               "Tipo \"D\": alimentazione lama d'aria\\Pcon sezionatore in loco;"
                                               "\\PTipo \"L\": alimentazione unit\xC3\xA0 esterna lama d'aria;");
    mtext->setLayer(0);
    doc.add(std::move(mtext));
    auto legendBlock = std::make_shared<BlockDef>();
    legendBlock->name = "Legenda FM";
    legendBlock->entities.push_back(std::make_unique<TextEntity>(Vec2{0, 0}, 2.5, "Tipo \"G\": alimentazione asciugamani elettrici"));
    doc.addBlock(legendBlock);
    const int layout = doc.addLayout("Legende");
    text({0, 50}, "C", 0, layout);
    text({300, 50}, "alimentazione fotocellule sanitari", 0, layout);

    const auto legend = readPointLegend(doc, {"C", "D", "G", "L", "X"});
    CHECK(legend.size() == 4);
    for (const LegendEntry& l : legend) {
        if (l.letter == "D")
            CHECK(l.description == "alimentazione lama d'aria con sezionatore in loco" && l.where == "Modello");
        if (l.letter == "G")
            CHECK(l.where == "blocco Legenda FM");
        if (l.letter == "C")
            CHECK(l.where == "Legende" && l.description == "alimentazione fotocellule sanitari");
    }

    std::vector<Circuit> circuits;
    const auto add = [&circuits](const std::string& code, const std::string& description) {
        Circuit c;
        c.code = code;
        c.board = code.substr(0, code.rfind(" C-"));
        c.number = std::atoi(code.substr(code.rfind(" C-") + 3).c_str());
        c.description = description;
        circuits.push_back(c);
    };
    add("QPT-SX-N C-3", "UE Barriera aria Varco OVEST 1");
    add("QPT-SX-N C-4", "UI Barriera aria Varco OVEST 1");
    add("QPT-SX-N C-5", "UE Barriera aria Varco EST 2");
    add("QPT-SX-N C-6", "UI Barriera aria Varco EST 2");
    add("QPT-SX-N C-7", "Alimentazione Asciugamani Elettrici");
    add("QPT-SX-N C-8", "Alimentazione Elettrovalvole Rubinetti");
    add("QPT-SX-N C-9", "Riserva");
    add("QPT-DX-N C-1", "Alimentazione Asciugamani Elettrici");   // quadro assente dalla pianta

    const auto matches = interpretLegend(doc, circuits);
    const auto find = [&matches](const std::string& letter) -> const LegendMatch* {
        for (const LegendMatch& m : matches)
            if (m.letter == letter)
                return &m;
        return nullptr;
    };
    const LegendMatch* d = find("D");
    CHECK((d && d->source == LegendSource::Plan && d->keywords == "UI Barriera aria Varco" && d->points == 2
          && d->tagged == 1 && d->circuits == std::vector<std::string>{"QPT-SX-N C-4", "QPT-SX-N C-6"}));
    const LegendMatch* l = find("L");
    CHECK((l && l->source == LegendSource::Text
          && l->circuits == std::vector<std::string>{"QPT-SX-N C-3", "QPT-SX-N C-5"}));
    const LegendMatch* g = find("G");
    CHECK(g && g->circuits == std::vector<std::string>{"QPT-SX-N C-7"});
    const LegendMatch* c = find("C");
    CHECK(c && c->circuits == std::vector<std::string>{"QPT-SX-N C-8"});
    const LegendMatch* x = find("X");
    CHECK(x && x->legend.empty() && !x->warning.empty());
    // Solo dal testo (nessuna sigla in pianta): D "lama d'aria" non prende le unità esterne, descritte
    // meglio dalla L.
    CircuitScanOptions noTags;
    noTags.tagLayers = "#NESSUNO#";
    for (const LegendMatch& m : interpretLegend(doc, circuits, noTags))
        if (m.letter == "D")
            CHECK((m.source == LegendSource::Text
                   && m.circuits == std::vector<std::string>{"QPT-SX-N C-4", "QPT-SX-N C-6"}));
    // Parole chiave dell'utente: hanno la precedenza.
    const auto manual = interpretLegend(doc, circuits, {}, {{"L", "Barriera aria"}});
    for (const LegendMatch& m : manual)
        if (m.letter == "L")
            CHECK(m.source == LegendSource::User && m.circuits.size() == 4);
}

// Scale di annotazione: testo e quota annotativi, cambio di scala (annullabile), scala delle finestre, salvataggio.
void testAnnotationScales()
{
    Document doc;
    TextStyle ts;
    ts.name = "Annotativo";
    ts.font = "arial.ttf";
    ts.annotative = true;
    doc.setTextStyle(ts);
    CHECK(doc.setAnnotationScale("1:50"));
    auto text = std::make_unique<TextEntity>(Vec2{100, 100}, 2.5, "Locale");
    text->style = "Annotativo";
    doc.prepareAnnotation(*text);
    CHECK(text->annotative() && std::abs(text->height - 125.0) < 1e-9 && std::abs(text->annoFactor() - 50.0) < 1e-9);
    const EntityId id = doc.add(std::move(text));
    // Testo non annotativo con lo stesso stile già nel disegno (letto da file): non cambia.
    auto plain = std::make_unique<TextEntity>(Vec2{0, 0}, 10.0, "Fisso");
    plain->style = "Annotativo";
    const EntityId plainId = doc.add(std::move(plain));

    // Scala non supportata: l'oggetto resta com'è (e si vede sempre).
    CHECK(doc.setAnnotationScale("1:100"));
    CHECK(std::abs(static_cast<const TextEntity*>(doc.find(id))->height - 125.0) < 1e-9);
    CHECK(!doc.supportsFactor(*doc.find(id), 100.0) && doc.find(plainId));
    // OBJECTSCALE: con la scala 1:100 l'oggetto passa subito a quella (altezza 2,5 * 100).
    CHECK(doc.addObjectScale({id}, "1:100") == 1);
    CHECK(std::abs(static_cast<const TextEntity*>(doc.find(id))->height - 250.0) < 1e-9);
    CHECK(doc.setAnnotationScale("1:50"));
    CHECK(std::abs(static_cast<const TextEntity*>(doc.find(id))->height - 125.0) < 1e-9);
    CHECK(std::abs(static_cast<const TextEntity*>(doc.find(plainId))->height - 10.0) < 1e-9);
    // Annulla: torna la scala 1:100 con il testo alto 250.
    CHECK(doc.undo());
    CHECK(doc.annotationScale() == "1:100");
    CHECK(std::abs(static_cast<const TextEntity*>(doc.find(id))->height - 250.0) < 1e-9);
    CHECK(doc.redo());
    CHECK(doc.annotationScale() == "1:50");

    // Copia alla scala di una finestra 1:100 (fattore 100).
    const auto at100 = annotationAt(*doc.find(id), 100.0);
    CHECK(std::abs(static_cast<const TextEntity&>(*at100).height - 250.0) < 1e-9);

    // Quota annotativa: DIMSCALE = scala di annotazione.
    DimStyle ds = doc.currentDimStyle();
    ds.annotative = true;
    ds.scale = 7.0;   // ignorata
    doc.setDimStyle(ds);
    auto dim = std::make_unique<DimensionEntity>();
    dim->style = doc.dimStyleForNew();
    dim->p1 = {0, 0};
    dim->p2 = {1000, 0};
    dim->dimLine = {500, 200};
    doc.prepareAnnotation(*dim);
    CHECK(dim->annotative() && std::abs(dim->style.scale - 50.0) < 1e-9);
    const EntityId dimId = doc.add(std::move(dim));

    // Salvataggio e lettura: scale, scala corrente e oggetti annotativi.
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_anno.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    CHECK(loaded.annotationScale() == "1:50");
    CHECK(loaded.findTextStyle("Annotativo") && loaded.findTextStyle("Annotativo")->annotative);
    int annotative = 0;
    for (const auto& e : loaded.entities())
        if (e->annotative()) {
            ++annotative;
            CHECK(std::abs(e->annoFactor() - 50.0) < 1e-9);
            if (e->type() == EntityType::Text)
                CHECK(e->annoScales().size() == 2 && std::abs(static_cast<const TextEntity&>(*e).height - 125.0) < 1e-6);
        }
    CHECK(annotative == 2);
    (void)dimId;
}

// Stili annullabili: modifiche a stili di testo, quota e direttrice e allo stile corrente tornano indietro con U.
void testStyleUndo()
{
    Document doc;
    doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1, 1}));
    doc.checkpoint();
    TextStyle ts = doc.textStyleOrStandard("Standard");
    const double before = ts.widthFactor;
    ts.widthFactor = 0.7;
    doc.setTextStyle(ts);
    doc.checkpoint();   // fine del passo (come dopo un comando o una finestra degli stili)
    DimStyle ds = doc.currentDimStyle();
    const double txt = ds.textHeight;
    ds.textHeight = 9.0;
    doc.setDimStyle(ds);
    Document::MLeaderStyle ms;
    ms.name = "Note";
    doc.setMLeaderStyle(ms);
    doc.setCurrentMLeaderStyle("Note");
    doc.checkpoint();
    CHECK(doc.undo());   // stile di quota e direttrice
    CHECK(std::abs(doc.currentDimStyle().textHeight - txt) < 1e-12);
    CHECK(!doc.findMLeaderStyle("Note") && doc.currentMLeaderStyle().empty());
    CHECK(std::abs(doc.textStyleOrStandard("Standard").widthFactor - 0.7) < 1e-12);
    CHECK(doc.undo());   // stile di testo
    CHECK(std::abs(doc.textStyleOrStandard("Standard").widthFactor - before) < 1e-12);
    CHECK(doc.entities().size() == 1);
    CHECK(doc.redo() && doc.redo());
    CHECK(std::abs(doc.currentDimStyle().textHeight - 9.0) < 1e-12 && doc.findMLeaderStyle("Note"));

    // Cambio di nome di uno stile usato da un testo e da un blocco: Annulla rimette tutto.
    TextStyle note;
    note.name = "Note";
    doc.setTextStyle(note);
    auto t = std::make_unique<TextEntity>(Vec2{0, 0}, 2.5, "A");
    t->style = "Note";
    const EntityId tid = doc.add(std::move(t));
    auto block = std::make_shared<BlockDef>();
    block->name = "B";
    auto bt = std::make_unique<TextEntity>(Vec2{0, 0}, 2.5, "B");
    bt->style = "Note";
    block->entities.push_back(std::move(bt));
    doc.addBlock(block);
    doc.checkpoint();
    CHECK(doc.renameTextStyle("Note", "Appunti"));
    CHECK(static_cast<const TextEntity*>(doc.find(tid))->style == "Appunti");
    CHECK(static_cast<const TextEntity&>(*doc.findBlock("B")->entities[0]).style == "Appunti");
    CHECK(doc.undo());
    CHECK(doc.findTextStyle("Note") && !doc.findTextStyle("Appunti"));
    CHECK(static_cast<const TextEntity*>(doc.find(tid))->style == "Note");
    CHECK(static_cast<const TextEntity&>(*doc.findBlock("B")->entities[0]).style == "Note");
}

// Annullando una cancellazione gli oggetti tornano al loro posto nell'ordine di disegno.
void testUndoKeepsOrder()
{
    Document doc;
    std::vector<EntityId> ids;
    for (int i = 0; i < 6; ++i)
        ids.push_back(doc.add(std::make_unique<LineEntity>(Vec2{double(i), 0}, Vec2{double(i), 1})));
    doc.erase({ids[1], ids[4]});
    doc.add(std::make_unique<LineEntity>(Vec2{9, 0}, Vec2{9, 1}));
    CHECK(doc.undo() && doc.undo());
    std::vector<EntityId> order;
    for (const auto& e : doc.entities())
        order.push_back(e->id());
    CHECK(order == ids);
    CHECK(doc.redo());
    CHECK(doc.entities().size() == 4 && !doc.find(ids[1]) && !doc.find(ids[4]));
}

// Frecce della larghezza del testo multilinea: il lato opposto a quello trascinato resta fermo.
void testMTextWidthGrips()
{
    for (int attachment : {1, 2, 3, 5, 9}) {
        MTextEntity m({100, 50}, 2.5, "Testo di prova", 40.0);
        m.attachment = attachment;
        CHECK(m.gripPoints().size() == 3);
        const auto before = m.box();
        auto right = gripEdit(m, 1, m.gripPoints()[1].position + Vec2{25, 3});
        const auto& r = static_cast<const MTextEntity&>(*right);
        CHECK(std::abs(r.width - 65.0) < 1e-9);
        CHECK(approx(r.box()[0], before[0]));   // lato sinistro fermo
        auto left = gripEdit(m, 2, m.gripPoints()[2].position + Vec2{10, 0});
        const auto& l = static_cast<const MTextEntity&>(*left);
        CHECK(std::abs(l.width - 30.0) < 1e-9);
        CHECK(approx(l.box()[1], before[1]));   // lato destro fermo
    }
    // Lo snap vede solo il punto di inserimento.
    MTextEntity m({0, 0}, 2.5, "A", 10.0);
    CHECK(m.snapPoints().size() == 1);
}

// Multidirettrice: più frecce sullo stesso testo, salvate come LEADER legati e ricomposte alla lettura.
void testMultiLeader()
{
    Document doc;
    auto l = std::make_unique<LeaderEntity>();
    l->points = {{0, 0}, {10, 10}, {12, 10}};
    l->text = "Quadro QE1";
    l->extraTips = {{0, 5}, {2, -3}};
    CHECK(approx(l->junction(), {10, 10}));
    std::size_t lines = 0;
    for (const auto& g : l->geometry())
        lines += g->type() == EntityType::Line ? 1 : 0;
    CHECK(lines == 4);   // 2 della principale + 2 frecce in più
    doc.add(std::move(l));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_mleader.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document loaded;
    CHECK(readDxf(path, loaded).ok);
    std::filesystem::remove(path);
    int leaders = 0;
    for (const auto& e : loaded.entities())
        if (e->type() == EntityType::Leader) {
            ++leaders;
            const auto& r = static_cast<const LeaderEntity&>(*e);
            CHECK(r.extraTips.size() == 2 && r.text == "Quadro QE1");
        }
    CHECK(leaders == 1);
}

void testPlant()
{
    using namespace plant;
    // Fascio di cerchi uguali: valori noti del cerchio minimo che li contiene.
    CHECK(approx(bundleDiameter({10}), 10.0, 1e-6));
    CHECK(approx(bundleDiameter({10, 10}), 20.0, 1e-6));
    CHECK(approx(bundleDiameter({10, 10, 10}), 21.547, 2e-3));
    CHECK(approx(bundleDiameter({10, 10, 10, 10}), 24.142, 2e-3));
    const double seven = bundleDiameter({10, 10, 10, 10, 10, 10, 10});
    CHECK(approx(seven, 30.0, 1e-3));   // esagono intorno al centrale
    CHECK(approx(bundleDiameter({10, 10, 10, 10, 10}), 27.013, 2e-3));
    CHECK(approx(bundleDiameter({20, 5}), 25.0, 1e-6));
    const double mixed = bundleDiameter({20, 10, 10});
    CHECK(approx(mixed, 30.45, 0.02));   // il grande e i due piccoli che si toccano sopra di lui

    // Formazioni.
    auto f = parseFormation("4(1x95)");
    CHECK(f.size() == 1 && f[0].count == 4 && f[0].cores == 1 && approx(f[0].section, 95.0));
    f = parseFormation("3G2,5");
    CHECK(f.size() == 1 && f[0].count == 1 && f[0].cores == 3 && approx(f[0].section, 2.5));
    f = parseFormation("3(1x95) + 1G50");
    CHECK(f.size() == 2 && f[0].count == 3 && f[1].cores == 1 && approx(f[1].section, 50.0));
    f = parseFormation("3x(1x120)+70");
    CHECK(f.size() == 2 && f[0].count == 3 && approx(f[0].section, 120.0) && approx(f[1].section, 70.0));
    f = parseFormation("2(1x1,5)+(1PE1,5)");
    CHECK(f.size() == 2 && f[0].count == 2 && f[1].pe && f[1].count == 1 && approx(f[1].section, 1.5));
    f = parseFormation("3(1x185)+(1x95)+(1PE95)");
    CHECK(f.size() == 3 && f[0].count == 3 && f[1].count == 1 && f[1].cores == 1 && approx(f[1].section, 95.0) && f[2].pe);
    f = parseFormation("3(3x1x240)+(2x240)+(1PE120)");
    CHECK(f.size() == 3 && f[0].count == 9 && f[0].cores == 1 && f[1].count == 2 && f[1].cores == 1 && f[2].pe);
    std::string type, form;
    splitCableText("Cavo / GENERAL - FG16M16/FG17 PE - 2(1x10)+(1PE10)", type, form);
    CHECK(type == "FG16M16/FG17 PE" && form == "2(1x10)+(1PE10)");
    splitCableText("NESSUNA LINEA / ---", type, form);
    CHECK(type.empty() && form.empty());
    splitCableText("FG16OM16 - 1(3G2,5)", type, form);
    CHECK(type == "FG16OM16" && form == "1(3G2,5)");
    splitCableText("FG16OM16 5G6", type, form);
    CHECK(type == "FG16OM16" && form == "5G6");

    // Cavi dei circuiti.
    Project p;
    p.circuits.push_back({"QE C-1", "Luce", "FG16OM16", "3G2,5"});
    p.circuits.push_back({"QE C-2", "Montante", "FG16M16", "4(1x95)"});
    p.circuits.push_back({"QE C-3", "Allarme", "FG18OM16", "3G2,5"});
    p.circuits.push_back({"QE C-4", "Rete dati", "U/UTP Cat.6", "", System::Signal});
    p.circuits.back().diameter = 5.8;
    std::string note;
    auto cables = cablesOf(p, *p.circuit("QE C-1"), &note);
    CHECK(cables.size() == 1 && approx(cables[0].d, 12.2) && note.empty());
    cables = cablesOf(p, *p.circuit("QE C-2"), &note);
    CHECK(cables.size() == 4 && approx(cables[0].d, 19.8) && cables[0].cores == 1 && cables[3].code == "QE C-2 /4");
    note.clear();
    cables = cablesOf(p, *p.circuit("QE C-3"), &note);
    CHECK(cables.size() == 1 && approx(cables[0].d, 12.2) && note.find("diametro del") != std::string::npos);
    cables = cablesOf(p, *p.circuit("QE C-4"), &note);
    CHECK(cables.size() == 1 && approx(cables[0].d, 5.8) && cables[0].sys == "SPECIAL_ELV");
    // Unipolari con PE di un altro tipo: le fasi FG16M16, il PE dal catalogo dei PE.
    p.circuits.push_back({"QE C-6", "Montante", "FG16M16/FG17 PE", "3(1x35)+(1PE16)"});
    cables = cablesOf(p, *p.circuit("QE C-6"), &note);
    CHECK(cables.size() == 4 && approx(cables[0].d, 13.5) && cables[3].form.find("PE") != std::string::npos && approx(cables[3].d, 7.4));
    // Cavo proprio del progetto: vince sul catalogo.
    p.cables.push_back({"MIO", "bt_multi", "X", "", "FG16OM16", "1(3G2,5)", 11.0, 0.2, 2.5, 2, 30, 8, "BT_POWER", ""});
    cables = cablesOf(p, *p.circuit("QE C-1"), &note);
    CHECK(cables.size() == 1 && approx(cables[0].d, 11.0));
    p.cables.clear();

    // Tubo: tre 3G2,5 (Ø12,2) hanno un fascio di 26,3 mm: serve un interno di 34,2 mm, quindi il Ø40 rigido.
    Run tube;
    tube.id = p.nextRunId();
    CHECK(tube.id == "T1");
    tube.kind = RunKind::Tube;
    tube.tubeOuter = 32;
    tube.circuits = {"QE C-1", "QE C-3", "QE C-1"};
    RunCheck rc = checkRun(p, tube);
    CHECK(rc.cableCount == 3 && approx(rc.bundle, 12.2 * 2.1547, 0.05) && approx(rc.tubeInner, 27.8));
    CHECK(rc.level == Level::Error);
    CHECK(rc.findings.size() >= 1 && rc.findings[1].text.find("serve il Ø40") != std::string::npos);
    tube.tubeOuter = 40;
    rc = checkRun(p, tube);
    bool tubeOk = false;
    for (const Finding& x : rc.findings)
        tubeOk = tubeOk || (x.level == Level::Ok && x.text.find("tubo rigido Ø40") != std::string::npos);
    CHECK(tubeOk && rc.level == Level::Warning);   // resta l'avviso sul tipo di cavo simile (FG18OM16)
    // Con 1,5 volte il fascio serve 39,4 mm: il Ø40 rigido (35,4) non basta più, serve il Ø50.
    tube.tubeRatio = 1.5;
    rc = checkRun(p, tube);
    CHECK(rc.level == Level::Error && rc.findings.size() >= 2 && rc.findings[1].text.find("1,5 volte il fascio") != std::string::npos &&
          rc.findings[1].text.find("serve il Ø50") != std::string::npos);
    tube.tubeRatio = 1.3;
    // Segnale non schermato nello stesso tubo dell'energia: segregazione non verificata.
    tube.circuits = {"QE C-1", "QE C-4"};
    rc = checkRun(p, tube);
    bool segError = false;
    for (const Finding& x : rc.findings)
        segError = segError || (x.level == Level::Error && x.text.find("segregazione") != std::string::npos);
    CHECK(segError);
    // Circuito sconosciuto.
    tube.circuits = {"QE C-99"};
    rc = checkRun(p, tube);
    CHECK(rc.level == Level::Error && rc.missing.size() == 1);

    // Canale 60×40: due montanti 4(1x95) (8 cavi Ø19,8 = 2463 mm² su 2400) non stanno: oltre il 50%.
    Run duct;
    duct.kind = RunKind::Duct;
    duct.width = 60;
    duct.height = 40;
    duct.circuits = {"QE C-2", "QE C-2"};
    rc = checkRun(p, duct);
    CHECK(rc.level == Level::Error && rc.fillPct > 50.0);
    duct.width = 300;
    duct.height = 80;
    rc = checkRun(p, duct);
    CHECK(rc.level == Level::Ok && rc.fillPct < 50.0);

    // Passerella con staffaggio: stessi calcoli di CanaloCAD.
    Run tray;
    tray.kind = RunKind::Tray;
    tray.width = 200;
    tray.height = 75;
    tray.support = "wall_light";
    tray.spanM = 3.0;
    tray.circuits = {"QE C-2", "QE C-2", "QE C-2"};
    rc = checkRun(p, tray, 25.0);
    CHECK(rc.tray.loadPct > 100.0 && rc.level == Level::Error);
    tray.support = "wall_heavy";
    tray.spanM = 1.5;
    rc = checkRun(p, tray, 25.0);
    CHECK(rc.tray.loadPct <= 100.0 && rc.level == Level::Ok);
    // Senza staffaggio: solo un avviso.
    tray.support = "none";
    rc = checkRun(p, tray);
    CHECK(rc.level == Level::Warning);

    // Fascio con le posizioni: dentro il cerchio del fascio e senza sovrapposizioni.
    {
        const std::vector<double> ds = {19.8, 12.2, 12.2, 5.8, 15.2};
        double B = 0.0;
        const auto centers = bundleLayout(ds, &B);
        CHECK(centers.size() == ds.size() && approx(B, bundleDiameter(ds), 1e-9));
        bool inside = true, apart = true;
        for (std::size_t i = 0; i < ds.size(); ++i) {
            inside = inside && std::hypot(centers[i].x, centers[i].y) + ds[i] / 2.0 <= B / 2.0 + 1e-3;
            for (std::size_t j = i + 1; j < ds.size(); ++j)
                apart = apart && std::hypot(centers[i].x - centers[j].x, centers[i].y - centers[j].y) >= (ds[i] + ds[j]) / 2.0 - 1e-3;
        }
        CHECK(inside && apart);
    }
    // Fasi e raggruppamento.
    CHECK(phasesOf(*p.circuit("QE C-1")) == 1 && phasesOf(*p.circuit("QE C-2")) == 3);
    CHECK(approx(groupingFactor(1), 1.0) && approx(groupingFactor(3), 0.70) && approx(groupingFactor(10), 0.50) && approx(groupingFactor(25), 0.38));
    // Tabelle: EPR 2 conduttori B2 2,5 mm² = 30 A; PVC 3 conduttori E 95 = 238; fuori tabella; temperatura.
    CHECK(approx(*tableAmpacity(Insulation::EPR, Method::B2, 2, 2.5), 30.0) && approx(*tableAmpacity(Insulation::PVC, Method::E, 3, 95), 238.0));
    CHECK(!tableAmpacity(Insulation::EPR, Method::A1, 2, 185).has_value() && !tableAmpacity(Insulation::EPR, Method::B2, 3, 3.0).has_value());
    CHECK(approx(temperatureFactor(Insulation::PVC, 40), 0.87) && approx(temperatureFactor(Insulation::EPR, 40), 0.91) &&
          approx(temperatureFactor(Insulation::EPR, 32), 0.96));
    CHECK(insulationOf("FS17") == Insulation::PVC && insulationOf("FG16OM16") == Insulation::EPR && insulationOf("H07V-K") == Insulation::PVC);
    CHECK(approx(trayGroupingFactor(1), 1.0) && approx(trayGroupingFactor(3), 0.82) && approx(trayGroupingFactor(20), 0.72));
    // Portata: 3G2,5 EPR in tubo a vista (B2, 30 A) con 3 circuiti: 30 × 0,70 = 21 A.
    {
        Project q;
        q.circuits.push_back({"A1", "Luce", "FG16OM16", "3G2,5"});
        q.circuits.back().ib = 8.0;
        q.circuits.back().breakerIn = 10.0;
        q.circuits.push_back({"A2", "Prese", "FG16OM16", "3G4"});
        q.circuits.back().ib = 16.0;
        q.circuits.back().lengthM = 30.0;
        q.circuits.push_back({"A3", "Montante", "FG16M16", "4(1x95)"});
        q.circuits.back().ib = 180.0;
        Run t;
        t.id = "T1";
        t.kind = RunKind::Tube;
        t.tubeOuter = 50;
        t.circuits = {"A1", "A2", "A2"};
        q.runs.push_back(t);
        plant::CircuitCheck cc = checkCircuit(q, *q.circuit("A1"));
        CHECK(approx(cc.iz0, 30.0) && cc.method == "B2" && cc.fromTable && approx(cc.kGroup, 0.70) && approx(cc.iz, 21.0));
        CHECK(cc.runs.size() == 1 && cc.findings.front().level == Level::Ok);
        // Tubo incassato a 40 °C: A2 (25 A) × 0,70 × 0,91 = 15,9 A.
        q.runs.front().laying = "incassato";
        q.runs.front().ambientC = 40.0;
        cc = checkCircuit(q, *q.circuit("A1"));
        CHECK(cc.method == "A2" && approx(cc.iz, 15.9));
        q.runs.front().laying = "vista";
        q.runs.front().ambientC = 30.0;
        q.circuit("A1")->breakerIn = 25.0;   // In oltre Iz': protezione non verificata
        cc = checkCircuit(q, *q.circuit("A1"));
        CHECK(cc.level == Level::Error);
        // Caduta di tensione monofase: 2 × 30 m × 16 A × 0,0214/4 × 0,9 / 230 = 2,01%.
        cc = checkCircuit(q, *q.circuit("A2"));
        CHECK(approx(cc.dvPct, 2.01) && !cc.measured);
        // Montante senza lunghezza nello schema: quella delle tratte disegnate; trifase, unipolari (posa 0,75).
        t.id = "T2";
        t.circuits = {"A3"};
        q.runs.push_back(t);
        cc = checkCircuit(q, *q.circuit("A3"), {{"T2", 120.0}});
        CHECK(cc.measured && approx(cc.lengthM, 120.0) && cc.method == "B1" && cc.parallel == 1);
        CHECK(approx(cc.iz, 269.0));   // EPR, 3 conduttori, B1, 95 mm²
        // 3 terne in parallelo.
        q.circuits.push_back({"A4", "Generale", "FG16M16/FG17 PE", "3(3x1x240)+(2x240)+(1PE120)"});
        cc = checkCircuit(q, *q.circuit("A4"));
        CHECK(cc.parallel == 3 && phasesOf(*q.circuit("A4")) == 3);

        // Sezione disegnata del tubo: 2 cerchi del tubo, 3 cavi con il numero, titolo e legenda.
        SectionStyle style;
        style.origin = {1000, 500};
        style.scale = 1.0;
        style.textHeight = 2.5;
        style.tag = "sezione:T1";
        const auto ents = drawRunSection(q, q.runs.front(), style);
        int circles = 0, texts = 0, mtexts = 0;
        bool tagged = true;
        for (const auto& e : ents) {
            circles += e->type() == EntityType::Circle;
            texts += e->type() == EntityType::Text;
            mtexts += e->type() == EntityType::MText;
            tagged = tagged && e->plantRun() == "sezione:T1";
        }
        CHECK(circles == 5 && texts == 3 && mtexts == 2 && tagged);
        // Passerella: profilo, cavi.
        Run tr;
        tr.id = "T3";
        tr.kind = RunKind::Tray;
        tr.width = 200;
        tr.height = 75;
        tr.circuits = {"A3", "A1"};
        const auto ents2 = drawRunSection(q, tr, style);
        int polys = 0;
        circles = 0;
        for (const auto& e : ents2) {
            polys += e->type() == EntityType::Polyline;
            circles += e->type() == EntityType::Circle;
        }
        CHECK(polys == 1 && circles == 5);
        CHECK(runLabel(q.runs.front(), checkRun(q, q.runs.front())).rfind("T1 tubo Ø50 - 3 circuiti", 0) == 0);
    }

    // Distinta: tubo rigido Ø32 di 18 m con 2 curve, passerella 200×75 di 8 m con staffe ogni 1,5 m.
    {
        Project q;
        q.circuits.push_back({"B1", "Luce", "FG16OM16", "3G2,5"});
        q.circuits.push_back({"B2", "Montante", "FG16M16", "4(1x95)"});
        q.circuits.back().lengthM = 40.0;
        Run t;
        t.id = "T1";
        t.kind = RunKind::Tube;
        t.tubeOuter = 32;
        t.circuits = {"B1"};
        Run p2;
        p2.id = "T2";
        p2.kind = RunKind::Tray;
        p2.width = 200;
        p2.height = 75;
        p2.support = "wall_med";
        p2.spanM = 1.5;
        p2.circuits = {"B1", "B2"};
        q.runs = {t, p2};
        const auto bom = plantBom(q, {{"T1", {18.0, 2, 0}}, {"T2", {8.0, 1, 1}}});
        auto find = [&bom](const std::string& d) -> const BomItem* {
            for (const BomItem& it : bom)
                if (it.description == d)
                    return &it;
            return nullptr;
        };
        const BomItem* tube = find("Tubo rigido Ø32");
        CHECK(tube && approx(tube->qty, 18.0) && tube->unit == "m");
        CHECK(find("Curva per tubo rigido Ø32") && approx(find("Curva per tubo rigido Ø32")->qty, 2.0));
        CHECK(find("Manicotto di giunzione Ø32") && approx(find("Manicotto di giunzione Ø32")->qty, 6.0));
        const BomItem* tray = find("Passerella asolata in lamiera forata 200×75");
        CHECK(tray && approx(tray->qty, 9.0));   // 3 verghe da 3 m
        CHECK(find("Mensola Rinforzata con Saetta L=250") && approx(find("Mensola Rinforzata con Saetta L=250")->qty, 7.0));
        CHECK(find("Derivazione a T per passerella 200×75") && approx(find("Derivazione a T per passerella 200×75")->qty, 1.0));
        // Cavi: B1 (senza lunghezza nello schema) = tratte 18 + 8 m; B2 = 4 unipolari × 40 m.
        CHECK(find("FG16OM16 3G2,5") && approx(find("FG16OM16 3G2,5")->qty, 26.0));
        CHECK(find("FG16M16 1(1x95)") && approx(find("FG16M16 1(1x95)")->qty, 160.0));
        const std::string csv = bomCsv(bom);
        CHECK(csv.rfind("Categoria;Descrizione;Quantità;UM;Note", 0) == 0 && csv.find("Tubo rigido Ø32;18;m;") != std::string::npos);
        const auto table = bomTable(bom, {0, 0}, 2.5);
        CHECK(table->rows == static_cast<int>(bom.size()) + 1 && table->columns == 5 && table->cell(1, 0) == "Tubi");
    }

    // DXF: il file di progetto collegato e il codice della tratta sulle linee restano.
    Document doc;
    doc.setPlantFile("Impianto.cdb");
    auto line = std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{1000, 0});
    line->setPlantRun("T7");
    doc.add(std::move(line));
    auto poly = std::make_unique<PolylineEntity>(std::vector<PolyVertex>{{{0, 0}}, {{0, 500}}, {{300, 500}}});
    poly->setPlantRun("T8");
    doc.add(std::move(poly));
    doc.add(std::make_unique<LineEntity>(Vec2{0, 10}, Vec2{10, 10}));
    const auto path = std::filesystem::temp_directory_path() / "cartesio_plant_rt.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    Document back;
    CHECK(readDxf(path, back).ok);
    CHECK(back.plantFile() == "Impianto.cdb");
    std::vector<std::string> runs;
    for (const auto& e : back.entities())
        runs.push_back(e->plantRun());
    CHECK(runs.size() == 3 && runs[0] == "T7" && runs[1] == "T8" && runs[2].empty());
    std::filesystem::remove(path);

    // Canaline portacavi: lo stato JSON (lungo, con caratteri non ASCII e spazi ai bordi dei pezzi) e il codice sugli
    // oggetti tornano uguali; le modifiche si annullano.
    {
        Document cd;
        std::string json = "{\"title\":\"  CANALINA Ø200 — 2,5 mm² \",\"cables\":[";
        for (int i = 0; i < 60; ++i)
            json += std::string(i ? "," : "") + "{\"code\":\"QE1 C-" + std::to_string(i) + "\",\"desc\":\"Utenza Ø " + std::to_string(i) + " \\\"x\\\"  \"}";
        json += "]}";
        cd.resetCheckpoint();
        cd.setCanaloTray("CN1", json);
        cd.setCanaloTray("CN2", "{\"width\":300}");
        cd.checkpoint();
        CHECK(cd.nextCanaloTrayId() == "CN3");
        auto outline = std::make_unique<PolylineEntity>(std::vector<PolyVertex>{{{0, 75}}, {{0, 0}}, {{200, 0}}, {{200, 75}}});
        outline->setCanaloTray("CN1");
        cd.add(std::move(outline));
        const auto cpath = std::filesystem::temp_directory_path() / "cartesio_canalo_rt.dxf";
        CHECK(writeDxf(cd, cpath, nullptr, nullptr, DxfVersion::R2000));
        Document cback;
        CHECK(readDxf(cpath, cback).ok);
        CHECK(cback.canaloTrays().size() == 2 && cback.canaloTray("CN1") && *cback.canaloTray("CN1") == json);
        CHECK(cback.canaloTray("CN2") && *cback.canaloTray("CN2") == "{\"width\":300}");
        CHECK(cback.entities().size() == 1 && cback.entities()[0]->canaloTray() == "CN1");
        std::filesystem::remove(cpath);
        // Annulla: lo stato torna quello di prima.
        cd.setCanaloTray("CN2", "{\"width\":400}");
        cd.checkpoint();
        CHECK(*cd.canaloTray("CN2") == "{\"width\":400}");
        cd.undo();
        CHECK(*cd.canaloTray("CN2") == "{\"width\":300}");
    }
}

// Manuale: ogni comando registrato ha la sua voce (il nome tra apici inversi in docs/manuale).
// Tratte da layer: testi con tipo e misura, legenda con il campione di linea, rami tra le derivazioni.
void testRunImport()
{
    using namespace plant;
    const auto a = parseRunText("{\\W0.8;CANALINA METALLICA IN ACCIAIO ZINCATO DIM. 400x75mm PER DORSALI}");
    CHECK(a && a->kind == RunKind::Duct && approx(a->width, 400) && approx(a->height, 75));
    const auto b = parseRunText("Passerella 75 x 300");
    CHECK(b && b->kind == RunKind::Tray && approx(b->width, 300) && approx(b->height, 75));
    const auto c = parseRunText("N.4 TUBI %%C125 SOTTOPAVIMENTO");
    CHECK(c && c->kind == RunKind::Tube && approx(c->tube, 125));
    const auto c2 = parseRunText("tubo corrugato \xC3\xB8" "32");
    CHECK(c2 && c2->kind == RunKind::Tube && approx(c2->tube, 32));
    const auto d = parseRunText("TUBAZIONI IN ACCIAIO ZINCATO PER DORSALI ENERGIA");
    CHECK(d && d->kind == RunKind::Tube && approx(d->tube, 0));
    CHECK(!parseRunText("36W - 4000lm - 600x60mm") && !parseRunText("CANALINA ESISTENTE"));

    Document doc;
    Layer trays{"EL-CANALINE", 3, true};
    trays.linetype = "Continuous";
    const int lt = doc.addLayer(trays);
    CHECK(suggestRunLayers(doc).empty());   // senza linee non si propone
    auto add = [&](std::unique_ptr<Entity> e, const std::string& linetype = "ByLayer") {
        e->setLayer(lt);
        e->setLinetype(linetype);
        return doc.add(std::move(e));
    };
    // Dorsale continua spezzata in due linee in fila, una derivazione a T tratteggiata, un ramo continuo staccato.
    const EntityId d1 = add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{100, 0}));
    const EntityId d2 = add(std::make_unique<LineEntity>(Vec2{100, 0}, Vec2{200, 0}));
    const EntityId t1 = add(std::make_unique<LineEntity>(Vec2{150, 0}, Vec2{150, 80}), "DASHED");
    const EntityId s1 = add(std::make_unique<LineEntity>(Vec2{0, 300}, Vec2{100, 300}));
    // Legenda (layer 0): campione continuo e campione tratteggiato a sinistra dei testi.
    doc.add(std::make_unique<LineEntity>(Vec2{1000, 1001}, Vec2{1016, 1001}));
    doc.add(std::make_unique<TextEntity>(Vec2{1018, 1000}, 2.0, "CANALINA METALLICA DIM. 400x75mm"));
    auto dashed = std::make_unique<LineEntity>(Vec2{1000, 993}, Vec2{1016, 993});
    dashed->setLinetype("DASHED");
    doc.add(std::move(dashed));
    doc.add(std::make_unique<TextEntity>(Vec2{1018, 992}, 2.0, "CANALINA METALLICA DIM. 200x75mm"));
    // Scritta vicino al ramo staccato: vale per quel ramo.
    doc.add(std::make_unique<TextEntity>(Vec2{40, 305}, 2.0, "PASSERELLA 300x75"));

    CHECK(suggestRunLayers(doc) == std::vector<int>({lt}));
    const auto styles = pathStyles(doc, {lt});
    CHECK(styles.size() == 2 && styles[0].style.linetype == "CONTINUOUS" && styles[0].ids.size() == 3 && approx(styles[0].length, 300));
    const auto legend = readRunLegend(doc);
    CHECK(legend.size() == 2);
    const auto find = [&legend](double w) {
        for (const LegendItem& l : legend)
            if (approx(l.spec.width, w))
                return l.style.linetype;
        return std::string();
    };
    CHECK(find(400) == "CONTINUOUS" && find(200) == "DASHED");
    const auto chains = chainRuns(doc, {d1, d2, t1, s1}, 0.5, 20.0, legend);
    CHECK(chains.size() == 3);
    int dorsal = 0, branch = 0, labelled = 0;
    for (const RunChain& ch : chains) {
        dorsal += ch.ids.size() == 2;
        branch += ch.ids.size() == 1 && ch.ids[0] == t1;
        if (ch.label && ch.ids == std::vector<EntityId>({s1}) && ch.label->kind == RunKind::Tray)
            ++labelled;
    }
    CHECK(dorsal == 1 && branch == 1 && labelled == 1);
}

// File malformati trovati dalle prove a caso (tests/fuzz.cpp): si leggono senza fermare il programma.
DxfReadResult readDxfText(const std::string& text, Document& doc)
{
    const auto path = std::filesystem::temp_directory_path() / "cartesio_malformato.dxf";
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
    }
    const DxfReadResult r = readDxf(path, doc);
    std::filesystem::remove(path);
    return r;
}

// Canaline di CanaloCAD e progetto della scheda nel DXF: salvati e riletti uguali, anche dopo il passaggio per il
// DWG, quando LibreDWG riscrive i pezzi di testo dell'XRECORD con il codice 3. I "%%c" dei dati di CanaloCAD restano.
void testCanaloXrecordRoundTrip()
{
    std::string tray = "{\"title\":\"CANALINA\",\"cables\":[";
    for (int i = 0; i < 40; ++i)
        tray += "{\"code\":\"FG16OM16-4G10 - L" + std::to_string(i) + " %%c19.5\",\"desc\":\"Cavo \xC3\x98" "19.5 mm\xC2\xB2\"},";
    tray += "{}]}";
    const std::string project = "{\n  \"format\": \"CanaloCAD project\",\n  \"projectSections\": [" + tray + "]\n}";
    std::string flat = project;
    for (char& c : flat)
        if (c == '\n')
            c = ' ';

    Document doc;
    doc.add(std::make_unique<LineEntity>(Vec2{0, 0}, Vec2{10, 0}));
    doc.setCanaloTray("CN1", tray);
    doc.setCanaloTabProject(project);
    const auto path = std::filesystem::temp_directory_path() / "cartesio_test_canalo.dxf";
    CHECK(writeDxf(doc, path, nullptr, nullptr, DxfVersion::R2000));
    {
        Document again;
        CHECK(readDxf(path, again).ok);
        CHECK(again.canaloTray("CN1") && *again.canaloTray("CN1") == tray);
        CHECK(again.canaloTabProject() == flat);
        CHECK(again.canaloTrays().size() == 1);   // il progetto non è una canalina
    }
    // Come lo restituisce LibreDWG: nell'XRECORD delle canaline i pezzi 1 diventano 3.
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        for (std::string line; std::getline(in, line);)
            lines.push_back(line);
    }
    bool inRecord = false;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        std::string code = lines[i];
        code.erase(0, code.find_first_not_of(' '));
        if (!code.empty() && code.back() == '\r')
            code.pop_back();
        if (code == "2" && (lines[i + 1].rfind("CN1", 0) == 0 || lines[i + 1].rfind("#SCHEDA", 0) == 0))
            inRecord = true;
        else if (code == "0")
            inRecord = false;
        else if (inRecord && code == "1")
            lines[i] = "  3";
    }
    {
        std::ofstream out(path, std::ios::binary);
        for (const std::string& line : lines)
            out << line << "\n";
    }
    Document converted;
    CHECK(readDxf(path, converted).ok);
    CHECK(converted.canaloTray("CN1") && *converted.canaloTray("CN1") == tray);
    CHECK(converted.canaloTabProject() == flat);
    std::filesystem::remove(path);
}

void testMalformedFiles()
{
    const auto dxf = [](const std::string& tables, const std::string& entities) {
        return "0\nSECTION\n2\nTABLES\n" + tables + "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" + entities
            + "0\nENDSEC\n0\nEOF\n";
    };
    {   // colore del layer -2147483648: il suo valore assoluto non esiste in un int
        Document doc;
        CHECK(readDxfText(dxf("0\nTABLE\n2\nLAYER\n0\nLAYER\n2\nMuri\n62\n-2147483648\n0\nLAYER\n2\nPorte\n62\n99999\n0\nENDTAB\n", ""), doc).ok);
        const int muri = doc.findLayer("Muri"), porte = doc.findLayer("Porte");
        CHECK(muri >= 0 && doc.layers()[static_cast<std::size_t>(muri)].color == 7);
        CHECK(porte >= 0 && doc.layers()[static_cast<std::size_t>(porte)].color == 7);
    }
    {   // tratteggio che dichiara due miliardi di vertici e ne ha tre; multilinea con conteggi enormi
        Document doc;
        const std::string hatch = "0\nHATCH\n8\n0\n2\nSOLID\n70\n1\n91\n1\n92\n2\n72\n0\n73\n1\n93\n2000000000\n"
                                  "10\n0\n20\n0\n10\n10\n20\n0\n10\n10\n20\n10\n97\n2147483647\n75\n1e300\n76\nnan\n";
        const std::string mline = "0\nMLINE\n8\n0\n72\n2147483647\n73\n2000000000\n10\n0\n20\n0\n11\n1\n21\n1\n";
        CHECK(readDxfText(dxf("", hatch + mline), doc).ok);
        CHECK(!doc.entities().empty() && doc.entities().front()->type() == EntityType::Hatch);
    }
    {   // freccia in più che punta a un'altra freccia in più, e due direttrici con lo stesso testo: ogni oggetto
        // si toglie una volta sola e non si usa dopo averlo tolto.
        const auto leader = [](const std::string& handle, double y, const std::string& extra) {
            return "0\nLEADER\n5\n" + handle + "\n8\n0\n10\n0\n20\n" + std::to_string(y) + "\n10\n5\n20\n"
                + std::to_string(y + 5) + "\n" + extra;
        };
        const auto leg = [](const std::string& main) { return "1001\nCARTESIOCAD\n1000\nMLEADERLEG\n1000\n" + main + "\n"; };
        const std::string text = "73\n0\n340\nT1\n";
        const std::string entities = leader("A1", 0, text) + leader("B1", 10, leg("A1")) + leader("C1", 20, leg("B1"))
            + leader("D1", 30, text) + "0\nMTEXT\n5\nT1\n8\n0\n10\n5\n20\n5\n40\n2.5\n1\nNota\n";
        Document doc;
        CHECK(readDxfText(dxf("", entities), doc).ok);
        int leaders = 0, texts = 0;
        for (const auto& e : doc.entities()) {
            leaders += e->type() == EntityType::Leader ? 1 : 0;
            texts += e->type() == EntityType::MText ? 1 : 0;
        }
        CHECK(leaders == 3 && texts == 0);   // B1 unita ad A1; C1 resta da sola (la sua principale non c'è più)
    }
    {   // stesso layout due volte, con il blocco dello spazio carta ripetuto
        const std::string blocks = "0\nSECTION\n2\nBLOCKS\n"
                                   "0\nBLOCK\n2\n*Paper_Space0\n0\nLINE\n5\nE1\n8\n0\n10\n0\n20\n0\n11\n1\n21\n1\n0\nENDBLK\n"
                                   "0\nBLOCK\n2\n*Paper_Space0\n0\nLINE\n5\nE2\n8\n0\n10\n0\n20\n0\n11\n2\n21\n2\n0\nENDBLK\n"
                                   "0\nENDSEC\n";
        const std::string tables = "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nBLOCK_RECORD\n"
                                   "0\nBLOCK_RECORD\n5\nR1\n2\n*Paper_Space0\n0\nENDTAB\n0\nENDSEC\n";
        const std::string objects = "0\nSECTION\n2\nOBJECTS\n"
                                    "0\nLAYOUT\n5\nL1\n100\nAcDbLayout\n1\nUno\n71\n1\n330\nR1\n"
                                    "0\nLAYOUT\n5\nL2\n100\nAcDbLayout\n1\nDue\n71\n2\n330\nR1\n"
                                    "0\nENDSEC\n";
        Document doc;
        const DxfReadResult r = readDxfText(tables + blocks + objects + "0\nEOF\n", doc);
        CHECK(r.ok && r.imported == 1);   // il secondo blocco sostituisce il primo; il secondo layout non ripete gli oggetti
    }
    {   // .ctb che si espande oltre il tetto (8 MB di zeri in pochi kB) e .ctb troncato
        const std::string zeros(8u << 20, '\0');
        uLongf size = compressBound(static_cast<uLong>(zeros.size()));
        std::string packed(size, '\0');
        CHECK(compress(reinterpret_cast<Bytef*>(packed.data()), &size, reinterpret_cast<const Bytef*>(zeros.data()),
                       static_cast<uLong>(zeros.size())) == Z_OK);
        packed.resize(size);
        std::string error;
        CHECK(!ctbText(ctbFileBytes(packed, 100), &error) && !error.empty());
        CHECK(!ctbText(ctbFileBytes(packed.substr(0, packed.size() / 2), 100), &error));
        const std::string table = builtinPlotStyleTable("Nero.ctb")->toText();
        size = compressBound(static_cast<uLong>(table.size()));
        packed.assign(size, '\0');
        compress(reinterpret_cast<Bytef*>(packed.data()), &size, reinterpret_cast<const Bytef*>(table.data()),
                 static_cast<uLong>(table.size()));
        packed.resize(size);
        const auto text = ctbText(ctbFileBytes(packed, table.size()));
        CHECK(text && *text == table && PlotStyleTable::fromText(*text));
    }
}

// Percorsi automatici: derivazione a T, incrocio senza contatto, tratte in fila, punti fuori portata.
void testRouting()
{
    using namespace plant;
    const std::vector<RouteSegment> net = {
        {{0, 0}, {100, 0}, "T1"},     // dorsale
        {{50, 0}, {50, 80}, "T2"},    // derivazione a T dal centro della dorsale
        {{0, 40}, {90, 40}, "T3"},    // incrocia T2 senza toccarla con un estremo: non collegata
        {{100, 0}, {100, 100}, "T4"}, // continua la dorsale
    };
    RouteOptions o;
    o.joinTolerance = 0.5;
    o.maxAccess = 10.0;
    o.unitsToM = 1.0;
    o.extraPerCircuit = 3.0;
    const std::vector<RouteRequest> req = {
        {"C1", {-2, 0}, {{50, 82}}},
        {"C2", {-2, 0}, {{10, 41}}},
        {"C3", {-2, 0}, {{100, 50}}},
        {"C4", {-2, 0}, {{20, 1}, {30, 1}}},
        {"C5", {1000, 1000}, {{20, 1}}},
        {"C6", {-2, 0}, {{50, 82}, {100, 50}, {500, 500}}},
    };
    const auto r = routeCircuits(net, req, o);
    CHECK(r.size() == 6);
    CHECK(r[0].reached == 1 && r[0].runs == std::vector<std::string>({"T1", "T2"}) && approx(r[0].lengthM, 137.0));
    CHECK(r[1].reached == 0 && r[1].unreached == 1 && r[1].runs.empty() && approx(r[1].lengthM, 0.0));
    CHECK(r[2].runs == std::vector<std::string>({"T1", "T4"}) && approx(r[2].lengthM, 155.0));
    CHECK(r[3].reached == 2 && r[3].runs == std::vector<std::string>({"T1"}) && approx(r[3].lengthM, 36.0));
    CHECK(!r[4].sourceOnNetwork && r[4].unreached == 1);
    CHECK(r[5].reached == 2 && r[5].unreached == 1 && r[5].runs == std::vector<std::string>({"T1", "T2", "T4"})
          && approx(r[5].lengthM, 155.0));
}

// Versioni degli aggiornamenti: lettura, confronto, major.
void testVersions()
{
    const auto v = Version::parse("v0.1.1");
    CHECK(v && v->major == 0 && v->minor == 1 && v->patch == 1 && v->text() == "0.1.1");
    CHECK(Version::parse("1.2") && Version::parse("1.2")->patch == 0);
    CHECK(!Version::parse("") && !Version::parse("1") && !Version::parse("1.2.3.4") && !Version::parse("1.x.3"));
    CHECK(!Version::parse("1..2") && !Version::parse("-1.2") && !Version::parse("1.2.3-beta") && !Version::parse("9999999.1"));
    CHECK(*Version::parse("0.1.1") < *Version::parse("0.1.2") && *Version::parse("0.1.9") < *Version::parse("0.2.0"));
    CHECK(*Version::parse("0.9.9") < *Version::parse("1.0.0") && !(*Version::parse("0.1.1") < *Version::parse("0.1.1")));
    CHECK(!isMajorUpdate(*Version::parse("0.1.1"), *Version::parse("0.2.0"), false));
    CHECK(isMajorUpdate(*Version::parse("0.1.1"), *Version::parse("0.2.0"), true));
    CHECK(isMajorUpdate(*Version::parse("0.9.0"), *Version::parse("1.0.0"), false));
}

void testManualCoversCommands()
{
#ifdef CARTESIO_SOURCE_DIR
    std::string docs;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(CARTESIO_SOURCE_DIR) / "docs" / "manuale")) {
        std::ifstream in(entry.path(), std::ios::binary);
        docs += std::string(std::istreambuf_iterator<char>(in), {});
    }
    CommandManager m;
    registerBuiltinCommands(m);
    std::string missing;
    for (const std::string& name : m.commandNames())
        if (docs.find("`" + name + "`") == std::string::npos)
            missing += name + " ";
    if (!missing.empty())
        std::printf("Comandi senza voce nel manuale: %s\n", missing.c_str());
    CHECK(missing.empty());
#endif
}

// CanaloCAD: stessi numeri della pagina (casi generati con i suoi calcoli).
#include "canalo_expected.inc"

int main()
{
    testInputParser();
    testDataExport();
    testUndoRedo();
    testPick();
    testLineCommand();
    testCircleAndErase();
    testDxfRoundTrip();
    testIntersections();
    testArcThroughPoints();
    testWindowSelection();
    testSelectionPicker();
    testSelectionUx();
    testTextAndClipboardUx();
    testDraftingTools();
    testMoveAndCopy();
    testOffset();
    testTrim();
    testArcDxf();
    testPolyline();
    testPolylineTrim();
    testRectangleAndOffset();
    testBlocksAndExplode();
    testDimension();
    testModernDxf();
    testPolylineWidth();
    testHatchSetPattern();
    testBindXref();
    testBlockEditAndImport();
    testUndoTables();
    testPickPriority();
    testLayerStatesAndXClip();
    testHatchTextBoxes();
    testFullRoundTrip();
    testDxfEncoding();
    testDxf2000RoundTrip();
    testTextStylesRoundTrip();
    testAttributesAndLayouts();
    testTransformCommands();
    testExtendFilletGrips();
    testClipboardBetweenDrawings();
    testScriptCommands();
    testOsnap();
    testAdvancedViewports();
    testTitleBlock();
    testLinetypesAndLineweights();
    testHatchBoundary();
    testChamferBreakJoinStretch();
    testDimStylesAndLeaders();
    testHatchPatternsPreserved();
    testExtraCommands();
    testWipeoutMlineTableGroup();
    testPlotSettingsAndFrame();
    testPlotStyleTables();
    testPlotSettingsInDxf();
    testInsUnitsAndScaledImport();
    testCullingAndLayoutBlocks();
    testMergeAlternativeConversion();
    testSingleLineDiagram();
    testCircuitTags();
    testPointLegend();
    testImageAndXrefRoundTrip();
    testOleRoundTrip();
    testDrawOrder();
    testFindIndex();
    testAttributeSync();
    testFindReplace();
    testQuickSelect();
    testDynamicBlockName();
    testAngularOrdinateDims();
    testAnnotationScales();
    testStyleUndo();
    testUndoKeepsOrder();
    testMTextWidthGrips();
    testMultiLeader();
    testCanaloAgainstPage();
    testCanaloSections();
    testPlant();
    testManualCoversCommands();
    testVersions();
    testRouting();
    testRunImport();
    testMalformedFiles();
    testCanaloXrecordRoundTrip();

    if (failures == 0)
        std::printf("Tutti i test superati.\n");
    else
        std::printf("%d controlli falliti.\n", failures);
    return failures == 0 ? 0 : 1;
}
