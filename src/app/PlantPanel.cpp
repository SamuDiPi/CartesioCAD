#include "app/PlantPanel.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PdfWords.h"
#include "app/PlantFile.h"
#include "app/QmlUi.h"
#include "app/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

#include <cmath>
#include <set>

namespace cartesio {

namespace {

using plant::Level;

QString q(const std::string& s) { return QString::fromStdString(s); }

QString num(double v, int decimals = 1)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(u'.')) {
        while (s.endsWith(u'0'))
            s.chop(1);
        if (s.endsWith(u'.'))
            s.chop(1);
    }
    return s.replace(u'.', u',');
}

QSettings store() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

QString levelText(Level l)
{
    switch (l) {
    case Level::Error: return QObject::tr("NON VERIFICATA");
    case Level::Warning: return QObject::tr("da controllare");
    default: return QObject::tr("verificata");
    }
}

QString runSize(const plant::Run& r)
{
    if (r.kind == plant::RunKind::Tube)
        return QObject::tr("Ø%1 %2").arg(num(r.tubeOuter, 0), q(r.tubeType));
    return QStringLiteral("%1×%2").arg(num(r.width, 0), num(r.height, 0));
}

std::vector<std::string> splitCodes(const QString& text)
{
    std::vector<std::string> out;
    for (QString part : text.split(QRegularExpression(QStringLiteral("[;\\n]"))))
        if (!(part = part.trimmed()).isEmpty())
            out.push_back(part.toStdString());
    return out;
}

QString joinCodes(const std::vector<std::string>& codes)
{
    QStringList list;
    for (const std::string& c : codes)
        list << q(c);
    return list.join(QStringLiteral("; "));
}

bool isPath(const Entity& e)
{
    return e.type() == EntityType::Line || e.type() == EntityType::Polyline || e.type() == EntityType::Arc;
}

double pathLength(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        return distance(l.start, l.end);
    }
    case EntityType::Polyline: return static_cast<const PolylineEntity&>(e).length();
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        return a.radius * a.sweep();
    }
    default: return 0.0;
    }
}

} // namespace

PlantPanel::PlantPanel(QWidget* parent) : QQuickWidget(parent)
{
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    rootContext()->setContextProperty(QStringLiteral("plant"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/PlantPanel.qml")));
    setMinimumWidth(360);
    status_ = tr("Nessun progetto: crearne uno nuovo o aprirne uno esistente (file .cdb, condiviso dai disegni "
                 "dell'impianto).");
}

QString PlantPanel::projectFile() const
{
    return path_.isEmpty() ? QString() : QDir::toNativeSeparators(path_);
}

QString PlantPanel::projectName() const
{
    if (path_.isEmpty())
        return {};
    return project_.name.empty() ? QFileInfo(path_).completeBaseName() : q(project_.name);
}

void PlantPanel::setDrawing(DrawingWidget* drawing)
{
    drawing_ = drawing;
    loadLinked();
    refresh();
}

void PlantPanel::refresh()
{
    loadLinked();   // il disegno può essere stato letto dopo setDrawing
    fill();
}

// ---------------------------------------------------------------- percorsi

QString PlantPanel::resolvedPath(const QString& stored) const
{
    if (stored.isEmpty() || !drawing_ || drawing_->filePath().isEmpty())
        return QDir::cleanPath(stored);
    const QDir folder = QFileInfo(drawing_->filePath()).absoluteDir();
    if (!QFileInfo(stored).isAbsolute())
        return QDir::cleanPath(folder.filePath(stored));
    // Percorso assoluto (disegno collegato prima di essere salvato): se la cartella è stata spostata, il progetto
    // con lo stesso nome accanto al disegno.
    if (!QFileInfo::exists(stored) && QFileInfo::exists(folder.filePath(QFileInfo(stored).fileName())))
        return QDir::cleanPath(folder.filePath(QFileInfo(stored).fileName()));
    return QDir::cleanPath(stored);
}

QString PlantPanel::storedPath(const QString& absolute) const
{
    // Relativo alla cartella del disegno (così l'impianto si sposta insieme ai disegni), se il disegno è salvato.
    if (!drawing_ || drawing_->filePath().isEmpty())
        return QDir::cleanPath(absolute);
    return QFileInfo(drawing_->filePath()).absoluteDir().relativeFilePath(absolute);
}

void PlantPanel::loadLinked()
{
    if (!drawing_)
        return;
    const QString stored = q(drawing_->document().plantFile());
    if (stored.isEmpty())
        return;
    const QString path = resolvedPath(stored);
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(path_).absoluteFilePath() && !path_.isEmpty())
        return;
    QString error;
    if (!openProject(path, &error))
        status_ = tr("Il disegno è collegato a %1, che non si riesce ad aprire: %2").arg(QDir::toNativeSeparators(stored), error);
    emit changed();
}

// ---------------------------------------------------------------- progetto

bool PlantPanel::save()
{
    if (path_.isEmpty())
        return false;
    QString error;
    if (!savePlant(path_, project_, &error)) {
        emit message(tr("Impianto: %1").arg(error));
        return false;
    }
    return true;
}

bool PlantPanel::createProject(const QString& path, QString* error)
{
    plant::Project p;
    p.name = QFileInfo(path).completeBaseName().toStdString();
    if (!savePlant(path, p, error))
        return false;
    project_ = std::move(p);
    path_ = QFileInfo(path).absoluteFilePath();
    checks_.clear();
    if (drawing_)
        drawing_->document().setPlantFile(storedPath(path_).toStdString());
    status_ = tr("Progetto nuovo: importare lo schema unifilare in PDF o aggiungere i circuiti a mano.");
    emit changed();
    fill();
    return true;
}

bool PlantPanel::openProject(const QString& path, QString* error)
{
    plant::Project p;
    if (!loadPlant(path, p, error))
        return false;
    project_ = std::move(p);
    path_ = QFileInfo(path).absoluteFilePath();
    checks_.clear();
    if (drawing_ && drawing_->document().plantFile().empty())
        drawing_->document().setPlantFile(storedPath(path_).toStdString());
    status_ = tr("Progetto aperto.");
    emit changed();
    fill();
    emit projectOpened();
    return true;
}

bool PlantPanel::importSchema(const QString& path, QString* error)
{
    if (path_.isEmpty()) {
        if (error)
            *error = tr("prima creare o aprire un progetto");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    std::vector<PdfWord> words;
    std::string failure;
    if (!readPdfWords(file.readAll().toStdString(), words, &failure)) {
        if (error)
            *error = q(failure);
        return false;
    }
    const std::vector<Circuit> schema = parseSingleLineDiagram(words);
    if (schema.empty()) {
        if (error)
            *error = tr("nel PDF non ci sono schemi unifilari riconoscibili (manca la riga \"Sigla utenza\")");
        return false;
    }
    const auto [added, updated] = mergeSchema(project_, schema);
    save();
    status_ = tr("Schema %1: %2 circuiti nuovi, %3 aggiornati.").arg(QFileInfo(path).fileName()).arg(added).arg(updated);
    emit changed();
    fill();
    emit schemaImported(schema, path);
    return true;
}

void PlantPanel::mergeFromSchema(const std::vector<cartesio::Circuit>& schema, const QString& path)
{
    if (path_.isEmpty())
        return;
    const auto [added, updated] = mergeSchema(project_, schema);
    save();
    status_ = tr("Schema %1 (dalla tavolozza Circuiti): %2 circuiti nuovi, %3 aggiornati.")
                  .arg(QFileInfo(path).fileName())
                  .arg(added)
                  .arg(updated);
    emit changed();
    fill();
}

std::vector<cartesio::Circuit> PlantPanel::asSchema() const
{
    std::vector<cartesio::Circuit> out;
    for (const plant::Circuit& pc : project_.circuits) {
        cartesio::Circuit c;
        c.code = pc.code;
        if (const auto space = pc.code.find_last_of(' '); space != std::string::npos)
            c.board = pc.code.substr(0, space);
        const auto dash = pc.code.find_last_of('-');
        if (dash != std::string::npos)
            c.number = std::atoi(pc.code.c_str() + dash + 1);
        c.description = pc.description;
        c.kind = classifyCircuit(pc.description);
        c.cable = (pc.cableType + " " + pc.formation);
        c.current = pc.ib > 0.0 ? num(pc.ib).toStdString() : std::string();
        c.length = pc.lengthM > 0.0 ? num(pc.lengthM).toStdString() : std::string();
        c.breaker = pc.breaker;
        out.push_back(c);
    }
    return out;
}

void PlantPanel::putCircuit(const plant::Circuit& circuit)
{
    if (plant::Circuit* c = project_.circuit(circuit.code))
        *c = circuit;
    else
        project_.circuits.push_back(circuit);
    save();
    status_ = tr("Circuito %1 salvato.").arg(q(circuit.code));
    emit changed();
    fill();
}

// ---------------------------------------------------------------- tratte e disegno

std::vector<EntityId> PlantPanel::selectedPathObjects() const
{
    std::vector<EntityId> out;
    if (!drawing_)
        return out;
    for (EntityId id : drawing_->selection())
        if (const Entity* e = drawing_->document().find(id); e && isPath(*e))
            out.push_back(id);
    return out;
}

std::vector<EntityId> PlantPanel::runEntities(const std::string& id) const
{
    std::vector<EntityId> out;
    if (!drawing_)
        return out;
    for (const auto& e : drawing_->document().entities())
        if (e->plantRun() == id)
            out.push_back(e->id());
    return out;
}

double PlantPanel::runLengthM(const std::string& id) const
{
    if (!drawing_)
        return 0.0;
    const Document& doc = drawing_->document();
    double length = 0.0;
    for (const auto& e : doc.entities())
        if (e->plantRun() == id)
            length += pathLength(*e);
    return length * plantMetersPerUnit(doc);
}

std::map<std::string, double> PlantPanel::runLengths() const
{
    std::map<std::string, double> out;
    for (const plant::Run& r : project_.runs)
        out[r.id] = runLengthM(r.id);
    return out;
}

// Lunghezza, curve e derivazioni di ogni tratta nel disegno: tratti delle linee e polilinee; una curva dove il
// percorso cambia direzione di oltre 15°, una derivazione dove si incontrano tre o più tratti.
std::map<std::string, plant::RunGeometry> PlantPanel::runGeometry() const
{
    std::map<std::string, plant::RunGeometry> out;
    if (!drawing_)
        return out;
    const Document& doc = drawing_->document();
    const double toM = plantMetersPerUnit(doc);
    const double tol = 0.001 / std::max(1e-12, toM);   // 1 mm nelle unità del disegno
    auto turn = [](Vec2 d1, Vec2 d2) {
        const double a = std::atan2(d1.x * d2.y - d1.y * d2.x, d1.x * d2.x + d1.y * d2.y);
        return std::abs(a) * 180.0 / kPi;
    };
    for (const plant::Run& r : project_.runs) {
        plant::RunGeometry g;
        struct End { Vec2 p; Vec2 dir; };   // estremo di un tratto e direzione verso l'esterno
        std::vector<End> ends;
        for (const auto& e : doc.entities()) {
            if (e->plantRun() != r.id)
                continue;
            g.lengthM += pathLength(*e) * toM;
            std::vector<Vec2> pts;
            if (e->type() == EntityType::Line) {
                const auto& l = static_cast<const LineEntity&>(*e);
                pts = {l.start, l.end};
            } else if (e->type() == EntityType::Polyline) {
                const auto& pl = static_cast<const PolylineEntity&>(*e);
                for (const PolyVertex& v : pl.vertices)
                    pts.push_back(v.point);
                for (std::size_t i = 1; i + 1 < pts.size(); ++i)
                    if (turn(pts[i] - pts[i - 1], pts[i + 1] - pts[i]) > 15.0)
                        ++g.bends;
            }
            if (pts.size() >= 2) {
                ends.push_back({pts.front(), pts[0] - pts[1]});
                ends.push_back({pts.back(), pts.back() - pts[pts.size() - 2]});
            }
        }
        // Estremi che coincidono: 2 = continuazione o curva, 3 o più = derivazione.
        std::vector<bool> used(ends.size(), false);
        for (std::size_t i = 0; i < ends.size(); ++i) {
            if (used[i])
                continue;
            std::vector<std::size_t> group{i};
            for (std::size_t j = i + 1; j < ends.size(); ++j)
                if (!used[j] && distance(ends[i].p, ends[j].p) <= tol)
                    group.push_back(j);
            for (std::size_t k : group)
                used[k] = true;
            if (group.size() == 2) {
                // Direzione di arrivo del primo e di partenza del secondo.
                if (turn(Vec2{} - ends[group[0]].dir, ends[group[1]].dir) > 15.0)
                    ++g.bends;
            } else if (group.size() >= 3) {
                g.tees += static_cast<int>(group.size()) - 2;
            }
        }
        out[r.id] = g;
    }
    return out;
}

std::vector<plant::BomItem> PlantPanel::bom(const QString& csvPath, bool table, QString* error)
{
    std::vector<plant::BomItem> items;
    if (!drawing_ || path_.isEmpty()) {
        if (error)
            *error = tr("prima creare o aprire un progetto");
        return items;
    }
    items = plant::plantBom(project_, runGeometry());
    if (items.empty()) {
        if (error)
            *error = tr("nessuna tratta disegnata in questo disegno");
        return items;
    }
    if (!csvPath.isEmpty()) {
        QFile f(csvPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error)
                *error = tr("impossibile scrivere %1").arg(QDir::toNativeSeparators(csvPath));
            return {};
        }
        f.write("\xEF\xBB\xBF");   // UTF-8 con BOM: Excel legge gli accenti e Ø
        f.write(QByteArray::fromStdString(plant::bomCsv(items)));
    }
    if (table) {
        Document& doc = drawing_->document();
        BBox box;
        // A destra di tutto ciò che è dell'impianto (tratte, etichette, sezioni), tranne la distinta di prima.
        for (const auto& e : doc.entities())
            if (!e->plantRun().empty() && e->plantRun() != "distinta:")
                box.expand(e->bounds());
        const double h = labelHeight();
        const Vec2 corner = box.isValid() ? Vec2{box.max.x + h * 6.0, box.max.y - h * 2.0} : Vec2{0.0, 0.0};
        auto t = plant::bomTable(items, corner, h);
        auto title = std::make_unique<MTextEntity>(Vec2{corner.x, corner.y + h * 2.0}, h * 1.3,
                                                   "DISTINTA MATERIALI - " + projectName().toStdString() + " (quantità senza sfrido)");
        int layer = doc.findLayer("IMP-DISTINTA");
        if (layer < 0) {
            Layer l;
            l.name = "IMP-DISTINTA";
            layer = doc.addLayer(l);
        }
        t->setLayer(layer);
        t->setPlantRun("distinta:");
        title->setLayer(layer);
        title->setPlantRun("distinta:");
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(t));
        added.push_back(std::move(title));
        replaceTagged("distinta:", std::move(added));
    }
    return items;
}

void PlantPanel::bomDialog()
{
    if (path_.isEmpty())
        return;
    QTimer::singleShot(0, this, [this] {
        bool csv = true, table = true;
        FormDialog d(tr("Distinta materiali dell'impianto"), FormDialog::Ok | FormDialog::Cancel, this);
        d.note(tr("Dalle tratte di questo disegno: tubi, canali, passerelle con accessori e staffe, metri di cavo per tipo e "
                  "formazione. Quantità senza sfrido."));
        d.check(tr("Salva per Excel (CSV)"), csv);
        d.check(tr("Inserisci la tabella nel disegno (sostituisce quella di prima)"), table);
        d.setSize(420, 220);
        if (d.exec() != QDialog::Accepted || (!csv && !table))
            return;
        QString path;
        if (csv) {
            QString start = store().value(QStringLiteral("Plant/lastFolder")).toString();
            if (drawing_ && !drawing_->filePath().isEmpty())
                start = QFileInfo(drawing_->filePath()).absolutePath() + u'/' + QFileInfo(path_).completeBaseName() + QStringLiteral("_distinta.csv");
            path = QFileDialog::getSaveFileName(this, tr("Distinta materiali"), start, tr("CSV per Excel (*.csv)"));
            if (path.isEmpty() && !table)
                return;
        }
        QString error;
        const auto items = bom(path, table, &error);
        status_ = items.empty() ? tr("Distinta: %1.").arg(error)
                                : tr("Distinta: %n voci%1.", nullptr, static_cast<int>(items.size()))
                                      .arg(path.isEmpty() ? QString() : tr(", salvata in %1").arg(QFileInfo(path).fileName()));
        emit changed();
    });
}

void PlantPanel::tagObjects(const std::vector<EntityId>& ids, const std::string& runId)
{
    if (!drawing_ || ids.empty())
        return;
    Document& doc = drawing_->document();
    std::vector<std::unique_ptr<Entity>> copies;
    std::vector<EntityId> changed;
    for (EntityId id : ids)
        if (const Entity* e = doc.find(id)) {
            auto copy = e->clone();
            copy->setPlantRun(runId);
            copies.push_back(std::move(copy));
            changed.push_back(id);
        }
    doc.commit(std::move(copies), changed);
    if (doc.plantFile().empty() && !path_.isEmpty())
        doc.setPlantFile(storedPath(path_).toStdString());
    drawing_->notifyChanged();
}

QString PlantPanel::newRun(plant::Run run, QString* error)
{
    if (path_.isEmpty()) {
        if (error)
            *error = tr("prima creare o aprire un progetto");
        return {};
    }
    const std::vector<EntityId> ids = selectedPathObjects();
    if (ids.empty()) {
        if (error)
            *error = tr("selezionare le linee o polilinee del percorso della tratta");
        return {};
    }
    if (run.id.empty() || project_.run(run.id))
        run.id = project_.nextRunId();
    const std::string id = run.id;
    project_.runs.push_back(std::move(run));
    save();
    tagObjects(ids, id);
    tab_ = 1;
    status_ = tr("Tratta %1 creata con %n oggetti del disegno.", nullptr, static_cast<int>(ids.size())).arg(q(id));
    emit changed();
    fill();
    return q(id);
}

QStringList PlantPanel::checkAll()
{
    loadLinked();
    QStringList out;
    checks_.clear();
    int bad = 0, warn = 0;
    for (const plant::Run& r : project_.runs) {
        const double length = runLengthM(r.id);
        const plant::RunCheck rc = plant::checkRun(project_, r, length);
        checks_[r.id] = rc;
        bad += rc.level == Level::Error;
        warn += rc.level == Level::Warning;
        out << tr("%1 %2 %3%4: %5 circuiti, %6 - %7")
                   .arg(q(r.id), QString::fromLatin1(plant::runKindName(r.kind)), runSize(r),
                        r.name.empty() ? QString() : QStringLiteral(" (%1)").arg(q(r.name)))
                   .arg(r.circuits.size())
                   .arg(length > 0.0 ? tr("%1 m in questo disegno").arg(num(length)) : tr("non disegnata qui"),
                        levelText(rc.level).toUpper());
        for (const plant::Finding& f : rc.findings)
            out << QStringLiteral("    %1 %2").arg(f.level == Level::Ok ? QStringLiteral("✓") : f.level == Level::Warning ? QStringLiteral("!")
                                                                                                                       : QStringLiteral("✗"),
                                                     q(f.text));
    }
    // Circuiti delle tratte (e quelli con la corrente d'impiego): portata e caduta di tensione.
    const auto lengths = runLengths();
    int badCircuits = 0;
    QStringList circuitLines;
    for (const plant::Circuit& c : project_.circuits) {
        const plant::CircuitCheck cc = plant::checkCircuit(project_, c, lengths);
        if (cc.findings.empty() || (cc.runs.empty() && c.ib <= 0.0))
            continue;
        badCircuits += cc.level == Level::Error;
        if (cc.level == Level::Ok)
            continue;
        circuitLines << tr("%1 %2 - %3").arg(q(c.code), q(c.description), levelText(cc.level).toUpper());
        for (const plant::Finding& f : cc.findings)
            if (f.level != Level::Ok)
                circuitLines << QStringLiteral("    %1 %2").arg(f.level == Level::Warning ? QStringLiteral("!") : QStringLiteral("✗"), q(f.text));
    }
    if (!circuitLines.isEmpty()) {
        out << tr("Circuiti da rivedere (portata, protezione, caduta di tensione):");
        out << circuitLines;
    }
    out << tr("Portate e cadute di tensione sono indicative: le tabelle di CartesioCAD non sono ancora state confrontate con il testo della norma (CEI 64-8:2024 Cap. 52 = IEC 60364-5-52:2009; CEI-UNEL 35024/1). Verificarle prima di usarle in un progetto.");
    out.prepend(tr("Impianto %1: %2 tratte, %3 non verificate, %4 da controllare; %5 circuiti non verificati.")
                    .arg(projectName())
                    .arg(project_.runs.size())
                    .arg(bad)
                    .arg(warn)
                    .arg(badCircuits));
    out << tr("Valori indicativi: diametri dei tubi e dei cavi da verificare con i dati dei costruttori.");
    return out;
}

// ---------------------------------------------------------------- tabella

void PlantPanel::fill()
{
    rows_.clear();
    rowIndex_.clear();
    if (path_.isEmpty()) {
        selected_ = -1;
        emit tableChanged();
        return;
    }
    // Tratte che contengono ogni circuito.
    std::map<std::string, QStringList> runsOf;
    for (const plant::Run& r : project_.runs)
        for (const std::string& c : r.circuits)
            if (!runsOf[c].contains(q(r.id)))
                runsOf[c] << q(r.id);
    const auto lengths = runLengths();
    if (tab_ == 0) {
        for (std::size_t i = 0; i < project_.circuits.size(); ++i) {
            const plant::Circuit& c = project_.circuits[i];
            std::string note;
            const auto cables = plant::cablesOf(project_, c, &note);
            // Circuito senza cavo nello schema (riserva, misure...): non è un errore finché non entra in una tratta.
            const bool noCable = c.formation.empty() && c.cableType.empty() && c.diameter <= 0.0;
            if (noCable)
                note = runsOf[c.code].isEmpty() ? "nessun cavo nello schema" : "nessun cavo: indicare formazione o diametro";
            QString cableText;
            if (!cables.empty())
                cableText = cables.size() == 1 ? tr("Ø%1").arg(num(cables[0].d)) : tr("%1 × Ø%2").arg(cables.size()).arg(num(cables[0].d));
            const QString cable = (q(c.cableType) + u' ' + q(c.formation)).trimmed();
            int level = noCable ? (runsOf[c.code].isEmpty() ? 0 : 2) : cables.empty() ? 2 : (note.empty() ? 0 : 1);
            QString iz, dv, esito;
            if (!cables.empty()) {
                const plant::CircuitCheck cc = plant::checkCircuit(project_, c, lengths);
                iz = num(cc.iz);
                dv = cc.dvPct > 0.0 ? num(cc.dvPct, 2) : QString();
                // L'esito conta se il circuito è in una tratta o ha la corrente d'impiego.
                if (!cc.runs.empty() || c.ib > 0.0) {
                    level = std::max(level, static_cast<int>(cc.level));
                    for (const plant::Finding& f : cc.findings)
                        if (static_cast<int>(f.level) == level && f.level != Level::Ok) {
                            note = f.text;
                            break;
                        }
                }
            }
            esito = noCable && runsOf[c.code].isEmpty() ? QStringLiteral("-") : levelText(static_cast<Level>(level));
            rows_.append(QVariantMap{
                {QStringLiteral("cells"), QStringList{q(c.code), esito, QString::fromLatin1(plant::systemName(c.system)), q(c.description), cable,
                                                      cableText, c.ib > 0.0 ? num(c.ib) : QString(), c.breakerIn > 0.0 ? num(c.breakerIn) : QString(),
                                                      iz, dv, runsOf[c.code].join(QStringLiteral(", ")), q(note)}},
                {QStringLiteral("level"), level}});
            rowIndex_.push_back(static_cast<int>(i));
        }
    } else if (tab_ == 1) {
        for (std::size_t i = 0; i < project_.runs.size(); ++i) {
            const plant::Run& r = project_.runs[i];
            const double length = runLengthM(r.id);
            const plant::RunCheck rc = plant::checkRun(project_, r, length);
            checks_[r.id] = rc;
            QString detail;
            for (const plant::Finding& f : rc.findings)
                if (f.level == rc.level) {
                    detail = q(f.text);
                    break;
                }
            rows_.append(QVariantMap{
                {QStringLiteral("cells"),
                 QStringList{q(r.id), levelText(rc.level), QString::fromLatin1(plant::runKindName(r.kind)) + (r.name.empty() ? QString() : u' ' + q(r.name)),
                             runSize(r), rc.cableCount > 0 ? num(rc.fillPct) + u'%' : QString(), length > 0.0 ? num(length) : QStringLiteral("-"),
                             joinCodes(r.circuits), detail}},
                {QStringLiteral("level"), static_cast<int>(rc.level)}});
            rowIndex_.push_back(static_cast<int>(i));
        }
    } else {
        for (std::size_t i = 0; i < project_.cables.size(); ++i) {
            const canalo::CableType& t = project_.cables[i];
            rows_.append(QVariantMap{{QStringLiteral("cells"), QStringList{q(t.code), q(t.type), q(t.form), num(t.d), num(t.w, 2), num(t.sec),
                                                                          QString::fromLatin1(plant::systemName(plant::systemFromName(t.sys)))}},
                                     {QStringLiteral("level"), 0}});
            rowIndex_.push_back(static_cast<int>(i));
        }
    }
    if (selected_ >= rows_.size())
        selected_ = -1;
    // Riepilogo: le tratte si verificano tutte (anche quelle non disegnate in questo disegno).
    int bad = 0, warn = 0;
    for (const plant::Run& r : project_.runs) {
        const plant::Level level = plant::checkRun(project_, r, runLengthM(r.id)).level;
        bad += level == Level::Error;
        warn += level == Level::Warning;
    }
    summary_ = tr("%1 circuiti, %2 tratte").arg(project_.circuits.size()).arg(project_.runs.size());
    if (bad > 0)
        summary_ += tr(", <font color=\"#ef5a5a\">%1 non verificate</font>").arg(bad);
    if (warn > 0)
        summary_ += tr(", <font color=\"#e3913a\">%1 da controllare</font>").arg(warn);
    emit tableChanged();
}

// ---------------------------------------------------------------- finestre

bool PlantPanel::editCircuit(plant::Circuit& c, bool isNew)
{
    QString code = q(c.code), desc = q(c.description), type = q(c.cableType), form = q(c.formation), breaker = q(c.breaker), notes = q(c.notes);
    int system = static_cast<int>(c.system);
    double ib = c.ib, length = c.lengthM, diameter = c.diameter, in = c.breakerIn, izRef = c.izRef;
    FormDialog d(isNew ? tr("Nuovo circuito") : tr("Circuito %1").arg(code), FormDialog::Ok | FormDialog::Cancel, this);
    d.text(tr("Sigla"), code, tr("QE1 C-3"));
    d.text(tr("Descrizione"), desc);
    d.choice(tr("Sistema"), {{tr("Energia"), 0}, {tr("Sicurezza (resistente al fuoco)"), 1}, {tr("Segnale (bassissima tensione, dati)"), 2}}, system,
             [&](const QVariant& v) { system = v.toInt(); });
    d.group(tr("Cavo"));
    d.text(tr("Tipo"), type, tr("FG16OM16"));
    d.text(tr("Formazione"), form, tr("3G2,5   4(1x95)   3(1x95)+1G50"));
    d.number(tr("Diametro imposto"), 0, 120, 1, diameter, QStringLiteral(" mm"));
    d.note(tr("Diametro 0 = dal catalogo dei cavi (e dai cavi propri del progetto) per tipo e formazione."));
    d.group(tr("Dati elettrici"));
    d.number(tr("Corrente d'impiego Ib"), 0, 5000, 1, ib, QStringLiteral(" A"));
    d.number(tr("Corrente nominale In"), 0, 6300, 1, in, QStringLiteral(" A"));
    d.number(tr("Portata di riferimento Iz"), 0, 5000, 0, izRef, QStringLiteral(" A"));
    d.note(tr("Iz 0 = dalle tabelle della IEC 60364-5-52 (CEI 64-8:2024 Cap. 52) / CEI-UNEL 35024/1 per la posa della tratta (rame, 30 °C); la verifica applica "
              "raggruppamento e temperatura. Scrivere qui la portata solo per imporla."));
    d.number(tr("Lunghezza"), 0, 10000, 1, length, QStringLiteral(" m"));
    d.note(tr("Lunghezza 0 = somma delle tratte disegnate che contengono il circuito."));
    d.text(tr("Interruttore"), breaker);
    d.text(tr("Note"), notes);
    d.setSize(460, 520);
    if (d.exec() != QDialog::Accepted || code.trimmed().isEmpty())
        return false;
    c.code = code.trimmed().toStdString();
    c.description = desc.toStdString();
    c.cableType = type.trimmed().toStdString();
    c.formation = form.trimmed().toStdString();
    c.system = static_cast<plant::System>(system);
    c.ib = ib;
    c.breakerIn = in;
    c.izRef = izRef;
    c.lengthM = length;
    c.breaker = breaker.toStdString();
    c.diameter = diameter;
    c.notes = notes.toStdString();
    return true;
}

bool PlantPanel::editRun(plant::Run& r, bool isNew)
{
    QString name = q(r.name), notes = q(r.notes), circuits = joinCodes(r.circuits);
    int kind = static_cast<int>(r.kind), septums = r.septums;
    QString tubeType = q(r.tubeType), cover = q(r.cover), support = q(r.support), series = q("TIPOLOGIE|" + canalo::seriesById(r.seriesId).id);
    double outer = r.tubeOuter, inner = r.tubeInner, width = r.width, height = r.height, span = r.spanM, ambient = r.ambientC;
    QString laying = q(r.laying);
    FormDialog d(isNew ? tr("Nuova tratta") : tr("Tratta %1").arg(q(r.id)), FormDialog::Ok | FormDialog::Cancel, this);
    d.choice(tr("Tipo"), {{tr("Tubo"), 0}, {tr("Canale"), 1}, {tr("Passerella"), 2}}, kind, [&](const QVariant& v) { kind = v.toInt(); });
    d.text(tr("Nome"), name, tr("es. dorsale piano terra"));
    d.group(tr("Tubo"));
    d.choice(tr("Tubo"), {{tr("Rigido"), QStringLiteral("rigido")}, {tr("Corrugato"), QStringLiteral("corrugato")}}, tubeType,
             [&](const QVariant& v) { tubeType = v.toString(); });
    QList<QPair<QString, QVariant>> sizes;
    for (const plant::TubeSize& s : plant::tubeSizes("rigido"))
        sizes.append({QStringLiteral("Ø%1").arg(num(s.outer, 0)), s.outer});
    d.choice(tr("Diametro esterno"), sizes, outer, [&](const QVariant& v) { outer = v.toDouble(); });
    d.number(tr("Diametro interno"), 0, 300, 1, inner, QStringLiteral(" mm"));
    d.note(tr("Interno 0 = valore indicativo della serie pesante in PVC; scrivere quello del costruttore se diverso."));
    d.choice(tr("Posa del tubo"), {{tr("A vista (su parete o soffitto)"), QStringLiteral("vista")}, {tr("Incassato in parete isolante"), QStringLiteral("incassato")}},
             laying, [&](const QVariant& v) { laying = v.toString(); });
    d.group(tr("Canale o passerella"));
    d.number(tr("Larghezza"), 20, 1200, 0, width, QStringLiteral(" mm"));
    d.number(tr("Altezza"), 10, 300, 0, height, QStringLiteral(" mm"));
    d.integer(tr("Setti separatori"), 0, 2, septums);
    d.choice(tr("Coperchio"), {{tr("Nessuno"), QStringLiteral("none")}, {tr("A scatto"), QStringLiteral("snap")}, {tr("Con fermi"), QStringLiteral("clamped")}},
             cover, [&](const QVariant& v) { cover = v.toString(); });
    QList<QPair<QString, QVariant>> seriesList;
    for (const auto& [brand, list] : canalo::brands())
        for (const canalo::Series& s : list)
            seriesList.append({q(s.name), q(brand + "|" + s.id)});
    d.choice(tr("Tipo (passerella)"), seriesList, series, [&](const QVariant& v) { series = v.toString(); });
    QList<QPair<QString, QVariant>> supports;
    for (const canalo::Support& s : canalo::supports())
        supports.append({q(s.name), q(s.key)});
    d.choice(tr("Staffaggio (passerella)"), supports, support, [&](const QVariant& v) { support = v.toString(); });
    d.number(tr("Passo staffaggio"), 1.0, 3.0, 2, span, QStringLiteral(" m"));
    d.number(tr("Temperatura ambiente"), 0, 70, 0, ambient, QStringLiteral(" °C"));
    d.group(tr("Circuiti"));
    const int codesField = d.text(tr("Sigle (separate da ;)"), circuits);
    QStringList items;
    std::vector<std::string> codes;
    for (const plant::Circuit& c : project_.circuits) {
        items << QStringLiteral("%1   %2").arg(q(c.code), q(c.description));
        codes.push_back(c.code);
    }
    d.list(items, -1, [](int) {}, [&](int i) {
        if (i < 0 || i >= static_cast<int>(codes.size()))
            return;
        circuits = circuits.trimmed();
        circuits += (circuits.isEmpty() ? QString() : QStringLiteral("; ")) + q(codes[static_cast<std::size_t>(i)]);
        d.setFieldText(codesField, circuits);
    });
    d.note(tr("Doppio clic su un circuito per aggiungerlo. Un circuito si può ripetere (due linee uguali)."));
    d.text(tr("Note"), notes);
    d.setSize(500, 720);
    if (d.exec() != QDialog::Accepted)
        return false;
    r.name = name.toStdString();
    r.kind = static_cast<plant::RunKind>(kind);
    r.tubeType = tubeType.toStdString();
    r.tubeOuter = outer;
    r.tubeInner = inner;
    r.width = width;
    r.height = height;
    r.septums = septums;
    r.cover = cover.toStdString();
    r.brand = series.section(u'|', 0, 0).toStdString();
    r.seriesId = series.section(u'|', 1, 1).toStdString();
    r.support = support.toStdString();
    r.spanM = std::round(std::clamp(span, 1.0, 3.0) * 20.0) / 20.0;
    r.circuits = splitCodes(circuits);
    r.notes = notes.toStdString();
    r.laying = laying.toStdString();
    r.ambientC = ambient;
    return true;
}

bool PlantPanel::editCable(canalo::CableType& t, bool isNew)
{
    return editCableType(t, isNew, this);
}

bool PlantPanel::editCableType(canalo::CableType& t, bool isNew, QWidget* parent)
{
    QString code = q(t.code), type = q(t.type), form = q(t.form), desc = q(t.desc);
    QString sys = q(t.sys.empty() ? std::string("BT_POWER") : t.sys);
    double d = t.d, w = t.w, sec = t.sec, iz0 = t.iz0;
    FormDialog dlg(isNew ? tr("Nuovo cavo") : tr("Cavo %1").arg(code), FormDialog::Ok | FormDialog::Cancel, parent);
    dlg.text(tr("Codice"), code, tr("es. FG18OM16-3G2,5"));
    dlg.text(tr("Tipo"), type, tr("FG18OM16"));
    dlg.text(tr("Formazione"), form, tr("1(3G2,5)"));
    dlg.text(tr("Descrizione"), desc);
    dlg.number(tr("Diametro esterno"), 1, 150, 1, d, QStringLiteral(" mm"));
    dlg.number(tr("Peso"), 0, 50, 2, w, QStringLiteral(" kg/m"));
    dlg.number(tr("Sezione"), 0, 1000, 1, sec, QStringLiteral(" mm²"));
    dlg.number(tr("Portata di riferimento Iz0"), 0, 2000, 0, iz0, QStringLiteral(" A"));
    dlg.choice(tr("Sistema"), {{tr("Energia"), QStringLiteral("BT_POWER")}, {tr("Sicurezza"), QStringLiteral("BT_FIRE")}, {tr("Segnale"), QStringLiteral("SPECIAL_ELV")}},
               sys, [&](const QVariant& v) { sys = v.toString(); });
    dlg.note(tr("Peso 0 = stimato dal diametro. I cavi propri valgono per i circuiti con lo stesso tipo e la stessa formazione."));
    dlg.setSize(440, 460);
    if (dlg.exec() != QDialog::Accepted || code.trimmed().isEmpty() || form.trimmed().isEmpty())
        return false;
    t.code = code.trimmed().toStdString();
    t.id = "CST_" + t.code;
    t.cat = "custom";
    t.type = type.trimmed().toStdString();
    t.form = form.trimmed().toStdString();
    t.desc = desc.toStdString();
    t.d = d;
    t.w = w > 0.0 ? w : canalo::fixed(std::pow(d / 10.0, 2) * 0.145, 2);
    t.sec = sec;
    const auto parts = plant::parseFormation(t.form);
    t.cores = parts.empty() ? 2 : (parts[0].cores == 1 ? 1 : parts[0].cores >= 4 ? 3 : 2);
    t.iz0 = iz0;
    t.bendK = 10.0;
    t.sys = sys.toStdString();
    t.standard = "Cavo del progetto";
    return true;
}

// ---------------------------------------------------------------- dal QML

void PlantPanel::newProjectDialog()
{
    QTimer::singleShot(0, this, [this] {
        QString start = store().value(QStringLiteral("Plant/lastFolder")).toString();
        if (drawing_ && !drawing_->filePath().isEmpty())
            start = QFileInfo(drawing_->filePath()).absolutePath() + QStringLiteral("/Impianto.cdb");
        const QString path = QFileDialog::getSaveFileName(this, tr("Nuovo progetto dell'impianto"), start, tr("Impianto (*.cdb)"));
        if (path.isEmpty())
            return;
        store().setValue(QStringLiteral("Plant/lastFolder"), QFileInfo(path).path());
        QString error;
        if (!createProject(path, &error))
            QMessageBox::warning(this, tr("Impianto"), error);
    });
}

void PlantPanel::openProjectDialog()
{
    QTimer::singleShot(0, this, [this] {
        QString start = store().value(QStringLiteral("Plant/lastFolder")).toString();
        if (drawing_ && !drawing_->filePath().isEmpty())
            start = QFileInfo(drawing_->filePath()).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, tr("Apri progetto dell'impianto"), start, tr("Impianto (*.cdb)"));
        if (path.isEmpty())
            return;
        store().setValue(QStringLiteral("Plant/lastFolder"), QFileInfo(path).path());
        QString error;
        if (!openProject(path, &error)) {
            QMessageBox::warning(this, tr("Impianto"), error);
            return;
        }
        // Il disegno corrente si collega a questo progetto.
        if (drawing_)
            drawing_->document().setPlantFile(storedPath(path_).toStdString());
    });
}

void PlantPanel::importSchemaDialog()
{
    QTimer::singleShot(0, this, [this] {
        if (!pdfSupport()) {
            QMessageBox::information(this, tr("Impianto"), tr("Questa versione di CartesioCAD è compilata senza la lettura dei PDF."));
            return;
        }
        const QString start = store().value(QStringLiteral("Circuits/lastFolder")).toString();
        const QString path = QFileDialog::getOpenFileName(this, tr("Schema unifilare"), start, tr("PDF (*.pdf)"));
        if (path.isEmpty())
            return;
        store().setValue(QStringLiteral("Circuits/lastFolder"), QFileInfo(path).path());
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = importSchema(path, &error);
        QApplication::restoreOverrideCursor();
        if (!ok)
            QMessageBox::warning(this, tr("Impianto"), error);
    });
}

void PlantPanel::setTab(int tab)
{
    if (tab == tab_)
        return;
    tab_ = tab;
    selected_ = -1;
    emit changed();
    fill();
}

void PlantPanel::selectRow(int row)
{
    selected_ = row;
    emit tableChanged();
}

void PlantPanel::editRow(int row)
{
    if (row < 0 || row >= static_cast<int>(rowIndex_.size()))
        return;
    const std::size_t i = static_cast<std::size_t>(rowIndex_[static_cast<std::size_t>(row)]);
    QTimer::singleShot(0, this, [this, i] {
        if (tab_ == 0 && i < project_.circuits.size()) {
            plant::Circuit c = project_.circuits[i];
            const std::string old = c.code;
            if (!editCircuit(c, false))
                return;
            if (c.code != old)   // sigla cambiata: anche nelle tratte
                for (plant::Run& r : project_.runs)
                    std::replace(r.circuits.begin(), r.circuits.end(), old, c.code);
            project_.circuits[i] = c;
        } else if (tab_ == 1 && i < project_.runs.size()) {
            plant::Run r = project_.runs[i];
            if (!editRun(r, false))
                return;
            project_.runs[i] = r;
        } else if (tab_ == 2 && i < project_.cables.size()) {
            canalo::CableType t = project_.cables[i];
            if (!editCable(t, false))
                return;
            project_.cables[i] = t;
        } else {
            return;
        }
        save();
        fill();
    });
}

void PlantPanel::addItem()
{
    if (path_.isEmpty())
        return;
    QTimer::singleShot(0, this, [this] {
        if (tab_ == 0) {
            plant::Circuit c;
            if (!editCircuit(c, true))
                return;
            putCircuit(c);
        } else if (tab_ == 1) {
            if (selectedPathObjects().empty()) {
                QMessageBox::information(this, tr("Impianto"),
                                         tr("Scegli nel disegno le linee o polilinee del percorso della tratta, poi riprova."));
                return;
            }
            plant::Run r;
            r.id = project_.nextRunId();
            if (!editRun(r, true))
                return;
            QString error;
            if (newRun(r, &error).isEmpty())
                QMessageBox::warning(this, tr("Impianto"), error);
        } else {
            canalo::CableType t;
            t.d = 10.0;
            t.sec = 2.5;
            t.iz0 = 20.0;
            if (!editCable(t, true))
                return;
            project_.cables.push_back(t);
            save();
            fill();
        }
    });
}

void PlantPanel::removeRow(int row)
{
    if (row < 0 || row >= static_cast<int>(rowIndex_.size()))
        return;
    const std::size_t i = static_cast<std::size_t>(rowIndex_[static_cast<std::size_t>(row)]);
    QTimer::singleShot(0, this, [this, i] {
        QString what;
        if (tab_ == 0 && i < project_.circuits.size())
            what = tr("il circuito %1").arg(q(project_.circuits[i].code));
        else if (tab_ == 1 && i < project_.runs.size())
            what = tr("la tratta %1 (le linee del disegno restano)").arg(q(project_.runs[i].id));
        else if (tab_ == 2 && i < project_.cables.size())
            what = tr("il cavo %1").arg(q(project_.cables[i].code));
        else
            return;
        if (QMessageBox::question(this, tr("Impianto"), tr("Eliminare %1 dal progetto?").arg(what)) != QMessageBox::Yes)
            return;
        if (tab_ == 0) {
            project_.circuits.erase(project_.circuits.begin() + static_cast<std::ptrdiff_t>(i));
        } else if (tab_ == 1) {
            const std::string id = project_.runs[i].id;
            project_.runs.erase(project_.runs.begin() + static_cast<std::ptrdiff_t>(i));
            tagObjects(runEntities(id), {});
        } else {
            project_.cables.erase(project_.cables.begin() + static_cast<std::ptrdiff_t>(i));
        }
        selected_ = -1;
        save();
        fill();
    });
}

void PlantPanel::addSelectionToRun(int row)
{
    if (tab_ != 1 || row < 0 || row >= static_cast<int>(rowIndex_.size()))
        return;
    const plant::Run& r = project_.runs[static_cast<std::size_t>(rowIndex_[static_cast<std::size_t>(row)])];
    const std::vector<EntityId> ids = selectedPathObjects();
    if (ids.empty()) {
        emit message(tr("Impianto: selezionare le linee o polilinee da aggiungere alla tratta %1.").arg(q(r.id)));
        return;
    }
    tagObjects(ids, r.id);
    emit message(tr("Impianto: %n oggetti aggiunti alla tratta %1.", nullptr, static_cast<int>(ids.size())).arg(q(r.id)));
    fill();
}

void PlantPanel::showRun(int row)
{
    if (tab_ != 1 || !drawing_ || row < 0 || row >= static_cast<int>(rowIndex_.size()))
        return;
    const std::vector<EntityId> ids = runEntities(project_.runs[static_cast<std::size_t>(rowIndex_[static_cast<std::size_t>(row)])].id);
    if (ids.empty()) {
        emit message(tr("Impianto: la tratta non è disegnata in questo disegno."));
        return;
    }
    drawing_->setSelection(ids);
    BBox box;
    for (EntityId id : ids)
        if (const Entity* e = drawing_->document().find(id))
            box.expand(e->bounds());
    if (box.isValid()) {
        const double margin = std::max({box.width(), box.height(), 1000.0}) * 0.3;
        box.min = box.min - Vec2{margin, margin};
        box.max = box.max + Vec2{margin, margin};
        drawing_->view()->zoomWindow(box);
    }
}

double PlantPanel::labelHeight() const
{
    // L'altezza dei testi più comune nel disegno (si adatta a unità e scala, anche con più piani nello stesso
    // disegno); senza testi, 2,5 mm sulla carta alla scala di annotazione o una frazione dell'ingombro delle tratte.
    const Document& doc = drawing_->document();
    std::map<long long, int> heights;
    for (const auto& e : doc.entities()) {
        if (e->space() != 0 || !e->plantRun().empty())
            continue;
        double th = 0.0;
        if (e->type() == EntityType::Text)
            th = static_cast<const TextEntity&>(*e).height;
        else if (e->type() == EntityType::MText)
            th = static_cast<const MTextEntity&>(*e).height;
        if (th > 0.0)
            ++heights[std::llround(th * 1000.0)];
    }
    if (!heights.empty()) {
        const auto most = std::max_element(heights.begin(), heights.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        return static_cast<double>(most->first) / 1000.0;
    }
    double h = 2.5 * doc.annotationFactor();
    BBox box;
    for (const auto& e : doc.entities())
        if (!e->plantRun().empty() && e->plantRun().find(':') == std::string::npos)
            box.expand(e->bounds());
    if (box.isValid())
        h = std::max(h, std::hypot(box.width(), box.height()) / 150.0);
    return h;
}

void PlantPanel::replaceTagged(const std::string& prefix, std::vector<std::unique_ptr<Entity>> added)
{
    Document& doc = drawing_->document();
    std::vector<EntityId> removed;
    for (const auto& e : doc.entities())
        if (e->plantRun().rfind(prefix, 0) == 0)
            removed.push_back(e->id());
    doc.commit(std::move(added), removed);
    drawing_->notifyChanged();
}

namespace {
int plantLayer(Document& doc, const char* name, int color)
{
    int index = doc.findLayer(name);
    if (index < 0) {
        Layer layer;
        layer.name = name;
        layer.color = color;
        index = doc.addLayer(layer);
    }
    return index;
}
} // namespace

int PlantPanel::writeLabels()
{
    if (!drawing_ || path_.isEmpty())
        return 0;
    Document& doc = drawing_->document();
    const double h = labelHeight();
    const int layer = plantLayer(doc, "IMP-TRATTE-ETICHETTE", 7);
    std::vector<std::unique_ptr<Entity>> labels;
    for (const plant::Run& r : project_.runs) {
        // Il tratto più lungo della tratta: l'etichetta sta a metà, sopra.
        Vec2 a, b;
        double best = -1.0;
        for (const auto& e : doc.entities()) {
            if (e->plantRun() != r.id)
                continue;
            std::vector<std::pair<Vec2, Vec2>> segs;
            if (e->type() == EntityType::Line) {
                const auto& l = static_cast<const LineEntity&>(*e);
                segs.push_back({l.start, l.end});
            } else if (e->type() == EntityType::Polyline) {
                const auto& pl = static_cast<const PolylineEntity&>(*e);
                for (std::size_t i = 0; i + 1 < pl.vertices.size(); ++i)
                    segs.push_back({pl.vertices[i].point, pl.vertices[i + 1].point});
            }
            for (const auto& [p1, p2] : segs)
                if (distance(p1, p2) > best) {
                    best = distance(p1, p2);
                    a = p1;
                    b = p2;
                }
        }
        if (best <= 0.0)
            continue;
        const plant::RunCheck rc = plant::checkRun(project_, r, runLengthM(r.id));
        const Vec2 mid{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
        auto m = std::make_unique<MTextEntity>(Vec2{mid.x, mid.y + h * 0.6}, h, plant::runLabel(r, rc));
        m->attachment = 8;   // in basso al centro
        m->setLayer(layer);
        m->setColor(rc.level == Level::Error ? 1 : rc.level == Level::Warning ? 30 : 3);
        m->setPlantRun("etichetta:" + r.id);
        labels.push_back(std::move(m));
    }
    const int count = static_cast<int>(labels.size());
    replaceTagged("etichetta:", std::move(labels));
    return count;
}

bool PlantPanel::drawSection(const QString& runId, QString* error)
{
    const plant::Run* r = project_.run(runId.toStdString());
    if (!drawing_ || !r) {
        if (error)
            *error = tr("tratta %1 non trovata").arg(runId);
        return false;
    }
    const std::vector<EntityId> ids = runEntities(r->id);
    if (ids.empty()) {
        if (error)
            *error = tr("la tratta %1 non è disegnata in questo disegno").arg(runId);
        return false;
    }
    Document& doc = drawing_->document();
    BBox box;
    for (EntityId id : ids)
        if (const Entity* e = doc.find(id))
            box.expand(e->bounds());
    // Ingrandita rispetto alla pianta quanto basta per leggerla: la larghezza della sezione circa 25 volte il testo.
    const double mm = 0.001 / std::max(1e-12, plantMetersPerUnit(doc));   // unità di disegno per millimetro
    const double h = labelHeight();
    const double real = r->kind == plant::RunKind::Tube ? r->tubeOuter : r->width;
    double factor = 1.0;
    for (double f : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0})
        if (real * mm * f <= h * 25.0)
            factor = f;
    plant::SectionStyle st;
    st.scale = mm * factor;
    st.textHeight = h;
    st.layer = plantLayer(doc, "IMP-SEZIONI", 7);
    st.tag = "sezione:" + r->id;
    st.scaleText = factor > 1.0 ? tr("ingrandita %1 volte rispetto alla pianta").arg(num(factor, 0)).toStdString() : std::string();
    const double half = r->kind == plant::RunKind::Tube ? r->tubeOuter / 2.0 * st.scale : 0.0;
    st.origin = {box.max.x + h * 4.0 + half,
                 (box.min.y + box.max.y) / 2.0 - (r->kind == plant::RunKind::Tube ? 0.0 : r->height * st.scale / 2.0)};
    replaceTagged(st.tag, plant::drawRunSection(project_, *r, st));
    return true;
}

void PlantPanel::labels()
{
    const int n = writeLabels();
    status_ = n > 0 ? tr("%n etichette scritte sul layer IMP-TRATTE-ETICHETTE (si annullano con Ctrl+Z).", nullptr, n)
                    : tr("Nessuna tratta disegnata in questo disegno.");
    emit changed();
}

void PlantPanel::section(int row)
{
    if (tab_ != 1 || row < 0 || row >= static_cast<int>(rowIndex_.size()))
        return;
    const QString id = q(project_.runs[static_cast<std::size_t>(rowIndex_[static_cast<std::size_t>(row)])].id);
    QString error;
    if (drawSection(id, &error)) {
        status_ = tr("Sezione della tratta %1 disegnata accanto al percorso, sul layer IMP-SEZIONI.").arg(id);
        showRun(row);
    } else {
        status_ = tr("Sezione: %1.").arg(error);
    }
    emit changed();
}

void PlantPanel::verify()
{
    if (path_.isEmpty())
        return;
    for (const QString& line : checkAll())
        emit message(line);
    ++verifyCount_;
    tab_ = 1;
    status_ = tr("Verifica fatta: il dettaglio di ogni tratta è anche nella riga di comando.");
    emit changed();
    fill();
}

} // namespace cartesio
