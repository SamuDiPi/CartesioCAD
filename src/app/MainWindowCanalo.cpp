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

namespace cartesio {

void MainWindow::setupCanalo(Ribbon* ribbon)
{
    auto* open = makeAction(tr("Apri\nCanaloCAD"), icon(Icon::Open), {},
                            tr("Apre CanaloCAD (progettazione canaline) nel browser\nComando: CANALOCAD"));
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

} // namespace cartesio
