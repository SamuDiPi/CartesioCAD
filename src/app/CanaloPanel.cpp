#include "app/CanaloPanel.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/CanaloEngine.h"
#include "app/CanaloPlugin.h"
#include "app/DrawingWidget.h"
#include "app/DwgConverter.h"
#include "app/FormDialog.h"
#include "app/PlantFile.h"
#include "app/PlantPanel.h"
#include "app/QmlUi.h"
#include "app/Theme.h"

#include "core/Canalo.h"
#include "core/Document.h"
#include "core/Import.h"

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <map>
#include <cmath>
#include <set>

namespace cartesio {

namespace {

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

double dv(const QJsonObject& o, const char* key, double fallback = 0.0)
{
    return o.value(QLatin1String(key)).toDouble(fallback);
}

QString sv(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : v.isDouble() ? num(v.toDouble(), 3) : QString();
}

QString compact(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// Codice della canalina dal codice sull'oggetto ("CN1" o "CN1/sezione").
QString trayOf(const std::string& tag)
{
    return QString::fromStdString(tag).section(u'/', 0, 0);
}

const char* const kSectionRole = "/sezione";

// Layer della sezione nella tavola di CanaloCAD (il resto è cornice, tabelle, vista 3D, curva di carico).
const std::set<std::string>& sectionLayers()
{
    static const std::set<std::string> layers = {"Canalina", "Cavi", "Anime_Cavi", "Bandierine", "Testi", "QUOTE",
                                                 "STAFFAGGI", "FASCETTE_ANCORAGGIO"};
    return layers;
}

QString systemName(const QString& sys)
{
    if (sys == QLatin1String("BT_FIRE"))
        return CanaloPanel::tr("Sicurezza");
    if (sys == QLatin1String("SPECIAL_ELV"))
        return CanaloPanel::tr("Segnale/dati");
    return CanaloPanel::tr("Energia");
}

QString trayTypeName(const QString& type)
{
    if (type == QLatin1String("solid"))
        return CanaloPanel::tr("Canale cieco");
    if (type == QLatin1String("mesh"))
        return CanaloPanel::tr("Passerella a filo");
    if (type == QLatin1String("ladder"))
        return CanaloPanel::tr("Passerella a scala");
    return CanaloPanel::tr("Passerella asolata");
}

// Libreria personale dei cavi: nelle impostazioni del programma (stesso formato dei cavi propri dell'Impianto).
const QString kPersonalCablesKey = QStringLiteral("Canalo/caviPersonali");

QSettings settingsStore() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

std::vector<canalo::CableType> personalCables()
{
    std::vector<canalo::CableType> out;
    const QJsonArray list = QJsonDocument::fromJson(settingsStore().value(kPersonalCablesKey).toByteArray()).array();
    for (const QJsonValue& v : list) {
        canalo::CableType t = cableTypeFromJson(v.toObject());
        if (t.code.empty())
            continue;
        t.id = "USR_" + t.code;
        t.standard = "Cavo personale";
        out.push_back(t);
    }
    return out;
}

void savePersonalCables(const std::vector<canalo::CableType>& cables)
{
    QJsonArray list;
    for (const canalo::CableType& t : cables)
        list.append(cableTypeToJson(t));
    settingsStore().setValue(kPersonalCablesKey, QJsonDocument(list).toJson(QJsonDocument::Compact));
}

// Un cavo proprio come voce del database della pagina.
QJsonObject catalogEntry(const canalo::CableType& t)
{
    auto q = [](const std::string& s) { return QString::fromStdString(s); };
    return {{QStringLiteral("id"), q(t.id)},   {QStringLiteral("cat"), QStringLiteral("custom")}, {QStringLiteral("code"), q(t.code)},
            {QStringLiteral("desc"), q(t.desc.empty() ? t.type + " " + t.form : t.desc)},
            {QStringLiteral("type"), q(t.type)}, {QStringLiteral("form"), q(t.form)},     {QStringLiteral("d"), t.d},
            {QStringLiteral("w"), t.w},          {QStringLiteral("sec"), t.sec},          {QStringLiteral("cores"), t.cores},
            {QStringLiteral("iz0"), t.iz0},      {QStringLiteral("bendK"), t.bendK},      {QStringLiteral("sys"), q(t.sys)},
            {QStringLiteral("std"), q(t.standard)}};
}

QString tempPath(const QString& name)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
}

// Ingombro del profilo della canalina disegnata (unità del disegno): i tratti orizzontali e verticali di linee e
// polilinee (le bandierine sono oblique); vuoto se non ce ne sono.
BBox outlineBox(const Document& doc, const std::vector<EntityId>& ids)
{
    BBox box;
    auto segment = [&](Vec2 a, Vec2 b) {
        const double dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
        if (dx < 1e-6 * std::max(1.0, dy) || dy < 1e-6 * std::max(1.0, dx)) {
            box.expand(a);
            box.expand(b);
        }
    };
    for (EntityId id : ids) {
        const Entity* e = doc.find(id);
        if (const auto* l = dynamic_cast<const LineEntity*>(e))
            segment(l->start, l->end);
        else if (const auto* pl = dynamic_cast<const PolylineEntity*>(e)) {
            for (std::size_t i = 0; i + 1 < pl->vertices.size(); ++i)
                segment(pl->vertices[i].point, pl->vertices[i + 1].point);
            if (pl->closed && pl->vertices.size() > 2)
                segment(pl->vertices.back().point, pl->vertices.front().point);
        }
    }
    return box;
}

} // namespace

CanaloPanel::CanaloPanel(QWidget* parent) : QQuickWidget(parent)
{
    // Finestra a sé, sopra la finestra principale, spostabile anche su un altro schermo.
    setWindowFlags(Qt::Window);
    setWindowTitle(tr("Canalina - CanaloCAD"));
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    rootContext()->setContextProperty(QStringLiteral("canalo"), this);
    setSource(QUrl(QStringLiteral("qrc:/qml/CanaloPanel.qml")));
    setMinimumSize(780, 480);
    resize(1180, 740);
}

void CanaloPanel::showWindow()
{
    if (!isVisible()) {
        const QByteArray geometry = settingsStore().value(QStringLiteral("Canalo/finestra")).toByteArray();
        if (!geometry.isEmpty())
            restoreGeometry(geometry);
    }
    show();
    raise();
    activateWindow();
    refresh();
}

void CanaloPanel::closeEvent(QCloseEvent* event)
{
    settingsStore().setValue(QStringLiteral("Canalo/finestra"), saveGeometry());
    QQuickWidget::closeEvent(event);
}

void CanaloPanel::setDrawing(DrawingWidget* drawing)
{
    drawing_ = drawing;
    trayId_.clear();
    refresh();
}

void CanaloPanel::refresh()
{
    CanaloEngine& engine = CanaloEngine::instance();
    if (!engine.ok()) {
        status_ = engine.error();
        emit changed();
        return;
    }
    // Importazione in attesa: la polilinea appena selezionata nel disegno.
    if (!pendingText_.isEmpty())
        linkPending();
    if (db_.isEmpty())
        syncOwnCables();
    // La canalina degli oggetti selezionati; senza, resta l'ultima (se c'è ancora).
    bool selected = false;
    if (drawing_) {
        const Document& doc = drawing_->document();
        for (EntityId id : drawing_->selection())
            if (const Entity* e = doc.find(id); e && !e->canaloTray().empty() && doc.canaloTray(trayOf(e->canaloTray()).toStdString())) {
                trayId_ = trayOf(e->canaloTray());
                selected = true;
                break;
            }
        if (!selected && !trayId_.isEmpty() && !doc.canaloTray(trayId_.toStdString()))
            trayId_.clear();
    } else {
        trayId_.clear();
    }
    updateSummary();
    if (!drawing_)
        status_ = tr("Nessun disegno aperto.");
    else if (trayId_.isEmpty())
        status_ = tr("Disegna la canalina (sezione L×H o percorso in pianta), selezionala e premi Usa selezione; oppure scegli "
                     "subito i cavi dal database: la canalina nasce con la misura adatta.");
    else if (!selected)
        status_ = tr("Ultima canalina usata (nel disegno non è selezionata).");
    else
        status_.clear();
    // Si chiama a ogni passo dei comandi del disegno: la finestra si ridisegna solo se cambia quello che mostra.
    const QString shown = trayId_ + u'|' + status_ + u'|' + metricsFor_ + u'|' + pendingText_ + u'|' + QString::number(cableSelected_);
    if (shown == shown_)
        return;
    shown_ = shown;
    emit changed();
}

// ---------------------------------------------------------------- stato

QJsonObject CanaloPanel::state() const
{
    if (drawing_ && !trayId_.isEmpty())
        if (const std::string* json = drawing_->document().canaloTray(trayId_.toStdString()))
            return QJsonDocument::fromJson(QByteArray::fromStdString(*json)).object();
    return CanaloEngine::instance().defaultState();
}

bool CanaloPanel::autoSize() const
{
    return hasTray() && state().value(QStringLiteral("cartesioAutoSize")).toBool(false);
}

double CanaloPanel::reserve() const
{
    return dv(state(), "cartesioReserve", 30.0);
}

bool CanaloPanel::sectionPlaced() const
{
    return hasTray() && state().contains(QStringLiteral("cartesioSection"));
}

void CanaloPanel::store(QJsonObject st, const QString& what)
{
    if (!drawing_ || trayId_.isEmpty())
        return;
    CanaloEngine& engine = CanaloEngine::instance();
    // Misura scelta dai cavi (come Dimensiona di CanaloCAD) se la canalina non ha la misura del disegno. Un solo
    // calcolo: i risultati servono anche al riepilogo.
    QJsonObject metrics;
    if (st.value(QStringLiteral("cartesioAutoSize")).toBool(false) && !st.value(QStringLiteral("cables")).toArray().isEmpty())
        st = engine.autoSize(st, dv(st, "cartesioReserve", 30.0), &metrics);
    else
        st = engine.calculate(st, &metrics);
    if (st.isEmpty()) {
        emit message(tr("CanaloCAD: %1").arg(engine.error()));
        return;
    }
    Document& doc = drawing_->document();
    // La sezione già nel disegno si aggiorna nello stesso passo (un solo Annulla).
    if (st.contains(QStringLiteral("cartesioSection")))
        commitSection(st);
    doc.setCanaloTray(trayId_.toStdString(), compact(st).toStdString());
    doc.checkpoint();
    setMetrics(st, metrics);
    drawing_->notifyChanged();
    if (!what.isEmpty())
        emit message(what);
    emit changed();
}

void CanaloPanel::setMetrics(const QJsonObject& st, QJsonObject metrics)
{
    // Canalina con la misura del disegno: la larghezza che servirebbe per questi cavi, se è più larga.
    metrics.insert(QStringLiteral("cartesioMinWidth"), st.value(QStringLiteral("cartesioAutoSize")).toBool(false) ? 0 : minimumWidth(st, metrics));
    metrics_ = metrics;
    metricsFor_ = trayId_ + u'|' + compact(st);
}

QString CanaloPanel::createTray(QJsonObject st)
{
    if (!drawing_)
        return {};
    Document& doc = drawing_->document();
    const QString id = QString::fromStdString(doc.nextCanaloTrayId());
    if (st.isEmpty()) {
        st = CanaloEngine::instance().defaultState();
        st.insert(QStringLiteral("cartesioAutoSize"), true);   // dal database: la misura la danno i cavi
    }
    if (!st.contains(QStringLiteral("cartesioReserve")))
        st.insert(QStringLiteral("cartesioReserve"), 30.0);
    st.insert(QStringLiteral("title"), tr("CANALINA %1 - L=%2mm H=%3mm").arg(id, num(dv(st, "width", 200), 0), num(dv(st, "height", 75), 0)));
    trayId_ = id;
    store(st, tr("Canalina %1 creata.").arg(id));
    return id;
}

void CanaloPanel::tag(const std::vector<EntityId>& ids, const QString& role)
{
    if (!drawing_ || ids.empty())
        return;
    Document& doc = drawing_->document();
    std::vector<std::unique_ptr<Entity>> tagged;
    std::vector<EntityId> replaced;
    for (EntityId eid : ids)
        if (const Entity* e = doc.find(eid)) {
            auto copy = e->clone();
            copy->setCanaloTray((trayId_ + role).toStdString());
            tagged.push_back(std::move(copy));
            replaced.push_back(eid);
        }
    doc.commit(std::move(tagged), replaced);
    drawing_->notifyChanged();
}

void CanaloPanel::syncOwnCables()
{
    CanaloEngine& engine = CanaloEngine::instance();
    engine.setOwnCables(ownCablesJson());
    db_ = engine.database().value(QStringLiteral("cables")).toArray();
    dbFiltered_.clear();
    const QString needle = search_.trimmed().toLower();
    for (int i = 0; i < db_.size(); ++i) {
        const QJsonObject c = db_[i].toObject();
        if (!category_.isEmpty() && c.value(QStringLiteral("cat")).toString() != category_)
            continue;
        if (!needle.isEmpty()) {
            const QString hay = (sv(c, "code") + u' ' + sv(c, "type") + u' ' + sv(c, "form") + u' ' + sv(c, "desc")).toLower();
            if (!hay.contains(needle))
                continue;
        }
        dbFiltered_.push_back(i);
    }
    if (dbSelected_ >= static_cast<int>(dbFiltered_.size()))
        dbSelected_ = -1;
    emit dbChanged();
}

QJsonArray CanaloPanel::ownCablesJson() const
{
    QJsonArray out;
    for (const canalo::CableType& t : personalCables())
        out.append(catalogEntry(t));
    if (plant_ && plant_->hasProject())
        for (const canalo::CableType& t : plant_->project().cables) {
            QJsonObject o = catalogEntry(t);
            o.insert(QStringLiteral("id"), QStringLiteral("CST_") + QString::fromStdString(t.code));
            out.append(o);
        }
    return out;
}

QJsonObject CanaloPanel::dbItem(int row) const
{
    if (row < 0 || row >= static_cast<int>(dbFiltered_.size()))
        return {};
    return db_[dbFiltered_[static_cast<std::size_t>(row)]].toObject();
}

std::vector<EntityId> CanaloPanel::sectionEntities() const
{
    std::vector<EntityId> out;
    if (!drawing_ || trayId_.isEmpty())
        return out;
    const std::string tag = (trayId_ + QLatin1String(kSectionRole)).toStdString();
    for (const auto& e : drawing_->document().entities())
        if (e->canaloTray() == tag)
            out.push_back(e->id());
    return out;
}

double CanaloPanel::drawingUnit() const
{
    if (!drawing_)
        return 1.0;
    const Document& doc = drawing_->document();
    return 0.001 / std::max(1e-12, plantMetersPerUnit(doc));   // unità di disegno per mm (unità reali dell'impianto)
}

int CanaloPanel::minimumWidth(const QJsonObject& st, const QJsonObject& metrics) const
{
    const QJsonArray cables = st.value(QStringLiteral("cables")).toArray();
    if (cables.isEmpty())
        return 0;
    // Prima il controllo veloce: riserva rispettata e cavi dentro l'altezza (posizioni già calcolate) = ci stanno.
    bool overflow = dv(metrics, "fillPct") > 100.0 - dv(st, "cartesioReserve", 30.0);
    for (const QJsonValue& v : cables) {
        const QJsonObject c = v.toObject();
        overflow = overflow || dv(c, "y") + dv(c, "d") / 2.0 > dv(st, "height") + 0.01;
    }
    if (!overflow)
        return 0;
    const QJsonObject sized = CanaloEngine::instance().autoSize(st, dv(st, "cartesioReserve", 30.0));
    const double need = dv(sized, "width");
    return need > dv(st, "width") + 0.5 ? static_cast<int>(need) : 0;
}

std::vector<EntityId> CanaloPanel::commitSection(const QJsonObject& st)
{
    std::vector<EntityId> ids;
    if (!drawing_)
        return ids;
    // La sezione della tavola di CanaloCAD al vero (profilo, cavi, bandierine, quote, staffaggio): le entità che scrive
    // l'export DXF della pagina, prese direttamente dal motore (niente file DXF da scrivere e rileggere).
    QStringList wanted;
    for (const std::string& l : sectionLayers())
        wanted << QString::fromStdString(l);
    const QJsonObject scene = CanaloEngine::instance().sectionEntities(st, wanted);
    const QJsonArray items = scene.value(QStringLiteral("entities")).toArray();
    if (items.isEmpty()) {
        emit message(tr("CANALINA: la tavola di CanaloCAD non ha la sezione (%1).").arg(CanaloEngine::instance().error()));
        return ids;
    }
    Document& doc = drawing_->document();
    // Layer e stili di testo della tavola di CanaloCAD (colori e spessori della pagina).
    const QJsonObject specs = scene.value(QStringLiteral("layers")).toObject();
    std::map<QString, int> layerIndex;
    auto layerOf = [&](const QString& name) {
        auto it = layerIndex.find(name);
        if (it != layerIndex.end())
            return it->second;
        int index = doc.findLayer(name.toStdString());
        if (index < 0) {
            const QJsonObject spec = specs.value(name).toObject();
            Layer l;
            l.name = name.toStdString();
            l.color = spec.value(QStringLiteral("aci")).toInt(7);
            if (spec.contains(QStringLiteral("tc")))
                l.trueColor = spec.value(QStringLiteral("tc")).toInt(-1);
            if (spec.contains(QStringLiteral("lw")))
                l.lineweight = spec.value(QStringLiteral("lw")).toInt();
            index = doc.addLayer(l);
        }
        layerIndex[name] = index;
        return index;
    };
    for (const char* style : {"ARIAL", "TITLE_CAD"})
        if (!doc.findTextStyle(style)) {
            TextStyle s;
            s.name = style;
            s.font = "arial.ttf";
            doc.setTextStyle(s);
        }
    const QJsonObject where = st.value(QStringLiteral("cartesioSection")).toObject();
    const Vec2 origin{dv(where, "x"), dv(where, "y")};
    const double unit = drawingUnit();
    auto P = [&](double x, double y) { return Vec2{origin.x + x * unit, origin.y + y * unit}; };
    const std::string tagName = (trayId_ + QLatin1String(kSectionRole)).toStdString();
    std::vector<std::unique_ptr<Entity>> added;
    std::vector<const Entity*> raw;
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("t")).toString();
        const QJsonArray p = o.value(QStringLiteral("p")).toArray();
        std::unique_ptr<Entity> e;
        if (type == QLatin1String("line")) {
            e = std::make_unique<LineEntity>(P(p[0].toDouble(), p[1].toDouble()), P(p[2].toDouble(), p[3].toDouble()));
        } else if (type == QLatin1String("circle")) {
            e = std::make_unique<CircleEntity>(P(p[0].toDouble(), p[1].toDouble()), p[2].toDouble() * unit);
        } else if (type == QLatin1String("text")) {
            auto t = std::make_unique<TextEntity>(P(p[0].toDouble(), p[1].toDouble()), p[2].toDouble() * unit,
                                                  o.value(QStringLiteral("s")).toString().toStdString());
            t->style = o.value(QStringLiteral("st")).toString().toStdString();
            const int align = o.value(QStringLiteral("a")).toInt();   // 72 del DXF: 1 centro, 2 destra
            t->hAlign = align == 1 ? TextEntity::Center : align == 2 ? TextEntity::Right : TextEntity::Left;
            t->alignPoint = t->position;
            e = std::move(t);
        } else if (type == QLatin1String("hatch")) {
            auto h = std::make_unique<HatchEntity>();
            std::vector<PolyVertex> loop;
            for (const QJsonValue& vv : o.value(QStringLiteral("v")).toArray()) {
                const QJsonArray a = vv.toArray();
                PolyVertex pv;
                pv.point = P(a[0].toDouble(), a[1].toDouble());
                pv.bulge = a[2].toDouble();
                loop.push_back(pv);
            }
            h->loops.push_back(std::move(loop));
            h->solid = true;
            h->pattern = "SOLID";
            h->hatchStyle = 1;
            e = std::move(h);
        }
        if (!e)
            continue;
        e->setLayer(layerOf(o.value(QStringLiteral("l")).toString()));
        e->setColor(o.value(QStringLiteral("c")).toInt(256));
        if (o.value(QStringLiteral("tc")).isDouble())
            e->setTrueColor(o.value(QStringLiteral("tc")).toInt());
        e->setCanaloTray(tagName);
        raw.push_back(e.get());
        added.push_back(std::move(e));
    }
    doc.commit(std::move(added), sectionEntities());
    for (const Entity* e : raw)
        ids.push_back(e->id());
    return ids;
}

void CanaloPanel::updateSummary()
{
    if (trayId_.isEmpty()) {
        metrics_ = {};
        metricsFor_.clear();
        return;
    }
    const QJsonObject st = state();
    const QString key = trayId_ + u'|' + compact(st);
    if (key == metricsFor_)
        return;
    // Uno stato nuovo (aperto dal file, Annulla): calcolato una volta, come quello salvato.
    QJsonObject metrics;
    CanaloEngine::instance().calculate(st, &metrics);
    setMetrics(st, metrics);
}

// ---------------------------------------------------------------- per il QML

QString CanaloPanel::trayTitle() const
{
    if (trayId_.isEmpty())
        return {};
    const QJsonObject st = state();
    return QStringLiteral("%1 — %2 %3 × %4 mm — %5")
        .arg(trayId_, trayTypeName(sv(st, "trayType")), num(dv(st, "width"), 0), num(dv(st, "height"), 0))
        .arg(tr("%n cavi", nullptr, static_cast<int>(st.value(QStringLiteral("cables")).toArray().size())));
}

QString CanaloPanel::quickSummary() const
{
    if (trayId_.isEmpty() || metrics_.isEmpty())
        return {};
    const QJsonObject st = state();
    QStringList parts{tr("%1 × %2 mm").arg(num(dv(st, "width"), 0), num(dv(st, "height"), 0)),
                      tr("riempimento %1%").arg(num(dv(metrics_, "fillPct"))), tr("Kr %1").arg(num(dv(metrics_, "kr"), 2)),
                      metrics_.value(QStringLiteral("segConform")).toBool(true) ? tr("segregazione conforme") : tr("segregazione NON CONFORME")};
    if (const int need = metrics_.value(QStringLiteral("cartesioMinWidth")).toInt(); need > 0)
        parts << tr("piccola: serve L ≥ %1 mm").arg(need);
    return parts.join(QStringLiteral("  ·  "));
}

int CanaloPanel::quickLevel() const
{
    for (const QVariant& v : summary())
        if (v.toMap().value(QStringLiteral("level")).toInt() == 2)
            return 2;
    return 0;
}

QVariantList CanaloPanel::categories() const
{
    QVariantList out;
    const QList<QPair<QString, QString>> list = {
        {QStringLiteral("bt_multi"), tr("BT multipolari")}, {QStringLiteral("bt_uni"), tr("BT unipolari")},
        {QStringLiteral("fire"), tr("Fuoco/sicurezza")},    {QStringLiteral("signal"), tr("Segnale e bus")},
        {QStringLiteral("data"), tr("Dati e fibra")},       {QStringLiteral("custom"), tr("Cavi propri")},
        {QString(), tr("Tutti")}};
    for (const auto& [key, label] : list)
        out.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label}});
    return out;
}

QVariantList CanaloPanel::dbRows() const
{
    QVariantList out;
    for (int i = 0; i < static_cast<int>(dbFiltered_.size()); ++i) {
        const QJsonObject c = db_[dbFiltered_[static_cast<std::size_t>(i)]].toObject();
        const QString sys = sv(c, "sys");
        out.append(QVariantMap{{QStringLiteral("cells"), QStringList{sv(c, "type"), sv(c, "form"), num(dv(c, "d")), num(dv(c, "w"), 2),
                                                                     systemName(sys), sv(c, "desc")}},
                               {QStringLiteral("level"), sys == QLatin1String("BT_FIRE") ? 2 : sys == QLatin1String("SPECIAL_ELV") ? 3 : 0}});
    }
    return out;
}

bool CanaloPanel::ownSelected() const
{
    return dbItem(dbSelected_).value(QStringLiteral("id")).toString().startsWith(QStringLiteral("USR_"));
}

QVariantList CanaloPanel::cableRows() const
{
    QVariantList out;
    if (trayId_.isEmpty())
        return out;
    const QJsonArray cables = state().value(QStringLiteral("cables")).toArray();
    for (int i = 0; i < cables.size(); ++i) {
        const QJsonObject c = cables[i].toObject();
        const QString sys = sv(c, "sys");
        out.append(QVariantMap{{QStringLiteral("cells"), QStringList{QStringLiteral("#%1").arg(i + 1), sv(c, "code"), sv(c, "type"), sv(c, "form"),
                                                                     num(dv(c, "d")), systemName(sys), sv(c, "desc")}},
                               {QStringLiteral("level"), sys == QLatin1String("BT_FIRE") ? 2 : sys == QLatin1String("SPECIAL_ELV") ? 3 : 0}});
    }
    return out;
}

QVariantList CanaloPanel::summary() const
{
    if (trayId_.isEmpty() || metrics_.isEmpty())
        return {};
    return summaryOf(state(), metrics_);
}

QVariantList CanaloPanel::summaryOf(const QJsonObject& st, const QJsonObject& m) const
{
    QVariantList out;
    auto row = [&](const QString& label, const QString& value, int level = 0) {
        out.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}, {QStringLiteral("level"), level}});
    };
    row(tr("Canalina"), tr("%1 %2 × %3 mm, sp. %4 mm%5").arg(trayTypeName(sv(st, "trayType")), num(dv(st, "width"), 0), num(dv(st, "height"), 0),
                                                             num(dv(st, "thk")),
                                                             st.value(QStringLiteral("cartesioAutoSize")).toBool(false)
                                                                 ? tr(" (misura dai cavi, riserva %1%)").arg(num(dv(st, "cartesioReserve", 30), 0))
                                                                 : QString()));
    if (const int need = m.value(QStringLiteral("cartesioMinWidth")).toInt(); need > 0)
        row(tr("Misura"), tr("Piccola per questi cavi: con la riserva del %1% serve una larghezza di almeno %2 mm")
                              .arg(num(dv(st, "cartesioReserve", 30), 0))
                              .arg(need),
            2);
    row(tr("Riempimento"), tr("%1% (riserva +%2%)").arg(num(dv(m, "fillPct")), num(dv(m, "sparePct"))));
    row(tr("Riduzione portata"), tr("Kr = %1 (fascio %2 × strati %3 [%4] × posa %5)")
                                     .arg(num(dv(m, "kr"), 2), num(dv(m, "kFascio"), 2), num(dv(m, "kStrati"), 2))
                                     .arg(dv(m, "nLayers", 1))
                                     .arg(num(dv(m, "kPosa"), 2)));
    const bool seg = m.value(QStringLiteral("segConform")).toBool(true);
    row(tr("Segregazione 528.1"), (seg ? tr("CONFORME") : tr("NON CONFORME")) + QStringLiteral(" — ") + sv(m, "segDetail"), seg ? 1 : 2);
    row(tr("Peso"), tr("%1 kg/m").arg(num(dv(m, "totalKgM"), 2)));
    if (sv(st, "support") == QLatin1String("none") || sv(st, "support").isEmpty()) {
        row(tr("Staffaggio"), tr("Nessun supporto: nessuna verifica di carico (Dati canalina)"));
    } else {
        const double load = dv(m, "loadPct");
        row(tr("Staffaggio"), tr("%1 kg / %2 kg (%3%) %4 — passo %5 m").arg(num(dv(m, "pointLoad")), num(dv(m, "maxPoint"), 0), num(load),
                                                                             load <= 100 ? tr("OK") : tr("SOVRACCARICO"), num(dv(st, "spanM"), 2)),
            load <= 100 ? 1 : 2);
        row(tr("Freccia"), tr("%1 mm (limite %2 mm)").arg(num(dv(m, "deflectionMm"), 2), num(dv(m, "maxDeflectionMm"))));
    }
    const bool pullOk = dv(m, "pullDaN") <= dv(m, "maxPullDaN");
    row(tr("Tiro di posa"), tr("%1 daN (max %2) %3").arg(num(dv(m, "pullDaN")), num(dv(m, "maxPullDaN"), 0), pullOk ? tr("OK") : tr("ECCESSIVO")),
        pullOk ? 1 : 2);
    row(tr("Raggio di curvatura"), tr("min %1 mm (%2) — curve R%3").arg(num(dv(m, "maxBendR"), 0), sv(m, "critCode"), num(dv(m, "fitR"), 0)));
    if (!sv(m, "deratingFailCode").isEmpty())
        row(tr("Portata"), tr("Iz' < Ib su %1").arg(sv(m, "deratingFailCode")), 2);
    for (const QJsonValue& b : m.value(QStringLiteral("bom")).toArray()) {
        const QJsonObject r = b.toObject();
        row(sv(r, "code"), QStringLiteral("%1 — %2 %3").arg(sv(r, "desc"), num(dv(r, "qty"), 0), sv(r, "um")), -1);
    }
    return out;
}

QStringList CanaloPanel::reportLines() const
{
    QStringList out;
    if (trayId_.isEmpty())
        return out;
    out << tr("CanaloCAD — %1").arg(trayTitle());
    bool bom = false;
    for (const QVariant& v : summary()) {
        const QVariantMap r = v.toMap();
        if (r.value(QStringLiteral("level")).toInt() < 0 && !bom) {
            out << tr("Distinta materiali (codici generici):");
            bom = true;
        }
        out << (bom ? QStringLiteral("  %1  %2") : QStringLiteral("%1: %2"))
                   .arg(r.value(QStringLiteral("label")).toString(), r.value(QStringLiteral("value")).toString());
    }
    out << tr("Calcoli di CanaloCAD: valori indicativi da verificare con le norme e con i dati dei costruttori.");
    return out;
}

// ---------------------------------------------------------------- selezione e canaline

void CanaloPanel::useSelection()
{
    if (!drawing_ || drawing_->selection().empty()) {
        emit message(tr("CANALINA: seleziona nel disegno la canalina (la sezione L×H, anche con i cavi, o il percorso in pianta)."));
        return;
    }
    const Document& doc = drawing_->document();
    for (EntityId id : drawing_->selection())
        if (const Entity* e = doc.find(id); e && !e->canaloTray().empty() && doc.canaloTray(trayOf(e->canaloTray()).toStdString())) {
            trayId_ = trayOf(e->canaloTray());
            refresh();
            emit message(tr("Canalina %1.").arg(trayId_));
            return;
        }
    // Una sezione (cerchi dei cavi, o un profilo di misure da sezione) o un percorso in pianta.
    const canalo::Drawn drawn = drawnForCanalo(doc, drawing_->selection());
    BBox box;
    for (const auto& s : drawn.segments) {
        box.expand(s.a);
        box.expand(s.b);
    }
    const bool section = !drawn.circles.empty() || (box.isValid() && box.max.x - box.min.x <= 1250 && box.max.y - box.min.y <= 450);
    if (section) {
        trayId_.clear();
        importSelection();
        return;
    }
    if (trayId_.isEmpty())
        createTray({});
    const std::vector<EntityId> route = drawing_->selection();
    tag(route, QString());
    // La lunghezza del tratto è quella del percorso disegnato.
    QJsonObject st = state();
    st.insert(QStringLiteral("lengthM"), std::max(1.0, std::round(routeLengthM(route) * 10.0) / 10.0));
    store(st, tr("Percorso collegato alla canalina %1 (tratto di %2 m): selezionandolo si vede il suo riepilogo.")
                  .arg(trayId_, num(dv(st, "lengthM"))));
    refresh();
}

void CanaloPanel::newTray()
{
    trayId_.clear();
    createTray({});
}

bool CanaloPanel::importSelection()
{
    if (!drawing_ || drawing_->selection().empty()) {
        emit message(tr("CANALINA: selezionare la sezione disegnata (profilo della canalina, cerchi dei cavi, sigle)."));
        return false;
    }
    const std::vector<EntityId> selection = drawing_->selection();
    const QString path = tempPath(QStringLiteral("CartesioCAD_sezione.dxf"));
    QString error;
    if (!exportForCanalo(drawing_->document(), selection, path, &error)) {
        emit message(tr("CANALINA: %1").arg(error));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    QFile::remove(path);
    // Cavi e sigle con l'importazione di CanaloCAD.
    QJsonObject st = CanaloEngine::instance().importDxf(CanaloEngine::instance().defaultState(), text,
                                                        QFileInfo(drawing_->displayName()).fileName());
    if (st.isEmpty()) {
        emit message(tr("CANALINA: %1").arg(CanaloEngine::instance().error()));
        return false;
    }
    // Le misure sono quelle disegnate (CanaloCAD le arrotonda alle misure di catalogo): il profilo orizzontale e
    // verticale, al millimetro; la sezione di CanaloCAD andrà al suo posto.
    const BBox outline = outlineBox(drawing_->document(), selection);
    const double unit = drawingUnit();
    if (outline.isValid()) {
        const double w = std::round((outline.max.x - outline.min.x) / unit), h = std::round((outline.max.y - outline.min.y) / unit);
        if (w >= 30 && w <= 1250 && h >= 20 && h <= 450) {
            st.insert(QStringLiteral("width"), w);
            st.insert(QStringLiteral("height"), h);
        }
        st.insert(QStringLiteral("cartesioSection"), QJsonObject{{QStringLiteral("x"), outline.min.x}, {QStringLiteral("y"), outline.min.y}});
    }
    st.insert(QStringLiteral("cartesioAutoSize"), false);   // la misura è quella disegnata
    // Gli oggetti disegnati diventano la sezione della canalina: CanaloCAD la ridisegna al loro posto.
    trayId_ = QString::fromStdString(drawing_->document().nextCanaloTrayId());
    tag(selection, QLatin1String(kSectionRole));
    trayId_.clear();
    const QString id = QString::fromStdString(drawing_->document().nextCanaloTrayId());
    createTray(st);
    emit message(tr("%1: canalina %2 × %3 mm dal disegno, %n cavi letti; la sezione di CanaloCAD è al suo posto (Ctrl+Z torna al disegno).",
                    nullptr, static_cast<int>(st.value(QStringLiteral("cables")).toArray().size()))
                     .arg(id, num(dv(st, "width"), 0), num(dv(st, "height"), 0)));
    return true;
}

void CanaloPanel::importFile(const QString& given)
{
    if (!drawing_)
        return;
    QStringList paths;
    if (!given.isEmpty())
        paths << given;
    else
        paths = QFileDialog::getOpenFileNames(this, tr("Importa DWG/DXF di progetto (una sezione per file)"), QString(), tr("Disegni (*.dwg *.dxf)"));
    if (paths.isEmpty())
        return;
    CanaloEngine& engine = CanaloEngine::instance();
    int letter = static_cast<int>(drawing_->document().canaloTrays().size());
    for (const QString& path : paths) {
        // Il file com'è, come Importa DWG/DXF di CanaloCAD: un DXF passa intero; un DWG si converte con LibreDWG (la
        // libreria che usa anche CanaloCAD) e il DXF ottenuto passa intero.
        QByteArray bytes;
        QString error;
        if (path.endsWith(QLatin1String(".dwg"), Qt::CaseInsensitive)) {
            const QString dxf = tempPath(QStringLiteral("CartesioCAD_import.dxf"));
            if (!DwgConverter::dwgToDxf(path, dxf, &error)) {
                emit message(tr("CANALINA: %1: %2").arg(QFileInfo(path).fileName(), error));
                continue;
            }
            QFile file(dxf);
            if (file.open(QIODevice::ReadOnly))
                bytes = file.readAll();
            file.close();
            QFile::remove(dxf);
        } else {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly))
                bytes = file.readAll();
        }
        if (bytes.isEmpty()) {
            emit message(tr("CANALINA: %1 non si legge.").arg(QFileInfo(path).fileName()));
            continue;
        }
        // Come una scheda nuova della pagina: titolo dal nome del file e sigla della sezione A-A, B-B...
        QJsonObject base = engine.defaultState();
        base.insert(QStringLiteral("title"), QFileInfo(path).completeBaseName());
        const QChar l(static_cast<char16_t>(u'A' + std::min(25, letter++)));
        base.insert(QStringLiteral("planTag"), QStringLiteral("SEZ. %1-%1 | H=+3.20m").arg(l));
        QJsonObject st = engine.importFileBytes(base, bytes, QFileInfo(path).fileName());
        if (st.isEmpty()) {
            emit message(tr("CANALINA: %1: %2").arg(QFileInfo(path).fileName(), engine.error()));
            continue;
        }
        st.insert(QStringLiteral("cartesioAutoSize"), false);   // le misure sono quelle del disegno importato
        imports_.push_back({st, QFileInfo(path).fileName()});
    }
    placeNextImport();
}

void CanaloPanel::placeNextImport()
{
    if (!drawing_ || !pendingText_.isEmpty() || imports_.empty())
        return;
    const Import item = imports_.front();
    imports_.erase(imports_.begin());
    const QJsonObject& st = item.state;
    const int cables = static_cast<int>(st.value(QStringLiteral("cables")).toArray().size());

    // A cosa si riferisce il tratto importato.
    QList<QPair<QString, QVariant>> choices;
    choices.append({tr("A una polilinea del disegno: la clicco (la lunghezza del tratto è la sua)"), QStringLiteral("pick")});
    for (const auto& [id, json] : drawing_->document().canaloTrays()) {
        const QJsonObject other = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
        choices.append({tr("Alla canalina %1 — %2 (ne prende misure e cavi)").arg(QString::fromStdString(id), sv(other, "title")),
                        QStringLiteral("tray:") + QString::fromStdString(id)});
    }
    choices.append({tr("Particolare a sé stante: la sezione nel disegno"), QStringLiteral("detail")});
    choices.append({tr("Solo i calcoli, niente nel disegno"), QStringLiteral("none")});
    QVariant choice = QStringLiteral("pick");
    FormDialog dialog(tr("Importa %1").arg(item.file), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.note(tr("<b>%1</b>: canalina %2 × %3 mm, %n cavi letti con l'importazione di CanaloCAD.<br>A cosa si riferisce questo tratto?",
                   nullptr, cables)
                    .arg(item.file.toHtmlEscaped(), num(dv(st, "width"), 0), num(dv(st, "height"), 0)));
    dialog.choice(tr("Si riferisce a"), choices, choice, [&](const QVariant& v) { choice = v; });
    if (!imports_.empty())
        dialog.note(tr("Poi restano %n file da collocare.", nullptr, static_cast<int>(imports_.size())));
    dialog.setSize(560, 260);
    if (!scriptPlacement_.isEmpty()) {
        // Negli script: la scelta è già data; "pick" usa la polilinea già selezionata.
        choice = scriptPlacement_.startsWith(QLatin1String("CN")) ? QStringLiteral("tray:") + scriptPlacement_ : scriptPlacement_;
    } else if (dialog.exec() != QDialog::Accepted) {
        emit message(tr("%1 non importato.").arg(item.file));
        placeNextImport();
        return;
    }
    const QString what = choice.toString();
    if (what == QLatin1String("pick") && !scriptPlacement_.isEmpty()) {
        pending_ = item;
        pendingText_ = tr("Polilinea per %1").arg(item.file);
        if (!linkPending()) {
            emit message(tr("-CANALOIMPORTA: per POLILINEA selezionare prima la polilinea."));
            pending_ = {};
            pendingText_.clear();
        }
        return;
    }
    if (what == QLatin1String("pick")) {
        pending_ = item;
        pendingText_ = tr("Clicca nel disegno la polilinea (o le linee) della canalina a cui si riferisce %1.").arg(item.file);
        emit message(pendingText_);
        emit changed();
        drawing_->setSelection({});
        drawing_->window()->activateWindow();   // si clicca nel disegno
        return;
    }
    if (what.startsWith(QLatin1String("tray:"))) {
        applyImportTo(what.mid(5), st);
    } else if (what == QLatin1String("detail")) {
        trayId_.clear();
        createTray(st);
        drawSection();
    } else {
        trayId_.clear();
        createTray(st);
    }
    placeNextImport();
}

bool CanaloPanel::linkPending()
{
    if (pendingText_.isEmpty() || !drawing_)
        return false;
    // Le linee e polilinee selezionate (il percorso della canalina).
    const Document& doc = drawing_->document();
    std::vector<EntityId> route;
    QString existing;
    for (EntityId id : drawing_->selection())
        if (const Entity* e = doc.find(id); e && (dynamic_cast<const LineEntity*>(e) || dynamic_cast<const PolylineEntity*>(e))) {
            route.push_back(id);
            if (existing.isEmpty() && !e->canaloTray().empty() && doc.canaloTray(trayOf(e->canaloTray()).toStdString()))
                existing = trayOf(e->canaloTray());
        }
    if (route.empty())
        return false;
    const Import item = pending_;
    pending_ = {};
    pendingText_.clear();
    QJsonObject st = item.state;
    st.insert(QStringLiteral("lengthM"), std::max(1.0, std::round(routeLengthM(route) * 10.0) / 10.0));
    if (!existing.isEmpty()) {
        // La polilinea è già di una canalina: il tratto importato è quello.
        applyImportTo(existing, st);
    } else {
        trayId_.clear();
        createTray(st);
        tag(route, QString());
        emit message(tr("%1 collegato alla polilinea: canalina %2, tratto di %3 m.").arg(item.file, trayId_, num(dv(st, "lengthM"))));
    }
    QTimer::singleShot(0, this, [this] { placeNextImport(); });
    return true;
}

void CanaloPanel::cancelPending()
{
    if (pendingText_.isEmpty())
        return;
    emit message(tr("%1 non collegato.").arg(pending_.file));
    pending_ = {};
    pendingText_.clear();
    emit changed();
    placeNextImport();
}

void CanaloPanel::applyImportTo(const QString& id, const QJsonObject& imported)
{
    // Misure, cavi e lunghezza dal tratto importato; il resto (staffaggio, tavola, posizione della sezione) resta.
    trayId_ = id;
    QJsonObject st = state();
    for (const char* key : {"width", "height", "cables", "lengthM"})
        if (imported.contains(QLatin1String(key)))
            st.insert(QLatin1String(key), imported.value(QLatin1String(key)));
    st.insert(QStringLiteral("cartesioAutoSize"), false);
    store(st, tr("Canalina %1: misure e cavi importati (%2 × %3 mm, %n cavi).", nullptr,
                 static_cast<int>(imported.value(QStringLiteral("cables")).toArray().size()))
                  .arg(id, num(dv(imported, "width"), 0), num(dv(imported, "height"), 0)));
}

double CanaloPanel::routeLengthM(const std::vector<EntityId>& ids) const
{
    if (!drawing_)
        return 0.0;
    const Document& doc = drawing_->document();
    double length = 0.0;
    auto segment = [&](Vec2 a, Vec2 b, double bulge) {
        const double chord = std::hypot(b.x - a.x, b.y - a.y);
        if (std::abs(bulge) < 1e-9) {
            length += chord;
        } else {   // arco: corda e angolo dalla curvatura
            const double angle = 4.0 * std::atan(std::abs(bulge));
            length += chord * angle / (2.0 * std::sin(angle / 2.0));
        }
    };
    for (EntityId id : ids) {
        const Entity* e = doc.find(id);
        if (const auto* l = dynamic_cast<const LineEntity*>(e))
            segment(l->start, l->end, 0.0);
        else if (const auto* pl = dynamic_cast<const PolylineEntity*>(e)) {
            for (std::size_t i = 0; i + 1 < pl->vertices.size(); ++i)
                segment(pl->vertices[i].point, pl->vertices[i + 1].point, pl->vertices[i].bulge);
            if (pl->closed && pl->vertices.size() > 2)
                segment(pl->vertices.back().point, pl->vertices.front().point, pl->vertices.back().bulge);
        }
    }
    return length * plantMetersPerUnit(doc);   // in metri
}

// ---------------------------------------------------------------- database

void CanaloPanel::setCategory(const QString& key)
{
    category_ = key;
    dbSelected_ = -1;
    syncOwnCables();
}

void CanaloPanel::setSearch(const QString& text)
{
    search_ = text;
    dbSelected_ = -1;
    syncOwnCables();
}

void CanaloPanel::selectDb(int row)
{
    dbSelected_ = row;
    emit dbChanged();
}

void CanaloPanel::addFromDb(int row, int quantity, bool trefoil)
{
    const QJsonObject item = dbItem(row);
    if (item.isEmpty() || !drawing_) {
        emit message(tr("CANALINA: scegliere un cavo nel database."));
        return;
    }
    if (trayId_.isEmpty())
        createTray({});
    const int qty = std::clamp(quantity, 1, 30);
    const QJsonObject st = CanaloEngine::instance().addCable(state(), sv(item, "id"), qty, trefoil);
    store(st, tr("%1: aggiunto %2 × %3 %4.").arg(trayId_).arg(qty).arg(sv(item, "type"), sv(item, "form")));
}

// ---------------------------------------------------------------- cavi della canalina

void CanaloPanel::selectCable(int row)
{
    cableSelected_ = row;
    emit changed();
}

void CanaloPanel::editCable(int row)
{
    QJsonObject st = state();
    QJsonArray cables = st.value(QStringLiteral("cables")).toArray();
    if (row < 0 || row >= cables.size())
        return;
    QJsonObject c = cables[row].toObject();
    QString code = sv(c, "code"), desc = sv(c, "desc");
    double ib = dv(c, "ib");
    FormDialog dialog(tr("Cavo #%1").arg(row + 1), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.note(tr("%1 %2 — Ø %3 mm").arg(sv(c, "type"), sv(c, "form"), num(dv(c, "d"))));
    dialog.text(tr("Sigla"), code);
    dialog.text(tr("Descrizione utenza"), desc);
    dialog.number(tr("Corrente d'impiego Ib"), 0, 5000, 1, ib, QStringLiteral(" A"));
    dialog.setSize(420, 260);
    if (dialog.exec() != QDialog::Accepted)
        return;
    c.insert(QStringLiteral("code"), code.trimmed());
    c.insert(QStringLiteral("desc"), desc);
    c.insert(QStringLiteral("ib"), ib);
    cables[row] = c;
    st.insert(QStringLiteral("cables"), cables);
    store(st, tr("%1: cavo %2 modificato.").arg(trayId_, code));
}

void CanaloPanel::removeCable(int row)
{
    QJsonObject st = state();
    QJsonArray cables = st.value(QStringLiteral("cables")).toArray();
    if (row < 0 || row >= cables.size())
        return;
    const QString code = sv(cables[row].toObject(), "code");
    cables.removeAt(row);
    st.insert(QStringLiteral("cables"), cables);
    cableSelected_ = std::min(cableSelected_, static_cast<int>(cables.size()) - 1);
    store(st, tr("%1: tolto il cavo %2.").arg(trayId_, code));
}

void CanaloPanel::clearCables()
{
    if (trayId_.isEmpty())
        return;
    QJsonObject st = state();
    st.insert(QStringLiteral("cables"), QJsonArray());
    cableSelected_ = -1;
    store(st, tr("%1: tolti tutti i cavi.").arg(trayId_));
}

void CanaloPanel::setAutoSize(bool on)
{
    if (trayId_.isEmpty())
        return;
    QJsonObject st = state();
    st.insert(QStringLiteral("cartesioAutoSize"), on);
    store(st, on ? tr("%1: misura dai cavi.").arg(trayId_) : tr("%1: misura fissa (Dati canalina).").arg(trayId_));
}

void CanaloPanel::setReserve(double percent)
{
    if (trayId_.isEmpty())
        return;
    QJsonObject st = state();
    if (std::abs(dv(st, "cartesioReserve", 30) - percent) < 0.01)
        return;
    st.insert(QStringLiteral("cartesioReserve"), std::clamp(percent, 0.0, 80.0));
    store(st, QString());
}

void CanaloPanel::editTray()
{
    if (!drawing_)
        return;
    if (trayId_.isEmpty())
        createTray({});
    QJsonObject st = state();
    QJsonObject tb = st.value(QStringLiteral("tb")).toObject();
    auto str = [&](const char* k) { return sv(st, k); };
    QString title = str("title"), planTag = str("planTag");
    const double oldWidth = dv(st, "width", 200), oldHeight = dv(st, "height", 75);
    double width = oldWidth, height = oldHeight, length = dv(st, "lengthM", 30), span = dv(st, "spanM", 1.5);
    int bends = static_cast<int>(dv(st, "bends90")), tees = static_cast<int>(dv(st, "tees")), reducers = static_cast<int>(dv(st, "reducers"));
    int rei = static_cast<int>(dv(st, "reiCount")), septums = static_cast<int>(dv(st, "septums"));
    QVariant thk = dv(st, "thk", 1.5), trayType = str("trayType"), material = str("material"), cover = str("cover"), earth = str("earth");
    QVariant packing = str("packing"), support = str("support"), seismic = str("seismic"), clearance = dv(st, "clearance", 150);
    QVariant scale = str("scale").isEmpty() ? QStringLiteral("1:1") : str("scale"), textScale = dv(st, "textScale", 1.0);
    QVariant layout = str("layoutMode").isEmpty() ? QStringLiteral("standard") : str("layoutMode");
    bool edge = st.value(QStringLiteral("edgeGuard")).toBool(true), clamps = st.value(QStringLiteral("clamps")).toBool(false);
    bool iso = st.value(QStringLiteral("showIso3D")).toBool(true), chart = st.value(QStringLiteral("showLoadChart")).toBool(true);
    bool bomOn = st.value(QStringLiteral("showBOM")).toBool(true), cart = st.value(QStringLiteral("showCartiglio")).toBool(true);
    bool desc = st.value(QStringLiteral("showTableDesc")).toBool(true) && !st.value(QStringLiteral("hideTableDesc")).toBool(false);
    bool callouts = st.value(QStringLiteral("showCableCallouts")).toBool(true), planTagOn = st.value(QStringLiteral("showPlanTag")).toBool(false);
    QString client = sv(tb, "client"), project = sv(tb, "project"), docCode = sv(tb, "docCode"), author = sv(tb, "author"),
            checker = sv(tb, "checker"), rev = sv(tb, "rev"), date = sv(tb, "date");

    auto set = [](QVariant& target) { return [&target](const QVariant& v) { target = v; }; };
    FormDialog dialog(tr("Canalina %1").arg(trayId_), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.page(tr("Canalina"));
    dialog.group(tr("Sezione"));
    dialog.text(tr("Titolo"), title);
    dialog.note(tr("Cambiando larghezza o altezza la misura non segue più i cavi (si rimette con Misura automatica)."));
    dialog.number(tr("Larghezza"), 30, 1200, 0, width, QStringLiteral(" mm"));
    dialog.number(tr("Altezza"), 20, 300, 0, height, QStringLiteral(" mm"));
    dialog.choice(tr("Spessore"), {{QStringLiteral("1,5 mm"), 1.5}, {QStringLiteral("2,0 mm"), 2.0}, {QStringLiteral("2,5 mm"), 2.5}}, thk, set(thk));
    dialog.choice(tr("Tipo di profilo"), {{tr("Passerella asolata"), QStringLiteral("perforated")}, {tr("Canale cieco"), QStringLiteral("solid")},
                                          {tr("Passerella a filo"), QStringLiteral("mesh")}, {tr("Passerella a scala (pioli)"), QStringLiteral("ladder")}},
                  trayType, set(trayType));
    dialog.choice(tr("Materiale e finitura"), {{tr("Acciaio zincato Sendzimir (EN 10346)"), QStringLiteral("sendzimir")},
                                               {tr("Zincato a caldo (EN 1461)"), QStringLiteral("hdg")},
                                               {tr("Inox AISI 304"), QStringLiteral("inox304")}, {tr("Inox AISI 316L"), QStringLiteral("inox316")},
                                               {tr("Verniciato RAL 7035"), QStringLiteral("ral7035")},
                                               {tr("Verniciato RAL 3000 (antincendio)"), QStringLiteral("ral3000")}},
                  material, set(material));
    dialog.choice(tr("Coperchio"), {{tr("Nessuno (a giorno)"), QStringLiteral("none")}, {tr("Cieco a scatto"), QStringLiteral("snap")},
                                    {tr("Aerato"), QStringLiteral("ventilated")}, {tr("Con clip antisfilamento"), QStringLiteral("clamped")}},
                  cover, set(cover));
    dialog.integer(tr("Setti separatori"), 0, 2, septums);
    dialog.choice(tr("Disposizione dei cavi"), {{tr("Per gravità"), QStringLiteral("gravity")}, {tr("Come disegnati"), QStringLiteral("input")}},
                  packing, set(packing));
    dialog.choice(tr("Conduttore PE"), {{tr("Giallo/verde FS17 16 mm²"), QStringLiteral("gv16")}, {tr("Giallo/verde FS17 25 mm²"), QStringLiteral("gv25")},
                                        {tr("Treccia di rame stagnato 25 mm²"), QStringLiteral("cu25")},
                                        {tr("Treccia di rame stagnato 35 mm²"), QStringLiteral("cu35")}, {tr("Nessun collegamento PE"), QStringLiteral("none")}},
                  earth, set(earth));
    dialog.check(tr("Paraspigoli"), edge);
    dialog.check(tr("Fascette e bloccacavi"), clamps);
    dialog.choice(tr("Distanza dal soffitto"), {{tr("Non quotare"), 0}, {tr("Min. 150 mm"), 150}, {tr("Min. 200 mm"), 200}}, clearance, set(clearance));
    dialog.group(tr("Tratto"));
    dialog.number(tr("Lunghezza"), 1, 5000, 1, length, QStringLiteral(" m"));
    dialog.integer(tr("Curve a 90°"), 0, 200, bends);
    dialog.integer(tr("Derivazioni a T"), 0, 200, tees);
    dialog.integer(tr("Riduzioni"), 0, 200, reducers);
    dialog.integer(tr("Attraversamenti EI120"), 0, 100, rei);
    dialog.group(tr("Staffaggio"));
    QList<QPair<QString, QVariant>> supports;
    const QJsonObject specs = CanaloEngine::instance().database().value(QStringLiteral("supports")).toObject();
    for (auto it = specs.begin(); it != specs.end(); ++it)
        supports.append({sv(it.value().toObject(), "name"), it.key()});
    dialog.choice(tr("Supporto"), supports, support, set(support));
    dialog.number(tr("Passo"), 1.0, 3.0, 2, span, QStringLiteral(" m"));
    dialog.choice(tr("Antisismico (NTC 2018)"), {{tr("Nessuno"), QStringLiteral("none")}, {tr("Funi in acciaio a 45° ogni 9 m"), QStringLiteral("cable_9m")},
                                                  {tr("Controvento rigido a 45° ogni 6 m"), QStringLiteral("rigid_6m")}},
                  seismic, set(seismic));
    dialog.page(tr("Tavola"));
    dialog.group(tr("Tavola"));
    dialog.choice(tr("Scala"), {{QStringLiteral("1:1"), QStringLiteral("1:1")}, {QStringLiteral("1:2"), QStringLiteral("1:2")}, {QStringLiteral("1:5"), QStringLiteral("1:5")},
                               {QStringLiteral("1:10"), QStringLiteral("1:10")}, {QStringLiteral("1:20"), QStringLiteral("1:20")},
                               {QStringLiteral("1:50"), QStringLiteral("1:50")}, {QStringLiteral("1:100"), QStringLiteral("1:100")}},
                  scale, set(scale));
    QList<QPair<QString, QVariant>> textScales;
    for (double v : {0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 1.8})
        textScales.append({QStringLiteral("%1%").arg(qRound(v * 100)), v});
    dialog.choice(tr("Dimensione dei testi"), textScales, textScale, set(textScale));
    dialog.choice(tr("Impaginazione"), {{tr("Completa"), QStringLiteral("standard")}, {tr("Minima"), QStringLiteral("minimal")}}, layout, set(layout));
    dialog.check(tr("Vista 3D del tratto"), iso);
    dialog.check(tr("Curva di carico"), chart);
    dialog.check(tr("Distinta materiali"), bomOn);
    dialog.check(tr("Cartiglio"), cart);
    dialog.check(tr("Descrizione utenze nella tabella"), desc);
    dialog.check(tr("Bandierine dei cavi"), callouts);
    dialog.check(tr("Cartellino in pianta"), planTagOn);
    dialog.text(tr("Sigla in pianta"), planTag);
    dialog.group(tr("Cartiglio"));
    dialog.text(tr("Committente"), client);
    dialog.text(tr("Progetto"), project);
    dialog.text(tr("Codice documento"), docCode);
    dialog.text(tr("Redatto"), author);
    dialog.text(tr("Verificato"), checker);
    dialog.text(tr("Revisione"), rev);
    dialog.text(tr("Data"), date);
    dialog.setSize(500, 720);
    if (dialog.exec() != QDialog::Accepted)
        return;

    st.insert(QStringLiteral("title"), title);
    st.insert(QStringLiteral("planTag"), planTag);
    st.insert(QStringLiteral("width"), width);
    st.insert(QStringLiteral("height"), height);
    if (std::abs(width - oldWidth) > 0.01 || std::abs(height - oldHeight) > 0.01)
        st.insert(QStringLiteral("cartesioAutoSize"), false);   // misura scelta a mano
    st.insert(QStringLiteral("thk"), thk.toDouble());
    st.insert(QStringLiteral("trayType"), trayType.toString());
    st.insert(QStringLiteral("material"), material.toString());
    st.insert(QStringLiteral("cover"), cover.toString());
    st.insert(QStringLiteral("septums"), septums);
    st.insert(QStringLiteral("packing"), packing.toString());
    st.insert(QStringLiteral("earth"), earth.toString());
    st.insert(QStringLiteral("edgeGuard"), edge);
    st.insert(QStringLiteral("clamps"), clamps);
    st.insert(QStringLiteral("clearance"), clearance.toDouble());
    st.insert(QStringLiteral("lengthM"), length);
    st.insert(QStringLiteral("bends90"), bends);
    st.insert(QStringLiteral("tees"), tees);
    st.insert(QStringLiteral("reducers"), reducers);
    st.insert(QStringLiteral("reiCount"), rei);
    st.insert(QStringLiteral("support"), support.toString());
    st.insert(QStringLiteral("spanM"), std::round(std::clamp(span, 1.0, 3.0) * 20.0) / 20.0);   // passo 0,05 m, come la pagina
    st.insert(QStringLiteral("seismic"), seismic.toString());
    st.insert(QStringLiteral("layoutMode"), layout.toString());
    st.insert(QStringLiteral("showIso3D"), iso);
    st.insert(QStringLiteral("showLoadChart"), chart);
    st.insert(QStringLiteral("showBOM"), bomOn);
    st.insert(QStringLiteral("showCartiglio"), cart);
    st.insert(QStringLiteral("showTableDesc"), desc);
    st.insert(QStringLiteral("hideTableDesc"), !desc);
    st.insert(QStringLiteral("showCableCallouts"), callouts);
    st.insert(QStringLiteral("showPlanTag"), planTagOn);
    tb.insert(QStringLiteral("client"), client);
    tb.insert(QStringLiteral("project"), project);
    tb.insert(QStringLiteral("docCode"), docCode);
    tb.insert(QStringLiteral("author"), author);
    tb.insert(QStringLiteral("checker"), checker);
    tb.insert(QStringLiteral("rev"), rev);
    tb.insert(QStringLiteral("date"), date);
    st.insert(QStringLiteral("tb"), tb);
    // Scale come le scelte della pagina (la scala va anche nel cartiglio).
    st = CanaloEngine::instance().setScales(st, scale.toString(), textScale.toDouble());
    store(st, tr("%1: dati della canalina aggiornati.").arg(trayId_));
}

// ---------------------------------------------------------------- sezione e tavola

void CanaloPanel::drawSection()
{
    if (!drawing_ || trayId_.isEmpty())
        return;
    QJsonObject st = state();
    const bool first = !st.contains(QStringLiteral("cartesioSection"));
    if (first) {   // a destra di quello che c'è nel disegno
        const BBox existing = drawing_->document().extents();
        const Vec2 at = existing.isValid() ? Vec2{existing.max.x + 300.0 * drawingUnit(), existing.min.y} : Vec2{0.0, 0.0};
        st.insert(QStringLiteral("cartesioSection"), QJsonObject{{QStringLiteral("x"), at.x}, {QStringLiteral("y"), at.y}});
    }
    store(st, first ? tr("%1: sezione disegnata al vero; si aggiorna da sola a ogni cambio (si sposta con MOVE).").arg(trayId_)
                    : tr("%1: sezione aggiornata.").arg(trayId_));
    const std::vector<EntityId> ids = sectionEntities();
    BBox box;
    for (EntityId id : ids)
        if (const Entity* e = drawing_->document().find(id))
            box.expand(e->bounds());
    drawing_->setSelection(ids);
    if (box.isValid()) {
        const double mx = (box.max.x - box.min.x) * 0.15, my = (box.max.y - box.min.y) * 0.25;
        box.expand(Vec2{box.min.x - mx, box.min.y - my});
        box.expand(Vec2{box.max.x + mx, box.max.y + my});
        drawing_->view()->zoomWindow(box);
    }
    refresh();
}

void CanaloPanel::insertSheet()
{
    if (!drawing_ || trayId_.isEmpty()) {
        emit message(tr("CANALINA: scegliere una canalina."));
        return;
    }
    // Il file della tavola come quello esportato da CanaloCAD, inserito come blocco "Tavola_CN1": si sceglie il punto.
    // Se il blocco c'è già si aggiorna (anche le tavole già inserite).
    const QString name = QStringLiteral("Tavola_%1").arg(trayId_);
    const QString path = tempPath(name + QStringLiteral(".dxf"));
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(CanaloEngine::instance().dxfBytes(state(), sv(state(), "title")));
    }
    Document& doc = drawing_->document();
    if (doc.findBlock(name.toStdString())) {
        Document source;
        QString error;
        if (DrawingWidget::readFile(path, source, &error)) {
            const int from = source.insUnits() > 0 ? source.insUnits() : appOptions().insertSourceUnits;
            importDrawingAsBlock(doc, source, name.toStdString(), unitsFactor(from, 6) / std::max(1e-12, plantMetersPerUnit(doc)));
            doc.checkpoint();
            drawing_->notifyChanged();
            emit message(tr("Tavola %1 aggiornata (anche quelle già inserite). Per inserirne un'altra copia: INSERT %1.").arg(name));
        }
        QFile::remove(path);
        return;
    }
    QString error;
    if (!drawing_->insertFile(path, &error))
        emit message(tr("CANALINA: %1").arg(error));
    else
        emit message(tr("Tavola di CanaloCAD (%1): indicare nel disegno il punto d'inserimento.").arg(name));
    QFile::remove(path);
    if (isActiveWindow())
        drawing_->window()->activateWindow();   // il punto si sceglie nel disegno
}

void CanaloPanel::insertSheetAt(Vec2 at)
{
    if (!drawing_ || trayId_.isEmpty())
        return;
    const QString name = QStringLiteral("Tavola_%1").arg(trayId_);
    const QString path = tempPath(name + QStringLiteral(".dxf"));
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(CanaloEngine::instance().dxfBytes(state(), sv(state(), "title")));
    }
    Document source;
    QString error;
    const bool read = DrawingWidget::readFile(path, source, &error);
    QFile::remove(path);
    if (!read)
        return;
    Document& doc = drawing_->document();
    const int from = source.insUnits() > 0 ? source.insUnits() : appOptions().insertSourceUnits;
    importDrawingAsBlock(doc, source, name.toStdString(), unitsFactor(from, 6) / std::max(1e-12, plantMetersPerUnit(doc)));
    std::vector<std::unique_ptr<Entity>> added;
    auto insert = std::make_unique<InsertEntity>(name.toStdString(), at);
    insert->setCanaloTray(trayId_.toStdString());
    added.push_back(std::move(insert));
    doc.commit(std::move(added));
    doc.checkpoint();
    drawing_->notifyChanged();
}

void CanaloPanel::saveSheet()
{
    if (trayId_.isEmpty())
        return;
    QString name = sv(state(), "title");
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("_"));
    const QString start = drawing_ && !drawing_->filePath().isEmpty() ? QFileInfo(drawing_->filePath()).absolutePath() : QString();
    const QString path = QFileDialog::getSaveFileName(this, tr("Salva la tavola DXF"), QDir(start).filePath(name + QStringLiteral(".dxf")),
                                                      tr("DXF (*.dxf)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit message(tr("CANALINA: impossibile scrivere %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    file.write(CanaloEngine::instance().dxfBytes(state(), QFileInfo(path).completeBaseName()));
    emit message(tr("Tavola salvata in %1 (lo stesso file che esporta CanaloCAD).").arg(QDir::toNativeSeparators(path)));
}

// ---------------------------------------------------------------- libreria personale

void CanaloPanel::newOwnCable()
{
    canalo::CableType t;
    t.d = 10.0;
    t.sec = 2.5;
    t.iz0 = 20.0;
    const QJsonObject base = dbItem(dbSelected_);
    if (!base.isEmpty()) {   // dai dati del cavo scelto: si cambia quello che serve
        t.type = sv(base, "type").toStdString();
        t.form = sv(base, "form").toStdString();
        t.d = dv(base, "d", 10);
        t.w = dv(base, "w", 0.1);
        t.sec = dv(base, "sec", 2.5);
        t.cores = static_cast<int>(dv(base, "cores", 2));
        t.iz0 = dv(base, "iz0", 20);
        t.bendK = dv(base, "bendK", 10);
        t.sys = sv(base, "sys").toStdString();
    }
    if (!PlantPanel::editCableType(t, true, this))
        return;
    t.id = "USR_" + t.code;
    t.standard = "Cavo personale";
    std::vector<canalo::CableType> personal = personalCables();
    std::erase_if(personal, [&](const canalo::CableType& p) { return p.id == t.id; });
    personal.push_back(t);
    std::sort(personal.begin(), personal.end(), [](const auto& a, const auto& b) { return a.code < b.code; });
    savePersonalCables(personal);
    category_ = QStringLiteral("custom");
    syncOwnCables();
    emit message(tr("Cavo personale %1 salvato (vale per tutti i disegni).").arg(QString::fromStdString(t.code)));
}

void CanaloPanel::editOwnCable()
{
    const QString id = sv(dbItem(dbSelected_), "id");
    if (!id.startsWith(QStringLiteral("USR_"))) {
        emit message(tr("CANALINA: si modificano solo i cavi personali (per uno del database: Nuovo cavo, che parte dai suoi dati)."));
        return;
    }
    std::vector<canalo::CableType> personal = personalCables();
    auto it = std::find_if(personal.begin(), personal.end(), [&](const canalo::CableType& p) { return QString::fromStdString(p.id) == id; });
    if (it == personal.end())
        return;
    canalo::CableType t = *it;
    if (!PlantPanel::editCableType(t, false, this))
        return;
    t.id = "USR_" + t.code;
    t.standard = "Cavo personale";
    personal.erase(it);
    std::erase_if(personal, [&](const canalo::CableType& p) { return p.id == t.id; });
    personal.push_back(t);
    std::sort(personal.begin(), personal.end(), [](const auto& a, const auto& b) { return a.code < b.code; });
    savePersonalCables(personal);
    syncOwnCables();
}

void CanaloPanel::removeOwnCable()
{
    const QString id = sv(dbItem(dbSelected_), "id");
    if (!id.startsWith(QStringLiteral("USR_")))
        return;
    if (QMessageBox::question(this, tr("Cavi personali"), tr("Eliminare il cavo personale %1?").arg(id.mid(4))) != QMessageBox::Yes)
        return;
    std::vector<canalo::CableType> personal = personalCables();
    std::erase_if(personal, [&](const canalo::CableType& p) { return QString::fromStdString(p.id) == id; });
    savePersonalCables(personal);
    dbSelected_ = -1;
    syncOwnCables();
}

void CanaloPanel::showGuide()
{
    emit guideRequested();
}

// ---------------------------------------------------------------- script

bool CanaloPanel::composeFromIds(const QString& list, QString* error)
{
    if (!drawing_)
        return false;
    syncOwnCables();
    CanaloEngine& engine = CanaloEngine::instance();
    QJsonObject st = engine.defaultState();
    for (const QString& item : list.split(u',', Qt::SkipEmptyParts)) {
        const QStringList parts = item.trimmed().split(u'*');
        const QString id = parts.value(0).trimmed();
        const int qty = parts.size() > 1 ? std::max(1, parts.value(1).toInt()) : 1;
        const int before = static_cast<int>(st.value(QStringLiteral("cables")).toArray().size());
        QJsonObject next = engine.addCable(st, id, qty, false);
        if (next.value(QStringLiteral("cables")).toArray().size() == before)
            next = engine.addCable(st, id.toUpper(), qty, false);
        if (next.value(QStringLiteral("cables")).toArray().size() == before) {
            if (error)
                *error = tr("%1 non è nel database dei cavi.").arg(id);
            return false;
        }
        st = next;
    }
    st.insert(QStringLiteral("cartesioAutoSize"), true);
    trayId_.clear();
    createTray(st);
    drawSection();
    return true;
}

// ---------------------------------------------------------------- per l'assistente IA

namespace {

QString trayTypeKey(const QString& given)
{
    const QString t = given.trimmed().toLower();
    if (t.contains(QStringLiteral("ciec")) || t == QLatin1String("solid"))
        return QStringLiteral("solid");
    if (t.contains(QStringLiteral("filo")) || t == QLatin1String("mesh"))
        return QStringLiteral("mesh");
    if (t.contains(QStringLiteral("scala")) || t == QLatin1String("ladder"))
        return QStringLiteral("ladder");
    return QStringLiteral("perforated");
}

} // namespace

QJsonArray CanaloPanel::searchCables(const QString& text, const QString& category, int limit)
{
    CanaloEngine& engine = CanaloEngine::instance();
    engine.setOwnCables(ownCablesJson());
    const QJsonArray all = engine.database().value(QStringLiteral("cables")).toArray();
    const QStringList words = text.toLower().split(u' ', Qt::SkipEmptyParts);
    QJsonArray out;
    for (const QJsonValue& v : all) {
        const QJsonObject c = v.toObject();
        if (!category.isEmpty() && sv(c, "cat") != category)
            continue;
        const QString hay = (sv(c, "id") + u' ' + sv(c, "code") + u' ' + sv(c, "type") + u' ' + sv(c, "form") + u' ' + sv(c, "desc")).toLower();
        if (!std::all_of(words.begin(), words.end(), [&](const QString& w) { return hay.contains(w); }))
            continue;
        out.append(QJsonObject{{QStringLiteral("id"), sv(c, "id")},
                               {QStringLiteral("tipo"), sv(c, "type")},
                               {QStringLiteral("formazione"), sv(c, "form")},
                               {QStringLiteral("descrizione"), sv(c, "desc")},
                               {QStringLiteral("diametro_mm"), dv(c, "d")},
                               {QStringLiteral("peso_kg_m"), dv(c, "w")},
                               {QStringLiteral("sezione_mm2"), dv(c, "sec")},
                               {QStringLiteral("sistema"), systemName(sv(c, "sys"))},
                               {QStringLiteral("categoria"), sv(c, "cat")}});
        if (out.size() >= limit)
            break;
    }
    return out;
}

QJsonObject CanaloPanel::stateFor(const QJsonArray& cables, const QJsonObject& options, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return QJsonObject();
    };
    if (cables.isEmpty())
        return fail(tr("Nessun cavo: indicare almeno un cavo del database (id da canalo_cerca_cavi)."));
    if (cables.size() > 60)
        return fail(tr("Troppi tipi di cavo (massimo 60)."));
    CanaloEngine& engine = CanaloEngine::instance();
    engine.setOwnCables(ownCablesJson());
    QJsonObject st = engine.defaultState();
    if (options.contains(QStringLiteral("tipologia")))
        st.insert(QStringLiteral("trayType"), trayTypeKey(options.value(QStringLiteral("tipologia")).toString()));
    for (const QJsonValue& v : cables) {
        const QJsonObject c = v.toObject();
        const QString id = c.value(QStringLiteral("id")).toString().trimmed();
        const int qty = std::clamp(c.value(QStringLiteral("quantita")).toInt(1), 1, 200);
        const bool trefoil = c.value(QStringLiteral("trifoglio")).toBool(false);
        const auto before = st.value(QStringLiteral("cables")).toArray().size();
        QJsonObject next = engine.addCable(st, id, qty, trefoil);
        if (next.value(QStringLiteral("cables")).toArray().size() == before)
            next = engine.addCable(st, id.toUpper(), qty, trefoil);
        if (next.value(QStringLiteral("cables")).toArray().size() == before)
            return fail(tr("Il cavo \"%1\" non è nel database di CanaloCAD (cercarlo con canalo_cerca_cavi).").arg(id));
        st = next;
    }
    const double reserve = std::clamp(options.value(QStringLiteral("riserva")).toDouble(30.0), 0.0, 90.0);
    st.insert(QStringLiteral("cartesioReserve"), reserve);
    const double w = options.value(QStringLiteral("larghezza")).toDouble(0.0), h = options.value(QStringLiteral("altezza")).toDouble(0.0);
    if (w > 0 && h > 0) {
        if (w > 2000 || h > 500)
            return fail(tr("Misura fuori dai limiti (larghezza fino a 2000 mm, altezza fino a 500 mm)."));
        st.insert(QStringLiteral("width"), w);
        st.insert(QStringLiteral("height"), h);
        st.insert(QStringLiteral("cartesioAutoSize"), false);
        st = engine.calculate(st);
    } else {
        st.insert(QStringLiteral("cartesioAutoSize"), true);
        st = engine.autoSize(st, reserve);
    }
    if (st.isEmpty())
        return fail(tr("Calcolo di CanaloCAD non riuscito: %1").arg(engine.error()));
    return st;
}

QJsonObject CanaloPanel::stateFromCables(const std::vector<canalo::Cable>& cables, const QJsonObject& options, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return QJsonObject();
    };
    if (cables.empty())
        return fail(tr("Nessun cavo."));
    if (cables.size() > 400)
        return fail(tr("Troppi cavi (massimo 400)."));
    CanaloEngine& engine = CanaloEngine::instance();
    engine.setOwnCables(ownCablesJson());
    QJsonObject st = engine.defaultState();
    if (options.contains(QStringLiteral("tipologia")))
        st.insert(QStringLiteral("trayType"), trayTypeKey(options.value(QStringLiteral("tipologia")).toString()));
    QJsonArray list;
    int n = 0;
    for (const canalo::Cable& c : cables)
        list.append(QJsonObject{{QStringLiteral("id"), n++},
                                {QStringLiteral("code"), QString::fromStdString(c.code)},
                                {QStringLiteral("desc"), QString::fromStdString(c.desc)},
                                {QStringLiteral("type"), QString::fromStdString(c.type)},
                                {QStringLiteral("form"), QString::fromStdString(c.form)},
                                {QStringLiteral("d"), c.d},
                                {QStringLiteral("w"), c.w},
                                {QStringLiteral("sec"), c.sec},
                                {QStringLiteral("cores"), c.cores},
                                {QStringLiteral("iz0"), c.iz0},
                                {QStringLiteral("ib"), c.ib},
                                {QStringLiteral("bendK"), c.bendK},
                                {QStringLiteral("sys"), QString::fromStdString(c.sys)},
                                {QStringLiteral("x"), 0.0},
                                {QStringLiteral("y"), 0.0}});
    st.insert(QStringLiteral("cables"), list);
    if (options.contains(QStringLiteral("lunghezza_m")))
        st.insert(QStringLiteral("lengthM"), std::max(1.0, options.value(QStringLiteral("lunghezza_m")).toDouble()));
    const double reserve = std::clamp(options.value(QStringLiteral("riserva")).toDouble(30.0), 0.0, 90.0);
    st.insert(QStringLiteral("cartesioReserve"), reserve);
    const double w = options.value(QStringLiteral("larghezza")).toDouble(0.0), h = options.value(QStringLiteral("altezza")).toDouble(0.0);
    if (w > 0 && h > 0) {
        st.insert(QStringLiteral("width"), std::min(w, 2000.0));
        st.insert(QStringLiteral("height"), std::min(h, 500.0));
        st.insert(QStringLiteral("cartesioAutoSize"), false);
        st = engine.calculate(st);
    } else {
        st.insert(QStringLiteral("cartesioAutoSize"), true);
        st = engine.autoSize(st, reserve);
    }
    if (st.isEmpty())
        return fail(tr("Calcolo di CanaloCAD non riuscito: %1").arg(engine.error()));
    return st;
}

void CanaloPanel::linkRoute(const QString& id, const std::vector<EntityId>& route, double lengthM)
{
    if (!drawing_ || trayState(id).isEmpty() || route.empty())
        return;
    trayId_ = id;
    tag(route, QString());
    QJsonObject st = state();
    if (lengthM > 0)
        st.insert(QStringLiteral("lengthM"), std::max(1.0, std::round(lengthM * 10.0) / 10.0));
    store(st, tr("Percorso collegato alla canalina %1.").arg(id));
    refresh();
}

QJsonObject CanaloPanel::analyze(const QJsonObject& state)
{
    QJsonObject m;
    const QJsonObject st = CanaloEngine::instance().calculate(state, &m);
    if (st.isEmpty())
        return {{QStringLiteral("errore"), tr("Calcolo di CanaloCAD non riuscito: %1").arg(CanaloEngine::instance().error())}};
    m.insert(QStringLiteral("cartesioMinWidth"), st.value(QStringLiteral("cartesioAutoSize")).toBool(false) ? 0 : minimumWidth(st, m));
    // Cavi raggruppati per sigla del catalogo (la pagina aggiunge la fase: "FG16OM16-4G10 - L1").
    QJsonArray cables;
    std::map<QString, int> index;
    for (const QJsonValue& v : st.value(QStringLiteral("cables")).toArray()) {
        const QJsonObject c = v.toObject();
        const QString code = sv(c, "code").section(QStringLiteral(" - "), 0, 0).trimmed();
        const QString phase = sv(c, "code").section(QStringLiteral(" - "), 1).trimmed();
        if (auto it = index.find(code); it != index.end()) {
            QJsonObject row = cables[it->second].toObject();
            row.insert(QStringLiteral("quantita"), row.value(QStringLiteral("quantita")).toInt() + 1);
            if (!phase.isEmpty()) {
                QJsonArray phases = row.value(QStringLiteral("fasi")).toArray();
                phases.append(phase);
                row.insert(QStringLiteral("fasi"), phases);
            }
            cables[it->second] = row;
            continue;
        }
        index[code] = static_cast<int>(cables.size());
        QJsonObject row{{QStringLiteral("sigla"), code}, {QStringLiteral("diametro_mm"), dv(c, "d")},
                        {QStringLiteral("sezione_mm2"), dv(c, "sec")}, {QStringLiteral("quantita"), 1}};
        if (!phase.isEmpty())
            row.insert(QStringLiteral("fasi"), QJsonArray{phase});
        cables.append(row);
    }
    QJsonArray lines;
    for (const QVariant& v : summaryOf(st, m)) {
        const QVariantMap r = v.toMap();
        lines.append(r.value(QStringLiteral("label")).toString() + QStringLiteral(": ") + r.value(QStringLiteral("value")).toString());
    }
    QJsonObject results{{QStringLiteral("riempimento_pct"), dv(m, "fillPct")},
                        {QStringLiteral("riserva_pct"), dv(m, "sparePct")},
                        {QStringLiteral("kr"), dv(m, "kr")},
                        {QStringLiteral("segregazione_conforme"), m.value(QStringLiteral("segConform")).toBool(true)},
                        {QStringLiteral("segregazione"), sv(m, "segDetail")},
                        {QStringLiteral("peso_kg_m"), dv(m, "totalKgM")},
                        {QStringLiteral("tiro_daN"), dv(m, "pullDaN")},
                        {QStringLiteral("tiro_max_daN"), dv(m, "maxPullDaN")}};
    if (const int need = m.value(QStringLiteral("cartesioMinWidth")).toInt(); need > 0)
        results.insert(QStringLiteral("larghezza_minima_mm"), need);
    if (!sv(m, "deratingFailCode").isEmpty())
        results.insert(QStringLiteral("portata_insufficiente"), sv(m, "deratingFailCode"));
    return {{QStringLiteral("canalina"), QJsonObject{{QStringLiteral("tipologia"), trayTypeName(sv(st, "trayType"))},
                                                     {QStringLiteral("larghezza_mm"), dv(st, "width")},
                                                     {QStringLiteral("altezza_mm"), dv(st, "height")},
                                                     {QStringLiteral("spessore_mm"), dv(st, "thk")},
                                                     {QStringLiteral("misura_dai_cavi"), st.value(QStringLiteral("cartesioAutoSize")).toBool(false)},
                                                     {QStringLiteral("riserva_voluta_pct"), dv(st, "cartesioReserve", 30)}}},
            {QStringLiteral("cavi"), cables},
            {QStringLiteral("risultati"), results},
            {QStringLiteral("riepilogo"), lines},
            {QStringLiteral("avvertenza"), tr("Calcoli di CanaloCAD: valori indicativi da verificare con le norme e con i dati dei costruttori.")}};
}

QJsonObject CanaloPanel::trayState(const QString& id) const
{
    if (drawing_)
        if (const std::string* json = drawing_->document().canaloTray(id.toStdString()))
            return QJsonDocument::fromJson(QByteArray::fromStdString(*json)).object();
    return {};
}

QJsonArray CanaloPanel::trayList()
{
    QJsonArray out;
    if (!drawing_)
        return out;
    for (const auto& [id, json] : drawing_->document().canaloTrays()) {
        const QJsonObject st = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
        const QJsonObject a = analyze(st);
        const QJsonObject r = a.value(QStringLiteral("risultati")).toObject();
        QJsonObject row{{QStringLiteral("codice"), QString::fromStdString(id)},
                        {QStringLiteral("titolo"), sv(st, "title")},
                        {QStringLiteral("canalina"), a.value(QStringLiteral("canalina"))},
                        {QStringLiteral("cavi"), static_cast<int>(st.value(QStringLiteral("cables")).toArray().size())},
                        {QStringLiteral("riempimento_pct"), r.value(QStringLiteral("riempimento_pct"))},
                        {QStringLiteral("segregazione_conforme"), r.value(QStringLiteral("segregazione_conforme"))}};
        if (r.contains(QStringLiteral("larghezza_minima_mm")))
            row.insert(QStringLiteral("larghezza_minima_mm"), r.value(QStringLiteral("larghezza_minima_mm")));
        if (st.contains(QStringLiteral("lengthM")))
            row.insert(QStringLiteral("lunghezza_m"), dv(st, "lengthM"));
        out.append(row);
        if (out.size() >= 200)
            break;
    }
    return out;
}

QString CanaloPanel::addTray(QJsonObject state, bool section, std::optional<Vec2> at)
{
    if (!drawing_)
        return {};
    if (section && at)
        state.insert(QStringLiteral("cartesioSection"), QJsonObject{{QStringLiteral("x"), at->x}, {QStringLiteral("y"), at->y}});
    trayId_.clear();
    const QString id = createTray(state);
    if (section)
        drawSection();
    refresh();
    return id;
}

bool CanaloPanel::selectTray(const QString& id)
{
    if (trayState(id).isEmpty())
        return false;
    trayId_ = id;
    refresh();
    return true;
}

Vec2 CanaloPanel::sheetPosition(std::optional<Vec2> at) const
{
    if (at)
        return *at;
    if (!drawing_)
        return {};
    BBox box;
    for (EntityId id : sectionEntities())
        if (const Entity* e = drawing_->document().find(id))
            box.expand(e->bounds());
    if (!box.isValid())
        box = drawing_->document().extents();
    return box.isValid() ? Vec2{box.max.x + 300.0 * drawingUnit(), box.min.y} : Vec2{0.0, 0.0};
}

bool CanaloPanel::replaceTray(const QString& id, QJsonObject state)
{
    QJsonObject current = trayState(id);
    if (current.isEmpty())
        return false;
    // Della canalina esistente restano posizione della sezione, percorso, titolo e dati di progetto: cambiano cavi,
    // tipologia e misura.
    for (const char* key : {"cables", "trayType", "width", "height", "thk", "cartesioAutoSize", "cartesioReserve"})
        if (state.contains(QLatin1String(key)))
            current.insert(QLatin1String(key), state.value(QLatin1String(key)));
    trayId_ = id;
    store(current, tr("Canalina %1 aggiornata dall'assistente IA.").arg(id));
    refresh();
    return true;
}

} // namespace cartesio
