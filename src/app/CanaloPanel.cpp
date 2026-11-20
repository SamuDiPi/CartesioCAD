#include "app/CanaloPanel.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/CanaloEngine.h"
#include "app/CanaloPlugin.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlantFile.h"
#include "app/PlantPanel.h"
#include "app/QmlUi.h"
#include "app/Theme.h"

#include "core/Document.h"
#include "core/Import.h"
#include "core/io/Dxf.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
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

} // namespace

CanaloPanel::CanaloPanel(QWidget* parent) : QQuickWidget(parent)
{
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    rootContext()->setContextProperty(QStringLiteral("canalo"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/CanaloPanel.qml")));
    setMinimumWidth(380);
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
    if (db_.isEmpty())
        syncOwnCables();
    // La canalina degli oggetti selezionati; senza, resta l'ultima (se c'è ancora).
    QString selected;
    if (drawing_) {
        const Document& doc = drawing_->document();
        for (EntityId id : drawing_->selection())
            if (const Entity* e = doc.find(id); e && !e->canaloTray().empty() && doc.canaloTray(e->canaloTray())) {
                selected = QString::fromStdString(e->canaloTray());
                break;
            }
        if (!selected.isEmpty())
            trayId_ = selected;
        else if (!trayId_.isEmpty() && !doc.canaloTray(trayId_.toStdString()))
            trayId_.clear();
    } else {
        trayId_.clear();
    }
    updateSummary();
    if (!drawing_)
        status_ = tr("Nessun disegno aperto.");
    else if (trayId_.isEmpty())
        status_ = tr("Selezionare un oggetto di una canalina (sezione o percorso), oppure crearne una: Nuova canalina "
                     "(con gli oggetti selezionati), Importa la sezione selezionata o un file DWG/DXF, o aggiungere i cavi "
                     "dal database.");
    else if (selected.isEmpty())
        status_ = tr("Ultima canalina usata (non selezionata nel disegno).");
    else
        status_.clear();
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

void CanaloPanel::store(const QJsonObject& st, const QString& what)
{
    if (!drawing_ || trayId_.isEmpty() || st.isEmpty()) {
        if (st.isEmpty())
            emit message(tr("CanaloCAD: %1").arg(CanaloEngine::instance().error()));
        return;
    }
    Document& doc = drawing_->document();
    doc.setCanaloTray(trayId_.toStdString(), compact(st).toStdString());
    doc.checkpoint();   // annullabile, insieme agli oggetti cambiati nello stesso comando
    drawing_->notifyChanged();
    updateSummary();
    if (!what.isEmpty())
        emit message(what);
    emit changed();
}

QString CanaloPanel::createTray(const QJsonObject& initial, bool linkSelected)
{
    if (!drawing_)
        return {};
    Document& doc = drawing_->document();
    const QString id = QString::fromStdString(doc.nextCanaloTrayId());
    QJsonObject st = initial.isEmpty() ? CanaloEngine::instance().defaultState() : initial;
    st.insert(QStringLiteral("title"), tr("CANALINA %1 - L=%2mm H=%3mm").arg(id, num(dv(st, "width", 200), 0), num(dv(st, "height", 75), 0)));
    st = CanaloEngine::instance().calculate(st);
    if (linkSelected) {
        std::vector<std::unique_ptr<Entity>> tagged;
        std::vector<EntityId> replaced;
        for (EntityId eid : drawing_->selection())
            if (const Entity* e = doc.find(eid)) {
                auto copy = e->clone();
                copy->setCanaloTray(id.toStdString());
                tagged.push_back(std::move(copy));
                replaced.push_back(eid);
            }
        if (!tagged.empty())
            doc.commit(std::move(tagged), replaced);
    }
    trayId_ = id;
    store(st, tr("Canalina %1 creata.").arg(id));
    return id;
}

void CanaloPanel::syncOwnCables()
{
    CanaloEngine& engine = CanaloEngine::instance();
    engine.setOwnCables(ownCablesJson());
    db_ = engine.database().value(QStringLiteral("cables")).toArray();
    // Il filtro della categoria e della ricerca.
    dbFiltered_.clear();
    const QString needle = search_.trimmed().toLower();
    for (int i = 0; i < db_.size(); ++i) {
        const QJsonObject c = db_[i].toObject();
        const QString cat = c.value(QStringLiteral("cat")).toString();
        if (!category_.isEmpty() && cat != category_)
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

std::vector<EntityId> CanaloPanel::trayEntities(const QString& id, bool sectionOnly) const
{
    std::vector<EntityId> out;
    if (!drawing_)
        return out;
    const Document& doc = drawing_->document();
    for (const auto& e : doc.entities()) {
        if (e->canaloTray() != id.toStdString())
            continue;
        if (sectionOnly) {
            const int li = e->layer();
            const auto& layers = doc.layers();
            const std::string name = li >= 0 && li < static_cast<int>(layers.size()) ? layers[static_cast<std::size_t>(li)].name : "";
            if (!sectionLayers().count(name))
                continue;
        }
        out.push_back(e->id());
    }
    return out;
}

double CanaloPanel::drawingUnit() const
{
    if (!drawing_)
        return 1.0;
    const Document& doc = drawing_->document();
    const int units = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
    return unitsFactor(4, units);
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
    CanaloEngine::instance().calculate(st, &metrics_);
    metricsFor_ = key;
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
    QVariantList out;
    if (trayId_.isEmpty() || metrics_.isEmpty())
        return out;
    const QJsonObject st = state();
    const QJsonObject& m = metrics_;
    auto row = [&](const QString& label, const QString& value, int level = 0) {
        out.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}, {QStringLiteral("level"), level}});
    };
    row(tr("Canalina"), tr("%1 %2 × %3 mm, sp. %4 mm").arg(trayTypeName(sv(st, "trayType")), num(dv(st, "width"), 0), num(dv(st, "height"), 0),
                                                           num(dv(st, "thk"))));
    row(tr("Riempimento"), tr("%1% (riserva +%2%)").arg(num(dv(m, "fillPct")), num(dv(m, "sparePct"))));
    row(tr("Riduzione portata"), tr("Kr = %1 (fascio %2 × strati %3 [%4] × posa %5)")
                                     .arg(num(dv(m, "kr"), 2), num(dv(m, "kFascio"), 2), num(dv(m, "kStrati"), 2))
                                     .arg(dv(m, "nLayers", 1))
                                     .arg(num(dv(m, "kPosa"), 2)));
    const bool seg = m.value(QStringLiteral("segConform")).toBool(true);
    row(tr("Segregazione 528.1"), (seg ? tr("CONFORME") : tr("NON CONFORME")) + QStringLiteral(" — ") + sv(m, "segDetail"), seg ? 1 : 2);
    row(tr("Peso"), tr("%1 kg/m").arg(num(dv(m, "totalKgM"), 2)));
    if (sv(st, "support") == QLatin1String("none") || sv(st, "support").isEmpty()) {
        row(tr("Staffaggio"), tr("Nessun supporto: nessuna verifica di carico"));
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
        createTray({}, false);
    const QJsonObject st = CanaloEngine::instance().addCable(state(), sv(item, "id"), std::clamp(quantity, 1, 30), trefoil);
    store(st, tr("%1: aggiunto %2 × %3 %4.").arg(trayId_).arg(std::clamp(quantity, 1, 30)).arg(sv(item, "type"), sv(item, "form")));
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
    store(CanaloEngine::instance().calculate(st), tr("%1: cavo %2 modificato.").arg(trayId_, code));
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
    store(CanaloEngine::instance().calculate(st), tr("%1: tolto il cavo %2.").arg(trayId_, code));
}

void CanaloPanel::clearCables()
{
    if (trayId_.isEmpty())
        return;
    QJsonObject st = state();
    st.insert(QStringLiteral("cables"), QJsonArray());
    cableSelected_ = -1;
    store(CanaloEngine::instance().calculate(st), tr("%1: tolti tutti i cavi.").arg(trayId_));
}

// ---------------------------------------------------------------- canaline e disegno

void CanaloPanel::newTray()
{
    if (!drawing_)
        return;
    // Una sezione disegnata selezionata (cerchi dei cavi o un profilo di misura da sezione): misure e cavi con
    // l'importazione di CanaloCAD. Altrimenti (percorso in pianta, niente) solo il collegamento.
    if (!drawing_->selection().empty()) {
        const canalo::Drawn drawn = drawnForCanalo(drawing_->document(), drawing_->selection());
        BBox box;
        for (const auto& s : drawn.segments) {
            box.expand(s.a);
            box.expand(s.b);
        }
        const bool sectionLike = !drawn.circles.empty() || (box.isValid() && box.max.x - box.min.x <= 1250 && box.max.y - box.min.y <= 450);
        if (sectionLike) {
            trayId_.clear();
            importSelection();
            return;
        }
    }
    createTray({}, true);
}

void CanaloPanel::linkSelection()
{
    if (!drawing_ || trayId_.isEmpty() || drawing_->selection().empty()) {
        emit message(tr("CANALINA: scegliere una canalina e selezionare gli oggetti da collegare (sezione o percorso in pianta)."));
        return;
    }
    Document& doc = drawing_->document();
    std::vector<std::unique_ptr<Entity>> tagged;
    std::vector<EntityId> replaced;
    for (EntityId eid : drawing_->selection())
        if (const Entity* e = doc.find(eid)) {
            auto copy = e->clone();
            copy->setCanaloTray(trayId_.toStdString());
            tagged.push_back(std::move(copy));
            replaced.push_back(eid);
        }
    doc.commit(std::move(tagged), replaced);
    drawing_->notifyChanged();
    emit message(tr("%n oggetti collegati alla canalina %1.", nullptr, static_cast<int>(replaced.size())).arg(trayId_));
    refresh();
}

bool CanaloPanel::importSelection()
{
    if (!drawing_ || drawing_->selection().empty()) {
        emit message(tr("CANALINA: selezionare la sezione disegnata (cerchi dei cavi, bordo della canalina, sigle)."));
        return false;
    }
    const QString path = tempPath(QStringLiteral("CartesioCAD_sezione.dxf"));
    QString error;
    if (!exportForCanalo(drawing_->document(), drawing_->selection(), path, &error)) {
        emit message(tr("CANALINA: %1").arg(error));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit message(tr("CANALINA: file temporaneo non leggibile."));
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    QFile::remove(path);
    const QString name = QFileInfo(drawing_->displayName()).fileName();
    const QJsonObject base = trayId_.isEmpty() ? CanaloEngine::instance().defaultState() : state();
    const QJsonObject st = CanaloEngine::instance().importDxf(base, text, name);
    if (st.isEmpty()) {
        emit message(tr("CANALINA: %1").arg(CanaloEngine::instance().error()));
        return false;
    }
    if (trayId_.isEmpty()) {
        createTray(st, true);
    } else {
        // Gli oggetti della sezione entrano nella canalina corrente, che prende misure e cavi letti.
        linkSelection();
        store(st, tr("%1: sezione importata dal disegno.").arg(trayId_));
    }
    emit message(tr("%1: letti %n cavi, canalina %2 × %3 mm.", nullptr, static_cast<int>(st.value(QStringLiteral("cables")).toArray().size()))
                     .arg(trayId_, num(dv(st, "width"), 0), num(dv(st, "height"), 0)));
    return true;
}

void CanaloPanel::importFile(const QString& given)
{
    if (!drawing_)
        return;
    QString path = given;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, tr("Importa la sezione da un disegno"), QString(), tr("Disegni (*.dwg *.dxf)"));
    if (path.isEmpty())
        return;
    // Il disegno (anche DWG) letto da CartesioCAD e passato all'importazione di CanaloCAD come DXF.
    Document source;
    QString error;
    if (!DrawingWidget::readFile(path, source, &error)) {
        emit message(tr("CANALINA: %1").arg(error));
        return;
    }
    std::vector<EntityId> all;
    for (const auto& e : source.entities())
        if (e->space() == 0)
            all.push_back(e->id());
    const QString dxfPath = tempPath(QStringLiteral("CartesioCAD_import.dxf"));
    if (!exportForCanalo(source, all, dxfPath, &error)) {
        emit message(tr("CANALINA: %1").arg(error));
        return;
    }
    QFile file(dxfPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    QFile::remove(dxfPath);
    const QJsonObject base = trayId_.isEmpty() ? CanaloEngine::instance().defaultState() : state();
    const QJsonObject st = CanaloEngine::instance().importDxf(base, text, QFileInfo(path).fileName());
    if (st.isEmpty()) {
        emit message(tr("CANALINA: %1").arg(CanaloEngine::instance().error()));
        return;
    }
    if (trayId_.isEmpty())
        createTray(st, false);
    else
        store(st, QString());
    emit message(tr("%1: importati %n cavi da %2 (canalina %3 × %4 mm). Disegna sezione la mette nel disegno.", nullptr,
                    static_cast<int>(st.value(QStringLiteral("cables")).toArray().size()))
                     .arg(trayId_, QFileInfo(path).fileName(), num(dv(st, "width"), 0), num(dv(st, "height"), 0)));
}

void CanaloPanel::editTray()
{
    if (!drawing_)
        return;
    if (trayId_.isEmpty())
        createTray({}, false);
    QJsonObject st = state();
    QJsonObject tb = st.value(QStringLiteral("tb")).toObject();
    auto str = [&](const char* k) { return sv(st, k); };
    QString title = str("title"), planTag = str("planTag");
    double width = dv(st, "width", 200), height = dv(st, "height", 75), length = dv(st, "lengthM", 30), span = dv(st, "spanM", 1.5);
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

void CanaloPanel::autoSize()
{
    if (trayId_.isEmpty() || state().value(QStringLiteral("cables")).toArray().isEmpty()) {
        emit message(tr("CANALINA: prima scegliere i cavi."));
        return;
    }
    double reserve = 30.0;
    FormDialog dialog(tr("Dimensiona la canalina"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.note(tr("Larghezza più stretta (50, 100, 150, 200, 300, 400, 500, 600, 800, 1000 mm) con l'altezza attuale che lascia la "
                   "riserva di spazio indicata, come Dimensiona di CanaloCAD."));
    dialog.number(tr("Riserva"), 0, 80, 0, reserve, QStringLiteral(" %"));
    dialog.setSize(420, 220);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QJsonObject st = CanaloEngine::instance().autoSize(state(), reserve);
    store(st, tr("%1: larghezza %2 mm (riserva %3%).").arg(trayId_, num(dv(st, "width"), 0), num(reserve, 0)));
}

void CanaloPanel::drawSection()
{
    if (!drawing_)
        return;
    if (trayId_.isEmpty() || state().value(QStringLiteral("cables")).toArray().isEmpty()) {
        emit message(tr("CANALINA: prima scegliere i cavi della canalina."));
        return;
    }
    CanaloEngine& engine = CanaloEngine::instance();
    QJsonObject st = state();
    // La sezione della tavola di CanaloCAD al vero (scala 1:1): profilo, cavi, bandierine, quote, staffaggio.
    QJsonObject real = st;
    real.insert(QStringLiteral("scale"), QStringLiteral("1:1"));
    const QString path = tempPath(QStringLiteral("CartesioCAD_%1_sezione.dxf").arg(trayId_));
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            emit message(tr("CANALINA: file temporaneo non scrivibile."));
            return;
        }
        file.write(engine.dxfBytes(real, sv(st, "title")));
    }
    Document source;
    QString error;
    const bool read = DrawingWidget::readFile(path, source, &error);
    QFile::remove(path);
    if (!read) {
        emit message(tr("CANALINA: %1").arg(error));
        return;
    }
    Document& doc = drawing_->document();
    const double unit = drawingUnit();
    // Dove: al posto della sezione di prima (stesso punto), altrimenti a destra del disegno.
    const std::vector<EntityId> old = trayEntities(trayId_, true);
    const QJsonObject where = st.value(QStringLiteral("cartesioSection")).toObject();
    Vec2 origin;
    if (!old.empty() && where.contains(QStringLiteral("x"))) {
        origin = {dv(where, "x"), dv(where, "y")};
    } else {
        const BBox existing = doc.extents();
        origin = existing.isValid() ? Vec2{existing.max.x + 300.0 * unit, existing.min.y} : Vec2{0.0, 0.0};
    }
    const Affine place = Affine::translation(origin) * Affine::scaling({0.0, 0.0}, unit);
    const ImportSource src = ImportSource::from(source);
    Importer importer(doc, src);
    std::vector<std::unique_ptr<Entity>> added;
    std::vector<const Entity*> raw;
    for (const auto& e : source.entities()) {
        const int li = e->layer();
        const std::string layer = li >= 0 && li < static_cast<int>(source.layers().size()) ? source.layers()[static_cast<std::size_t>(li)].name : "";
        if (!sectionLayers().count(layer) || e->space() != 0)
            continue;
        auto copy = importer.copy(*e);
        copy->transform(place);
        copy->setCanaloTray(trayId_.toStdString());
        raw.push_back(copy.get());
        added.push_back(std::move(copy));
    }
    if (added.empty()) {
        emit message(tr("CANALINA: la tavola di CanaloCAD non ha la sezione."));
        return;
    }
    doc.commit(std::move(added), old);
    st.insert(QStringLiteral("cartesioSection"), QJsonObject{{QStringLiteral("x"), origin.x}, {QStringLiteral("y"), origin.y}});
    std::vector<EntityId> ids;
    BBox box;
    for (const Entity* e : raw) {
        ids.push_back(e->id());
        box.expand(e->bounds());
    }
    store(st, old.empty() ? tr("%1: sezione disegnata al vero (si sposta con MOVE; Ctrl+Z la toglie).").arg(trayId_)
                          : tr("%1: sezione ridisegnata con i dati attuali.").arg(trayId_));
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(CanaloEngine::instance().dxfBytes(state(), sv(state(), "title")));
    file.close();
    Document& doc = drawing_->document();
    if (doc.findBlock(name.toStdString())) {
        Document source;
        QString error;
        if (DrawingWidget::readFile(path, source, &error)) {
            const int from = source.insUnits() > 0 ? source.insUnits() : appOptions().insertSourceUnits;
            const int to = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
            importDrawingAsBlock(doc, source, name.toStdString(), unitsFactor(from, to));
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
        emit message(tr("Tavola di CanaloCAD (%1): indicare il punto d'inserimento.").arg(name));
    QFile::remove(path);
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
    const int to = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
    importDrawingAsBlock(doc, source, name.toStdString(), unitsFactor(from, to));
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
    const QString title = sv(state(), "title");
    QString name = title;
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
    const QJsonObject base = dbItem(dbSelected_);
    const QString id = sv(base, "id");
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
        QString id = parts.value(0).trimmed();
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
    st = engine.autoSize(st, 30.0);
    trayId_.clear();
    createTray(st, false);
    drawSection();
    return true;
}

} // namespace cartesio
