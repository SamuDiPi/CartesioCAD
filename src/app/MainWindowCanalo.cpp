// Plugin CanaloCAD nella finestra principale: scheda "Plugin" della barra multifunzione, comandi
// CANALOCAD, CANALOINVIA, CANALOINS, CANALOULTIMA, CANALOVERIFICA (calcoli sulla sezione selezionata) e
// sorveglianza della cartella di scambio.
#include "app/MainWindow.h"
#include "app/MainView.h"

#include "app/AppOptions.h"
#include "app/CanaloPlugin.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlantPanel.h"
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
#include <QMessageBox>
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
    auto* check = makeAction(tr("Verifica\nsezione"), glyphIcon(QStringLiteral("\u00D8"), true), {},
                             tr("Calcoli di CanaloCAD sulla sezione selezionata (cerchi = cavi, linee = canalina, testi = "
                                "sigle): riempimento, portata, segregazione, staffaggio, tiro e distinta\nComando: CANALOVERIFICA"));
    connect(check, &QAction::triggered, this, [this] { canaloCheck(true); });
    drawingActions_ << send << insert << latest << check;
    RibbonPanel* panel = ribbon->addPanel(tr("Plugin"), tr("CanaloCAD"));
    panel->addLargeButton(check);
    panel->addLargeButton(open);
    panel->addSmallColumn({send, insert, latest});

    auto* circuits = makeAction(tr("Circuiti"), icon(Icon::ListIcon), {},
                                tr("Schema unifilare in PDF: verifica e assegnazione delle sigle dei circuiti\n"
                                   "Comando: CIRCUITI"));
    connect(circuits, &QAction::triggered, this, [this] {
        showPanel(MainView::Circuits);
    });
    RibbonPanel* electric = ribbon->addPanel(tr("Plugin"), tr("Impianti elettrici"));
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
} // namespace

void MainWindow::canaloCheck(bool askData, bool sheet)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    canalo::Drawn drawn = drawnForCanalo(d->document(), d->selection());
    drawn.fileName = QFileInfo(d->displayName()).fileName().toStdString();
    if (drawn.circles.empty()) {
        print(tr("CANALOVERIFICA: selezionare la sezione della canalina (i cavi sono i cerchi, la canalina le linee del "
                 "bordo, le sigle i testi) e ripetere il comando."));
        return;
    }
    canalo::Tray tray = canalo::readSection(drawn);
    // Dati del tratto: gli ultimi usati (le misure vengono dal disegno).
    static canalo::Tray last;
    tray.thk = last.thk;
    tray.lengthM = last.lengthM;
    tray.bends90 = last.bends90;
    tray.tees = last.tees;
    tray.reducers = last.reducers;
    tray.seismic = last.seismic;
    tray.trayType = last.trayType;
    tray.packing = last.packing;
    tray.cover = last.cover;
    tray.septums = last.septums;
    tray.earth = last.earth;
    tray.reiCount = last.reiCount;
    tray.support = last.support;
    tray.spanM = last.spanM;
    tray.brand = last.brand;
    tray.seriesId = last.seriesId;
    if (askData) {
        FormDialog dialog(tr("Verifica sezione CanaloCAD"), FormDialog::Ok | FormDialog::Cancel, this);
        dialog.note(tr("Letti dal disegno: %n cavi, canalina %1 × %2 mm.", nullptr, static_cast<int>(tray.cables.size()))
                        .arg(num(tray.width, 0), num(tray.height, 0)));
        dialog.group(tr("Canalina"));
        dialog.number(tr("Larghezza"), 30, 1200, 0, tray.width, QStringLiteral(" mm"));
        dialog.number(tr("Altezza"), 20, 300, 0, tray.height, QStringLiteral(" mm"));
        dialog.choice(tr("Spessore"), {{QStringLiteral("1,5 mm"), 1.5}, {QStringLiteral("2,0 mm"), 2.0}, {QStringLiteral("2,5 mm"), 2.5}}, tray.thk,
                      [&](const QVariant& v) { tray.thk = v.toDouble(); });
        QList<QPair<QString, QVariant>> series;
        for (const auto& [brand, list] : canalo::brands())
            for (const canalo::Series& s : list)
                series.append({QString::fromStdString(s.name), QString::fromStdString(brand + "|" + s.id)});
        dialog.choice(tr("Serie"), series, QString::fromStdString(tray.brand + "|" + tray.seriesId), [&](const QVariant& v) {
            const QStringList parts = v.toString().split(u'|');
            tray.brand = parts.value(0).toStdString();
            tray.seriesId = parts.value(1).toStdString();
            for (const canalo::Series& s : canalo::seriesOf(tray.brand))
                if (s.id == tray.seriesId)
                    tray.trayType = s.type;
        });
        dialog.choice(tr("Coperchio"), {{tr("Nessuno"), QStringLiteral("none")}, {tr("A scatto"), QStringLiteral("snap")}, {tr("Con fermi"), QStringLiteral("clamped")}},
                      QString::fromStdString(tray.cover), [&](const QVariant& v) { tray.cover = v.toString().toStdString(); });
        dialog.integer(tr("Setti separatori"), 0, 2, tray.septums);
        dialog.choice(tr("Posa dei cavi"), {{tr("Per gravità (come CanaloCAD)"), QStringLiteral("gravity")}, {tr("Come disegnati"), QStringLiteral("input")}},
                      QString::fromStdString(tray.packing), [&](const QVariant& v) { tray.packing = v.toString().toStdString(); });
        dialog.group(tr("Tratto"));
        dialog.number(tr("Lunghezza"), 1, 2000, 1, tray.lengthM, QStringLiteral(" m"));
        dialog.integer(tr("Curve a 90°"), 0, 100, tray.bends90);
        dialog.integer(tr("Derivazioni a T"), 0, 100, tray.tees);
        dialog.integer(tr("Riduzioni"), 0, 100, tray.reducers);
        dialog.integer(tr("Attraversamenti EI120"), 0, 50, tray.reiCount);
        dialog.group(tr("Staffaggio"));
        QList<QPair<QString, QVariant>> sup;
        for (const canalo::Support& s : canalo::supports())
            sup.append({QString::fromStdString(s.name), QString::fromStdString(s.key)});
        dialog.choice(tr("Supporto"), sup, QString::fromStdString(tray.support), [&](const QVariant& v) { tray.support = v.toString().toStdString(); });
        dialog.number(tr("Passo"), 1.0, 3.0, 2, tray.spanM, QStringLiteral(" m"));
        dialog.choice(tr("Antisismico (NTC 2018)"), {{tr("Nessuno"), QStringLiteral("none")}, {tr("Funi a 45° ogni 9 m"), QStringLiteral("cable_9m")},
                                                     {tr("Puntoni a 45° ogni 6 m"), QStringLiteral("strut_6m")}},
                      QString::fromStdString(tray.seismic), [&](const QVariant& v) { tray.seismic = v.toString().toStdString(); });
        dialog.group(tr("Tavola"));
        dialog.check(tr("Disegna la tavola A3 accanto alla sezione"), sheet);
        dialog.setSize(460, 680);
        if (dialog.exec() != QDialog::Accepted)
            return;
        tray.spanM = std::round(std::clamp(tray.spanM, 1.0, 3.0) * 20.0) / 20.0;   // passo 0,05 m, come la pagina
        last = tray;
        last.cables.clear();
    }
    const canalo::Metrics m = canalo::recalculate(tray);
    ++canaloChecks_;

    QStringList out;
    out << tr("CanaloCAD - canalina %1 × %2 mm, %n cavi:", nullptr, static_cast<int>(tray.cables.size()))
               .arg(num(tray.width, 0), num(tray.height, 0));
    QMap<QString, int> kinds;
    for (const canalo::Cable& c : tray.cables)
        ++kinds[QStringLiteral("%1 %2 (Ø %3)").arg(QString::fromStdString(c.type), QString::fromStdString(c.form), num(c.d))];
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it)
        out << QStringLiteral("  %1 × %2").arg(it.value()).arg(it.key());
    out << tr("Riempimento %1% (riserva %2%): cavi %3 mm² su %4 mm²")
               .arg(num(m.fillPct), num(m.sparePct), num(m.usedArea, 0), num(m.trayArea, 0));
    out << tr("Coefficiente di riduzione della portata Kr = %1 (fascio %2 × strati %3 [%4] × posa %5)")
               .arg(num(m.kr, 2), num(m.kFascio, 2), num(m.kStrati, 2)).arg(m.nLayers).arg(num(m.kPosa, 2));
    out << tr("Segregazione CEI 64-8 art. 528.1: %1 - %2")
               .arg(m.segConform ? tr("CONFORME") : tr("NON CONFORME"), QString::fromStdString(m.segDetail));
    out << tr("Peso %1 kg/m (cavi %2, canalina %3)").arg(num(m.totalKgM, 2), num(m.totalKgM - m.trayKgM, 2), num(m.trayKgM, 2));
    if (tray.support != "none") {
        const canalo::Support* s = canalo::support(tray.support);
        out << tr("Staffaggio %1 ogni %2 m: %3 kg su %4 kg ammessi (%5%) %6; freccia %7 mm su %8 mm")
                   .arg(QString::fromStdString(s ? s->name : tray.support), num(tray.spanM, 2), num(m.pointLoad), num(m.maxPoint, 0),
                        num(m.loadPct), m.loadPct <= 100 ? tr("OK") : tr("SOVRACCARICO"), num(m.deflectionMm, 2), num(m.maxDeflectionMm));
    } else {
        out << tr("Staffaggio non indicato: nessuna verifica di carico.");
    }
    out << tr("Tiro di posa %1 daN (massimo %2 daN) %3; raggio di curvatura minimo %4 mm (%5): curve R%6")
               .arg(num(m.pullDaN), num(m.maxPullDaN, 0), m.pullDaN <= m.maxPullDaN ? tr("OK") : tr("ECCESSIVO"), num(m.maxBendR, 0),
                    QString::fromStdString(m.critCode), num(m.fitR, 0));
    if (!m.deratingFailCode.empty())
        out << tr("ATTENZIONE: portata ridotta inferiore alla corrente d'impiego su %1").arg(QString::fromStdString(m.deratingFailCode));
    out << tr("Distinta materiali (codici articolo indicativi, non dei costruttori: i nomi delle serie sono marchi dei "
              "rispettivi proprietari):");
    for (const canalo::BomRow& r : m.bom)
        out << QStringLiteral("  %1  %2  %3 %4  (%5)")
                   .arg(QString::fromStdString(r.code), QString::fromStdString(r.desc), num(r.qty, 0), QString::fromStdString(r.um),
                        QString::fromStdString(r.note));
    out << tr("Valori indicativi (carichi degli staffaggi, coefficienti, tiro): verificarli con le norme e con i dati "
              "dei costruttori. CartesioCAD è distribuito senza alcuna garanzia.");
    for (const QString& line : out)
        print(line);

    if (sheet) {
        // A destra della sezione selezionata, alla scala di annotazione (1 mm di carta = fattore × 1 mm).
        Document& doc = d->document();
        BBox box;
        for (EntityId id : d->selection())
            if (const Entity* e = doc.find(id))
                box.expand(e->bounds());
        const int units = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
        canalo::SheetOptions o;
        o.unit = unitsFactor(4, units) * std::max(1.0, doc.annotationFactor());
        o.origin = box.isValid() ? Vec2{box.max.x + 20.0 * o.unit, box.min.y} : Vec2{0.0, 0.0};
        o.project = QFileInfo(d->displayName()).completeBaseName().toStdString();
        o.date = QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy")).toStdString();
        int layer = doc.findLayer("CANALO-TAVOLA");
        if (layer < 0) {
            Layer l;
            l.name = "CANALO-TAVOLA";
            layer = doc.addLayer(l);
        }
        o.layer = layer;
        doc.commit(canalo::drawSheet(tray, m, o));
        d->notifyChanged();
        print(tr("Tavola A3 disegnata accanto alla sezione sul layer CANALO-TAVOLA (Ctrl+Z la toglie)."));
    }
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
            print(tr("CanaloCAD ha esportato %1: CANALOULTIMA (o Plugin > Ultima tavola) per inserirla.")
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
