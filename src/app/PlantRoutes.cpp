// Percorsi automatici dei circuiti (tavolozza Impianto, scheda Tratte): dal quadro agli utilizzatori sulle tratte
// disegnate. Il calcolo è in core/Routing; qui si leggono dal disegno tratte, quadri e sigle, si mostra il
// riepilogo e si aggiornano tratte e lunghezze del progetto.

#include "app/AppOptions.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlantPanel.h"
#include "core/CircuitTags.h"
#include "core/Routing.h"

#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <set>

namespace cartesio {

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QSettings store() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

// Quadro di una sigla di circuito: "QPT-SX-N C-14" -> "QPT-SX-N".
std::string boardOf(const std::string& code)
{
    const auto pos = code.rfind(" C-");
    return pos == std::string::npos ? std::string() : code.substr(0, pos);
}

std::string upperTrim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    s = s.substr(i);
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Tratti di una linea, polilinea o arco (archi e rigonfiamenti approssimati con corde).
void addSegments(const Entity& e, const std::string& run, std::vector<plant::RouteSegment>& out)
{
    switch (e.type()) {
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        out.push_back({l.start, l.end, run});
        break;
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        for (std::size_t i = 1; i < pl.vertices.size(); ++i)
            out.push_back({pl.vertices[i - 1].point, pl.vertices[i].point, run});
        if (pl.closed && pl.vertices.size() > 2)
            out.push_back({pl.vertices.back().point, pl.vertices.front().point, run});
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        const int n = 8;
        Vec2 prev = a.center + Vec2::polar(a.radius, a.startAngle);
        for (int k = 1; k <= n; ++k) {
            const Vec2 p = a.center + Vec2::polar(a.radius, a.startAngle + a.sweep() * k / n);
            out.push_back({prev, p, run});
            prev = p;
        }
        break;
    }
    default: break;
    }
}

} // namespace

PlantPanel::RouteReport PlantPanel::computeRoutes(double maxAccessM, double extraM) const
{
    RouteReport report;
    if (!drawing_)
        return report;
    const Document& doc = drawing_->document();
    const int units = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
    const double toM = unitsFactor(units, 6);

    // Rete: le tratte del progetto disegnate qui.
    std::vector<plant::RouteSegment> segments;
    std::set<std::string> runIds;
    for (const plant::Run& r : project_.runs)
        runIds.insert(r.id);
    for (const auto& e : doc.entities())
        if (e->space() == 0 && runIds.count(e->plantRun())) {
            addSegments(*e, e->plantRun(), segments);
            report.runsInDrawing.insert(e->plantRun());
        }

    // Quadri: oggetto segnato con "Quadro dalla selezione", altrimenti un testo con il solo nome del quadro.
    std::map<std::string, Vec2> boards;
    for (const auto& e : doc.entities()) {
        if (e->space() != 0)
            continue;
        const std::string& tag = e->plantRun();
        if (tag.rfind("quadro:", 0) == 0) {
            const BBox b = e->bounds();
            if (b.isValid())
                boards[upperTrim(tag.substr(7))] = (b.min + b.max) * 0.5;
        }
    }
    for (const auto& e : doc.entities()) {
        if (e->space() != 0)
            continue;
        std::string text;
        Vec2 at;
        if (e->type() == EntityType::Text) {
            text = static_cast<const TextEntity&>(*e).text;
            at = static_cast<const TextEntity&>(*e).position;
        } else if (e->type() == EntityType::MText) {
            text = mtextPlainText(static_cast<const MTextEntity&>(*e).text);
            at = static_cast<const MTextEntity&>(*e).position;
        } else {
            continue;
        }
        const std::string name = upperTrim(text);
        if (!name.empty() && !boards.count(name))
            boards.emplace(name, at);   // il segno esplicito vince sul testo
    }

    // Arrivi: le sigle dei circuiti in pianta, al punto (utilizzatore) più vicino se c'è.
    QSettings s = store();
    CircuitScanOptions scan;
    scan.pointLayers = s.value(QStringLiteral("Circuits/pointLayers"), QStringLiteral("E-ELEC-FIXT")).toString().toStdString();
    scan.letterLayers = s.value(QStringLiteral("Circuits/letterLayers"), QStringLiteral("E-ELEC-FIXT-IDEN")).toString().toStdString();
    scan.tagLayers = s.value(QStringLiteral("Circuits/tagLayers"), QStringLiteral("E-ELEC-CIRC")).toString().toStdString();
    const std::vector<CircuitPoint> points = findCircuitPoints(doc, scan);
    std::map<std::string, std::vector<Vec2>> targets;
    for (const CircuitTag& t : findCircuitTags(doc, scan)) {
        if (!project_.circuit(t.code))
            continue;
        Vec2 at = t.position;
        double best = scan.tagRadius;
        for (const CircuitPoint& p : points)
            if (const double d = distance(p.position, t.position); d <= best) {
                best = d;
                at = p.position;
            }
        targets[t.code].push_back(at);
    }

    std::vector<plant::RouteRequest> requests;
    std::set<std::string> missingBoards;
    for (const auto& [code, to] : targets) {
        const std::string board = upperTrim(boardOf(code));
        const auto it = boards.find(board);
        if (it == boards.end()) {
            missingBoards.insert(board);
            continue;
        }
        requests.push_back({code, it->second, to});
    }
    for (const std::string& b : missingBoards)
        report.missingBoards << q(b);
    report.circuitsWithTags = static_cast<int>(targets.size());

    plant::RouteOptions o;
    o.unitsToM = toM;
    o.joinTolerance = 0.005 / toM;   // 5 mm
    o.maxAccess = maxAccessM / toM;
    o.extraPerCircuit = extraM;
    report.results = plant::routeCircuits(segments, requests, o);

    for (const plant::RouteResult& r : report.results) {
        QStringList runs;
        for (const std::string& id : r.runs)
            runs << q(id);
        if (r.reached == 0)
            report.lines << tr("%1: non raggiunto (%2)")
                                .arg(q(r.circuit), r.sourceOnNetwork ? tr("utilizzatori lontani dalle tratte o in una rete non collegata al quadro")
                                                                     : tr("quadro lontano dalle tratte"));
        else
            report.lines << tr("%1: %2 - %3 m%4")
                                .arg(q(r.circuit), runs.isEmpty() ? tr("(solo attacchi)") : runs.join(QStringLiteral(", ")))
                                .arg(QString::number(r.lengthM, 'f', 1).replace(u'.', u','))
                                .arg(r.unreached > 0 ? tr(" - %1 utilizzatori non raggiunti").arg(r.unreached) : QString());
    }
    if (!report.missingBoards.isEmpty())
        report.lines << tr("Quadri non trovati nel disegno (circuiti non calcolati): %1. Seleziona il simbolo del quadro e usa "
                           "\"Quadro dalla selezione\", oppure scrivi il nome del quadro come testo accanto al quadro.")
                            .arg(report.missingBoards.join(QStringLiteral(", ")));
    return report;
}

int PlantPanel::applyRoutes(const RouteReport& report)
{
    int applied = 0;
    for (const plant::RouteResult& r : report.results) {
        if (r.reached == 0)
            continue;
        // Il circuito esce dalle tratte di questo disegno ed entra in quelle del percorso.
        for (plant::Run& run : project_.runs) {
            if (!report.runsInDrawing.count(run.id))
                continue;
            run.circuits.erase(std::remove(run.circuits.begin(), run.circuits.end(), r.circuit), run.circuits.end());
            if (std::find(r.runs.begin(), r.runs.end(), run.id) != r.runs.end())
                run.circuits.push_back(r.circuit);
        }
        if (plant::Circuit* c = project_.circuit(r.circuit))
            c->pathLengthM = r.lengthM;
        ++applied;
    }
    if (applied > 0) {
        save();
        status_ = tr("Percorsi applicati a %n circuiti.", nullptr, applied);
        fill();
        emit changed();
    }
    return applied;
}

void PlantPanel::routesDialog()
{
    if (path_.isEmpty() || !drawing_) {
        QMessageBox::information(nullptr, tr("Percorsi automatici"), tr("Prima creare o aprire un progetto d'impianto."));
        return;
    }
    QSettings s = store();
    double maxAccess = s.value(QStringLiteral("Plant/routeMaxAccess"), 5.0).toDouble();
    double extra = s.value(QStringLiteral("Plant/routeExtra"), 3.0).toDouble();
    FormDialog dialog(tr("Percorsi automatici"), FormDialog::Ok | FormDialog::Cancel);
    dialog.note(tr("Per ogni circuito con la sigla in pianta: dal quadro agli utilizzatori lungo le tratte disegnate. Le tratte "
                   "si collegano dove si toccano gli estremi o dove un estremo cade su un'altra tratta; due tratte che si "
                   "incrociano e basta non sono collegate. Il quadro è l'oggetto segnato con \"Quadro dalla selezione\" o un "
                   "testo con il suo nome."));
    dialog.number(tr("Distanza massima di quadro e utilizzatori dalle tratte"), 0.5, 100, 1, maxAccess, QStringLiteral(" m"));
    dialog.number(tr("Da aggiungere a ogni circuito (risalite, discese, scorte)"), 0, 100, 1, extra, QStringLiteral(" m"));
    dialog.note(tr("Il risultato si vede prima di applicarlo. Applicato, ogni circuito calcolato è assegnato alle tratte del suo "
                   "percorso in questo disegno e prende la lunghezza dell'utilizzatore più lontano (usata per la caduta di "
                   "tensione se lo schema non ne dà una)."));
    if (dialog.exec() != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("Plant/routeMaxAccess"), maxAccess);
    s.setValue(QStringLiteral("Plant/routeExtra"), extra);

    const RouteReport report = computeRoutes(maxAccess, extra);
    int reached = 0, partial = 0;
    for (const plant::RouteResult& r : report.results) {
        reached += r.reached > 0;
        partial += r.reached > 0 && r.unreached > 0;
    }
    QMessageBox box;
    box.setWindowTitle(tr("Percorsi automatici"));
    box.setText(tr("Circuiti con la sigla in pianta: %1. Percorso trovato: %2 (%3 con qualche utilizzatore non raggiunto). "
                   "Non calcolati: %4.")
                    .arg(report.circuitsWithTags)
                    .arg(reached)
                    .arg(partial)
                    .arg(report.circuitsWithTags - reached));
    box.setInformativeText(report.missingBoards.isEmpty() ? tr("Applicare i percorsi trovati?")
                                                          : tr("Quadri non trovati: %1.\n\nApplicare i percorsi trovati?")
                                                                .arg(report.missingBoards.join(QStringLiteral(", "))));
    box.setDetailedText(report.lines.join(u'\n'));
    QPushButton* apply = box.addButton(tr("Applica"), QMessageBox::AcceptRole);
    box.addButton(tr("Annulla"), QMessageBox::RejectRole);
    apply->setEnabled(reached > 0);
    box.exec();
    if (box.clickedButton() != apply)
        return;
    applyRoutes(report);
    for (const QString& line : report.lines)
        emit message(line);
}

bool PlantPanel::tagBoard(const QString& name, QString* error)
{
    if (!drawing_ || path_.isEmpty()) {
        if (error)
            *error = tr("prima creare o aprire un progetto d'impianto");
        return false;
    }
    const std::vector<EntityId> selected = drawing_->selection();
    if (selected.empty()) {
        if (error)
            *error = tr("selezionare prima il simbolo del quadro");
        return false;
    }
    const std::string tag = "quadro:" + upperTrim(name.toStdString());
    // Un solo segno per quadro: si toglie da altri oggetti.
    Document& doc = drawing_->document();
    std::vector<std::unique_ptr<Entity>> copies;
    std::vector<EntityId> modified;
    for (const auto& e : doc.entities())
        if (e->plantRun() == tag && std::find(selected.begin(), selected.end(), e->id()) == selected.end()) {
            auto copy = e->clone();
            copy->setPlantRun({});
            copies.push_back(std::move(copy));
            modified.push_back(e->id());
        }
    for (EntityId id : selected)
        if (const Entity* e = doc.find(id)) {
            auto copy = e->clone();
            copy->setPlantRun(tag);
            copies.push_back(std::move(copy));
            modified.push_back(id);
        }
    doc.commit(std::move(copies), modified);
    drawing_->notifyChanged();
    status_ = tr("Quadro %1 segnato nel disegno.").arg(q(tag.substr(7)));
    emit changed();
    return true;
}

void PlantPanel::boardFromSelection()
{
    if (!drawing_ || path_.isEmpty()) {
        QMessageBox::information(nullptr, tr("Quadro dalla selezione"), tr("Prima creare o aprire un progetto d'impianto."));
        return;
    }
    if (drawing_->selection().empty()) {
        QMessageBox::information(nullptr, tr("Quadro dalla selezione"), tr("Seleziona prima nel disegno il simbolo del quadro."));
        return;
    }
    std::set<std::string> boards;
    for (const plant::Circuit& c : project_.circuits)
        if (const std::string b = boardOf(c.code); !b.empty())
            boards.insert(b);
    QList<QPair<QString, QVariant>> items;
    for (const std::string& b : boards)
        items.append({q(b), q(b)});
    QString name = items.isEmpty() ? QString() : items.front().second.toString();
    FormDialog dialog(tr("Quadro dalla selezione"), FormDialog::Ok | FormDialog::Cancel);
    dialog.note(tr("Gli oggetti selezionati diventano il punto di partenza dei circuiti di questo quadro (il centro del loro ingombro)."));
    if (!items.isEmpty())
        dialog.choice(tr("Quadro"), items, name, [&name](const QVariant& v) { name = v.toString(); });
    else
        dialog.text(tr("Nome del quadro"), name, QStringLiteral("QPT-SX-N"));
    if (dialog.exec() != QDialog::Accepted || name.trimmed().isEmpty())
        return;
    QString error;
    if (!tagBoard(name, &error))
        QMessageBox::warning(nullptr, tr("Quadro dalla selezione"), error);
}

} // namespace cartesio
