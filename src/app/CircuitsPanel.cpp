#include "app/CircuitsPanel.h"

#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/PdfWords.h"
#include "app/QmlUi.h"
#include "app/Theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QQmlContext>
#include <QSettings>
#include <QTimer>

#include <map>
#include <set>

namespace cartesio {

namespace {

QSettings store()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

} // namespace

CircuitsPanel::CircuitsPanel(QWidget* parent) : QQuickWidget(parent)
{
    status_ = tr("Nessuno schema importato.");
    board_ = new ComboModel(200, this);
    board_->addItem(tr("Tutti i quadri"), QString());
    kind_ = new ComboModel(110, this);
    kind_->addItem(tr("Tutti"), -1);
    kind_->addItem(tr("Luce"), static_cast<int>(CircuitKind::Lighting));
    kind_->addItem(tr("FM"), static_cast<int>(CircuitKind::Power));
    kind_->addItem(tr("Utenze"), static_cast<int>(CircuitKind::Other));
    const auto choose = [this](ComboModel* combo) {
        return [this, combo](int index) {
            combo->setCurrentIndex(index);
            codeFilter_.clear();
            fillTable();
        };
    };
    connect(board_, &ComboModel::activated, this, choose(board_));
    connect(kind_, &ComboModel::activated, this, choose(kind_));

    QSettings s = store();
    pointLayers_ = s.value(QStringLiteral("Circuits/pointLayers"), QStringLiteral("E-ELEC-FIXT")).toString();
    letterLayers_ = s.value(QStringLiteral("Circuits/letterLayers"), QStringLiteral("E-ELEC-FIXT-IDEN")).toString();
    tagLayers_ = s.value(QStringLiteral("Circuits/tagLayers"), QStringLiteral("E-ELEC-CIRC")).toString();

    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    rootContext()->setContextProperty(QStringLiteral("panel"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/CircuitsPanel.qml")));
    setMinimumWidth(360);
}

QObject* CircuitsPanel::boardCombo() const
{
    return board_;
}

QObject* CircuitsPanel::kindCombo() const
{
    return kind_;
}

void CircuitsPanel::setDrawing(DrawingWidget* drawing)
{
    drawing_ = drawing;
    refresh();
}

void CircuitsPanel::refresh()
{
    if (!isVisible())
        return;
    fillTable();
    if (tab_ == 2)
        fillLegend();
}

CircuitScanOptions CircuitsPanel::scanOptions() const
{
    CircuitScanOptions o;
    o.pointLayers = pointLayers_.toStdString();
    o.letterLayers = letterLayers_.toStdString();
    o.tagLayers = tagLayers_.toStdString();
    return o;
}

// ---------------------------------------------------------------- dal QML

void CircuitsPanel::chooseFile()
{
    QTimer::singleShot(0, this, [this] {
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
            QMessageBox::critical(this, tr("Circuiti"), error);
    });
}

void CircuitsPanel::verify()
{
    const QString summary = runCheck();
    emit message(summary);
    setTab(1);
}

void CircuitsPanel::setTab(int tab)
{
    if (tab_ == tab)
        return;
    tab_ = tab;
    emit changed();
    if (tab_ == 2)
        fillLegend();
}

void CircuitsPanel::setSearch(const QString& text)
{
    search_ = text.trimmed();
    codeFilter_.clear();
    fillTable();
}

void CircuitsPanel::selectCircuit(int row)
{
    selected_ = row;
    emit tableChanged();
}

void CircuitsPanel::openCircuit(int row)
{
    if (!drawing_ || row < 0 || row >= static_cast<int>(rowCodes_.size()))
        return;
    const std::string& code = rowCodes_[static_cast<std::size_t>(row)];
    std::vector<EntityId> ids;
    for (const CircuitTag& t : findCircuitTags(drawing_->document(), scanOptions()))
        if (t.code == code)
            ids.push_back(t.id);
    if (ids.empty())
        emit message(tr("%1 non compare in pianta.").arg(q(code)));
    showEntities(ids);
}

void CircuitsPanel::toggleGroup(int group)
{
    if (group < 0 || group >= checkGroups_.size())
        return;
    QVariantMap g = checkGroups_[group].toMap();
    g[QStringLiteral("expanded")] = !g.value(QStringLiteral("expanded")).toBool();
    checkGroups_[group] = g;
    emit checkChanged();
}

void CircuitsPanel::openCheckItem(int group, int item)
{
    if (group < 0 || group >= static_cast<int>(checkIds_.size()))
        return;
    const auto& ids = checkIds_[static_cast<std::size_t>(group)];
    if (item >= 0 && item < static_cast<int>(ids.size()) && ids[static_cast<std::size_t>(item)] != 0)
        showEntities({ids[static_cast<std::size_t>(item)]});
}

// Parole chiave corrette dall'utente: si salvano per questo disegno e si ricalcola.
void CircuitsPanel::editKeywords(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(matches_.size()))
        return;
    const LegendMatch& m = matches_[static_cast<std::size_t>(row)];
    QSettings s = store();
    const QString key = QStringLiteral("CircuitsLegend/%1/%2").arg(legendKey(), q(m.letter));
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || (m.source != LegendSource::User && trimmed == q(m.keywords)))
        s.remove(key);
    else
        s.setValue(key, trimmed);
    QTimer::singleShot(0, this, [this] { fillLegend(); });
}

void CircuitsPanel::openLegend(int row, int column)
{
    if (row < 0 || row >= static_cast<int>(matches_.size()))
        return;
    const LegendMatch& m = matches_[static_cast<std::size_t>(row)];
    if (column == 0 || column == 4) {   // i punti con questa lettera
        if (!drawing_)
            return;
        std::vector<EntityId> ids;
        for (const CircuitPoint& p : findCircuitPoints(drawing_->document(), scanOptions()))
            if (p.letter == m.letter)
                ids.push_back(p.id);
        showEntities(ids);
    } else if (column == 3 && !m.circuits.empty()) {   // i circuiti corrispondenti
        search_.clear();
        board_->setCurrentIndex(0);
        codeFilter_ = std::set<std::string>(m.circuits.begin(), m.circuits.end());
        fillTable();
        setTab(0);
        emit changed();
        emit message(tr("Circuiti per la lettera %1 (%2): %3").arg(q(m.letter), q(m.keywords)).arg(m.circuits.size()));
    }
}

void CircuitsPanel::assignSelected()
{
    const QString code = selectedCode();
    QTimer::singleShot(0, this, [this, code] {
        if (code.isEmpty()) {
            QMessageBox::information(this, tr("Circuiti"), tr("Scegliere un circuito nell'elenco."));
            return;
        }
        if (assignToSelection(code) == 0)
            QMessageBox::information(this, tr("Circuiti"), tr("Selezionare nel disegno i punti a cui assegnare %1.").arg(code));
    });
}

void CircuitsPanel::setLayers(const QString& points, const QString& letters, const QString& tags)
{
    pointLayers_ = points.trimmed();
    letterLayers_ = letters.trimmed();
    tagLayers_ = tags.trimmed();
    QSettings st = store();
    st.setValue(QStringLiteral("Circuits/pointLayers"), pointLayers_);
    st.setValue(QStringLiteral("Circuits/letterLayers"), letterLayers_);
    st.setValue(QStringLiteral("Circuits/tagLayers"), tagLayers_);
    emit changed();
    refresh();
}

// ---------------------------------------------------------------- schema, tabella, verifica

bool CircuitsPanel::importSchema(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    const QByteArray bytes = file.readAll();
    std::vector<PdfWord> words;
    std::string failure;
    if (!readPdfWords(bytes.toStdString(), words, &failure)) {
        if (error)
            *error = q(failure);
        return false;
    }
    std::vector<Circuit> circuits = parseSingleLineDiagram(words);
    if (circuits.empty()) {
        if (error)
            *error = tr("Nel PDF non ci sono schemi unifilari riconoscibili (manca la riga \"Sigla utenza\").");
        return false;
    }
    useSchema(std::move(circuits), path);
    emit schemaImported(circuits_, path);
    return true;
}

void CircuitsPanel::useSchema(std::vector<Circuit> circuits, const QString& path)
{
    circuits_ = std::move(circuits);
    source_ = path;
    std::set<std::string> boards;
    for (const Circuit& c : circuits_)
        boards.insert(c.board);
    board_->clear();
    board_->addItem(tr("Tutti i quadri"), QString());
    board_->addItem(tr("Quadri presenti in pianta"), QStringLiteral("*"));
    for (const std::string& b : boards)
        board_->addItem(q(b), q(b));
    if (drawing_ && !findCircuitTags(drawing_->document(), scanOptions()).empty())
        board_->setCurrentIndex(1);
    status_ = tr("%1 circuiti di %2 quadri da %3").arg(circuits_.size()).arg(boards.size()).arg(QFileInfo(path).fileName());
    emit changed();
    codeFilter_.clear();
    fillTable();
    fillLegend();
    emit message(tr("Schema unifilare: %1 circuiti di %2 quadri.").arg(circuits_.size()).arg(boards.size()));
}

void CircuitsPanel::fillTable()
{
    std::map<std::string, int> inPlan;
    if (drawing_)
        for (const CircuitTag& t : findCircuitTags(drawing_->document(), scanOptions()))
            ++inPlan[t.code];
    const QString board = board_->currentData().toString();
    std::set<std::string> planBoards;   // "Quadri presenti in pianta"
    for (const auto& [code, count] : inPlan)
        planBoards.insert(code.substr(0, code.rfind(" C-")));
    const int kind = kind_->currentData().toInt();
    const QString previous = selectedCode();
    circuitRows_.clear();
    rowCodes_.clear();
    selected_ = -1;
    for (const Circuit& c : circuits_) {
        if (!codeFilter_.empty() && !codeFilter_.count(c.code))
            continue;
        if (board == QLatin1String("*") ? !planBoards.count(c.board) : (!board.isEmpty() && q(c.board) != board))
            continue;
        if (kind >= 0 && static_cast<int>(c.kind) != kind)
            continue;
        if (!search_.isEmpty() && !q(c.code).contains(search_, Qt::CaseInsensitive)
            && !q(c.description).contains(search_, Qt::CaseInsensitive))
            continue;
        const int count = inPlan.count(c.code) ? inPlan[c.code] : 0;
        const QStringList cells = {q(c.code), QString::fromLatin1(circuitKindName(c.kind)), q(c.description),
                                   QString::number(c.powerKw, 'g', 4), q(c.phase), q(c.breaker), q(c.cable),
                                   count ? QString::number(count) : QString()};
        if (q(c.code) == previous)
            selected_ = static_cast<int>(rowCodes_.size());
        rowCodes_.push_back(c.code);
        circuitRows_.append(QVariantMap{{QStringLiteral("cells"), cells},
                                        {QStringLiteral("dim"), count == 0},
                                        {QStringLiteral("tip"), tr("Pagina %1 dello schema").arg(c.page)}});
    }
    emit tableChanged();
}

QString CircuitsPanel::selectedCode() const
{
    return selected_ >= 0 && selected_ < static_cast<int>(rowCodes_.size()) ? q(rowCodes_[static_cast<std::size_t>(selected_)])
                                                                            : QString();
}

void CircuitsPanel::showEntities(const std::vector<EntityId>& ids)
{
    if (!drawing_ || ids.empty())
        return;
    drawing_->setSelection(ids);
    BBox box;
    for (EntityId id : ids)
        if (const Entity* e = drawing_->document().find(id))
            box.expand(e->bounds());
    if (box.isValid()) {
        const double margin = std::max({box.width(), box.height(), 2000.0}) * 0.5;
        box.min = box.min - Vec2{margin, margin};
        box.max = box.max + Vec2{margin, margin};
        drawing_->view()->zoomWindow(box);
    }
}

QString CircuitsPanel::runCheck()
{
    checkGroups_.clear();
    checkIds_.clear();
    if (!drawing_ || circuits_.empty()) {
        emit checkChanged();
        return !drawing_ ? tr("Nessun disegno aperto.") : tr("Importare prima lo schema unifilare (PDF).");
    }
    const CircuitCheck c = checkCircuits(drawing_->document(), circuits_, scanOptions());
    const auto group = [this](const QString& title, const QVariantList& items, std::vector<EntityId> ids) {
        checkGroups_.append(QVariantMap{{QStringLiteral("title"), title},
                                        {QStringLiteral("count"), items.size()},
                                        {QStringLiteral("expanded"), !items.isEmpty() && items.size() <= 40},
                                        {QStringLiteral("items"), items}});
        checkIds_.push_back(std::move(ids));
    };
    const auto item = [](const QString& text, const QString& detail) {
        return QVariant(QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("detail"), detail}});
    };
    QVariantList unknown;
    std::vector<EntityId> unknownIds;
    for (const CircuitTag& t : c.unknownTags) {
        unknown.append(item(q(t.code), tr("doppio clic per vederla")));
        unknownIds.push_back(t.id);
    }
    group(tr("Sigle non presenti nello schema"), unknown, unknownIds);
    std::map<std::string, const Circuit*> byCode;
    for (const Circuit& ci : circuits_)
        byCode[ci.code] = &ci;
    QVariantList missing;
    for (const std::string& code : c.circuitsWithoutTags)
        missing.append(item(q(code), byCode.count(code) ? q(byCode[code]->description) : QString()));
    group(tr("Circuiti dei quadri in pianta senza sigla"), missing, std::vector<EntityId>(missing.size(), 0));
    QVariantList points;
    std::vector<EntityId> pointIds;
    for (const CircuitPoint& p : c.pointsWithoutTags) {
        points.append(item(q(p.family), p.letter.empty() ? QString() : tr("lettera %1").arg(q(p.letter))));
        pointIds.push_back(p.id);
    }
    group(tr("Punti senza sigla vicina"), points, pointIds);
    emit checkChanged();
    fillLegend();
    return tr("Verifica circuiti: %1 sigle trovate nello schema, %2 sigle sconosciute, %3 circuiti senza sigla, "
              "%4 punti senza sigla.")
        .arg(c.matchedTags)
        .arg(c.unknownTags.size())
        .arg(c.circuitsWithoutTags.size())
        .arg(c.pointsWithoutTags.size());
}

int CircuitsPanel::assignToSelection(const QString& code)
{
    if (!drawing_ || drawing_->selection().empty())
        return 0;
    Document& doc = drawing_->document();
    const std::vector<CircuitPoint> points = pointsFromObjects(doc, drawing_->selection());
    if (points.empty())
        return 0;
    CircuitTagStyle style;
    const std::string layers = tagLayers_.toStdString();
    style.layer = layers.substr(0, layers.find(';'));
    if (style.layer.empty())
        style.layer = "E-ELEC-CIRC";
    style.height = usualTagHeight(findCircuitTags(doc, scanOptions()), 120.0);
    style.offset = style.height * 2.0;
    doc.commit(makeCircuitTags(doc, points, code.toStdString(), style));
    drawing_->setSelection({});
    drawing_->notifyChanged();
    emit message(tr("%1 assegnato a %2 punti.").arg(code).arg(points.size()));
    fillTable();
    return static_cast<int>(points.size());
}

// ---------------------------------------------------------------- legenda

QString CircuitsPanel::legendKey() const
{
    // Le parole chiave valgono per il disegno: chiave dal percorso del file (o dal nome, se non salvato).
    const QString id = drawing_ ? (drawing_->filePath().isEmpty() ? drawing_->displayName()
                                                                   : QFileInfo(drawing_->filePath()).absoluteFilePath().toLower())
                                : QString();
    return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
}

std::map<std::string, std::string> CircuitsPanel::userKeywords() const
{
    std::map<std::string, std::string> out;
    QSettings s = store();
    s.beginGroup(QStringLiteral("CircuitsLegend/") + legendKey());
    for (const QString& letter : s.childKeys())
        out[letter.toStdString()] = s.value(letter).toString().toStdString();
    return out;
}

void CircuitsPanel::showLegend()
{
    tab_ = 2;
    emit changed();
    fillLegend();
}

QString CircuitsPanel::fillLegend()
{
    legendRows_.clear();
    matches_.clear();
    if (!drawing_) {
        emit legendChanged();
        return tr("Nessun disegno aperto.");
    }
    matches_ = interpretLegend(drawing_->document(), circuits_, scanOptions(), userKeywords());
    std::map<std::string, const Circuit*> byCode;
    for (const Circuit& c : circuits_)
        byCode.emplace(c.code, &c);
    QStringList report;
    int found = 0;
    for (const LegendMatch& m : matches_) {
        found += m.legend.empty() ? 0 : 1;
        QStringList notes;
        if (!m.warning.empty())
            notes << q(m.warning);
        if (!m.outliers.empty()) {
            QStringList odd;
            for (const std::string& code : m.outliers)
                odd << q(code);
            notes << tr("sigle diverse vicino ai punti: %1").arg(odd.join(QStringLiteral(", ")));
        }
        if (m.source != LegendSource::None)
            notes << tr("da %1").arg(QString::fromLatin1(legendSourceName(m.source)));
        QStringList list;
        for (const std::string& code : m.circuits)
            list << q(code) + QStringLiteral("  ") + (byCode.count(code) ? q(byCode[code]->description) : QString());
        const QString circuits = circuits_.empty() ? tr("(manca lo schema)")
                                 : m.circuits.empty() ? QString()
                                 : m.circuits.size() == 1 ? q(m.circuits.front())
                                                          : tr("%1 circuiti").arg(m.circuits.size());
        const QString points = m.points ? tr("%1 (%2 con sigla)").arg(m.points).arg(m.tagged) : QString();
        const QStringList cells = {q(m.letter), m.legend.empty() ? tr("(non in legenda)") : q(m.legend),
                                   q(m.keywords), circuits, points, notes.join(QStringLiteral("; "))};
        const QStringList tips = {
            (notes.isEmpty() ? QString() : notes.join(QLatin1Char('\n')) + QStringLiteral("\n\n"))
                + tr("Doppio clic: seleziona i punti con questa lettera"),
            cells[1],
            tr("Parole che la descrizione del circuito nello schema deve contenere.\n"
               "Si possono correggere (doppio clic); vuoto = proposta automatica."),
            list.isEmpty() ? QString() : list.join(QStringLiteral("\n")) + tr("\n\nDoppio clic: elenco dei circuiti"),
            tr("Doppio clic: seleziona i punti con questa lettera"),
            cells[5]};
        legendRows_.append(QVariantMap{{QStringLiteral("cells"), cells},
                                       {QStringLiteral("tips"), tips},
                                       {QStringLiteral("dim"), m.points == 0},
                                       {QStringLiteral("attention"), !m.warning.empty() || !m.outliers.empty()}});
        if (m.points > 0 || !m.warning.empty())
            report << tr("  %1 = %2 -> [%3] %4 | %5 | %6")
                          .arg(q(m.letter), m.legend.empty() ? tr("(non in legenda)") : q(m.legend), q(m.keywords),
                               list.isEmpty() ? QStringLiteral("-") : list.join(QStringLiteral(", ")), points,
                               notes.join(QStringLiteral("; ")));
    }
    emit legendChanged();
    QString summary = found ? tr("Legenda: %1 lettere.").arg(found)
                            : tr("Legenda delle lettere non trovata nel disegno (righe come Tipo \"A\": descrizione).");
    if (!report.isEmpty())
        summary += QLatin1Char('\n') + report.join(QLatin1Char('\n'));
    return summary;
}

} // namespace cartesio
