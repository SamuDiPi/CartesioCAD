// Plugin CanaloCAD nella finestra principale: scheda "Impianto" della barra multifunzione, comandi
// CANALOCAD, CANALOINVIA, CANALOINS, CANALOULTIMA, CANALOVERIFICA (calcoli sulla sezione selezionata) e
// sorveglianza della cartella di scambio.
#include "app/MainWindow.h"
#include "app/MainView.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/CanaloPlugin.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/GuidePanel.h"
#include "app/PlantFile.h"
#include "app/PlantPanel.h"
#include "app/CanaloPanel.h"
#include "app/Icons.h"
#include "app/Ribbon.h"
#include "app/CanaloTab.h"
#include "app/WebView2Host.h"
#include "app/PlotEngine.h"
#include "app/Theme.h"
#include "core/Import.h"
#include "core/commands/CommandManager.h"

#include <QAction>
#include <QCryptographicHash>
#include <QDate>
#include <QBuffer>
#include <QClipboard>
#include <QMimeData>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QEventLoop>
#include <QJsonObject>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cartesio {

void MainWindow::setupCanalo(Ribbon* ribbon)
{
    auto* open = makeAction(tr("Apri\nCanaloCAD"), icon(Icon::Open), {},
                            tr("Apre CanaloCAD (progettazione canaline) in una scheda accanto ai disegni (su Windows; altrimenti nel "
                               "browser)\nComando: CANALOCAD"));
    auto* send = makeAction(tr("Invia sezione"), icon(Icon::SaveAs), {},
                            tr("Salva gli oggetti selezionati in un DXF da importare in CanaloCAD\nComando: CANALOINVIA"));
    auto* insert = makeAction(tr("Inserisci tavola"), icon(Icon::BlockInsert), {},
                              tr("Inserisce una tavola esportata da CanaloCAD (DXF)\nComando: CANALOINS"));
    auto* latest = makeAction(tr("Ultima tavola"), icon(Icon::BlockInsert), {},
                              tr("Inserisce l'ultima tavola esportata da CanaloCAD nella cartella di scambio\n"
                                 "Comando: CANALOULTIMA"));
    connect(open, &QAction::triggered, this, &MainWindow::canaloOpen);
    connect(send, &QAction::triggered, this, [this] { canaloSend(); });
    connect(insert, &QAction::triggered, this, [this] { canaloInsert(); });
    connect(latest, &QAction::triggered, this, &MainWindow::canaloInsertLatest);
    auto* trays = makeAction(tr("Canalina"), glyphIcon(QStringLiteral("Ø"), true), {},
                             tr("Finestra Canalina: database dei cavi, cavi e riepilogo dei calcoli di CanaloCAD della canalina "
                                "selezionata; sezione e tavola di CanaloCAD nel disegno\nComando: CANALINA"));
    connect(trays, &QAction::triggered, this, [this] {
        if (canalo_->isVisible())
            canalo_->hide();
        else
            canalo_->showWindow();
        canalo_->refresh();
    });
    auto* check = makeAction(tr("Verifica\nsezione"), glyphIcon(QStringLiteral("✓"), true), {},
                             tr("La sezione selezionata nella finestra Canalina: misure e cavi letti dal disegno (cerchi = cavi, "
                                "linee = canalina, testi = sigle) e calcoli di CanaloCAD\nComando: CANALOVERIFICA"));
    connect(check, &QAction::triggered, this, [this] { canaloCheck(true); });
    drawingActions_ << send << insert << latest << check << trays;
    RibbonPanel* panel = ribbon->addPanel(tr("Impianto"), tr("CanaloCAD"));
    panel->addLargeButton(trays);
    panel->addLargeButton(check);
    panel->addLargeButton(open);
    panel->addSmallColumn({send, insert, latest});

    auto* circuits = makeAction(tr("Circuiti"), icon(Icon::ListIcon), {},
                                tr("Schema unifilare in PDF: verifica e assegnazione delle sigle dei circuiti\n"
                                   "Comando: CIRCUITI"));
    connect(circuits, &QAction::triggered, this, [this] {
        showPanel(MainView::Circuits);
    });
    RibbonPanel* electric = ribbon->addPanel(tr("Impianto"), tr("Impianti elettrici"));
    electric->addLargeButton(circuits);

    // Impianto: database di progetto (circuiti, tratte, cavi) e verifiche di tubi, canali e passerelle.
    auto* plantAct = makeAction(tr("Impianto"), icon(Icon::ListIcon), {},
                                tr("File di progetto dell'impianto condiviso dai disegni: circuiti (dallo schema in PDF o a mano), "
                                   "tratte di tubi, canali e passerelle, cavi propri\nComando: IMPIANTO"));
    connect(plantAct, &QAction::triggered, this, [this] { showPanel(MainView::Plant, !panelShown(MainView::Plant)); });
    auto* runAct = makeAction(tr("Nuova tratta"), glyphIcon(QStringLiteral("\u2550")), {},
                              tr("Le linee o polilinee selezionate diventano una tratta (tubo, canale o passerella) con i suoi "
                                 "circuiti\nComando: TRATTA"));
    connect(runAct, &QAction::triggered, this, [this] { plantNewRun(); });
    auto* checkPlantAct = makeAction(tr("Verifica impianto"), glyphIcon(QStringLiteral("\u2713"), true), {},
                                     tr("Verifica tutte le tratte: riempimento, segregazione, staffaggio\nComando: VERIFICAIMPIANTO"));
    connect(checkPlantAct, &QAction::triggered, this, [this] { plantCheck(); });
    drawingActions_ << runAct << checkPlantAct;
    electric->addSmallColumn({plantAct, runAct, checkPlantAct});
    auto* frontAct = makeAction(tr("Fronte quadro"), glyphIcon(QStringLiteral("▦")), {},
                                tr("Dallo schema unifilare in PDF il fronte dei quadri con ingombri, carpenteria, grado IP e "
                                   "forma, più la distinta CSV e il file IFC per il BIM\nComando: FRONTEQUADRO"));
    connect(frontAct, &QAction::triggered, this, [this] { showSwitchboardFront(); });
    electric->addLargeButton(frontAct);
    auto* libraryAct = makeAction(tr("Importa libreria"), glyphIcon(QStringLiteral("⭳")), {},
                                  tr("Dalla libreria di blocchi del costruttore installata su questo PC i blocchi per il "
                                     "fronte quadro, in mm e senza marchi, nella tua cartella, con i cataloghi aggiornati\n"
                                     "Comando: IMPORTALIBRERIA"));
    connect(libraryAct, &QAction::triggered, this, [this] { showLibraryImport(); });
    electric->addLargeButton(libraryAct);
}

void MainWindow::canaloOpen()
{
    const QString html = canaloHtmlPath();
    if (html.isEmpty()) {
        QMessageBox::information(this, tr("CanaloCAD"),
                                 tr("Canalo.html non trovato: manca la copia in plugins\\CanaloCAD accanto a "
                                    "CartesioCAD.exe. Indicarne il percorso nelle Opzioni (scheda Plugin)."));
        showOptions(6);   // scheda Plugin
        return;
    }
    if (canaloEmbedded()) {
        canaloShowTab();
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(html));
    print(tr("CanaloCAD aperto nel browser. Le tavole esportate in DXF in %1 si inseriscono con CANALOULTIMA.")
              .arg(QDir::toNativeSeparators(canaloExchangeFolder())));
}

bool MainWindow::canaloSend(const QString& target)
{
    DrawingWidget* d = current();
    if (!d)
        return false;
    const bool fromScript = !target.isEmpty();
    QString path = target;
    if (path.isEmpty()) {
        const QString base = QFileInfo(d->displayName()).completeBaseName();
        path = canaloExchangeFolder() + QStringLiteral("/") + base + QStringLiteral("_per_CanaloCAD.dxf");
    }
    QString error;
    if (!exportForCanalo(d->document(), d->selection(), path, &error)) {
        if (fromScript)
            print(tr("CANALOINVIA: %1").arg(error));
        else
            QMessageBox::information(this, tr("CanaloCAD"), error);
        return false;
    }
    // Scheda CanaloCAD: la sezione si apre direttamente nella pagina, niente percorsi da incollare.
    if (!fromScript && canaloEmbedded() && !canaloHtmlPath().isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QString name = QFileInfo(path).fileName();
            const QString data = QString::fromLatin1(file.readAll().toBase64());
            canaloFrom_ = d;
            canaloShowTab([this, name, data] {
                canaloTab_->host()->runAsync(
                    QStringLiteral("window.CanaloHost.importDxf(%1, %2)")
                        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact)).mid(1).chopped(1),
                             QLatin1Char('"') + data + QLatin1Char('"')),
                    [this, name](const QJsonValue& value, const QString& error) {
                        if (!error.isEmpty())
                            print(tr("CanaloCAD: sezione %1 non importata: %2").arg(name, error));
                        else
                            print(tr("Sezione aperta in CanaloCAD: %1 cavi nella scheda \"%2\". Per riportare la tavola nel "
                                     "disegno: Esporta > Inserisci nel disegno.")
                                      .arg(value.toObject().value(QStringLiteral("cavi")).toInt())
                                      .arg(value.toObject().value(QStringLiteral("titolo")).toString()));
                    });
            });
            return true;
        }
    }
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    print(tr("Sezione per CanaloCAD salvata in %1 (percorso copiato negli appunti): in CanaloCAD usare "
             "Importa DWG/DXF e incollare il percorso.")
              .arg(QDir::toNativeSeparators(path)));
    if (!fromScript && !canaloHtmlPath().isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(canaloHtmlPath()));
    return true;
}

void MainWindow::wireCanalo()
{
    canalo_->setPlant(plant_);
    connect(canalo_, &CanaloPanel::message, this, &MainWindow::print);
    connect(canalo_, &CanaloPanel::guideRequested, this, [this] {
        guide_->openCommand(QStringLiteral("CANALINA"));
        showPanel(MainView::Guide);
    });
}

// CANALOVERIFICA: la finestra Canalina con la canalina selezionata; una sezione disegnata non ancora collegata si
// importa (misure e cavi letti dal disegno con l'importazione di CanaloCAD). Negli script (-CANALOVERIFICA,
// -CANALOTAVOLA) senza finestre: i risultati nella riga di comando, con la tavola a destra della sezione.
void MainWindow::canaloCheck(bool askData, bool sheet)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    if (askData)
        canalo_->showWindow();
    canalo_->setDrawing(d);
    bool linked = false;
    for (EntityId id : d->selection())
        if (const Entity* e = d->document().find(id); e && !e->canaloTray().empty())
            linked = true;
    if (!linked) {
        if (d->selection().empty()) {
            if (!askData)
                print(tr("CANALOVERIFICA: selezionare la sezione della canalina (i cavi sono i cerchi, la canalina le linee del "
                         "bordo, le sigle i testi) e ripetere il comando."));
            return;
        }
        if (!canalo_->importSelection())
            return;
    }
    canalo_->refresh();
    ++canaloChecks_;
    if (askData)
        return;
    for (const QString& line : canalo_->reportLines())
        print(line);
    if (sheet) {
        BBox box;
        for (EntityId id : d->selection())
            if (const Entity* e = d->document().find(id))
                box.expand(e->bounds());
        const double unit = 0.001 / std::max(1e-12, plantMetersPerUnit(d->document()));   // unità di disegno per mm
        canalo_->insertSheetAt(box.isValid() ? Vec2{box.max.x + 50.0 * unit, box.min.y} : Vec2{0.0, 0.0});
        print(tr("Tavola di CanaloCAD inserita accanto alla sezione (Ctrl+Z la toglie)."));
    }
}

// CANALOSEZIONE: la finestra Canalina (database dei cavi). Negli script -CANALOSEZIONE FG16_4G10*2,DATA_CAT6*4: canalina
// nuova con quei cavi, dimensionata (riserva 30%) e disegnata.
void MainWindow::canaloCompose(const QString& scriptCables)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    canalo_->setDrawing(d);
    if (scriptCables.isEmpty()) {
        canalo_->showWindow();
        canalo_->refresh();
        return;
    }
    QString error;
    if (!canalo_->composeFromIds(scriptCables, &error)) {
        print(tr("-CANALOSEZIONE: %1").arg(error));
        return;
    }
    ++canaloChecks_;
    for (const QString& line : canalo_->reportLines())
        print(line);
}

void MainWindow::plantNewRun()
{
    showPanel(MainView::Plant);
    if (!plant_->hasProject()) {
        print(tr("TRATTA: prima creare o aprire il progetto dell'impianto (tavolozza Impianto)."));
        return;
    }
    plant_->setTab(1);
    plant_->addItem();
}

void MainWindow::plantCheck()
{
    showPanel(MainView::Plant);
    if (!plant_->hasProject()) {
        print(tr("VERIFICAIMPIANTO: prima creare o aprire il progetto dell'impianto (tavolozza Impianto)."));
        return;
    }
    plant_->verify();
}

bool MainWindow::canaloInsert(const QString& source)
{
    DrawingWidget* d = current();
    if (!d)
        return false;
    QString path = source;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, tr("Inserisci tavola CanaloCAD"), canaloExchangeFolder(),
                                            tr("Tavole DXF (*.dxf);;Disegni (*.dxf *.dwg)"));
        if (path.isEmpty())
            return false;
    }
    if (!isCanaloDrawing(path))
        print(tr("%1 non sembra una tavola di CanaloCAD (mancano i layer Canalina e Cavi): la inserisco comunque.")
                  .arg(QFileInfo(path).fileName()));
    QString error;
    if (!d->insertFile(path, &error)) {
        print(tr("Inserimento non riuscito: %1").arg(error));
        return false;
    }
    canaloSeen_ = QFileInfo(path).lastModified();
    return true;
}

void MainWindow::canaloInsertLatest()
{
    const QString path = latestCanaloDrawing(canaloExchangeFolder());
    if (path.isEmpty()) {
        QMessageBox::information(this, tr("CanaloCAD"),
                                 tr("Nessuna tavola di CanaloCAD in %1. In CanaloCAD: Esporta > DXF.")
                                     .arg(QDir::toNativeSeparators(canaloExchangeFolder())));
        return;
    }
    print(tr("Tavola CanaloCAD: %1").arg(QDir::toNativeSeparators(path)));
    canaloInsert(path);
}

void MainWindow::updateCanaloWatch()
{
    if (!canaloWatcher_) {
        canaloWatcher_ = new QFileSystemWatcher(this);
        canaloSeen_ = QDateTime::currentDateTime();
        connect(canaloWatcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
            const QString path = latestCanaloDrawing(canaloExchangeFolder(), canaloSeen_);
            if (path.isEmpty())
                return;
            canaloSeen_ = QFileInfo(path).lastModified();
            print(tr("CanaloCAD ha esportato %1: CANALOULTIMA (o Impianto > Ultima tavola) per inserirla.")
                      .arg(QFileInfo(path).fileName()));
        });
    }
    if (!canaloWatcher_->directories().isEmpty())
        canaloWatcher_->removePaths(canaloWatcher_->directories());
    const QString folder = canaloExchangeFolder();
    if (appOptions().canaloWatch && !folder.isEmpty() && QFileInfo(folder).isDir())
        canaloWatcher_->addPath(folder);
}

// ============================================================ scheda CanaloCAD (WebView2)

namespace {

// Dati della pagina (progetti e preferenze di CanaloCAD, cache del motore): solo sul PC, nella cartella dei dati
// locali di CartesioCAD.
QString canaloWebDataFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/CanaloCAD-pagina");
}

} // namespace

// Progetto della pagina compatto, su una riga (si salva nel disegno); vuoto se non è un progetto valido.
std::string compactCanaloProject(const QString& text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject() || !doc.object().value(QStringLiteral("projectSections")).isArray())
        return {};
    return doc.toJson(QJsonDocument::Compact).toStdString();
}

bool MainWindow::canaloEmbedded() const
{
    static const bool ok = WebView2Host::available();
    return ok;
}

bool MainWindow::isCanaloTab(int index) const
{
    return canaloPlaceholder_ && index >= 0 && index < stack_->count() && stack_->widget(index) == canaloPlaceholder_;
}

void MainWindow::canaloShowTab(std::function<void()> then)
{
    if (DrawingWidget* d = current())
        canaloFrom_ = d;
    if (then) {
        if (canaloReady_)
            QTimer::singleShot(0, this, then);
        else
            canaloPending_.push_back(std::move(then));
    }
    if (!canaloPlaceholder_) {
        canaloReady_ = false;
        if (mainView_) {   // scena unica: finestra nativa sopra la vista, nell'area riportata dal QML
            canaloPlaceholder_ = new QWidget(stack_);
            canaloTab_ = new CanaloTab(mainView_);
            canaloTab_->hide();
            connect(mainView_, &MainView::canaloAreaChanged, this, &MainWindow::positionCanaloTab);
        }
        else {
            canaloTab_ = new CanaloTab(stack_);
            canaloPlaceholder_ = canaloTab_;
        }
        stack_->addWidget(canaloPlaceholder_);
        fileTabs_->addTab(tr("CanaloCAD"));
        WebView2Host* host = canaloTab_->host();
        connect(host, &WebView2Host::message, this, &MainWindow::canaloMessage);
        connect(host, &WebView2Host::failed, this, [this](const QString& reason) {
            print(tr("CanaloCAD non si apre nella scheda (%1): lo apro nel browser.").arg(reason));
            canaloPending_.clear();
            closeCanaloTab();
            QDesktopServices::openUrl(QUrl::fromLocalFile(canaloHtmlPath()));
        });
        canaloTab_->start(QUrl::fromLocalFile(canaloHtmlPath()), canaloWebDataFolder());
    }
    fileTabs_->setCurrentIndex(stack_->indexOf(canaloPlaceholder_));
    canaloSyncOwner();
}

void MainWindow::canaloSyncOwner()
{
    if (!canaloTab_ || !canaloReady_)
        return;
    DrawingWidget* d = canaloFrom_;
    if (!d || (d == canaloOwner_ && canaloOwnerLoaded_))
        return;   // senza disegno la pagina tiene il suo progetto (salvato solo sul computer)
    // Prima il progetto attuale della pagina nel disegno di prima (l'ultimo avviso della pagina arriva con un secondo
    // di ritardo), poi quello del disegno nuovo.
    QPointer<DrawingWidget> previous = canaloOwnerLoaded_ ? canaloOwner_ : nullptr;
    canaloOwner_ = d;
    canaloOwnerLoaded_ = false;
    if (previous)
        canaloTab_->host()->runAsync(QStringLiteral("window.CanaloHost.project()"), [previous](const QJsonValue& value, const QString& error) {
            const std::string json = value.isString() ? compactCanaloProject(value.toString()) : std::string();
            if (previous && error.isEmpty() && !json.empty() && json != previous->document().canaloTabProject()) {
                previous->document().setCanaloTabProject(json);
                previous->notifyChanged();
            }
        });
    const QString text = QString::fromStdString(d->document().canaloTabProject());
    const QString arg = QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    QPointer<DrawingWidget> owner(d);
    canaloTab_->host()->runAsync(QStringLiteral("window.CanaloHost.loadProject(%1)").arg(arg),
                                 [this, owner](const QJsonValue& value, const QString& error) {
        if (!owner || owner != canaloOwner_)
            return;
        if (!error.isEmpty()) {
            print(tr("CanaloCAD: progetto del disegno non aperto nella scheda: %1").arg(error));
            return;
        }
        canaloOwnerLoaded_ = true;
        const int sections = value.toObject().value(QStringLiteral("sezioni")).toInt();
        if (!owner->document().canaloTabProject().empty())
            print(tr("CanaloCAD: progetto di %1 (%n schede canalina), salvato nel disegno.", nullptr, sections).arg(owner->displayName()));
    });
}

void MainWindow::positionCanaloTab()
{
    if (!canaloTab_ || !mainView_)
        return;
    const bool shown = mainView_->canaloVisible();
    if (shown) {
        canaloTab_->setGeometry(mainView_->canaloArea());
        canaloTab_->show();
        canaloTab_->raise();
        canaloTab_->setFocus();
    }
    else
        canaloTab_->hide();
}

void MainWindow::closeCanaloTab()
{
    if (!canaloPlaceholder_)
        return;
    const int index = stack_->indexOf(canaloPlaceholder_);
    QWidget* placeholder = canaloPlaceholder_;
    CanaloTab* tab = canaloTab_;
    canaloPlaceholder_ = nullptr;
    canaloTab_ = nullptr;
    canaloReady_ = false;
    if (mainView_)
        mainView_->setCanaloVisible(false);
    stack_->removeWidget(placeholder);
    if (index >= 0)
        fileTabs_->removeTab(index);
    if (tab != placeholder)
        tab->deleteLater();
    placeholder->deleteLater();
    onTabChanged(fileTabs_->currentIndex());
}

void MainWindow::canaloMessage(const QJsonValue& value)
{
    const QJsonObject msg = value.toObject();
    const QString kind = msg.value(QStringLiteral("tipo")).toString();
    if (kind != QLatin1String("pronta") && msg.value(QStringLiteral("dxf")).toString().size() > 88 * 1024 * 1024) {
        print(tr("CanaloCAD: tavola troppo grande, ignorata."));
        return;
    }
    if (kind == QLatin1String("pronta")) {
        canaloReady_ = true;
        canaloSyncOwner();
        auto pending = std::move(canaloPending_);
        canaloPending_.clear();
        for (auto& f : pending)
            f();
        return;
    }
    if (kind == QLatin1String("progetto")) {   // il progetto della pagina va nel disegno a cui appartiene
        if (canaloOwner_ && canaloOwnerLoaded_) {
            const std::string json = compactCanaloProject(msg.value(QStringLiteral("progetto")).toString());
            if (!json.empty() && json.size() < 64u * 1024u * 1024u && json != canaloOwner_->document().canaloTabProject()) {
                canaloOwner_->document().setCanaloTabProject(json);
                canaloOwner_->notifyChanged();
            }
        }
        return;
    }
    if (kind == QLatin1String("immagine")) {   // negli appunti di Windows, da incollare in Word o in una relazione
        QString error;
        const QImage image = canaloSheetImage(QByteArray::fromBase64(msg.value(QStringLiteral("dxf")).toString().toLatin1()), &error);
        if (image.isNull()) {
            print(tr("CanaloCAD: immagine della tavola non creata: %1").arg(error));
            return;
        }
        auto* data = new QMimeData;
        data->setImageData(image);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        data->setData(QStringLiteral("image/png"), png);
        data->setData(QStringLiteral("PNG"), png);
        QGuiApplication::clipboard()->setMimeData(data);
        print(tr("Tavola di CanaloCAD negli appunti come immagine (%1 × %2 pixel, 300 dpi): si incolla in Word, in una "
                 "relazione o in una mail con Ctrl+V.").arg(image.width()).arg(image.height()));
        return;
    }
    if (kind == QLatin1String("copia")) {
        QString error;
        const QString block = canaloCopyAsBlock(QByteArray::fromBase64(msg.value(QStringLiteral("dxf")).toString().toLatin1()),
                                                msg.value(QStringLiteral("nome")).toString(), &error);
        if (block.isEmpty())
            print(tr("CanaloCAD: tavola non copiata: %1").arg(error));
        else
            print(tr("Tavola di CanaloCAD copiata negli appunti come blocco %1, con i suoi caratteri e stili: Ctrl+V la "
                     "incolla in qualsiasi disegno aperto.").arg(block));
        return;
    }
    if (kind != QLatin1String("tavola"))
        return;
    // Tavola dalla pagina: una copia nella cartella di scambio (come l'esportazione dal browser), poi nel disegno.
    QString name = QFileInfo(msg.value(QStringLiteral("nome")).toString()).fileName();
    if (!name.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive))
        name = QStringLiteral("Canalina.dxf");
    QString folder = canaloExchangeFolder();
    if (folder.isEmpty() || !QDir().mkpath(folder))
        folder = QDir::tempPath();
    const QString path = folder + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QByteArray::fromBase64(msg.value(QStringLiteral("dxf")).toString().toLatin1())) < 0) {
        print(tr("CanaloCAD: tavola non salvata in %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    file.close();
    DrawingWidget* target = canaloFrom_;
    if (!target)
        for (int i = 1; i < stack_->count() && !target; ++i)
            target = qobject_cast<DrawingWidget*>(stack_->widget(i));
    if (!target) {
        print(tr("Tavola di CanaloCAD salvata in %1: aprire un disegno e usare CANALOULTIMA per inserirla.")
                  .arg(QDir::toNativeSeparators(path)));
        return;
    }
    fileTabs_->setCurrentIndex(stack_->indexOf(target));
    if (canaloInsert(path))
        print(tr("Tavola di CanaloCAD inserita (%1); copia in %2.").arg(name, QDir::toNativeSeparators(folder)));
}

QString MainWindow::canaloCopyAsBlock(const QByteArray& dxf, const QString& fileName, QString* error)
{
    // La tavola letta come un DXF qualsiasi (layer, stili di testo con i loro caratteri, tipi di linea)...
    const QString path = QDir::tempPath() + QStringLiteral("/cartesio-canalo-copia.dxf");
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(dxf) != dxf.size()) {
            if (error)
                *error = tr("file temporaneo non scritto");
            return {};
        }
    }
    Document source;
    const bool read = DrawingWidget::readFile(path, source, error);
    QFile::remove(path);
    if (!read)
        return {};
    // ...diventa un blocco con un nome legato al contenuto (la stessa tavola incollata due volte è lo stesso blocco,
    // una tavola cambiata ne fa uno nuovo invece di prendere il vecchio con lo stesso nome).
    QString base = QFileInfo(fileName).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("Tavola");
    const QString name = QStringLiteral("CanaloCAD_%1_%2")
                             .arg(base.left(60), QString::fromLatin1(QCryptographicHash::hash(dxf, QCryptographicHash::Sha1).toHex().left(6)));
    // In scala per il disegno da cui si è arrivati a CanaloCAD (tavola in mm).
    DrawingWidget* target = canaloFrom_;
    for (int i = 1; i < stack_->count() && !target; ++i)
        target = qobject_cast<DrawingWidget*>(stack_->widget(i));
    const int from = source.insUnits() > 0 ? source.insUnits() : appOptions().insertSourceUnits;
    const int to = target && target->document().insUnits() > 0 ? target->document().insUnits() : appOptions().insertTargetUnits;
    Document holder;
    importDrawingAsBlock(holder, source, name.toStdString(), unitsFactor(from, to));
    std::vector<std::unique_ptr<Entity>> added;
    added.push_back(std::make_unique<InsertEntity>(name.toStdString(), Vec2{0.0, 0.0}));
    holder.commit(std::move(added));
    holder.linkBlocks();
    std::vector<EntityId> ids;
    for (const auto& e : holder.entities())
        ids.push_back(e->id());
    setClipboard(holder, ids, Vec2{0.0, 0.0});
    return name;
}

QImage MainWindow::canaloSheetImage(const QByteArray& dxf, QString* error)
{
    const QString path = QDir::tempPath() + QStringLiteral("/cartesio-canalo-immagine.dxf");
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(dxf) != dxf.size()) {
            if (error)
                *error = tr("file temporaneo non scritto");
            return {};
        }
    }
    // La tavola in un disegno nascosto, stampata con il motore di stampa: fondo bianco, colori degli oggetti,
    // riempimenti e caratteri; foglio con le proporzioni della tavola, largo 297 mm, a 300 dpi.
    DrawingWidget sheet(settings_, QStringLiteral("tavola"));
    sheet.setAttribute(Qt::WA_DontShowOnScreen);
    QStringList notes;
    const bool loaded = sheet.load(path, error, &notes);
    QFile::remove(path);
    if (!loaded)
        return {};
    // I colori molto chiari (pensati per lo sfondo scuro del CAD) su carta bianca non si leggono: diventano grigio scuro.
    Document& doc = sheet.document();
    const auto lightRgb = [](int rgb) { return rgb >= 0 && QColor::fromRgb(QRgb(rgb)).lightnessF() > 0.78; };
    const auto lightAci = [](int aci) { return aci >= 1 && aci <= 255 && aci != 7 && theme::aciColor(aci).lightnessF() > 0.78; };
    const auto darken = [&](Entity& e) {
        bool changed = false;
        if (lightRgb(e.trueColor())) {
            e.setTrueColor(-1);
            e.setColor(250);
            changed = true;
        } else if (e.trueColor() < 0 && lightAci(e.color())) {
            e.setColor(250);
            changed = true;
        }
        return changed;
    };
    for (int i = 0; i < static_cast<int>(doc.layers().size()); ++i) {
        Layer& layer = doc.layer(i);
        if (lightRgb(layer.trueColor) || (layer.trueColor < 0 && lightAci(layer.color))) {
            layer.trueColor = -1;
            layer.color = 250;
        }
    }
    for (const auto& def : doc.blocks())
        for (auto& e : def->entities)
            darken(*e);
    std::vector<std::unique_ptr<Entity>> replaced;
    std::vector<EntityId> removed;
    for (const auto& e : doc.entities()) {
        auto copy = e->clone();
        if (darken(*copy)) {
            removed.push_back(e->id());
            replaced.push_back(std::move(copy));
        }
    }
    if (!removed.empty())
        doc.commit(std::move(replaced), removed);
    const BBox box = doc.extents();
    if (!box.isValid()) {
        if (error)
            *error = tr("tavola vuota");
        return {};
    }
    PlotJob job = plotJobFor(sheet, 0);
    job.settings = PlotSettings::forModel();
    job.settings.styleSheet.clear();
    job.settings.mediaName.clear();
    const double width = 297.0, margin = 4.0;
    job.settings.paperWidth = width;
    job.settings.paperHeight = std::clamp(width * box.height() / std::max(1e-9, box.width()), 40.0, 1200.0);
    job.settings.marginLeft = job.settings.marginBottom = job.settings.marginRight = job.settings.marginTop = margin;
    job.stamp = false;
    return renderPlotPreview(sheet, job, 300.0 / 25.4, error);
}

void MainWindow::canaloTabSelfTest(const QString& section, const QString& sheet)
{
#if defined(Q_OS_WIN) || defined(CARTESIO_HAS_WEBENGINE)
    QString why;
    if (!WebView2Host::available(&why)) {
        print(tr("-CANALOSCHEDA: %1").arg(why));
        return;
    }
    QFile in(section);
    if (!in.open(QIODevice::ReadOnly)) {
        print(tr("-CANALOSCHEDA: %1 non leggibile").arg(section));
        return;
    }
    const QString data = QString::fromLatin1(in.readAll().toBase64());
#ifdef Q_OS_WIN
    // Finestra di Windows mai mostrata: la pagina gira senza schermo.
    HWND window = CreateWindowExW(0, L"STATIC", L"CanaloCAD", WS_OVERLAPPEDWINDOW, 0, 0, 1400, 900, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
#else
    // Qt WebEngine: la pagina in un widget mai mostrato.
    QWidget container;
    container.setAttribute(Qt::WA_DontShowOnScreen);
    container.resize(1400, 900);
#endif
    WebView2Host host;
    QEventLoop loop;
    QTimer::singleShot(120000, &loop, [&] {
        print(tr("-CANALOSCHEDA: tempo scaduto"));
        loop.quit();
    });
    connect(&host, &WebView2Host::failed, &loop, [&](const QString& reason) {
        print(tr("-CANALOSCHEDA: %1").arg(reason));
        loop.quit();
    });
    connect(&host, &WebView2Host::message, &loop, [&](const QJsonValue& value) {
        if (value.toObject().value(QStringLiteral("tipo")).toString() != QLatin1String("pronta"))
            return;
        // Come la scheda vera: prima il progetto salvato nel disegno aperto (o uno nuovo).
        const QString saved = current() ? QString::fromStdString(current()->document().canaloTabProject()) : QString();
        if (!saved.isEmpty()) {
            QJsonParseError parseError;
            QJsonDocument::fromJson(saved.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError)
                print(tr("-CANALOSCHEDA: progetto del disegno non valido al carattere %1 (%2): ...%3...")
                          .arg(parseError.offset).arg(parseError.errorString(), saved.mid(std::max(0, parseError.offset - 60), 120)));
        }
        host.runAsync(QStringLiteral("window.CanaloHost.loadProject(%1)")
                          .arg(QString::fromUtf8(QJsonDocument(QJsonArray{saved}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)),
                      [this](const QJsonValue& v, const QString& e) {
            print(e.isEmpty() ? tr("-CANALOSCHEDA: progetto del disegno nella pagina: %1 schede").arg(v.toObject().value(QStringLiteral("sezioni")).toInt())
                              : tr("-CANALOSCHEDA: progetto del disegno non caricato: %1").arg(e));
        });
        host.runAsync(QStringLiteral("window.CanaloHost.importDxf(%1, \"%2\")")
                          .arg(QString::fromUtf8(QJsonDocument(QJsonArray{QFileInfo(section).fileName()}).toJson(QJsonDocument::Compact)).mid(1).chopped(1), data),
                      [&](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                print(tr("-CANALOSCHEDA: importazione non riuscita: %1").arg(error));
                loop.quit();
                return;
            }
            const QJsonObject r = result.toObject();
            print(tr("-CANALOSCHEDA: sezione importata nella pagina: %1 cavi, scheda \"%2\"")
                      .arg(r.value(QStringLiteral("cavi")).toInt()).arg(r.value(QStringLiteral("titolo")).toString()));
            host.runAsync(QStringLiteral("window.CanaloHost.sheetDxf()"), [&](const QJsonValue& out, const QString& error2) {
                const QString dxf = out.toObject().value(QStringLiteral("dxf")).toString();
                QFile file(sheet);
                if (error2.isEmpty() && !dxf.isEmpty() && file.open(QIODevice::WriteOnly)) {
                    file.write(dxf.toLatin1());
                    print(tr("-CANALOSCHEDA: tavola %1 (%2 byte) salvata in %3")
                              .arg(out.toObject().value(QStringLiteral("nome")).toString()).arg(dxf.size())
                              .arg(QDir::toNativeSeparators(sheet)));
                    // Come "Copia come blocco": negli appunti, da incollare con PASTECLIP.
                    QString copyError;
                    const QString block = canaloCopyAsBlock(dxf.toLatin1(), out.toObject().value(QStringLiteral("nome")).toString(), &copyError);
                    print(block.isEmpty() ? tr("-CANALOSCHEDA: copia come blocco non riuscita: %1").arg(copyError)
                                          : tr("-CANALOSCHEDA: tavola negli appunti come blocco %1").arg(block));
                    // Come "Copia come immagine": l'immagine si salva accanto alla tavola per controllarla.
                    const QImage image = canaloSheetImage(dxf.toLatin1(), &copyError);
                    const QString png = QFileInfo(sheet).absolutePath() + QLatin1Char('/') + QFileInfo(sheet).completeBaseName() + QStringLiteral(".png");
                    print(!image.isNull() && image.save(png)
                              ? tr("-CANALOSCHEDA: immagine %1 × %2 salvata in %3").arg(image.width()).arg(image.height()).arg(QDir::toNativeSeparators(png))
                              : tr("-CANALOSCHEDA: immagine non creata: %1").arg(copyError));
                }
                else
                    print(tr("-CANALOSCHEDA: tavola non ottenuta %1").arg(error2));
                // Come canalo_scheda "leggi" dell'assistente IA: lo stato della pagina ricalcolato da CartesioCAD.
                host.runAsync(QStringLiteral("window.CanaloHost.state()"), [&](const QJsonValue& st, const QString& error3) {
                    const QJsonObject a = error3.isEmpty() ? canalo_->analyze(st.toObject()) : QJsonObject();
                    const QJsonObject res = a.value(QStringLiteral("risultati")).toObject();
                    if (a.isEmpty() || a.contains(QStringLiteral("errore")))
                        print(tr("-CANALOSCHEDA: stato della pagina non letto %1").arg(error3));
                    else
                        print(tr("-CANALOSCHEDA: stato della pagina letto: %1 tipi di cavo, riempimento %2%")
                                  .arg(a.value(QStringLiteral("cavi")).toArray().size())
                                  .arg(res.value(QStringLiteral("riempimento_pct")).toDouble()));
                    // Come l'avviso "progetto" della pagina: il progetto intero nel disegno.
                    host.runAsync(QStringLiteral("window.CanaloHost.project()"), [&](const QJsonValue& pv, const QString& error4) {
                        DrawingWidget* d = current();
                        if (d && error4.isEmpty() && pv.isString()) {
                            d->document().setCanaloTabProject(compactCanaloProject(pv.toString()));
                            d->notifyChanged();
                            print(tr("-CANALOSCHEDA: progetto salvato nel disegno: %1 schede")
                                      .arg(QJsonDocument::fromJson(pv.toString().toUtf8()).object().value(QStringLiteral("projectSections")).toArray().size()));
                        } else
                            print(tr("-CANALOSCHEDA: progetto non salvato nel disegno %1").arg(error4));
                        loop.quit();
                    });
                });
            });
        });
    });
#ifdef Q_OS_WIN
    host.start(reinterpret_cast<quintptr>(window), QDir::tempPath() + QStringLiteral("/cartesio-canalo-prova"),
               QUrl::fromLocalFile(canaloHtmlPath()));
    loop.exec();
    DestroyWindow(window);
#else
    host.startInWidget(&container, QDir::tempPath() + QStringLiteral("/cartesio-canalo-prova"), QUrl::fromLocalFile(canaloHtmlPath()));
    loop.exec();
#endif
#else
    Q_UNUSED(section);
    Q_UNUSED(sheet);
    print(tr("-CANALOSCHEDA: scheda CanaloCAD non disponibile in questa versione"));
#endif
}

} // namespace cartesio
