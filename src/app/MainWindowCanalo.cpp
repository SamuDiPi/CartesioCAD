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

#include <QAction>
#include <QDate>
#include <QClipboard>
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
    if (kind == QLatin1String("pronta")) {
        canaloReady_ = true;
        auto pending = std::move(canaloPending_);
        canaloPending_.clear();
        for (auto& f : pending)
            f();
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

void MainWindow::canaloTabSelfTest(const QString& section, const QString& sheet)
{
#ifdef Q_OS_WIN
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
    // Finestra di Windows mai mostrata: la pagina gira senza schermo.
    HWND window = CreateWindowExW(0, L"STATIC", L"CanaloCAD", WS_OVERLAPPEDWINDOW, 0, 0, 1400, 900, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
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
        host.runAsync(QStringLiteral("window.CanaloHost.importDxf(\"%1\", \"%2\")").arg(QFileInfo(section).fileName(), data),
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
                }
                else
                    print(tr("-CANALOSCHEDA: tavola non ottenuta %1").arg(error2));
                loop.quit();
            });
        });
    });
    host.start(reinterpret_cast<quintptr>(window), QDir::tempPath() + QStringLiteral("/cartesio-canalo-prova"),
               QUrl::fromLocalFile(canaloHtmlPath()));
    loop.exec();
    DestroyWindow(window);
#else
    Q_UNUSED(section);
    Q_UNUSED(sheet);
    print(tr("-CANALOSCHEDA: disponibile solo su Windows"));
#endif
}

} // namespace cartesio
