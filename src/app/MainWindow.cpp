#include "app/MainWindow.h"

#include "app/CadView.h"
#include "app/CircuitsPanel.h"
#include "app/PlantPanel.h"
#include "app/CanaloPanel.h"
#include "app/GuidePanel.h"
#include "app/ColorDialog.h"
#include "app/CommandLine.h"
#include "app/QmlUi.h"
#include "app/DimStyleDialog.h"
#include "app/StyleDialogs.h"
#include "core/TitleBlock.h"
#include "app/DrawingWidget.h"
#include "app/Icons.h"
#include "app/LayerManager.h"
#include "app/OptionsDialog.h"
#include "app/MainView.h"
#include "app/PlotDialog.h"
#include "app/DwgConverter.h"
#include "core/io/Dxf.h"
#include "app/PropertiesPalette.h"
#include "app/Ribbon.h"
#include "app/StartPage.h"
#include "app/BlockLibrary.h"
#include "app/Thumbnails.h"
#include "app/FormDialog.h"
#include "core/Import.h"
#include "core/DocCompare.h"
#include "app/TextEditInPlace.h"
#include "app/Theme.h"

#include <QAbstractItemView>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QScreen>
#include <QFrame>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QQmlContext>
#include <QTabBar>
#include <QThread>
#include <QTimer>

#include <filesystem>
#include <QToolButton>
#include <QVBoxLayout>

#include <atomic>
#include <set>

namespace cartesio {

// Un disegno in lettura in un altro thread (vedi openFile).
struct OpenJob {
    QString path;
    QString phase;
    Document document;
    DxfReadResult result;
    QString error;
    bool ok = false;
    std::atomic_bool cancelled{false};
    bool asTemplate = false;   // modello: si apre come disegno nuovo senza nome
};

namespace {


QSettings settingsStore()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

QIcon colorIcon(int aci)
{
    QPixmap pm(14, 14);
    pm.fill(aci == kColorByLayer || aci == kColorByBlock ? QColor(0, 0, 0, 0) : theme::aciColor(aci));
    QPainter p(&pm);
    p.setPen(QColor(150, 158, 170));
    p.drawRect(0, 0, 13, 13);
    return QIcon(pm);
}

// Icona composta per l'elenco dei layer: lampadina, sole/fiocco, lucchetto e colore.
QIcon layerIcon(const Layer& l)
{
    QPixmap pm(4 * 18, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    icon(l.visible ? Icon::BulbOn : Icon::BulbOff).paint(&p, QRect(0, 0, 16, 16));
    icon(l.frozen ? Icon::Snowflake : Icon::Sun).paint(&p, QRect(18, 0, 16, 16));
    icon(l.locked ? Icon::LockClosed : Icon::LockOpen).paint(&p, QRect(36, 0, 16, 16));
    p.fillRect(QRect(56, 2, 12, 12), l.trueColor >= 0 ? QColor::fromRgb(static_cast<QRgb>(l.trueColor)) : theme::aciColor(l.color));
    p.setPen(QColor(20, 24, 30));
    p.drawRect(QRect(56, 2, 11, 11));
    return QIcon(pm);
}

// Comandi gestiti dall'interfaccia (non dal nucleo) e i loro alias.
const QStringList kUiCommands = {
    QStringLiteral("STYLE"), QStringLiteral("ST"), QStringLiteral("STILE"), QStringLiteral("MLEADERSTYLE"),
    QStringLiteral("MLS"), QStringLiteral("STILEDIRETTRICE"), QStringLiteral("RECOVER"), QStringLiteral("RECUPERA"),
    QStringLiteral("ETRANSMIT"), QStringLiteral("ARCHIVIA"),
    QStringLiteral("AGGIORNAMENTI"), QStringLiteral("AGGIORNA"), QStringLiteral("VERSIONEPRECEDENTE"), QStringLiteral("ASSISTENTE"),
    QStringLiteral("LAYERSTATE"), QStringLiteral("LAS"), QStringLiteral("STATILAYER"),
    QStringLiteral("DWGCONVERT"), QStringLiteral("CONVERTI"), QStringLiteral("CONVERSIONE"),
    QStringLiteral("ESTRAIDATI"), QStringLiteral("ESPORTADATI"), QStringLiteral("DATICSV"),
    QStringLiteral("FRONTEQUADRO"), QStringLiteral("FRONTEQ"), QStringLiteral("FQ"),
    QStringLiteral("BEDIT"), QStringLiteral("BE"), QStringLiteral("EDITBLOCCO"), QStringLiteral("BSAVE"), QStringLiteral("BS"),
    QStringLiteral("SALVABLOCCO"), QStringLiteral("BCLOSE"), QStringLiteral("BC"), QStringLiteral("CHIUDIBLOCCO"),
    QStringLiteral("NEW"), QStringLiteral("NUOVO"), QStringLiteral("OPEN"), QStringLiteral("APRI"),
    QStringLiteral("QSAVE"), QStringLiteral("SAVE"), QStringLiteral("SALVA"), QStringLiteral("SAVEAS"),
    QStringLiteral("SALVACOME"), QStringLiteral("CLOSE"), QStringLiteral("CHIUDI"), QStringLiteral("QUIT"),
    QStringLiteral("EXIT"), QStringLiteral("ESCI"), QStringLiteral("LAYER"), QStringLiteral("LA"),
    QStringLiteral("PROPERTIES"), QStringLiteral("PR"), QStringLiteral("PROPRIETA"), QStringLiteral("HELP"),
    QStringLiteral("AIUTO"), QStringLiteral("ABOUT"), QStringLiteral("INFORMAZIONI"), QStringLiteral("SEGNALA"), QStringLiteral("SEGNALAPROBLEMA"), QStringLiteral("BUGREPORT"), QStringLiteral("TEXTSCR"), QStringLiteral("SELECTALL"), QStringLiteral("LAYON"),
    QStringLiteral("LAYMCUR"), QStringLiteral("SCRIPT"), QStringLiteral("SCR"), QStringLiteral("EXPORTPDF"),
    QStringLiteral("ESPORTAPDF"), QStringLiteral("PLOT"), QStringLiteral("STAMPA"), QStringLiteral("PAGESETUP"),
    QStringLiteral("CIRCUITI"), QStringLiteral("CIRCUITS"), QStringLiteral("BLOCKSPALETTE"), QStringLiteral("BLOCCHI"),
    QStringLiteral("LIBRERIA"), QStringLiteral("SIMBOLI"), QStringLiteral("ADC"), QStringLiteral("ADCENTER"),
    QStringLiteral("CANALOCAD"), QStringLiteral("CANALO"), QStringLiteral("CANALOINVIA"), QStringLiteral("CANALOSEND"),
    QStringLiteral("CANALOINS"), QStringLiteral("CANALOINSERISCI"), QStringLiteral("CANALOULTIMA"), QStringLiteral("CANALOLAST"),
    QStringLiteral("CANALOVERIFICA"), QStringLiteral("CANALOCHECK"), QStringLiteral("CANALOSEZIONE"), QStringLiteral("CANALOCAVI"), QStringLiteral("CANALINA"), QStringLiteral("CANALINE"), QStringLiteral("CABLETRAY"), QStringLiteral("IMPIANTO"), QStringLiteral("TRATTA"),
    QStringLiteral("VERIFICAIMPIANTO"), QStringLiteral("GUIDA"), QStringLiteral("GUIDE"), QStringLiteral("ETICHETTETRATTE"), QStringLiteral("SEZIONETRATTA"),
    QStringLiteral("DISTINTAIMPIANTO"), QStringLiteral("PERCORSIIMPIANTO"), QStringLiteral("PERCORSI"), QStringLiteral("QUADROIMPIANTO"), QStringLiteral("TRATTEDALAYER"),
    QStringLiteral("OPTIONS"), QStringLiteral("OPZIONI"), QStringLiteral("OP"), QStringLiteral("PREFERENCES"),
    QStringLiteral("PRINT"), QStringLiteral("PREVIEW"), QStringLiteral("ANTEPRIMA"), QStringLiteral("PRE"),
    QStringLiteral("PUBLISH"), QStringLiteral("PUBBLICA"), QStringLiteral("-PLOT"), QStringLiteral("-STAMPA"),
    QStringLiteral("LAYOUT"), QStringLiteral("MSPACE"), QStringLiteral("MS"), QStringLiteral("PSPACE"),
    QStringLiteral("DIMSTYLE"), QStringLiteral("D"), QStringLiteral("DST"), QStringLiteral("STILEQUOTA"),
    QStringLiteral("PAN"), QStringLiteral("P"), QStringLiteral("MANINA"), QStringLiteral("INSERTFILE"),
    QStringLiteral("XATTACH"), QStringLiteral("XA"), QStringLiteral("RIFX"),
    QStringLiteral("STYLESMANAGER"), QStringLiteral("STILISTAMPA"), QStringLiteral("CTB"),
    QStringLiteral("TESTINASCOSTI"), QStringLiteral("HIDDENTEXT"), QStringLiteral("BATTMAN"),
    QStringLiteral("GESTATT"), QStringLiteral("FIND"), QStringLiteral("TROVA"),
    QStringLiteral("QSELECT"), QStringLiteral("SELRAPIDA"), QStringLiteral("DIAGGPU"), QStringLiteral("XRIF"),
    QStringLiteral("XREF"), QStringLiteral("XR"), QStringLiteral("EXTERNALREFERENCES"), QStringLiteral("ER"),
    QStringLiteral("RIFERIMENTI"), QStringLiteral("IMAGEATTACH"), QStringLiteral("IAT"), QStringLiteral("IMMAGINE"),
    QStringLiteral("-BENCH"),
    QStringLiteral("PS"),
};

} // namespace

MainWindow::~MainWindow()
{
    // Prima la scena QML, poi i modelli che mostra (barra, schede, riga di comando, pannelli). Qt distrugge i figli
    // nell'ordine in cui sono stati creati: la scena, creata dopo i modelli, resterebbe viva su oggetti già distrutti
    // (errori "of null" e memoria corrotta alla chiusura, visibili su Linux).
    delete mainView_;
    mainView_ = nullptr;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowIcon(icon(Icon::AppLogo));
    setDockNestingEnabled(true);
    setAcceptDrops(true);   // apertura dei file trascinati sulla finestra

    createActions();
    if (qmlRootUi()) {
        // Interfaccia tutta in QML: i componenti restano i modelli, la scena unica li mostra tutti.
        buildTopArea()->hide();
        buildCentralArea()->hide();
        buildPalettes();
        buildStatusBar();
        mainView_ = new MainView({{QStringLiteral("ribbon"), ribbon_},
                                  {QStringLiteral("tabs"), fileTabs_},
                                  {QStringLiteral("cmd"), commandLine_},
                                  {QStringLiteral("sbar"), status_},
                                  {QStringLiteral("props"), properties_},
                                  {QStringLiteral("layers"), layerManager_},
                                  {QStringLiteral("panel"), circuits_},
                                  {QStringLiteral("plant"), plant_},
                                  {QStringLiteral("guide"), guide_},
                                  {QStringLiteral("library"), library_},
                                  {QStringLiteral("start"), startPage_}},
                                 this);
        setCentralWidget(mainView_);
        connect(commandLine_, &CommandLine::focusInRootRequested, mainView_, &MainView::focusCommandLine);
        connect(mainView_, &MainView::commandBarChanged, this, &MainWindow::showCommandHistory);
        connect(mainView_, &MainView::panelVisibilityChanged, this, [this](int panel, bool on) {
            if (panel == MainView::Library && on)
                library_->refresh();
            if (panel == MainView::Circuits && on)
                circuits_->refresh();
            if (panel == MainView::Plant && on)
                plant_->refresh();
        });
    } else {
        setMenuWidget(buildTopArea());
        setCentralWidget(buildCentralArea());
        buildPalettes();
        buildStatusBar();
    }

    // Parole per l'autocompletamento: comandi del nucleo più quelli dell'interfaccia.
    CommandManager words;
    registerBuiltinCommands(words);
    QStringList completions = kUiCommands;
    for (const auto& w : words.commandWords())
        completions << QString::fromStdString(w);
    completions.removeDuplicates();
    completions.sort();
    commandLine_->setCompletions(completions);

    QSettings store = settingsStore();
    restoreGeometry(store.value("window/geometry").toByteArray());
    if (mainView_)
        mainView_->restoreState(store.value("window/panels").toByteArray());
    else
        restoreState(store.value("window/state").toByteArray());

    // Chiusura imprevista: la sessione precedente non ha spento il suo segno. Solo una riga nella riga di comando.
    previousCrash_ = store.value("sessione/aperta", false).toBool();
    store.setValue("sessione/aperta", true);

    updateRecentMenus();
    print(tr("CartesioCAD %1 - digitare un comando, oppure F1 per la guida passo per passo.")
              .arg(QApplication::applicationVersion()));
    if (previousCrash_)
        print(tr("L'ultima volta CartesioCAD si è chiuso in modo imprevisto. Se vuoi segnalarlo: menu dell'applicazione > "
                 "Segnala un problema (comando SEGNALA)."));
    // Si parte dalla pagina Inizia; con un file o uno script sulla riga di comando (e con
    // l'opzione spenta) c'è subito un disegno nuovo, che l'apertura del file sostituisce.
    if (!appOptions().startPageAtLaunch || QApplication::arguments().size() > 1)
        newDrawing();
    else
        onTabChanged(0);
    applyOptions();
    startupUpdates();
    startupClaude();
}

// ======================================================================= azioni

QAction* MainWindow::makeAction(const QString& text, const QIcon& ic, const QKeySequence& shortcut,
                                const QString& tip)
{
    auto* action = new QAction(ic, text, this);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    QString tooltip = tip.isEmpty() ? text : tip;
    if (!shortcut.isEmpty())
        tooltip += QStringLiteral(" (%1)").arg(shortcut.toString(QKeySequence::NativeText));
    action->setToolTip(tooltip);
    addAction(action);   // le scorciatoie funzionano anche se il pulsante non è visibile
    return action;
}

QAction* MainWindow::makeCommand(const QString& text, const QIcon& ic, const QString& command,
                                 const QKeySequence& shortcut)
{
    QAction* action = makeAction(text, ic, shortcut, QStringLiteral("%1\nComando: %2").arg(text, command));
    connect(action, &QAction::triggered, this, [this, command] { runCommand(command); });
    drawingActions_ << action;
    return action;
}

QAction* MainWindow::makeToggle(const QString& text, const QIcon& ic, const QKeySequence& shortcut,
                                bool DraftSettings::*field)
{
    QAction* action = makeAction(text, ic, shortcut);
    action->setCheckable(true);
    action->setChecked(settings_.*field);
    connect(action, &QAction::toggled, this, [this, text, field](bool on) {
        settings_.*field = on;
        // ORTHO e puntamento polare si escludono a vicenda.
        if (on && field == &DraftSettings::ortho && polarAct_->isChecked())
            polarAct_->setChecked(false);
        if (on && field == &DraftSettings::polar && orthoAct_->isChecked())
            orthoAct_->setChecked(false);
        print(QStringLiteral("<%1 %2>").arg(text, on ? tr("attivato") : tr("disattivato")));
        if (DrawingWidget* d = current())
            d->view()->refresh();
    });
    return action;
}

void MainWindow::createActions()
{
    newAct_ = makeAction(tr("Nuovo"), icon(Icon::New), QKeySequence::New);
    openAct_ = makeAction(tr("Apri..."), icon(Icon::Open), QKeySequence::Open);
    saveAct_ = makeAction(tr("Salva"), icon(Icon::Save), QKeySequence::Save);
    saveAsAct_ = makeAction(tr("Salva con nome..."), icon(Icon::SaveAs), QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    closeAct_ = makeAction(tr("Chiudi disegno"), icon(Icon::Close), QKeySequence(QStringLiteral("Ctrl+F4")));
    exitAct_ = makeAction(tr("Esci da CartesioCAD"), QIcon(), QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(newAct_, &QAction::triggered, this, &MainWindow::newDrawing);
    connect(openAct_, &QAction::triggered, this, &MainWindow::open);
    connect(saveAct_, &QAction::triggered, this, [this] { save(); });
    connect(saveAsAct_, &QAction::triggered, this, [this] { saveAs(); });
    connect(closeAct_, &QAction::triggered, this, [this] { closeDrawingAt(fileTabs_->currentIndex()); });
    connect(exitAct_, &QAction::triggered, this, &QWidget::close);
    drawingActions_ << saveAct_ << saveAsAct_ << closeAct_;

    undoAct_ = makeCommand(tr("Annulla"), icon(Icon::Undo), QStringLiteral("UNDO"), QKeySequence::Undo);
    redoAct_ = makeCommand(tr("Ripristina"), icon(Icon::Redo), QStringLiteral("REDO"), QKeySequence::Redo);

    lineAct_ = makeCommand(tr("Linea"), icon(Icon::Line), QStringLiteral("LINE"));
    circleAct_ = makeCommand(tr("Cerchio"), icon(Icon::Circle), QStringLiteral("CIRCLE"));
    arcAct_ = makeCommand(tr("Arco"), icon(Icon::Arc), QStringLiteral("ARC"));
    polylineAct_ = makeCommand(tr("Polilinea"), icon(Icon::Polyline), QStringLiteral("PLINE"));
    rectangleAct_ = makeCommand(tr("Rettangolo"), icon(Icon::Rectangle), QStringLiteral("RECTANG"));
    ellipseAct_ = makeCommand(tr("Ellisse"), icon(Icon::Ellipse), QStringLiteral("ELLIPSE"));
    pointAct_ = makeCommand(tr("Punto"), icon(Icon::PointIcon), QStringLiteral("POINT"));
    hatchAct_ = makeCommand(tr("Tratteggio"), icon(Icon::Hatch), QStringLiteral("HATCH"));
    textAct_ = makeCommand(tr("Testo riga singola"), icon(Icon::TextSingle), QStringLiteral("TEXT"));
    mtextAct_ = makeCommand(tr("Testo"), icon(Icon::TextMulti), QStringLiteral("MTEXT"));
    dimLinearAct_ = makeCommand(tr("Lineare"), icon(Icon::DimLinear), QStringLiteral("DIMLINEAR"));
    dimStyleAct_ = makeCommand(tr("Stile di quota"), icon(Icon::DimLinear), QStringLiteral("DIMSTYLE"));
    leaderAct_ = makeCommand(tr("Direttrice"), icon(Icon::Leader), QStringLiteral("MLEADER"));
    dimAlignedAct_ = makeCommand(tr("Allineata"), icon(Icon::DimAligned), QStringLiteral("DIMALIGNED"));
    dimRadiusAct_ = makeCommand(tr("Raggio"), icon(Icon::DimRadius), QStringLiteral("DIMRADIUS"));
    dimDiameterAct_ = makeCommand(tr("Diametro"), icon(Icon::DimDiameter), QStringLiteral("DIMDIAMETER"));
    dimAngularAct_ = makeCommand(tr("Angolare"), icon(Icon::DimAligned), QStringLiteral("DIMANGULAR"));
    dimOrdinateAct_ = makeCommand(tr("Coordinata"), icon(Icon::DimLinear), QStringLiteral("DIMORDINATE"));
    dimContinueAct_ = makeCommand(tr("Continua"), icon(Icon::DimLinear), QStringLiteral("DIMCONTINUE"));
    dimBaselineAct_ = makeCommand(tr("Linea di base"), icon(Icon::DimLinear), QStringLiteral("DIMBASELINE"));
    qdimAct_ = makeCommand(tr("Quotatura rapida"), icon(Icon::DimLinear), QStringLiteral("QDIM"));
    blockAct_ = makeCommand(tr("Crea"), icon(Icon::BlockCreate), QStringLiteral("BLOCK"));
    insertAct_ = makeCommand(tr("Inserisci"), icon(Icon::BlockInsert), QStringLiteral("INSERT"));
    libraryAct_ = makeCommand(tr("Libreria\nsimboli"), icon(Icon::BlockInsert), QStringLiteral("BLOCKSPALETTE"));
    explodeAct_ = makeCommand(tr("Esplodi"), icon(Icon::Explode), QStringLiteral("EXPLODE"));
    attdefAct_ = makeCommand(tr("Definisci\nattributi"), icon(Icon::AttributeDef), QStringLiteral("ATTDEF"));
    rotateAct_ = makeCommand(tr("Ruota"), icon(Icon::Rotate), QStringLiteral("ROTATE"));
    scaleAct_ = makeCommand(tr("Scala"), icon(Icon::ScaleIcon), QStringLiteral("SCALE"));
    mirrorAct_ = makeCommand(tr("Specchio"), icon(Icon::Mirror), QStringLiteral("MIRROR"));
    arrayAct_ = makeCommand(tr("Serie"), icon(Icon::ArrayIcon), QStringLiteral("ARRAY"));
    extendAct_ = makeCommand(tr("Estendi"), icon(Icon::Extend), QStringLiteral("EXTEND"));
    filletAct_ = makeCommand(tr("Raccordo"), icon(Icon::Fillet), QStringLiteral("FILLET"));
    chamferAct_ = makeCommand(tr("Cima"), icon(Icon::Chamfer), QStringLiteral("CHAMFER"));
    stretchAct_ = makeCommand(tr("Stira"), icon(Icon::Stretch), QStringLiteral("STRETCH"));
    breakAct_ = makeCommand(tr("Interrompi"), icon(Icon::Break), QStringLiteral("BREAK"));
    joinAct_ = makeCommand(tr("Unisci"), icon(Icon::Join), QStringLiteral("JOIN"));
    peditAct_ = makeCommand(tr("Modifica polilinea"), icon(Icon::Polyline), QStringLiteral("PEDIT"));
    distAct_ = makeCommand(tr("Misura"), icon(Icon::Measure), QStringLiteral("DIST"));
    areaAct_ = makeCommand(tr("Area"), icon(Icon::AreaIcon), QStringLiteral("AREA"));
    idAct_ = makeCommand(tr("Coordinate"), icon(Icon::PointIcon), QStringLiteral("ID"));
    listAct_ = makeCommand(tr("Elenco"), icon(Icon::ListIcon), QStringLiteral("LIST"));
    matchPropAct_ = makeCommand(tr("Copia\nproprietà"), icon(Icon::MatchProp), QStringLiteral("MATCHPROP"));
    copyClipAct_ = makeCommand(tr("Copia negli appunti"), icon(Icon::Copy), QStringLiteral("COPYCLIP"), QKeySequence::Copy);
    pasteAct_ = makeCommand(tr("Incolla"), icon(Icon::Paste), QStringLiteral("PASTECLIP"), QKeySequence::Paste);
    mviewAct_ = makeCommand(tr("Finestra"), icon(Icon::ViewportIcon), QStringLiteral("MVIEW"));
    vpclipAct_ = makeCommand(tr("Ritaglia"), icon(Icon::ViewportIcon), QStringLiteral("VPCLIP"));
    titleBlockAct_ = makeCommand(tr("Cartiglio"), icon(Icon::TitleBlock), QStringLiteral("CARTIGLIO"));
    titleUpdateAct_ = makeCommand(tr("Aggiorna cartiglio"), icon(Icon::TitleBlock), QStringLiteral("AGGCARTIGLIO"));
    layoutNewAct_ = makeAction(tr("Nuovo\nlayout"), icon(Icon::LayoutNew), {},
                               tr("Nuovo layout con una finestra sul modello"));
    pageSetupAct_ = makeAction(tr("Imposta\npagina"), icon(Icon::PageSetup), {},
                               tr("Gestione impostazioni di pagina del layout o del modello\nComando: PAGESETUP"));
    plotAct_ = makeAction(tr("Stampa"), icon(Icon::PlotIcon), QKeySequence(QStringLiteral("Ctrl+P")),
                          tr("Stampa su PDF o stampante, con tutte le opzioni\nComando: PLOT"));
    previewAct_ = makeAction(tr("Anteprima"), icon(Icon::PreviewIcon), {},
                             tr("Anteprima di stampa del layout o del modello\nComando: PREVIEW"));
    pdfAct_ = makeAction(tr("Esporta\nPDF"), icon(Icon::Pdf), {},
                         tr("Esporta il layout attivo (o il modello) in PDF\nComando: EXPORTPDF"));
    publishAct_ = makeAction(tr("Pubblica"), icon(Icon::Pdf), {},
                             tr("Tutti i layout in un solo PDF, ciascuno con le sue impostazioni\nComando: PUBLISH"));
    scriptAct_ = makeAction(tr("Esegui\nscript"), icon(Icon::Script), {},
                            tr("Esegue un file di script .scr\nComando: SCRIPT"));
    connect(layoutNewAct_, &QAction::triggered, this, &MainWindow::newLayout);
    connect(pageSetupAct_, &QAction::triggered, this, [this] {
        if (DrawingWidget* d = current())
            pageSetup(d->shownSpace());
    });
    connect(plotAct_, &QAction::triggered, this, [this] { plotDialog(); });
    connect(pdfAct_, &QAction::triggered, this, [this] { plotDialog(); });
    connect(previewAct_, &QAction::triggered, this, &MainWindow::plotPreview);
    connect(publishAct_, &QAction::triggered, this, &MainWindow::publish);
    connect(scriptAct_, &QAction::triggered, this, &MainWindow::runScriptDialog);
    drawingActions_ << layoutNewAct_ << pageSetupAct_ << pdfAct_ << plotAct_ << previewAct_ << publishAct_;
    moveAct_ = makeCommand(tr("Sposta"), icon(Icon::Move), QStringLiteral("MOVE"));
    copyAct_ = makeCommand(tr("Copia"), icon(Icon::Copy), QStringLiteral("COPY"));
    offsetAct_ = makeCommand(tr("Offset"), icon(Icon::Offset), QStringLiteral("OFFSET"));
    trimAct_ = makeCommand(tr("Taglia"), icon(Icon::Trim), QStringLiteral("TRIM"));
    eraseAct_ = makeCommand(tr("Cancella"), icon(Icon::Erase), QStringLiteral("ERASE"), QKeySequence::Delete);

    selectAllAct_ = makeAction(tr("Seleziona tutto"), QIcon(), QKeySequence::SelectAll);
    connect(selectAllAct_, &QAction::triggered, this, &MainWindow::selectAll);
    drawingActions_ << selectAllAct_;

    layerPropsAct_ = makeAction(tr("Proprietà\nlayer"), icon(Icon::LayerProperties), {},
                                tr("Gestore proprietà layer\nComando: LAYER"));
    layerFromObjectAct_ = makeAction(tr("Rendi corrente"), icon(Icon::LayerMakeCurrent), {},
                                     tr("Rende corrente il layer dell'oggetto selezionato\nComando: LAYMCUR"));
    layerAllOnAct_ = makeAction(tr("Accendi tutti"), icon(Icon::LayerAllOn), {},
                                tr("Accende tutti i layer\nComando: LAYON"));
    connect(layerFromObjectAct_, &QAction::triggered, this, &MainWindow::layerFromObject);
    connect(layerAllOnAct_, &QAction::triggered, this, &MainWindow::allLayersOn);
    drawingActions_ << layerPropsAct_ << layerFromObjectAct_ << layerAllOnAct_;

    zoomExtentsAct_ = makeAction(tr("Estensioni"), icon(Icon::ZoomExtents), {}, tr("Zoom estensioni"));
    panAct_ = makeCommand(tr("Pan"), icon(Icon::Pan), QStringLiteral("PAN"));
    zoomInAct_ = makeAction(tr("Zoom avanti"), icon(Icon::ZoomIn));
    zoomOutAct_ = makeAction(tr("Zoom indietro"), icon(Icon::ZoomOut));
    connect(zoomExtentsAct_, &QAction::triggered, this, [this] {
        if (DrawingWidget* d = current())
            d->view()->zoomExtents();
    });
    connect(zoomInAct_, &QAction::triggered, this, [this] {
        if (DrawingWidget* d = current())
            d->view()->zoomBy(1.5);
    });
    connect(zoomOutAct_, &QAction::triggered, this, [this] {
        if (DrawingWidget* d = current())
            d->view()->zoomBy(1.0 / 1.5);
    });
    drawingActions_ << zoomExtentsAct_ << zoomInAct_ << zoomOutAct_;

    propertiesAct_ = makeAction(tr("Proprietà"), icon(Icon::Properties), QKeySequence(QStringLiteral("Ctrl+1")));
    commandLineAct_ = makeAction(tr("Riga di comando"), icon(Icon::CommandLine), QKeySequence(QStringLiteral("Ctrl+9")));
    historyAct_ = makeAction(tr("Cronologia"), icon(Icon::History), QKeySequence(Qt::Key_F2),
                             tr("Cronologia della riga di comando"));
    helpAct_ = makeAction(tr("Comandi"), QIcon(), {}, tr("Elenco dei comandi"));
    guideAct_ = makeAction(tr("Guida"), QIcon(), QKeySequence(Qt::Key_F1), tr("Guida passo per passo (F1)"));
    nextTabAct_ = makeAction(tr("Disegno successivo"), QIcon(), QKeySequence(QStringLiteral("Ctrl+Tab")));
    commandLineAct_->setCheckable(true);
    commandLineAct_->setChecked(true);
    connect(commandLineAct_, &QAction::toggled, this, [this](bool on) {
        setCommandLineShown(on && current());
        positionCommandLine();
    });
    connect(historyAct_, &QAction::triggered, this, &MainWindow::showHistory);
    connect(helpAct_, &QAction::triggered, this, &MainWindow::showHelp);
    connect(guideAct_, &QAction::triggered, this, &MainWindow::showGuide);
    // Il "? Guida" della barra multifunzione si collega quando la barra esiste (buildTopArea).
    connect(nextTabAct_, &QAction::triggered, this, [this] {
        if (fileTabs_->count() > 1)
            fileTabs_->setCurrentIndex(fileTabs_->currentIndex() % (fileTabs_->count() - 1) + 1);
    });

    gridAct_ = makeToggle(tr("Griglia"), icon(Icon::Grid), QKeySequence(Qt::Key_F7), &DraftSettings::grid);
    snapAct_ = makeToggle(tr("Snap"), icon(Icon::Snap), QKeySequence(Qt::Key_F9), &DraftSettings::snap);
    orthoAct_ = makeToggle(tr("Orto"), icon(Icon::Ortho), QKeySequence(Qt::Key_F8), &DraftSettings::ortho);
    polarAct_ = makeToggle(tr("Polare"), icon(Icon::Polar), QKeySequence(Qt::Key_F10), &DraftSettings::polar);
    osnapAct_ = makeToggle(tr("Osnap"), icon(Icon::Osnap), QKeySequence(Qt::Key_F3), &DraftSettings::osnap);
    otrackAct_ = makeToggle(tr("Puntamento snap a oggetto"), icon(Icon::OTrack), QKeySequence(Qt::Key_F11),
                            &DraftSettings::otrack);
    dynAct_ = makeToggle(tr("Input dinamico"), icon(Icon::DynInput), QKeySequence(Qt::Key_F12),
                         &DraftSettings::dynInput);
    lwAct_ = makeToggle(tr("Mostra spessori di linea"), icon(Icon::Lineweight), {}, &DraftSettings::lwDisplay);
}

// ============================================================ parte superiore

QWidget* MainWindow::buildTopArea()
{
    // Barra multifunzione in QML, con la barra di accesso rapido e il pulsante dell'applicazione.
    auto* ribbon = ribbon_ = new Ribbon(this);
    connect(ribbon, &Ribbon::helpRequested, this, [this] { showPanel(MainView::Guide, !panelShown(MainView::Guide)); });
    auto* appMenu = new QMenu(this);
    appMenu->addAction(newAct_);
    appMenu->addAction(openAct_);
    appMenu->addAction(saveAct_);
    appMenu->addAction(saveAsAct_);
    QMenu* drawingUtilities = appMenu->addMenu(tr("Utilità disegno"));
    drawingUtilities->addAction(tr("Controlla (AUDIT)"), this, [this] { runCommand(QStringLiteral("AUDIT")); });
    drawingUtilities->addAction(tr("Recupera un file danneggiato (RECOVER)..."), this, [this] { recoverFile(); });
    drawingUtilities->addAction(tr("Archivia per l'invio (ETRANSMIT)..."), this, [this] { etransmit(); });
    drawingUtilities->addAction(tr("Estrai dati in CSV (ESTRAIDATI)..."), this, [this] { showDataExport(); });
    drawingUtilities->addAction(tr("Conversione in serie (DWGCONVERT)..."), this, [this] { showBatchConvert(); });
    appMenu->addSeparator();
    recentMenu_ = appMenu->addMenu(tr("Documenti recenti"));
    appMenu->addSeparator();
    appMenu->addAction(tr("Opzioni..."), this, [this] { showOptions(); });
    appMenu->addAction(guideAct_);
    updateAct_ = appMenu->addAction(tr("Controlla aggiornamenti..."), this, [this] {
        if (pendingUpdate_)
            showUpdateOffer(*pendingUpdate_);
        else
            checkForUpdates(true);
    });
    appMenu->addAction(tr("Torna alla versione precedente..."), this, [this] { rollbackUpdate(); });
    appMenu->addAction(tr("Assistente IA..."), this, [this] { showClaudeDialog(); });
    appMenu->addAction(tr("Segnala un problema..."), this, [this] { showReportDialog(); });
    appMenu->addAction(tr("Informazioni su CartesioCAD..."), this, [this] { showAbout(); });
    appMenu->addSeparator();
    appMenu->addAction(closeAct_);
    appMenu->addAction(exitAct_);
    auto* appAct = new QAction(icon(Icon::AppLogo), tr("CartesioCAD"), this);
    appAct->setMenu(appMenu);
    ribbon->setQuickAccess(appAct, {newAct_, openAct_, saveAct_, saveAsAct_, undoAct_, redoAct_});

    // Scheda Inizio con la stessa disposizione: Disegna, Edita, Annotazione, Layer, Blocco,
    // Proprietà, Utilità, Appunti. I pulsanti con la freccia aprono le varianti del comando.
    const auto withMenu = [this](QAction* main, std::initializer_list<QAction*> items) {
        auto* menu = new QMenu(this);
        for (QAction* a : items)
            menu->addAction(a);
        main->setMenu(menu);
        return main;
    };

    RibbonPanel* draw = ribbon->addPanel(tr("Disegno"), tr("Linee e forme"));
    draw->setPriority(10);
    draw->addLargeButton(lineAct_);
    draw->addLargeButton(polylineAct_);
    draw->addLargeButton(circleAct_);
    draw->addLargeButton(arcAct_);
    QAction* polygonAct = makeCommand(tr("Poligono"), icon(Icon::Polygon), QStringLiteral("POLYGON"));
    QAction* xlineAct = makeCommand(tr("Linea di costruzione"), icon(Icon::XLineIcon), QStringLiteral("XLINE"));
    QAction* rayAct = makeCommand(tr("Semiretta"), icon(Icon::XLineIcon), QStringLiteral("RAY"));
    QAction* mlineAct = makeCommand(tr("Multilinea"), icon(Icon::MlineIcon), QStringLiteral("MLINE"));
    QAction* wipeoutAct = makeCommand(tr("Copertura"), icon(Icon::WipeoutIcon), QStringLiteral("WIPEOUT"));
    QAction* revcloudAct = makeCommand(tr("Nuvola di revisione"), icon(Icon::Revcloud), QStringLiteral("REVCLOUD"));
    QAction* divideAct = makeCommand(tr("Dividi"), icon(Icon::PointIcon), QStringLiteral("DIVIDE"));
    QAction* measureAlongAct = makeCommand(tr("Misura (punti a distanza)"), icon(Icon::PointIcon), QStringLiteral("MEASURE"));
    QAction* splineAct = makeCommand(tr("Spline"), icon(Icon::Polyline), QStringLiteral("SPLINE"));
    draw->addSmallColumn({withMenu(rectangleAct_, {rectangleAct_, polygonAct}), withMenu(ellipseAct_, {ellipseAct_, splineAct}), hatchAct_}, false);
    draw->addSmallColumn({withMenu(pointAct_, {pointAct_, divideAct, measureAlongAct}),
                          withMenu(xlineAct, {xlineAct, rayAct}), withMenu(revcloudAct, {revcloudAct, wipeoutAct, mlineAct})}, false);

    RibbonPanel* modify = ribbon->addPanel(tr("Disegno"), tr("Modifica"));
    QAction* lengthenAct = makeCommand(tr("Allunga"), icon(Icon::Extend), QStringLiteral("LENGTHEN"));
    QAction* alignAct = makeCommand(tr("Allinea"), icon(Icon::Move), QStringLiteral("ALIGN"));
    modify->setPriority(9);
    modify->addSmallColumn({moveAct_, copyAct_, stretchAct_});
    modify->addSmallColumn({withMenu(rotateAct_, {rotateAct_, alignAct}), mirrorAct_, scaleAct_});
    QAction* breakAtPointAct = makeCommand(tr("Spezza in un punto"), icon(Icon::Break), QStringLiteral("BREAKATPOINT"));
    modify->addSmallColumn({withMenu(trimAct_, {trimAct_, extendAct_, lengthenAct}),
                            withMenu(filletAct_, {filletAct_, chamferAct_, joinAct_}), arrayAct_});
    modify->addSmallColumn({eraseAct_, explodeAct_, offsetAct_}, false);
    modify->addSmallColumn({withMenu(breakAct_, {breakAct_, breakAtPointAct}), withMenu(joinAct_, {joinAct_, peditAct_}), chamferAct_}, false);

    RibbonPanel* annotation = ribbon->addPanel(tr("Disegno"), tr("Annotazioni"));
    annotation->setPriority(6);
    annotation->addLargeButton(withMenu(mtextAct_, {mtextAct_, textAct_}));
    QAction* dimAct = makeCommand(tr("Quota"), icon(Icon::DimLinear), QStringLiteral("DIMLINEAR"));
    annotation->addLargeButton(withMenu(dimAct, {dimLinearAct_, dimAlignedAct_, dimAngularAct_, dimRadiusAct_, dimDiameterAct_,
                                                 dimOrdinateAct_, dimContinueAct_, dimBaselineAct_, qdimAct_}));
    QAction* tableAct = makeCommand(tr("Tabella"), icon(Icon::TableIcon), QStringLiteral("TABLE"));
    QAction* textStyleAct = makeCommand(tr("Stili di testo"), icon(Icon::TextSingle), QStringLiteral("STYLE"));
    QAction* leaderStyleAct = makeCommand(tr("Stili direttrici"), icon(Icon::Leader), QStringLiteral("MLEADERSTYLE"));
    QAction* linearHome = makeAction(tr("Lineare"), icon(Icon::DimLinear), {}, tr("Quote lineari e altre"));
    connect(linearHome, &QAction::triggered, dimLinearAct_, &QAction::trigger);
    QAction* leaderHome = makeAction(tr("Direttrice"), icon(Icon::Leader), {}, tr("Direttrice e stili"));
    connect(leaderHome, &QAction::triggered, leaderAct_, &QAction::trigger);
    annotation->addSmallColumn({withMenu(linearHome, {dimLinearAct_, dimAlignedAct_, dimAngularAct_, dimRadiusAct_, dimDiameterAct_,
                                                      dimOrdinateAct_, dimContinueAct_, dimBaselineAct_, qdimAct_, dimStyleAct_}),
                                withMenu(leaderHome, {leaderAct_, leaderStyleAct}), withMenu(tableAct, {tableAct, textStyleAct})});

    RibbonPanel* layerPanel = ribbon->addPanel(tr("Disegno"), tr("Layer"));
    layerPanel->setPriority(7);
    layerPanel->addLargeButton(layerPropsAct_);
    layerCombo_ = new ComboModel(240, this);
    layerCombo_->setIconWidth(4 * 18);
    layerCombo_->setIconSlots(4);   // acceso, congelato, bloccato, colore: cliccabili nell'elenco
    layerCombo_->setToolTip(tr("Layer corrente, o layer degli oggetti selezionati.\n"
                               "Nell'elenco: clic sulle icone per accendere/spegnere, congelare, bloccare, colore."));
    QAction* layOff = makeCommand(tr("Spegni (layer dell'oggetto)"), icon(Icon::BulbOff), QStringLiteral("LAYOFF"));
    QAction* layIso = makeCommand(tr("Isola"), icon(Icon::LayerProperties), QStringLiteral("LAYISO"));
    QAction* layFrz = makeCommand(tr("Congela (layer dell'oggetto)"), icon(Icon::Snowflake), QStringLiteral("LAYFRZ"));
    QAction* layLck = makeCommand(tr("Blocca (layer dell'oggetto)"), icon(Icon::LockClosed), QStringLiteral("LAYLCK"));
    QAction* layUniso = makeCommand(tr("Annulla isolamento"), icon(Icon::LayerAllOn), QStringLiteral("LAYUNISO"));
    QAction* layThw = makeCommand(tr("Scongela tutti"), icon(Icon::Sun), QStringLiteral("LAYTHW"));
    QAction* layUlk = makeCommand(tr("Sblocca (layer dell'oggetto)"), icon(Icon::LockOpen), QStringLiteral("LAYULK"));
    QAction* layMatch = makeCommand(tr("Uguaglia layer"), icon(Icon::MatchProp), QStringLiteral("LAYMATCH"));
    // Strumenti layer su due file sotto l'elenco.
    layerPanel->addComboColumn({layerCombo_}, {{layOff, layIso, layFrz, layLck, layerFromObjectAct_},
                                               {layerAllOnAct_, layUniso, layThw, layUlk, layMatch}});
    layerPanel->addSmallColumn({makeCommand(tr("Rendi corrente"), icon(Icon::LayerMakeCurrent), QStringLiteral("LAYMCUR")),
                                makeCommand(tr("Uguaglia layer"), icon(Icon::MatchProp), QStringLiteral("LAYMATCH"))});

    // Gruppi: crea, separa, selezione dei gruppi attiva/disattiva (PICKSTYLE).
    RibbonPanel* groupsPanel = ribbon->addPanel(tr("Disegno"), tr("Gruppi"));
    groupsPanel->setPriority(2);
    groupsPanel->addLargeButton(makeCommand(tr("Gruppo"), icon(Icon::GroupIcon), QStringLiteral("GROUP")));
    QAction* pickStyleAct = pickStyleAct_ = makeAction(tr("Selezione gruppi"), icon(Icon::GroupIcon), {},
                                       tr("Selezionando un oggetto si seleziona tutto il suo gruppo (PICKSTYLE)"));
    pickStyleAct->setCheckable(true);
    pickStyleAct->setChecked(true);
    connect(pickStyleAct, &QAction::toggled, this, [this](bool on) {
        if (DrawingWidget* d = current())
            d->document().groupSelection = on;
    });
    groupsPanel->addSmallColumn({makeCommand(tr("Separa"), icon(Icon::UngroupIcon), QStringLiteral("UNGROUP")),
                                 pickStyleAct}, false);

    RibbonPanel* homeBlock = ribbon->addPanel(tr("Disegno"), tr("Blocchi"));
    homeBlock->setPriority(3);
    // Inserisci: i blocchi del disegno, oppure un altro DWG/DXF da inserire come blocco.
    auto* insertMenu = new QMenu(this);
    connect(insertMenu, &QMenu::aboutToShow, this, [this, insertMenu] {
        insertMenu->clear();
        DrawingWidget* d = current();
        if (d)
            for (const auto& b : d->document().blocks()) {
                if (b->name.empty() || b->name.front() == '*' || b->name == kTitleBlockName)
                    continue;
                const QString name = QString::fromStdString(b->name);
                connect(insertMenu->addAction(icon(Icon::BlockInsert), name), &QAction::triggered, this, [this, name] {
                    if (DrawingWidget* dw = current()) {
                        dw->submit(QStringLiteral("INSERT"));
                        dw->submit(name);
                    }
                });
            }
        if (!insertMenu->isEmpty())
            insertMenu->addSeparator();
        connect(insertMenu->addAction(tr("Da file DWG/DXF...")), &QAction::triggered, this, [this] { insertFromFile(); });
        connect(insertMenu->addAction(tr("Riferimento esterno DWG/DXF (XATTACH)...")), &QAction::triggered, this,
                [this] { attachXrefFromFile(); });
        connect(insertMenu->addAction(tr("Immagine raster (IMAGEATTACH)...")), &QAction::triggered, this, [this] {
            const QString file = QFileDialog::getOpenFileName(this, tr("Collega immagine"), QString(),
                                                              tr("Immagini (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)"));
            if (!file.isEmpty())
                if (const auto type = askRefPathType(file))
                    attachImage(file, *type);
        });
        connect(insertMenu->addAction(tr("Riferimenti esterni (XRIF)...")), &QAction::triggered, this,
                [this] { showExternalReferences(); });
    });
    insertAct_->setMenu(insertMenu);
    homeBlock->addLargeButton(insertAct_);
    homeBlock->addLargeButton(libraryAct_);
    homeBlock->addSmallColumn({blockAct_, makeCommand(tr("Modifica"), icon(Icon::BlockCreate), QStringLiteral("BEDIT")),
                               makeCommand(tr("Edita attributi"), icon(Icon::AttributeDef), QStringLiteral("EATTEDIT"))});

    RibbonPanel* propsPanel = ribbon->addPanel(tr("Disegno"), tr("Proprietà"));
    propsPanel->setPriority(5);
    propsPanel->addLargeButton(matchPropAct_);
    colorCombo_ = new ComboModel(170, this);
    colorCombo_->setToolTip(tr("Colore corrente, o colore degli oggetti selezionati"));
    linetypeCombo_ = new ComboModel(170, this);
    linetypeCombo_->setToolTip(tr("Tipo di linea corrente, o degli oggetti selezionati"));
    lineweightCombo_ = new ComboModel(170, this);
    lineweightCombo_->setToolTip(tr("Spessore di linea corrente, o degli oggetti selezionati"));
    propsPanel->addComboColumn({colorCombo_, linetypeCombo_, lineweightCombo_});
    propsPanel->addSmallColumn({propertiesAct_}, false);

    RibbonPanel* utilities = ribbon->addPanel(tr("Disegno"), tr("Misure"));
    utilities->setPriority(2);
    QAction* measureAct = makeCommand(tr("Misura"), icon(Icon::Measure), QStringLiteral("DIST"));
    utilities->addLargeButton(withMenu(measureAct, {distAct_, areaAct_, idAct_, listAct_}));

    RibbonPanel* clipboardPanel = ribbon->addPanel(tr("Disegno"), tr("Copia e incolla"));
    clipboardPanel->setPriority(1);
    clipboardPanel->addLargeButton(pasteAct_);
    clipboardPanel->addSmallColumn({copyClipAct_}, false);

    // Inserimenti, Testi e quote, Schermo, Strumenti, Stampa e PDF, Tavola (contestuale): MainWindowRibbonTabs.cpp.
    setupRibbonTabs(ribbon);
    setupCompareTab(ribbon);
    setupCanalo(ribbon);
    setupTextEditorTab(ribbon);   // contestuale: in fondo, compare solo mentre si scrive
    // Organizzazione di CartesioCAD: dal disegno alla stampa, poi impianto, strumenti e schermo.
    ribbon->arrange({tr("Disegno"), tr("Testi e quote"), tr("Inserimenti"), tr("Tavola"), tr("Stampa e PDF"), tr("Impianto"),
                     tr("Strumenti"), tr("Schermo")},
                    {{tr("Disegno"), {tr("Linee e forme"), tr("Modifica"), tr("Layer"), tr("Proprietà"), tr("Copia e incolla"),
                                      tr("Blocchi"), tr("Gruppi"), tr("Annotazioni"), tr("Misure")}},
                     {tr("Testi e quote"), {tr("Testo"), tr("Quote"), tr("Direttrici"), tr("Tabelle"), tr("Assi"),
                                            tr("Scale"), tr("Revisione")}},
                     {tr("Stampa e PDF"), {tr("PDF"), tr("Stampa")}},
                     {tr("Impianto"), {tr("Impianti elettrici"), tr("CanaloCAD")}},
                     {tr("Strumenti"), {tr("Revisioni"), tr("Pulizia"), tr("Personalizzazione")}},
                     {tr("Schermo"), {tr("Pannelli"), tr("Elementi"), tr("Viste salvate"), tr("Finestre del modello"),
                                      tr("Interfaccia")}}});


    // Elenco dei layer aperto: un clic sulle icone (acceso, congelato, bloccato, colore) cambia il layer senza
    // chiudere l'elenco; un clic sul nome lo sceglie.
    connect(layerCombo_, &ComboModel::slotClicked, this, [this](int row, int slot) {
        DrawingWidget* d = current();
        const int layerIndex = layerCombo_->itemData(row).toInt();
        if (!d || layerIndex < 0 || layerIndex >= static_cast<int>(d->document().layers().size()))
            return;
        Document& doc = d->document();
        Layer& layer = doc.layer(layerIndex);
        switch (slot) {
        case 0:
            layer.visible = !layer.visible;
            break;
        case 1:
            if (layerIndex == doc.currentLayer() && !layer.frozen)
                print(tr("Impossibile congelare il layer corrente."));
            else
                layer.frozen = !layer.frozen;
            break;
        case 2:
            layer.locked = !layer.locked;
            break;
        default:
            if (const auto color = ColorDialog::pick(layer.color, false, this)) {
                layer.color = *color;
                layer.trueColor = -1;
            }
            break;
        }
        doc.touch();
        layerCombo_->setItemIcon(row, layerIcon(layer));
        d->view()->update();
        layerListChanged_ = true;
    });
    connect(layerCombo_, &ComboModel::popupClosed, this, [this] {
        if (!layerListChanged_)
            return;
        layerListChanged_ = false;
        if (DrawingWidget* d = current())
            QTimer::singleShot(0, d, [d] { d->notifyChanged(); });
    });

    connect(layerCombo_, &ComboModel::activated, this, [this](int index) {
        DrawingWidget* d = current();
        if (!d || updatingCombos_)
            return;
        const int layer = layerCombo_->itemData(index).toInt();
        if (layer < 0)
            return;
        if (d->selection().empty()) {
            d->document().setCurrentLayer(layer);
            d->notifyChanged();
        } else {
            d->modifyEntities(d->selection(), [layer](Entity& e) { e.setLayer(layer); });
        }
        d->view()->setFocus();
    });
    connect(linetypeCombo_, &ComboModel::activated, this, [this](int index) {
        if (updatingCombos_)
            return;
        QString name = linetypeCombo_->itemData(index).toString();
        if (name == QLatin1String("*ALTRO*")) {   // carica un tipo di linea standard
            QStringList names;
            for (const Linetype& lt : standardLinetypes())
                names << QString::fromStdString(lt.name + "  " + lt.description);
            bool ok = false;
            const QString picked = QInputDialog::getItem(this, tr("Carica tipo di linea"), tr("Tipo di linea:"), names,
                                                         0, false, &ok);
            if (!ok) {
                updateLinetypeCombos();
                return;
            }
            name = picked.section(QLatin1Char(' '), 0, 0);
            if (DrawingWidget* d = current())
                d->document().loadStandardLinetype(name.toStdString());
        }
        if (!name.isEmpty())
            applyLinetype(name);
    });
    connect(lineweightCombo_, &ComboModel::activated, this, [this](int index) {
        if (updatingCombos_)
            return;
        const QVariant v = lineweightCombo_->itemData(index);
        if (v.isValid() && v.toInt() != kLineweightUnset)
            applyLineweight(v.toInt());
    });
    connect(colorCombo_, &ComboModel::activated, this, [this](int index) {
        if (updatingCombos_)
            return;
        int color = colorCombo_->itemData(index).toInt();
        if (color == -2) {   // "Seleziona colore..."
            const auto picked = ColorDialog::pick(kColorByLayer, true, this);
            if (!picked) {
                updateColorCombo();
                return;
            }
            color = *picked;
        }
        if (color >= 0)
            applyColor(color);
    });

    return ribbon;
}

// ============================================================ area centrale

QWidget* MainWindow::buildCentralArea()
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Schede dei file (QML).
    fileTabs_ = new TabsModel(this);
    fileTabs_->setShown(appOptions().showFileTabs);
    if (qmlRootUi()) {
        // Scena unica: qui solo i modelli e i contenitori nascosti dei disegni.
        connect(fileTabs_, &TabsModel::newRequested, this, &MainWindow::newDrawing);
        stack_ = new QStackedWidget(central);
        startPage_ = new StartPage(stack_);
        stack_->addWidget(startPage_);
        fileTabs_->addTab(tr("Inizia"), false);
        connectStartPage(startPage_);
        commandLine_ = new CommandLine(central);
        commandLine_->setKeyHandler([this](QKeyEvent& key) { return handleCommandKey(key); });
        connect(commandLine_->input(), &CommandInput::returnPressed, this, &MainWindow::submitCommandLine);
        connect(commandLine_, &CommandLine::completionChosen, this, [this](const QString& word) {
            QTimer::singleShot(0, this, [this, word] {
                commandLine_->input()->clear();
                runCommand(word);
            });
        });
        connect(commandLine_, &CommandLine::historyRequested, this, &MainWindow::showHistory);
        connect(commandLine_, &CommandLine::closeRequested, this, [this] { commandLineAct_->setChecked(false); });
        connect(commandLine_->input(), &CommandInput::textChanged, this, [this](const QString& text) {
            if (DrawingWidget* d = current())
                d->view()->setTypedText(text);
        });
        connect(commandLine_, &CommandLine::historyChanged, this, &MainWindow::showCommandHistory);
        connect(fileTabs_, &TabsModel::currentChanged, this, &MainWindow::onTabChanged);
        connect(fileTabs_, &TabsModel::tabCloseRequested, this, [this](int index) { closeDrawingAt(index); });
        return central;
    }
    auto* tabsView = makeQuickWidget(central, theme::kPanelDark);
    tabsView->setFocusPolicy(Qt::NoFocus);
    tabsView->rootContext()->setContextProperty(QStringLiteral("tabs"), fileTabs_);
    tabsView->setSource(QUrl(QStringLiteral("qrc:/qml/FileTabs.qml")));
    tabsView->setFixedHeight(32);
    connect(fileTabs_, &TabsModel::shownChanged, tabsView, [this, tabsView] { tabsView->setVisible(fileTabs_->shown()); });
    connect(fileTabs_, &TabsModel::newRequested, this, &MainWindow::newDrawing);
    layout->addWidget(tabsView);

    // Area di disegno con la riga di comando sovrapposta.
    drawingArea_ = new QWidget(central);
    auto* areaLayout = new QVBoxLayout(drawingArea_);
    areaLayout->setContentsMargins(0, 0, 0, 0);
    stack_ = new QStackedWidget(drawingArea_);
    areaLayout->addWidget(stack_);
    layout->addWidget(drawingArea_, 1);

    startPage_ = new StartPage(stack_);
    stack_->addWidget(startPage_);
    fileTabs_->addTab(tr("Inizia"), false);
    connectStartPage(startPage_);

    // Riga di comando (QML) sopra il disegno; le righe della cronologia le disegna la vista.
    commandLine_ = new CommandLine(drawingArea_);
    commandLine_->raise();
    commandLine_->setKeyHandler([this](QKeyEvent& key) { return handleCommandKey(key); });
    drawingArea_->installEventFilter(this);
    connect(commandLine_, &CommandLine::heightChanged, this, &MainWindow::positionCommandLine);
    connect(commandLine_->input(), &CommandInput::returnPressed, this, &MainWindow::submitCommandLine);
    connect(commandLine_, &CommandLine::completionChosen, this, [this](const QString& word) {
        // Scelta di un suggerimento: il comando parte subito.
        QTimer::singleShot(0, this, [this, word] {
            commandLine_->input()->clear();
            runCommand(word);
        });
    });
    connect(commandLine_, &CommandLine::historyRequested, this, &MainWindow::showHistory);
    connect(commandLine_, &CommandLine::closeRequested, this, [this] { commandLineAct_->setChecked(false); });
    connect(commandLine_->input(), &CommandInput::textChanged, this, [this](const QString& text) {
        if (DrawingWidget* d = current())
            d->view()->setTypedText(text);
    });
    connect(commandLine_, &CommandLine::historyChanged, this, &MainWindow::showCommandHistory);

    connect(fileTabs_, &TabsModel::currentChanged, this, &MainWindow::onTabChanged);
    connect(fileTabs_, &TabsModel::tabCloseRequested, this, [this](int index) { closeDrawingAt(index); });
    return central;
}

void MainWindow::hideDocksForStart(bool start)
{
    if (start) {
        if (!startHiddenDocks_.isEmpty())
            return;
        for (QDockWidget* dock : {propertiesDock_, layersDock_, circuitsDock_, libraryDock_, plantDock_, guideDock_})
            if (dock && dock->isVisible() && !dock->isFloating()) {
                startHiddenDocks_ << dock;
                dock->hide();
            }
        return;
    }
    for (QDockWidget* dock : std::as_const(startHiddenDocks_))
        dock->show();
    startHiddenDocks_.clear();
}

// Tavolozze Circuiti e Impianto: lo schema letto in una arriva anche all'altra; il progetto aperto riempie
// Circuiti se è vuota.
void MainWindow::linkPlantAndCircuits()
{
    connect(circuits_, &CircuitsPanel::schemaImported, this, [this](const std::vector<Circuit>& schema, const QString& path) {
        if (plant_->hasProject())
            plant_->mergeFromSchema(schema, path);
    });
    connect(plant_, &PlantPanel::schemaImported, this, [this](const std::vector<Circuit>& schema, const QString& path) {
        circuits_->useSchema(schema, path);
    });
    connect(plant_, &PlantPanel::projectOpened, this, [this] {
        if (circuits_->circuits().empty() && !plant_->project().circuits.empty())
            circuits_->useSchema(plant_->asSchema(), plant_->projectFile());
    });
}

void MainWindow::guideAction(const QString& action)
{
    const QString arg = action.section(u':', 1);
    if (action.startsWith(QLatin1String("tab:"))) {
        ribbon_->selectTab(arg);
    } else if (action.startsWith(QLatin1String("panel:"))) {
        static const QStringList names = {QStringLiteral("Properties"), QStringLiteral("Layers"), QStringLiteral("Circuits"),
                                          QStringLiteral("Library"), QStringLiteral("Plant"), QStringLiteral("Guide")};
        const int index = names.indexOf(arg);
        if (index >= 0)
            showPanel(index, true);
        if (index == MainView::Plant)
            ribbon_->selectTab(tr("Impianto"));
    } else if (action.startsWith(QLatin1String("cmd:"))) {
        if (!current() && arg != QLatin1String("OPEN") && arg != QLatin1String("OPTIONS")) {
            print(tr("Prima aprire o creare un disegno (Apri, Nuovo)."));
            return;
        }
        runCommand(arg);
    } else if (action == QLatin1String("help")) {
        showHelp();
    }
}

void MainWindow::showPanel(int panel, bool on)
{
    if (mainView_) {
        mainView_->setPanelVisible(static_cast<MainView::Panel>(panel), on);
        return;
    }
    QDockWidget* dock = panel == MainView::Properties ? propertiesDock_
                        : panel == MainView::Layers   ? layersDock_
                        : panel == MainView::Library  ? libraryDock_
                        : panel == MainView::Plant    ? plantDock_
                        : panel == MainView::Guide    ? guideDock_
                                                      : circuitsDock_;
    dock->setVisible(on);
    if (on)
        dock->raise();
}

bool MainWindow::panelShown(int panel) const
{
    if (mainView_)
        return mainView_->panelVisible(static_cast<MainView::Panel>(panel));
    const QDockWidget* dock = panel == MainView::Properties ? propertiesDock_
                              : panel == MainView::Layers   ? layersDock_
                              : panel == MainView::Library  ? libraryDock_
                              : panel == MainView::Plant    ? plantDock_
                              : panel == MainView::Guide    ? guideDock_
                                                            : circuitsDock_;
    return dock && dock->isVisible();
}

void MainWindow::setCommandLineShown(bool on)
{
    if (mainView_)
        mainView_->setCommandLineVisible(on);
    else
        commandLine_->setVisible(on);
}

bool MainWindow::commandLineShown() const
{
    return mainView_ ? mainView_->commandLineVisible() : commandLine_->isVisible();
}

void MainWindow::positionCommandLine()
{
    if (mainView_) {   // scena unica: la posizione la decide MainView.qml
        showCommandHistory();
        return;
    }
    if (!drawingArea_ || !commandLine_)
        return;
    const int areaW = drawingArea_->width();
    const int w = std::min(areaW - 40, std::max(520, static_cast<int>(areaW * 0.62)));
    const int h = commandLine_->wantedHeight();
    commandLine_->setGeometry((areaW - w) / 2, drawingArea_->height() - h - 14, w, h);
    showCommandHistory();
}

// Ultime righe della cronologia sopra la riga di comando: le disegna la vista (i clic passano al disegno).
void MainWindow::showCommandHistory()
{
    DrawingWidget* d = current();
    if (!d || !commandLine_)
        return;
    const QStringList& lines = commandLine_->history();
    const QStringList last = commandLineAct_->isChecked() ? lines.mid(std::max<qsizetype>(0, lines.size() - 3)) : QStringList();
    if (mainView_) {   // scena unica: la barra la riporta MainView.qml, già nelle coordinate della vista
        d->view()->setCommandHistory(last, mainView_->commandBar());
        return;
    }
    // Posizione della barra (senza i suggerimenti) nelle coordinate della vista.
    const QPoint barTopLeft = d->view()->mapFrom(drawingArea_, QPoint(commandLine_->x(), commandLine_->y() + commandLine_->height() - 30));
    d->view()->setCommandHistory(last, QRect(barTopLeft, QSize(commandLine_->width(), 30)));
}

// ================================================================= tavolozze

void MainWindow::buildPalettes()
{
    const auto wireLibrary = [this] {
        connect(library_, &BlockLibrary::message, this, &MainWindow::print);
        connect(library_, &BlockLibrary::insertBlockRequested, this,
                [this](const QString& source, const QString& name) { insertFromLibrary(source, name); });
        connect(library_, &BlockLibrary::insertFileRequested, this, [this](const QString& path) {
            DrawingWidget* d = current();
            if (!d) {
                print(tr("Aprire un disegno per inserire il simbolo."));
                return;
            }
            d->cancel();
            QString error;
            if (!d->insertFile(path, &error))
                print(error);
            else
                library_->addRecent(path, QString());
        });
        connect(library_, &BlockLibrary::editBlockRequested, this, [this](const QString& name) { editBlock(current(), name); });
        connect(library_, &BlockLibrary::openFileRequested, this, [this](const QString& path) { openFile(path); });
    };
    struct Wire {
        std::function<void()> fn;
        ~Wire() { fn(); }
    } wire{wireLibrary};   // alla fine, in tutte e due le interfacce
    if (qmlRootUi()) {
        // Scena unica: le tavolozze sono pannelli di MainView.qml; qui i modelli (nascosti).
        properties_ = new PropertiesPalette(this);
        properties_->hide();
        circuits_ = new CircuitsPanel(this);
        circuits_->hide();
        connect(circuits_, &CircuitsPanel::message, this, &MainWindow::print);
        plant_ = new PlantPanel(this);
        plant_->hide();
        connect(plant_, &PlantPanel::message, this, &MainWindow::print);
        linkPlantAndCircuits();
        canalo_ = new CanaloPanel(this);
        canalo_->hide();
        wireCanalo();
        guide_ = new GuidePanel(this);
        guide_->hide();
        connect(guide_, &GuidePanel::stepEntered, this, &MainWindow::guideStepEntered);
        connect(plant_, &PlantPanel::guideRequested, this, [this] {
            guide_->openCommand(QStringLiteral("IMPIANTO"));
            showPanel(MainView::Guide);
        });
        library_ = new BlockLibrary(this);
        library_->hide();
        layerManager_ = new LayerManager(this);
        layerManager_->hide();
        connect(propertiesAct_, &QAction::triggered, this,
                [this] { showPanel(MainView::Properties, !panelShown(MainView::Properties)); });
        connect(layerPropsAct_, &QAction::triggered, this, [this] { showPanel(MainView::Layers); });
        return;
    }
    properties_ = new PropertiesPalette(this);
    propertiesDock_ = new QDockWidget(tr("Proprietà"), this);
    propertiesDock_->setObjectName("PropertiesDock");
    propertiesDock_->setWidget(properties_);
    propertiesDock_->setMinimumWidth(260);
    addDockWidget(Qt::LeftDockWidgetArea, propertiesDock_);
    propertiesDock_->hide();

    circuits_ = new CircuitsPanel(this);
    circuitsDock_ = new QDockWidget(tr("Circuiti"), this);
    circuitsDock_->setObjectName("CircuitsDock");
    circuitsDock_->setWidget(circuits_);
    circuitsDock_->setMinimumWidth(420);
    addDockWidget(Qt::RightDockWidgetArea, circuitsDock_);
    circuitsDock_->hide();
    connect(circuits_, &CircuitsPanel::message, this, &MainWindow::print);
    connect(circuitsDock_, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (on)
            circuits_->refresh();
    });

    plant_ = new PlantPanel(this);
    plantDock_ = new QDockWidget(tr("Impianto"), this);
    plantDock_->setObjectName("PlantDock");
    plantDock_->setWidget(plant_);
    plantDock_->setMinimumWidth(420);
    addDockWidget(Qt::RightDockWidgetArea, plantDock_);
    plantDock_->hide();
    connect(plant_, &PlantPanel::message, this, &MainWindow::print);
    linkPlantAndCircuits();
    connect(plantDock_, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (on)
            plant_->refresh();
    });

    canalo_ = new CanaloPanel(this);
    wireCanalo();

    guide_ = new GuidePanel(this);
    guideDock_ = new QDockWidget(tr("Guida"), this);
    guideDock_->setObjectName("GuideDock");
    guideDock_->setWidget(guide_);
    guideDock_->setMinimumWidth(340);
    addDockWidget(Qt::RightDockWidgetArea, guideDock_);
    guideDock_->hide();
    connect(guide_, &GuidePanel::stepEntered, this, &MainWindow::guideStepEntered);
    connect(plant_, &PlantPanel::guideRequested, this, [this] {
        guide_->openCommand(QStringLiteral("IMPIANTO"));
        showPanel(MainView::Guide);
    });

    library_ = new BlockLibrary(this);
    libraryDock_ = new QDockWidget(tr("Blocchi"), this);
    libraryDock_->setObjectName("LibraryDock");
    libraryDock_->setWidget(library_);
    libraryDock_->setMinimumWidth(260);
    addDockWidget(Qt::RightDockWidgetArea, libraryDock_);
    libraryDock_->hide();
    connect(libraryDock_, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (on)
            library_->refresh();
    });

    layerManager_ = new LayerManager(this);
    layersDock_ = new QDockWidget(tr("Gestore proprietà layer"), this);
    layersDock_->setObjectName("LayersDock");
    layersDock_->setWidget(layerManager_);
    layersDock_->setMinimumWidth(520);
    addDockWidget(Qt::LeftDockWidgetArea, layersDock_);
    layersDock_->hide();

    connect(propertiesAct_, &QAction::triggered, this, [this] {
        propertiesDock_->setVisible(!propertiesDock_->isVisible());
        if (propertiesDock_->isVisible())
            propertiesDock_->raise();
    });
    connect(layerPropsAct_, &QAction::triggered, this, [this] {
        showPanel(MainView::Layers);
    });
}

// ============================================================= barra di stato

void MainWindow::buildStatusBar()
{
    // Barra di stato in QML, a tutta larghezza sotto le tavolozze.
    status_ = new StatusBar(this);
    if (qmlRootUi()) {
        status_->hide();   // la mostra MainView.qml
    } else {
        statusBar()->setSizeGripEnabled(false);
        statusBar()->setContentsMargins(0, 0, 0, 0);
        statusBar()->addWidget(status_, 1);
    }

    // MODELLO / CARTA: nei layout passa dallo spazio carta al modello nella finestra.
    connect(status_, &StatusBar::spaceClicked, this, [this] {
        DrawingWidget* d = current();
        if (d && d->shownSpace() != 0)
            handleUiCommand(d->document().activeSpace() == 0 ? QStringLiteral("PSPACE") : QStringLiteral("MSPACE"));
    });

    // Scala e blocco della finestra corrente (nel modello) o della finestra selezionata (sul foglio).
    vpScaleMenu_ = new QMenu(this);
    const std::pair<const char*, double> scales[] = {
        {"1:1", 1.0},       {"1:2", 0.5},       {"1:5", 0.2},       {"1:10", 0.1},     {"1:20", 0.05},
        {"1:25", 0.04},     {"1:50", 0.02},     {"1:100", 0.01},    {"1:200", 0.005},  {"1:250", 0.004},
        {"1:500", 0.002},   {"1:1000", 0.001},  {"2:1", 2.0},       {"5:1", 5.0},      {"10:1", 10.0},
    };
    for (const auto& [label, value] : scales) {
        const double s = value;
        connect(vpScaleMenu_->addAction(QString::fromLatin1(label)), &QAction::triggered, this,
                [this, s] { setViewportScale(s); });
    }
    vpScaleMenu_->addSeparator();
    connect(vpScaleMenu_->addAction(tr("Personalizzata...")), &QAction::triggered, this, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Scala della finestra"),
                                                   tr("Scala (unità carta : unità modello, es. 1:75):"),
                                                   QLineEdit::Normal, status_->viewportScale().remove(QStringLiteral(" ▾")), &ok);
        if (!ok)
            return;
        const QStringList parts = text.split(u':');
        const double a = parts.value(0).trimmed().replace(u',', u'.').toDouble();
        const double b = parts.size() > 1 ? parts.value(1).trimmed().replace(u',', u'.').toDouble() : 1.0;
        if (a > 0.0 && b > 0.0)
            setViewportScale(a / b);
    });
    connect(status_, &StatusBar::viewportScaleClicked, this, [this](QPoint pos) {
        vpScaleMenu_->popup(pos - QPoint(0, vpScaleMenu_->sizeHint().height()));
    });
    // Scala di annotazione: le scale del disegno, poi la modifica della lista.
    connect(status_, &StatusBar::annoScaleClicked, this, [this](QPoint pos) {
        DrawingWidget* d = current();
        if (!d)
            return;
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const std::string currentScale = d->document().annotationScale();
        for (const AnnoScale& s : d->document().annoScales()) {
            QAction* a = menu->addAction(QString::fromStdString(s.name));
            a->setCheckable(true);
            a->setChecked(s.name == currentScale);
            const std::string name = s.name;
            connect(a, &QAction::triggered, this, [this, name] {
                if (DrawingWidget* dw = current()) {
                    dw->document().setAnnotationScale(name);
                    dw->notifyChanged();
                    print(tr("Scala di annotazione: %1").arg(QString::fromStdString(name)));
                }
            });
        }
        menu->addSeparator();
        menu->addAction(tr("Modifica elenco scale..."), this, [this] { runCommand(QStringLiteral("SCALELISTEDIT")); });
        menu->addAction(tr("Scale degli oggetti selezionati..."), this, [this] { runCommand(QStringLiteral("OBJECTSCALE")); });
        menu->popup(pos - QPoint(0, menu->sizeHint().height()));
    });
    connect(status_, &StatusBar::lockClicked, this, [this] {
        DrawingWidget* d = current();
        const EntityId id = statusViewport();
        if (!d || !id)
            return;
        d->modifyEntities({id}, [](Entity& e) {
            auto& vp = static_cast<ViewportEntity&>(e);
            vp.locked = !vp.locked;
        });
    });

    // Menu delle modalità di snap ad oggetto (freccia accanto al pulsante).
    auto* osnapMenu = new QMenu(this);
    const std::pair<int, QString> modes[] = {
        {Osnap::Endpoint, tr("Punto finale")},     {Osnap::Midpoint, tr("Punto medio")},
        {Osnap::Center, tr("Centro")},             {Osnap::Node, tr("Nodo")},
        {Osnap::Quadrant, tr("Quadrante")},        {Osnap::Intersection, tr("Intersezione")},
        {Osnap::Insertion, tr("Inserimento")},     {Osnap::Perpendicular, tr("Perpendicolare")},
        {Osnap::Tangent, tr("Tangente")},          {Osnap::Nearest, tr("Vicino")},
    };
    for (const auto& [bit, label] : modes) {
        QAction* a = osnapMenu->addAction(label);
        a->setCheckable(true);
        a->setData(bit);
        connect(a, &QAction::triggered, this, [this, bit](bool on) {
            settings_.osmode = on ? (settings_.osmode | bit) : (settings_.osmode & ~bit);
            osnapAct_->setChecked(true);
        });
    }
    osnapMenu->addSeparator();
    connect(osnapMenu->addAction(tr("Seleziona tutto")), &QAction::triggered, this, [this] {
        settings_.osmode = Osnap::Default | Osnap::Nearest;
        osnapAct_->setChecked(true);
    });
    connect(osnapMenu->addAction(tr("Ripristina predefinite")), &QAction::triggered, this, [this] {
        settings_.osmode = Osnap::Default;
        osnapAct_->setChecked(true);
    });
    connect(osnapMenu, &QMenu::aboutToShow, this, [this, osnapMenu] {
        for (QAction* a : osnapMenu->actions())
            if (a->isCheckable())
                a->setChecked(settings_.osmode & a->data().toInt());
    });
    osnapAct_->setMenu(osnapMenu);

    // Menu degli incrementi del puntamento polare (freccia accanto al pulsante).
    auto* polarMenu = new QMenu(this);
    for (double deg : {90.0, 45.0, 30.0, 22.5, 18.0, 15.0, 10.0, 5.0}) {
        QAction* a = polarMenu->addAction(QStringLiteral("%1°").arg(deg));
        a->setCheckable(true);
        a->setChecked(deg == settings_.polarAngleDeg);
        connect(a, &QAction::triggered, this, [this, deg, polarMenu, a] {
            settings_.polarAngleDeg = deg;
            for (QAction* other : polarMenu->actions())
                other->setChecked(other == a);
            polarAct_->setChecked(true);
            print(tr("Incremento angolo polare: %1°").arg(deg));
        });
    }
    polarAct_->setMenu(polarMenu);

    status_->setToggles({gridAct_, snapAct_, orthoAct_, polarAct_, osnapAct_, otrackAct_, dynAct_, lwAct_, commandLineAct_});
    // Annulla durante l'apertura: la lettura finisce nel suo thread, ma il disegno non si apre.
    connect(status_, &StatusBar::loadingCancelled, this, [this] {
        for (const auto& job : opening_)
            job->cancelled = true;
        opening_.clear();
        updateLoadingStatus();
    });
    // Schede Modello / layout in basso a sinistra.
    buildLayoutTabs();
}


// ==================================================================== disegni

DrawingWidget* MainWindow::current() const
{
    return qobject_cast<DrawingWidget*>(stack_->currentWidget());
}

DrawingWidget* MainWindow::createDrawing(const QString& name)
{
    // Il nome dei disegni nuovi ha l'estensione del formato scelto nelle Opzioni (DWG di default).
    const QString extension = appOptions().saveFormat == AppOptions::SaveDwg ? QStringLiteral("dwg") : QStringLiteral("dxf");
    const QString title = name.isEmpty() ? tr("Disegno%1.%2").arg(++untitledCounter_).arg(extension) : name;
    auto* drawing = new DrawingWidget(settings_, title, stack_);
    drawing->setUiCommandHandler([this](const QString& word) { return handleUiCommand(word); });
    // HATCH, opzione Motivo: anteprime dei motivi (negli script resta la riga di comando).
    drawing->setHatchPatternChooser([this] { return !scriptRunning_ && chooseHatchPattern(this); });
    drawing->setTextInPlaceAllowed([this] { return !scriptRunning_ || textInPlaceInScripts_; });
    drawing->setVariableHandlers(
        [this](const std::string& name) { return systemVariable(QString::fromStdString(name)).toStdString(); },
        [this](const std::string& name, const std::string& value) {
            return setSystemVariable(QString::fromStdString(name), QString::fromStdString(value));
        });

    setupView(drawing, drawing->view());
    // Finestre del modello affiancate: anche le viste nuove con gli stessi gestori; il QML le dispone.
    connect(drawing, &DrawingWidget::viewAdded, this, [this, drawing](CadView* v) { setupView(drawing, v); });
    connect(drawing, &DrawingWidget::viewsChanged, this, [this, drawing] {
        if (drawing == current()) {
            syncMainViews();
            updateUiState();
        }
    });

    connect(drawing, &DrawingWidget::commandFinished, this, &MainWindow::guideCommandFinished);
    connect(drawing, &DrawingWidget::message, this, [this, drawing](const QString& text) {
        if (drawing == current())
            print(text);
    });
    connect(drawing, &DrawingWidget::changed, this, &MainWindow::onDrawingChanged);
    connect(drawing, &DrawingWidget::cursorMoved, this, [this](Vec2 p) {
        status_->setCoordinates(QStringLiteral("%1, %2, %3")
                                 .arg(p.x, 0, 'f', 4)
                                 .arg(p.y, 0, 'f', 4)
                                 .arg(0.0, 0, 'f', 4));
    });

    stack_->addWidget(drawing);
    const int index = fileTabs_->addTab(drawing->displayName());
    fileTabs_->setCurrentIndex(index);
    return drawing;
}

// Gestori della finestra principale per una vista di un disegno (la prima e quelle affiancate).
void MainWindow::setupView(DrawingWidget* drawing, CadView* view)
{
    view->setFocusPolicy(Qt::ClickFocus);
    view->setFocusProxy(commandLine_);
    connect(view, &CadView::enterRequested, this, &MainWindow::submitCommandLine);
    connect(view, &CadView::blockDropped, this,
            [this, drawing](const QString& payload, Vec2 world) { dropLibraryItem(drawing, payload, world); });
    connect(drawing, &DrawingWidget::attributesRequested, this, &MainWindow::editAttributes);
    connect(view, &CadView::entityDoubleClicked, this, [this, drawing, view](EntityId id) {
        const Vec2 at = view->rawCursorWorld();
        // Blocchi con attributi (cartigli): doppio clic sul valore = si scrive lì; altrove la finestra degli
        // attributi.
        if (const Entity* e = drawing->document().find(id);
            e && e->type() == EntityType::Insert && !static_cast<const InsertEntity&>(*e).attributes.empty()) {
            if (editTextInPlace(*drawing, id, at))
                return;
            drawing->setSelection({id});
            editAttributes(id);
            return;
        }
        drawing->setSelection({id});
        // Blocco senza attributi: editor blocchi, come il doppio clic (non per i riferimenti esterni
        // e i blocchi anonimi dei blocchi dinamici).
        if (const Entity* e = drawing->document().find(id); e && e->type() == EntityType::Insert) {
            const std::string block = static_cast<const InsertEntity&>(*e).blockName;
            const auto def = drawing->document().findBlock(block);
            if (def && !def->isXref() && !block.empty() && block[0] != '*') {
                editBlock(drawing, QString::fromStdString(block));
                return;
            }
        }
        // Testi, direttrici, quote, tabelle: si scrive direttamente nel disegno; gli altri
        // oggetti: tavolozza Proprietà.
        if (const Entity* e = drawing->document().find(id); e && canEditTextInPlace(*e)) {
            editTextInPlace(*drawing, id, at);
            return;
        }
        showPanel(MainView::Properties);
    });
    connect(view, &CadView::contextMenuRequested, this, [this, drawing](QPoint pos) { showDrawingMenu(drawing, pos); });
    connect(view, &CadView::commandMenuRequested, this, [this, drawing](QPoint pos) { showCommandMenu(drawing, pos); });

}

// Viste mostrate dall'interfaccia QML: nel modello tutte quelle affiancate, nei layout la sola attiva.
void MainWindow::syncMainViews()
{
    if (!mainView_)
        return;
    DrawingWidget* d = current();
    if (!d) {
        mainView_->setViews({}, 0);
        return;
    }
    const bool model = d->shownSpace() == 0;
    std::vector<CadView*> views = model ? d->views() : std::vector<CadView*>{d->view()};
    const int mode = model ? static_cast<int>(d->tiles()) : 0;
    QVariantList now;
    for (CadView* v : views)
        now.append(QVariant::fromValue(static_cast<QObject*>(v)));
    if (now != mainView_->views() || mode != mainView_->tileMode())
        mainView_->setViews(views, mode);
    if (mainView_->view() != d->view())
        mainView_->setView(d->view(), false);
}

void MainWindow::editBlock(DrawingWidget* host, const QString& name)
{
    if (!host)
        return;
    if (host->isBlockEditor()) {
        print(tr("Dentro l'editor blocchi non si apre un altro editor: prima BCLOSE."));
        return;
    }
    // Già aperto: si passa alla sua scheda.
    for (int i = 1; i < stack_->count(); ++i)
        if (auto* d = qobject_cast<DrawingWidget*>(stack_->widget(i)); d && d->blockHost() == host && d->blockName() == name) {
            fileTabs_->setCurrentIndex(i);
            return;
        }
    Document temp;
    std::string error;
    if (!copyBlockForEditing(host->document(), name.toStdString(), temp, &error)) {
        print(QString::fromStdString(error));
        return;
    }
    DrawingWidget* editor = createDrawing(tr("Blocco: %1").arg(name));
    editor->adopt(std::move(temp), QString(), DxfReadResult{}, nullptr);
    editor->forgetFile();
    editor->setBlockEdit(host, name);
    const auto def = host->document().findBlock(name.toStdString());
    fileTabs_->setTabText(stack_->indexOf(editor), editor->displayName());
    QTimer::singleShot(0, editor, [editor] { editor->view()->zoomExtents(); });
    onDrawingChanged();
    print(tr("Editor blocchi: \"%1\" (%2 oggetti, punto base %3, %4). Si modifica con i comandi normali; "
             "BSAVE (o Salva) aggiorna il blocco nel disegno, BCLOSE chiude.")
              .arg(name)
              .arg(def ? def->entities.size() : 0)
              .arg(def ? def->base.x : 0.0, 0, 'f', 4)
              .arg(def ? def->base.y : 0.0, 0, 'f', 4));
}

void MainWindow::insertFromLibrary(const QString& source, const QString& name, std::optional<Vec2> at)
{
    DrawingWidget* d = current();
    if (!d) {
        print(tr("Aprire un disegno per inserire il simbolo."));
        return;
    }
    const std::string block = name.toStdString();
    if (!source.isEmpty() && !d->document().findBlock(block)) {
        std::shared_ptr<const Document> doc = library_->loadedDocument(source);
        if (!doc) {   // es. un recente di un altro file trascinato sul disegno: si legge ora
            auto read = std::make_shared<Document>();
            QString error;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool ok = DrawingWidget::readFile(source, *read, &error);
            QApplication::restoreOverrideCursor();
            if (!ok) {
                print(error);
                return;
            }
            doc = read;
        }
        // Unità diverse (es. libreria in cm, disegno in mm): il simbolo si scala come con l'inserimento di un file.
        const AppOptions& options = appOptions();
        const int from = doc->insUnits() > 0 ? doc->insUnits() : options.insertSourceUnits;
        const int to = d->document().insUnits() > 0 ? d->document().insUnits() : options.insertTargetUnits;
        const double scale = unitsFactor(from, to);
        if (!importBlock(d->document(), *doc, block, scale)) {
            print(tr("Blocco \"%1\" non trovato in %2.").arg(name, QDir::toNativeSeparators(source)));
            return;
        }
        if (std::abs(scale - 1.0) > 1e-12)
            print(tr("Unità della libreria: %1, del disegno: %2 - simbolo scalato %3.")
                      .arg(unitsName(from), unitsName(to))
                      .arg(scale, 0, 'g', 6));
        d->document().touch();
    } else if (!d->document().findBlock(block)) {
        print(tr("Il disegno non ha il blocco \"%1\".").arg(name));
        return;
    }
    library_->addRecent(source, name);
    d->cancel();
    d->notifyChanged();
    d->submit(QStringLiteral("INSERT"));
    d->submit(name);
    if (at) {   // rilasciato: punto, scala 1, rotazione 0 (restano solo gli eventuali attributi)
        d->submit(QStringLiteral("%1,%2").arg(at->x, 0, 'f', 8).arg(at->y, 0, 'f', 8));
        d->submit(QStringLiteral("1"));
        d->submit(QStringLiteral("0"));
    }
}

void MainWindow::dropLibraryItem(DrawingWidget* drawing, const QString& payload, Vec2 world)
{
    const QStringList parts = payload.split(QLatin1Char('\t'));
    if (parts.size() < 3 || !drawing)
        return;
    fileTabs_->setCurrentIndex(stack_->indexOf(drawing));
    if (parts.at(0) == QLatin1String("file")) {
        drawing->cancel();
        QString error;
        if (!drawing->insertFile(parts.at(1), &error)) {
            print(error);
            return;
        }
        library_->addRecent(parts.at(1), QString());
        drawing->submit(QStringLiteral("%1,%2").arg(world.x, 0, 'f', 8).arg(world.y, 0, 'f', 8));
        drawing->submit(QStringLiteral("1"));
        drawing->submit(QStringLiteral("0"));
        return;
    }
    insertFromLibrary(parts.at(1), parts.at(2), world);
}

void MainWindow::showLayerStates()
{
    DrawingWidget* d = current();
    if (!d) {
        print(tr("LAYERSTATE: nessun disegno aperto."));
        return;
    }
    Document& doc = d->document();
    FormDialog dialog(tr("Gestione stati dei layer"), FormDialog::Close, this);
    int selected = 0;
    int listField = -1;
    int infoField = -1;
    const auto names = [&] {
        QStringList rows;
        for (const auto& s : doc.layerStates())
            rows << (s.description.empty() ? QString::fromStdString(s.name)
                                           : QStringLiteral("%1   —   %2").arg(QString::fromStdString(s.name),
                                                                              QString::fromStdString(s.description)));
        return rows;
    };
    const auto refill = [&](int select) {
        const QStringList rows = names();
        selected = rows.isEmpty() ? -1 : std::clamp(select, 0, static_cast<int>(rows.size()) - 1);
        dialog.setItems(listField, rows, selected);
        dialog.setFieldText(infoField, rows.isEmpty()
                                           ? tr("Nessuno stato salvato. <b>Nuovo</b> salva accensione, congelamento, blocco, "
                                                "colore, tipo e spessore di linea e stampa di tutti i layer.")
                                           : tr("%1 stati. <b>Ripristina</b> rimette le proprietà salvate ai layer con lo "
                                                "stesso nome (si annulla con U); i layer nuovi restano come sono.")
                                                 .arg(rows.size()));
    };
    const auto chosen = [&]() -> std::string {
        if (selected < 0 || selected >= static_cast<int>(doc.layerStates().size()))
            return {};
        return doc.layerStates()[static_cast<std::size_t>(selected)].name;
    };
    dialog.group(tr("Stati salvati nel disegno"));
    listField = dialog.list(names(), 0, [&](int i) { selected = i; }, [&](int i) {
        selected = i;
        const int n = doc.restoreLayerState(chosen());
        d->notifyChanged();
        print(tr("Stato \"%1\" ripristinato: %2 layer.").arg(QString::fromStdString(chosen())).arg(n));
    });
    dialog.buttonsRow({
        {tr("Nuovo..."), [&] {
             bool ok = false;
             const QString name = QInputDialog::getText(&dialog, dialog.windowTitle(), tr("Nome del nuovo stato:"),
                                                        QLineEdit::Normal, QString(), &ok).trimmed();
             if (!ok || name.isEmpty())
                 return;
             const QString description = QInputDialog::getText(&dialog, dialog.windowTitle(), tr("Descrizione (facoltativa):"));
             doc.saveLayerState(name.toStdString(), description.toStdString());
             d->notifyChanged();
             refill(static_cast<int>(doc.layerStates().size()) - 1);
         }},
        {tr("Ripristina"), [&] {
             const std::string name = chosen();
             if (name.empty())
                 return;
             const int n = doc.restoreLayerState(name);
             d->notifyChanged();
             print(tr("Stato \"%1\" ripristinato: %2 layer.").arg(QString::fromStdString(name)).arg(n));
         }},
        {tr("Aggiorna"), [&] {
             const std::string name = chosen();
             if (name.empty())
                 return;
             std::string description;
             for (const auto& s : doc.layerStates())
                 if (s.name == name)
                     description = s.description;
             doc.saveLayerState(name, description);
             d->notifyChanged();
             refill(selected);
         }},
        {tr("Elimina"), [&] {
             const std::string name = chosen();
             if (name.empty())
                 return;
             doc.removeLayerState(name);
             d->notifyChanged();
             refill(selected);
         }},
    });
    infoField = dialog.note(QString());
    refill(0);
    dialog.setSize(560, 460);
    dialog.exec();
}

void MainWindow::chooseBlockToEdit()
{
    DrawingWidget* d = current();
    if (!d) {
        print(tr("BEDIT: nessun disegno aperto."));
        return;
    }
    if (d->isBlockEditor()) {
        print(tr("Dentro l'editor blocchi non si apre un altro editor: prima BCLOSE."));
        return;
    }
    // Un blocco selezionato si modifica subito.
    if (d->selection().size() == 1)
        if (const Entity* e = d->document().find(d->selection().front()); e && e->type() == EntityType::Insert) {
            const std::string block = static_cast<const InsertEntity&>(*e).blockName;
            if (!block.empty() && block[0] != '*') {
                editBlock(d, QString::fromStdString(block));
                return;
            }
        }
    const std::vector<std::string> names = userBlockNames(d->document());
    if (names.empty()) {
        print(tr("BEDIT: il disegno non ha blocchi."));
        return;
    }
    QStringList items;
    for (const std::string& n : names)
        items << QString::fromStdString(n);
    int chosen = 0;
    FormDialog dialog(tr("Modifica definizione blocco"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.note(tr("Blocco da modificare (%1 nel disegno):").arg(items.size()));
    dialog.list(items, 0, [&chosen](int i) { chosen = i; }, [&dialog, &chosen](int i) {
        chosen = i;
        dialog.accept();
    });
    dialog.setSize(420, 480);
    if (dialog.exec() != QDialog::Accepted || chosen < 0 || chosen >= items.size())
        return;
    editBlock(d, items.at(chosen));
}

bool MainWindow::saveBlockEdit(DrawingWidget* editor)
{
    DrawingWidget* host = editor->blockHost();
    if (!host) {
        print(tr("Il disegno del blocco \"%1\" è stato chiuso: le modifiche non si possono salvare.").arg(editor->blockName()));
        return false;
    }
    std::string error;
    const int inserts = saveEditedBlock(host->document(), editor->blockName().toStdString(), editor->document(), &error);
    if (inserts < 0) {
        print(QString::fromStdString(error));
        return false;
    }
    editor->markSaved();
    host->notifyChanged();
    onDrawingChanged();
    QString text = tr("Blocco \"%1\" salvato: %2 inserimenti aggiornati.").arg(editor->blockName()).arg(inserts);
    for (const auto& e : editor->document().entities())
        if (e->type() == EntityType::AttDef) {
            text += tr(" Gli attributi degli inserimenti esistenti si allineano con ATTSYNC.");
            break;
        }
    print(text);
    return true;
}

void MainWindow::closeBlockEditor()
{
    DrawingWidget* d = current();
    if (!d || !d->isBlockEditor()) {
        print(tr("BCLOSE vale solo nell'editor blocchi (BEDIT)."));
        return;
    }
    DrawingWidget* host = d->blockHost();
    if (closeDrawingAt(stack_->indexOf(d)) && host)
        fileTabs_->setCurrentIndex(stack_->indexOf(host));
}

void MainWindow::verifySavedDwg(const QString& path, const QString& summaryOwner)
{
    const auto it = pendingSummaries_.find(summaryOwner);
    if (it == pendingSummaries_.end())
        return;
    const std::shared_ptr<DocSummary> before = it->second;
    pendingSummaries_.erase(it);
    print(tr("Verifica del DWG salvato in corso..."));
    auto after = std::make_shared<DocSummary>();
    auto failure = std::make_shared<QString>();
    QPointer<MainWindow> self(this);
    QThread* thread = QThread::create([path, after, failure] {
        Document again;
        QString error;
        if (DrawingWidget::readFile(path, again, &error))
            *after = summarizeDocument(again);
        else
            *failure = error;
    });
    connect(thread, &QThread::finished, this, [this, self, thread, path, before, after, failure] {
        thread->deleteLater();
        if (!self)
            return;
        if (!failure->isEmpty()) {
            print(tr("Verifica del DWG: il file salvato non si rilegge (%1).").arg(failure->left(300)));
            QMessageBox::warning(this, tr("Verifica del DWG"),
                                 tr("Il file DWG appena salvato non si riesce a rileggere:\n%1\n\n"
                                    "Conviene salvare anche in DXF 2000, che conserva tutto.")
                                     .arg(QDir::toNativeSeparators(path)));
            return;
        }
        const DocComparison c = compareSummaries(*before, *after);
        if (c.identical()) {
            print(tr("Verifica del DWG: riletto, nessuna differenza (%1 oggetti).").arg(c.entitiesAfter));
            return;
        }
        const QString report = QString::fromStdString(c.report(12));
        print(tr("Verifica del DWG: %1").arg(report));
        QMessageBox::warning(this, tr("Verifica del DWG"),
                             tr("Il DWG salvato con %1 non contiene tutto il disegno:\n\n%2\n\n"
                                "Il disegno aperto non è cambiato. Per un DWG completo si può installare ODA File "
                                "Converter (gratuito, Opzioni > Apri e salva); il formato DXF 2000 conserva tutto e "
                                "gli altri CAD lo aprono direttamente.")
                                 .arg(DwgConverter::engineName(), report));
    });
    thread->start();
}

void MainWindow::newDrawing()
{
    DrawingWidget* drawing = createDrawing();
    // Opzioni per i disegni nuovi: unità, stampante, tabella stili, opzioni di stampa e foglio dei layout.
    Document& doc = drawing->document();
    doc.setInsUnits(appOptions().insertTargetUnits);
    PlotSettings model = doc.modelPlot();
    applyPlotDefaults(model);
    doc.setModelPlot(model);
    for (const Layout& l : doc.layouts())
        if (Layout* layout = doc.findLayout(l.id))
            applyPlotSettings(*layout, newLayoutPlotSettings());
    drawing->markSaved();
    QTimer::singleShot(0, drawing, [drawing] { drawing->view()->zoomExtents(); });
}

void MainWindow::open()
{
    const QStringList recent = recentFiles();
    const QString startDir = recent.isEmpty() ? QString() : QFileInfo(recent.first()).path();
    const QString path = QFileDialog::getOpenFileName(this, tr("Seleziona file"), startDir,
                                                      tr("Disegni (*.dwg *.dxf);;Disegni DWG (*.dwg);;Disegni DXF (*.dxf);;"
                                                         "Modelli DWT (*.dwt);;Tutti i file (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::openMany()
{
    const QStringList recent = recentFiles();
    const QString startDir = recent.isEmpty() ? QString() : QFileInfo(recent.first()).path();
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Seleziona i file da aprire"), startDir,
                                                            tr("Disegni (*.dwg *.dxf *.dwt);;Tutti i file (*)"));
    for (const QString& path : paths)
        openFile(path);
}

void MainWindow::newFromTemplate(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (!path.isEmpty())
            print(tr("Modello non trovato: %1. Disegno nuovo vuoto.").arg(QDir::toNativeSeparators(path)));
        newDrawing();
        return;
    }
    openFileAs(path, true);
}

void MainWindow::saveThumbnail(DrawingWidget* drawing, const QString& path)
{
    if (drawing && !path.isEmpty())
        storeThumbnail(path, drawing->view()->renderThumbnail({kThumbnailWidth, kThumbnailHeight}));
}

// Il file non ha un'anteprima (né in cache né salvata nel file): la si disegna appena il disegno è a video.
void MainWindow::scheduleThumbnail(DrawingWidget* drawing, const QString& path)
{
    if (scriptRunning_)
        return;
    QPointer<DrawingWidget> target(drawing);
    QTimer::singleShot(1500, this, [this, target, path] {
        if (target && target->filePath() == path && thumbnailFile(path).isEmpty())
            saveThumbnail(target, path);
    });
}

bool MainWindow::openFile(const QString& path)
{
    return openFileAs(path, false);
}

bool MainWindow::openFileAs(const QString& path, bool asTemplate)
{
    if (path.endsWith(QStringLiteral(".scr"), Qt::CaseInsensitive))
        return runScript(path);

    // Se il file è già aperto si passa alla sua scheda (un modello si apre sempre come disegno nuovo).
    for (int i = 1; i < stack_->count() && !asTemplate; ++i) {
        auto* d = qobject_cast<DrawingWidget*>(stack_->widget(i));
        if (d && QFileInfo(d->filePath()) == QFileInfo(path)) {
            fileTabs_->setCurrentIndex(i);
            return true;
        }
    }

    // Fuori dagli script il disegno si legge in un altro thread (conversione DWG, lettura): la finestra resta
    // utilizzabile e la barra di stato mostra la fase, con Annulla. Gli script aspettano (i comandi seguenti
    // lavorano sul disegno aperto).
    if ((!scriptRunning_ || forceAsyncOpen_) && isVisible()) {
        forceAsyncOpen_ = false;
        for (const auto& job : opening_)
            if (QFileInfo(job->path) == QFileInfo(path) && job->asTemplate == asTemplate)
                return true;   // già in apertura
        auto job = std::make_shared<OpenJob>();
        job->path = path;
        job->asTemplate = asTemplate;
        job->phase = tr("Apertura");
        opening_.push_back(job);
        updateLoadingStatus();
        QPointer<MainWindow> self(this);
        QThread* thread = QThread::create([job, self] {
            job->ok = DrawingWidget::readFile(job->path, job->document, &job->error, &job->result,
                                              [job, self](const QString& phase) {
                                                  QMetaObject::invokeMethod(qApp, [job, self, phase] {
                                                      job->phase = phase;
                                                      if (self)
                                                          self->updateLoadingStatus();
                                                  }, Qt::QueuedConnection);
                                              });
        });
        connect(thread, &QThread::finished, this, [this, job, thread] {
            thread->deleteLater();
            finishOpen(job);
        });
        thread->start();
        return true;
    }

    // Un disegno nuovo e vuoto viene sostituito.
    DrawingWidget* target = current();
    const bool reuse = target && target->filePath().isEmpty() && !target->isModified()
                       && target->document().entities().empty();
    if (!reuse)
        target = createDrawing();

    QString error;
    QStringList notes;
    if (!target->load(path, &error, &notes)) {
        const QString message = tr("Impossibile aprire %1:\n%2").arg(QDir::toNativeSeparators(path), error);
        if (scriptRunning_)
            print(message);
        else
            QMessageBox::critical(this, tr("Errore di apertura"), message);
        if (!reuse)
            closeDrawingAt(stack_->indexOf(target));
        return false;
    }
    for (const QString& n : notes)
        print(n);
    if (asTemplate)
        target->forgetFile();
    else
        addRecentFile(path);
    QTimer::singleShot(0, target, [target] { target->view()->zoomExtents(); });
    onDrawingChanged();
    auditAfterOpen(path);
    return true;
}

// Fine della lettura in un altro thread: il disegno va in una scheda (o sostituisce quello nuovo e vuoto).
void MainWindow::finishOpen(const std::shared_ptr<OpenJob>& job)
{
    std::erase(opening_, job);
    updateLoadingStatus();
    if (job->cancelled) {
        print(tr("Apertura di %1 annullata.").arg(QFileInfo(job->path).fileName()));
        return;
    }
    if (!job->ok) {
        QMessageBox::critical(this, tr("Errore di apertura"),
                              tr("Impossibile aprire %1:\n%2").arg(QDir::toNativeSeparators(job->path), job->error));
        return;
    }
    DrawingWidget* target = current();
    const bool reuse = target && target->filePath().isEmpty() && !target->isModified()
                       && target->document().entities().empty();
    if (!reuse)
        target = createDrawing();
    QStringList notes;
    target->adopt(std::move(job->document), job->path, job->result, &notes);
    for (const QString& n : notes)
        print(n);
    if (job->asTemplate) {
        target->forgetFile();
        print(tr("Disegno nuovo dal modello %1.").arg(QFileInfo(job->path).fileName()));
    } else {
        addRecentFile(job->path);
        scheduleThumbnail(target, job->path);
    }
    fileTabs_->setCurrentIndex(stack_->indexOf(target));
    QTimer::singleShot(0, target, [target] { target->view()->zoomExtents(); });
    onDrawingChanged();
    auditAfterOpen(job->path);
}

void MainWindow::updateLoadingStatus()
{
    if (!status_)
        return;
    if (opening_.empty()) {
        status_->setLoading(QString());
        return;
    }
    const auto& job = opening_.back();
    QString text = QStringLiteral("%1: %2").arg(job->phase, QFileInfo(job->path).fileName());
    if (opening_.size() > 1)
        text += tr("  (+%1)").arg(opening_.size() - 1);
    status_->setLoading(text);
}

bool MainWindow::save()
{
    DrawingWidget* d = current();
    return d && saveDrawing(d, d->filePath().isEmpty());
}

bool MainWindow::saveAs()
{
    DrawingWidget* d = current();
    return d && saveDrawing(d, true);
}

bool MainWindow::saveDrawing(DrawingWidget* drawing, bool askName)
{
    if (drawing->isBlockEditor())   // nell'editor blocchi Salva ridefinisce il blocco
        return saveBlockEdit(drawing);
    QString path = drawing->filePath();
    const QString autosaved = autosavePath(drawing);   // dopo il salvataggio non serve più

    // Sovrascrivere un DWG aperto: CartesioCAD non conserva ancora tutti gli oggetti DWG.
    if (!askName && drawing->isDwg()) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("CartesioCAD"),
                        tr("Questo disegno è un file DWG.\n\n"
                           "CartesioCAD non gestisce ancora tutti gli oggetti dei DWG (layout, riferimenti "
                           "esterni, immagini...) e la scrittura DWG di LibreDWG è sperimentale: "
                           "sovrascrivendo il file originale si potrebbero perdere dati.\n\n"
                           "Si consiglia di salvare una copia."),
                        QMessageBox::NoButton, this);
        QPushButton* copy = box.addButton(tr("Salva una copia..."), QMessageBox::AcceptRole);
        QPushButton* overwrite = box.addButton(tr("Sovrascrivi"), QMessageBox::DestructiveRole);
        box.addButton(tr("Annulla"), QMessageBox::RejectRole);
        box.setDefaultButton(copy);
        box.exec();
        if (box.clickedButton() == copy)
            askName = true;
        else if (box.clickedButton() != overwrite)
            return false;
    }

    DxfVersion version = drawing->dxfVersion();
    if (askName) {
        const QString dxfFilter = tr("Disegno DXF R12 - massima compatibilità (*.dxf)");
        const QString dxf2000Filter = tr("Disegno DXF 2000 - conserva tratteggi, ellissi, spline (*.dxf)");
        const QString dwgFilter = DwgConverter::engineName() == QLatin1String("LibreDWG")
                                      ? tr("Disegno DWG 2000 - tramite LibreDWG, sperimentale (*.dwg)")
                                      : tr("Disegno DWG - tramite ODA File Converter (*.dwg)");
        QString filter = drawing->isDwg() ? dwgFilter : (version == DxfVersion::R2000 ? dxf2000Filter : dxfFilter);
        if (path.isEmpty()) {   // disegno nuovo: il formato scelto nelle Opzioni
            const int format = appOptions().saveFormat;
            filter = format == AppOptions::SaveDwg ? dwgFilter : format == AppOptions::SaveDxf2000 ? dxf2000Filter : dxfFilter;
        }
        QString suggested = path.isEmpty() ? drawing->displayName() : path;
        if (drawing->isDwg() && !path.isEmpty())
            suggested = QFileInfo(path).path() + QStringLiteral("/") + QFileInfo(path).completeBaseName()
                        + tr(" - copia.dwg");
        path = QFileDialog::getSaveFileName(this, tr("Salva disegno con nome"), suggested,
                                            dxfFilter + QStringLiteral(";;") + dxf2000Filter + QStringLiteral(";;") + dwgFilter, &filter);
        if (path.isEmpty())
            return false;
        const bool wantDwg = filter == dwgFilter;
        version = filter == dxf2000Filter ? DxfVersion::R2000 : DxfVersion::R12;
        const QString suffix = wantDwg ? QStringLiteral(".dwg") : QStringLiteral(".dxf");
        if (!path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)
            && !path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive))
            path += suffix;
    }
    QString error;
    if (!drawing->saveTo(path, &error, version)) {
        QMessageBox::critical(this, tr("Errore di salvataggio"),
                              tr("Impossibile salvare %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    print(tr("Salvato: %1").arg(QDir::toNativeSeparators(path)));
    if (autosaved_.remove(autosaved))
        QFile::remove(autosaved);
    saveThumbnail(drawing, path);
    addRecentFile(path);
    if (path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)) {
        pendingSummaries_[path] = std::make_shared<DocSummary>(summarizeDocument(drawing->document()));
        verifySavedDwg(path, path);
    }
    return true;
}

bool MainWindow::maybeSave(DrawingWidget* drawing)
{
    if (!drawing->isModified())
        return true;
    fileTabs_->setCurrentIndex(stack_->indexOf(drawing));
    const QString question = drawing->isBlockEditor()
                                 ? tr("Salvare le modifiche al blocco \"%1\"? Tutti i suoi inserimenti si aggiornano.").arg(drawing->blockName())
                                 : tr("Salvare le modifiche a %1?").arg(drawing->displayName());
    const auto answer = QMessageBox::warning(
        this, QStringLiteral("CartesioCAD"), question,
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveDrawing(drawing, drawing->filePath().isEmpty());
    return answer == QMessageBox::Discard;
}

bool MainWindow::closeDrawingAt(int tabIndex)
{
    if (tabIndex <= 0)
        return false;
    if (isCanaloTab(tabIndex)) {   // i progetti di CanaloCAD restano nella memoria della pagina
        closeCanaloTab();
        return true;
    }
    auto* drawing = qobject_cast<DrawingWidget*>(stack_->widget(tabIndex));
    if (!drawing)
        return false;
    for (int i = stack_->count() - 1; i >= 1; --i)
        if (auto* editor = qobject_cast<DrawingWidget*>(stack_->widget(i)); editor && editor->blockHost() == drawing)
            if (!closeDrawingAt(i))
                return false;
    tabIndex = stack_->indexOf(drawing);
    fileTabs_->setCurrentIndex(tabIndex);
    if (!maybeSave(drawing))
        return false;

    properties_->setDrawing(nullptr);
    circuits_->setDrawing(nullptr);
    plant_->setDrawing(nullptr);
    canalo_->setDrawing(nullptr);
    library_->setDrawing(nullptr);
    layerManager_->setDrawing(nullptr);
    stack_->removeWidget(drawing);
    fileTabs_->removeTab(tabIndex);
    drawing->deleteLater();
    onTabChanged(fileTabs_->currentIndex());
    return true;
}

void MainWindow::onTabChanged(int index)
{
    if (index < 0 || index >= stack_->count())
        return;
    // Un testo in modifica nel disegno che si lascia: si conferma.
    for (int i = 0; i < stack_->count(); ++i)
        if (auto* d = qobject_cast<DrawingWidget*>(stack_->widget(i)); d && i != index)
            d->commitTextEditor();
    stack_->setCurrentIndex(index);
    DrawingWidget* d = current();
    properties_->setDrawing(d);
    circuits_->setDrawing(d);
    plant_->setDrawing(d);
    canalo_->setDrawing(d);
    library_->setDrawing(d);
    if (pickStyleAct_ && d) {
        const QSignalBlocker block(pickStyleAct_);
        pickStyleAct_->setChecked(d->document().groupSelection);
    }
    layerManager_->setDrawing(d);
    setCommandLineShown(d && commandLineAct_->isChecked());
    const bool canaloTab = isCanaloTab(index);
    if (d)
        lastDrawing_ = d;
    else if (canaloTab && lastDrawing_) {   // la scheda CanaloCAD mostra il progetto dell'ultimo disegno usato
        canaloFrom_ = lastDrawing_;
        canaloSyncOwner();
    }
    if (mainView_) {
        mainView_->setView(d ? d->view() : nullptr, d == nullptr && !canaloTab);
        mainView_->setCanaloVisible(canaloTab);
        syncMainViews();
        positionCanaloTab();
    }
    else
        hideDocksForStart(d == nullptr);
    if (!d && !canaloTab)
        fillStartPage(startPage_);
    positionCommandLine();
    updateUiState();
    if (d) {
        d->view()->setTypedText(commandLine_->input()->text());
        commandLine_->input()->setFocus();
    }
}

// ============================================================ riga di comando

void MainWindow::submitCommandLine()
{
    const QString text = commandLine_->input()->text();
    commandLine_->input()->clear();
    if (DrawingWidget* d = current()) {
        d->submit(text);
    } else if (!handleUiCommand(text.trimmed().toUpper())) {
        print(tr("Aprire o creare un disegno."));
    }
}

void MainWindow::runCommand(const QString& name)
{
    if (DrawingWidget* d = current())
        d->runCommand(name);
    commandLine_->input()->setFocus();
}

bool MainWindow::handleUiCommand(const QString& word)
{
    const auto is = [&word](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (word == QLatin1String(n))
                return true;
        return false;
    };

    // Rimandati al giro successivo dell'evento: possono aprire finestre o cambiare scheda.
    const auto later = [this](auto fn) { QTimer::singleShot(0, this, fn); };

    if (is({"NEW", "NUOVO"}))
        later([this] { newDrawing(); });
    else if (is({"VPORTS", "FINESTREMODELLO"}))
        later([this] { chooseTiles(); });
    else if (is({"COMPARE", "DWGCOMPARE", "CONFRONTADWG", "CONFRONTA"}))
        later([this] { startCompare(); });
    else if (is({"OPEN", "APRI"}))
        later([this] { open(); });
    else if (is({"QSAVE", "SAVE", "SALVA"}))
        later([this] { save(); });
    else if (is({"SAVEAS", "SALVACOME"}))
        later([this] { saveAs(); });
    else if (is({"CLOSE", "CHIUDI"}))
        later([this] { closeDrawingAt(fileTabs_->currentIndex()); });
    else if (is({"DWGCONVERT", "CONVERTI", "CONVERSIONE"}))
        later([this] { showBatchConvert(); });
    else if (is({"ESTRAIDATI", "ESPORTADATI", "DATICSV"}))
        later([this] { showDataExport(); });
    else if (is({"FRONTEQUADRO", "FRONTEQ", "FQ"}))
        later([this] { showSwitchboardFront(); });
    else if (is({"LAYERSTATE", "LAS", "STATILAYER"}))
        later([this] { showLayerStates(); });
    else if (is({"BEDIT", "BE", "EDITBLOCCO"}))
        later([this] { chooseBlockToEdit(); });
    else if (is({"BSAVE", "BS", "SALVABLOCCO"}))
        later([this] {
            if (DrawingWidget* d = current(); d && d->isBlockEditor())
                saveBlockEdit(d);
            else
                print(tr("BSAVE vale solo nell'editor blocchi (BEDIT)."));
        });
    else if (is({"BCLOSE", "BC", "CHIUDIBLOCCO"}))
        later([this] { closeBlockEditor(); });
    else if (is({"QUIT", "EXIT", "ESCI"}))
        later([this] { close(); });
    else if (is({"LAYER", "LA"}))
        layerPropsAct_->trigger();
    else if (is({"PROPERTIES", "PR", "PROPRIETA"})) {
        showPanel(MainView::Properties);
    } else if (is({"ABOUT", "INFORMAZIONI"})) {
        later([this] { showAbout(); });
    } else if (is({"SEGNALA", "SEGNALAPROBLEMA", "BUGREPORT"})) {
        later([this] { showReportDialog(); });
    } else if (is({"HELP", "AIUTO"}))
        showHelp();
    else if (is({"TEXTSCR"}))
        showHistory();
    else if (is({"TEXTEDIT", "DDEDIT", "ED", "MODTESTO"})) {
        // Modifica del testo selezionato direttamente nel disegno.
        DrawingWidget* d = current();
        const Entity* text = nullptr;
        if (d)
            for (EntityId id : d->selection())
                if (const Entity* e = d->document().find(id); e && canEditTextInPlace(*e)) {
                    text = e;
                    break;
                }
        if (text)
            editTextInPlace(*d, text->id());
        else
            print(tr("Scegli un testo (oppure doppio clic sul testo)."));
    } else if (is({"SELECTALL"}))
        selectAll();
    else if (is({"LAYON"}))
        allLayersOn();
    else if (is({"LAYMCUR"}))
        layerFromObject();
    else if (is({"SCRIPT", "SCR"}))
        later([this] { runScriptDialog(); });
    else if (is({"EXPORTPDF", "ESPORTAPDF", "PLOT", "STAMPA", "PRINT"}))
        later([this] { plotDialog(); });
    else if (is({"PREVIEW", "ANTEPRIMA", "PRE"}))
        later([this] { plotPreview(); });
    else if (is({"PUBLISH", "PUBBLICA"}))
        later([this] { publish(); });
    else if (is({"-PLOT", "-STAMPA"}))
        print(tr("-PLOT si usa negli script, su una riga: -PLOT AREA=ESTENSIONI SCALA=1:100 FOGLIO=A3 "
                 "STILI=Nero.ctb FILE=uscita.pdf"));
    else if (is({"BLOCKSPALETTE", "BLOCCHI", "LIBRERIA", "SIMBOLI", "ADC", "ADCENTER"})) {
        showPanel(MainView::Library);
    } else if (is({"CIRCUITI", "CIRCUITS"})) {
        showPanel(MainView::Circuits);
    } else if (is({"CANALOCAD", "CANALO"}))
        later([this] { canaloOpen(); });
    else if (is({"CANALINA", "CANALINE", "CABLETRAY"})) {
        canalo_->showWindow();
        canalo_->refresh();
    } else if (is({"CANALOSEZIONE", "CANALOCAVI"}))
        later([this] { canaloCompose(); });
    else if (is({"CANALOVERIFICA", "CANALOCHECK"}))
        later([this] { canaloCheck(true); });
    else if (is({"IMPIANTO"}))
        showPanel(MainView::Plant);
    else if (is({"GUIDA", "GUIDE"}))
        showPanel(MainView::Guide);
    else if (is({"TRATTA"}))
        later([this] { plantNewRun(); });
    else if (is({"VERIFICAIMPIANTO"}))
        later([this] { plantCheck(); });
    else if (is({"DISTINTAIMPIANTO"}))
        later([this] {
            showPanel(MainView::Plant);
            if (!plant_->hasProject()) {
                print(tr("DISTINTAIMPIANTO: prima creare o aprire il progetto dell'impianto (tavolozza Impianto)."));
                return;
            }
            plant_->bomDialog();
        });
    else if (is({"PERCORSIIMPIANTO", "PERCORSI"}))
        later([this] {
            showPanel(MainView::Plant);
            plant_->routesDialog();
        });
    else if (is({"TRATTEDALAYER"}))
        later([this] {
            showPanel(MainView::Plant);
            plant_->runsFromLayersDialog();
        });
    else if (is({"QUADROIMPIANTO"}))
        later([this] {
            showPanel(MainView::Plant);
            plant_->boardFromSelection();
        });
    else if (is({"ETICHETTETRATTE"}))
        later([this] {
            showPanel(MainView::Plant);
            plant_->labels();
            print(plant_->statusText());
        });
    else if (is({"SEZIONETRATTA"}))
        later([this] {
            showPanel(MainView::Plant);
            plant_->setTab(1);
            if (plant_->selectedRow() < 0)
                print(tr("SEZIONETRATTA: scegliere la tratta nella tavolozza Impianto (scheda Tratte), poi Disegna sezione."));
            else
                plant_->section(plant_->selectedRow());
        });
    else if (is({"CANALOINVIA", "CANALOSEND"}))
        later([this] { canaloSend(); });
    else if (is({"CANALOINS", "CANALOINSERISCI"}))
        later([this] { canaloInsert(); });
    else if (is({"CANALOULTIMA", "CANALOLAST"}))
        later([this] { canaloInsertLatest(); });
    else if (is({"OPTIONS", "OPZIONI", "OP", "PREFERENCES"}))
        later([this] { showOptions(); });
    else if (is({"PAGESETUP"}))
        pageSetupAct_->trigger();
    else if (is({"LAYOUT"}))
        later([this] { newLayout(); });
    else if (is({"-BENCH"})) {   // diagnosi delle prestazioni (non documentato)
        if (DrawingWidget* d = current()) {
            resize(1920, 1080);   // misura a grandezza reale
            QCoreApplication::processEvents();
            const double ms = d->view()->benchmarkRender(5);
            const QString line = QStringLiteral("ridisegno %1 ms (%2x%3)").arg(ms, 0, 'f', 1).arg(d->view()->width()).arg(d->view()->height());
            print(line);
            if (const QByteArray profile = qgetenv("CARTESIO_PROFILE"); !profile.isEmpty()) {
                QFile log(QString::fromLocal8Bit(profile));
                if (log.open(QIODevice::Append | QIODevice::Text))
                    log.write(line.toUtf8() + "\n");
            }
        }
    }
    else if (is({"PAN", "P", "MANINA"})) {
        if (DrawingWidget* d = current()) {
            d->view()->setPanMode(true);
            print(tr("PAN: trascinare con il tasto sinistro. Esc, Invio o tasto destro per uscire."));
        }
    } else if (is({"INSERTFILE"}))
        later([this] { insertFromFile(); });
    else if (is({"DIAGGPU"}))
        printGpuDiagnostics();
    else if (is({"QSELECT", "SELRAPIDA"}))
        later([this] { showQuickSelect(); });
    else if (is({"FIND", "TROVA"}))
        later([this] { showFind(); });
    else if (is({"BATTMAN", "GESTATT"}))
        later([this] { showAttributeManager(); });
    else if (is({"TESTINASCOSTI", "HIDDENTEXT"})) {
        if (DrawingWidget* d = current())
            for (const QString& line : d->view()->diagnoseTexts())
                print(line);
    } else if (is({"STYLESMANAGER", "STILISTAMPA", "CTB"}))
        later([this] { showPlotStyleManager(this); });
    else if (is({"XATTACH", "XA", "RIFX"}))
        later([this] { attachXrefFromFile(); });
    else if (is({"XRIF", "XREF", "XR", "EXTERNALREFERENCES", "ER", "RIFERIMENTI"}))
        later([this] { showExternalReferences(); });
    else if (is({"IMAGEATTACH", "IAT", "IMMAGINE"}))
        later([this] {
            const QString file = QFileDialog::getOpenFileName(this, tr("Collega immagine"), QString(),
                                                              tr("Immagini (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)"));
            if (!file.isEmpty())
                if (const auto type = askRefPathType(file))
                    attachImage(file, *type);
        });
    else if (is({"STYLE", "ST", "STILE"})) {
        if (DrawingWidget* d = current())
            later([this, d] {
                TextStyleDialog dialog(d, this);
                dialog.exec();
            });
    } else if (is({"MLEADERSTYLE", "MLS", "STILEDIRETTRICE"})) {
        if (DrawingWidget* d = current())
            later([this, d] {
                MLeaderStyleDialog dialog(d, this);
                dialog.exec();
            });
    } else if (is({"RECOVER", "RECUPERA"}))
        later([this] { recoverFile(); });
    else if (is({"ETRANSMIT", "ARCHIVIA"}))
        later([this] { etransmit(); });
    else if (is({"AGGIORNAMENTI", "AGGIORNA"}))
        later([this] { checkForUpdates(true); });
    else if (is({"VERSIONEPRECEDENTE"}))
        later([this] { rollbackUpdate(); });
    else if (is({"ASSISTENTE"}))
        later([this] { showClaudeDialog(); });
    else if (is({"DIMSTYLE", "D", "DST", "STILEQUOTA"})) {
        if (DrawingWidget* d = current())
            later([this, d] {
                DimStyleDialog dialog(d, this);
                dialog.exec();
            });
    }
    else if (is({"MSPACE", "MS"})) {
        // Si lavora nel modello attraverso la prima finestra del layout.
        if (DrawingWidget* d = current(); d && d->shownSpace() != 0)
            for (const auto& e : d->document().entities())
                if (e->space() == d->shownSpace() && e->type() == EntityType::Viewport) {
                    d->setViewport(e->id());
                    break;
                }
    } else if (is({"PSPACE", "PS"})) {
        if (DrawingWidget* d = current())
            d->setViewport(0);
    }
    else
        return false;
    return true;
}

void MainWindow::print(const QString& text)
{
    commandLine_->appendHistory(text);
    if (!logFile_.isEmpty()) {
        QFile log(logFile_);
        if (log.open(QIODevice::Append | QIODevice::Text))
            log.write((text + QLatin1Char('\n')).toUtf8());
    }
    positionCommandLine();
}

void MainWindow::showHistory()
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("CartesioCAD - Finestra di testo"));
    dialog->resize(720, 420);
    auto* layout = new QVBoxLayout(dialog);
    auto* text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(commandLine_->history().join('\n'));
    text->moveCursor(QTextCursor::End);
    layout->addWidget(text);
    dialog->show();
}

void MainWindow::showHelp()
{
    print(tr("Disegno:  LINE (L)  PLINE (PL)  CIRCLE (C)  ARC (A)  RECTANG (REC)  ELLIPSE (EL)  POINT (PO)  HATCH (H)"));
    print(tr("Modifica: MOVE (M)  COPY (CO)  OFFSET (O)  TRIM (TR)  EXPLODE (X)  ERASE (E)  UNDO (U)  REDO"));
    print(tr("Annota:   MTEXT (T)  TEXT (DT)  DIMLINEAR (DLI)  DIMALIGNED (DAL)  DIMRADIUS (DRA)  DIMDIAMETER (DDI)"));
    print(tr("Modifica: ROTATE (RO)  SCALE (SC)  MIRROR (MI)  ARRAY (AR)  EXTEND (EX)  FILLET (F)  MATCHPROP (MA)"));
    print(tr("Blocchi:  BLOCK (B)  INSERT (I)  ATTDEF (ATT)   Layout: MVIEW (MV)  EXPORTPDF (Ctrl+P)"));
    print(tr("Utilità:  DIST (DI)  AREA (AA)  ID  LIST (LI)  COPYCLIP (Ctrl+C)  PASTECLIP (Ctrl+V)"));
    print(tr("          MEASUREGEOM (MEA, misura rapida)  COPYBASE (Ctrl+Maiusc+C)  ADDSELECTED"));
    print(tr("Stili:    STYLE (ST)  MLEADERSTYLE (MLS)  DIMSTYLE   Quote: DIMTEDIT  DIMEDIT   Disegno: SPLINE (SPL)"));
    print(tr("File:     AUDIT  RECOVER  ETRANSMIT (archivio da inviare)  AGGIORNAMENTI  VERSIONEPRECEDENTE  ASSISTENTE"));
    print(tr("          ESTRAIDATI (dati in CSV)  DWGCONVERT (conversione in serie)"));
    print(tr("Impianto: CIRCUITI  IMPIANTO  TRATTA  VERIFICAIMPIANTO  FRONTEQUADRO (FQ, fronte quadri dallo schema)"));
    print(tr("Selezione: Maiusc+Spazio oggetto sovrapposto successivo  trascinando = lazo  SELECTPREVIOUS"));
    print(tr("          Tutto/Ultimo/Precedente/Rimuovi nelle richieste  ISOLATEOBJECTS  HIDEOBJECTS  UNISOLATEOBJECTS"));
    print(tr("Script:   SCRIPT (SCR)  SETVAR  -LAYER  COLOR  OSMODE  ORTHOMODE  TILEMODE  ATTREQ ..."));
    print(tr("Vista e file: ZOOM (Z)  LAYER (LA)  PROPERTIES (PR, Ctrl+1)  NEW  OPEN  QSAVE  SAVEAS  CLOSE"));
    print(tr("Coordinate: 10,20  @5,0  @50<30   Tasti: F2 cronologia  F3 osnap  F7 griglia  F8 orto  "
             "F9 snap  F10 polare  F12 input dinamico"));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (!commandLine_ || !drawingArea_)   // finestra ancora in costruzione
        return QMainWindow::eventFilter(watched, event);
    if (watched == drawingArea_ && event->type() == QEvent::Resize) {
        positionCommandLine();
        return false;
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::handleCommandKey(QKeyEvent& key)
{
    CommandInput* input = commandLine_->input();
    const bool empty = input->text().isEmpty();
    DrawingWidget* d = current();

    if (key.key() == Qt::Key_Escape) {
        input->clear();
        if (d)
            d->cancel();
        return true;
    }
    // Ciclo di selezione: con oggetti sovrapposti sotto il mirino, Maiusc+Spazio evidenzia il successivo.
    if (key.key() == Qt::Key_Space && key.modifiers().testFlag(Qt::ShiftModifier) && empty && d && !d->wantsText()
        && d->view()->hoverCandidateCount() > 1) {
        d->view()->cycleHover();
        return true;
    }
    if (key.key() == Qt::Key_Space && !(d && d->wantsLine())) {   // lo spazio equivale a Invio
        submitCommandLine();
        return true;
    }
    if (empty && key.matches(QKeySequence::Undo)) {
        runCommand(QStringLiteral("UNDO"));
        return true;
    }
    if (empty && key.matches(QKeySequence::Redo)) {
        runCommand(QStringLiteral("REDO"));
        return true;
    }
    if (empty && key.key() == Qt::Key_Delete) {
        runCommand(QStringLiteral("ERASE"));
        return true;
    }
    if (empty && key.key() == Qt::Key_V && key.modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        runCommand(QStringLiteral("PASTEBLOCK"));   // Ctrl+Maiusc+V: incolla come blocco
        return true;
    }
    if (empty && key.matches(QKeySequence::Cut)) {
        runCommand(QStringLiteral("CUTCLIP"));
        return true;
    }
    if (empty && key.key() == Qt::Key_C && key.modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        runCommand(QStringLiteral("COPYBASE"));   // Ctrl+Maiusc+C: copia con punto base
        return true;
    }
    if (empty && key.matches(QKeySequence::Copy)) {
        runCommand(QStringLiteral("COPYCLIP"));
        return true;
    }
    if (empty && key.matches(QKeySequence::Paste)) {
        runCommand(QStringLiteral("PASTECLIP"));
        return true;
    }
    if (empty && key.matches(QKeySequence::SelectAll)) {
        selectAll();
        return true;
    }
    return false;
}

// Con la riga di comando nascosta, iniziare a scrivere la fa ricomparire.
void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (current() && !commandLineShown() && !event->text().trimmed().isEmpty()) {
        commandLineAct_->setChecked(true);
        commandLine_->input()->setFocus();
        commandLine_->input()->insert(event->text());
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// ========================================================= stato dell'interfaccia

void MainWindow::onDrawingChanged()
{
    auto* drawing = qobject_cast<DrawingWidget*>(sender());
    if (!drawing)
        drawing = current();
    if (!drawing)
        return;

    const int index = stack_->indexOf(drawing);
    if (index > 0)
        fileTabs_->setTabText(index, drawing->displayName() + (drawing->isModified() ? QStringLiteral("*") : QString()));

    if (drawing == current())
        updateUiState();
}

// Proprietà rapide (QPMODE): compaiono accanto al cursore quando si selezionano oggetti senza un
// comando attivo, spariscono deselezionando o avviando un comando. Non prendono il fuoco: si continua a
// scrivere nella riga di comando.
// Ultimi comandi dati (dalla cronologia "Comando: XXX"), dal più recente, senza ripetizioni.
QStringList MainWindow::recentCommands(int max) const
{
    QStringList result;
    const QStringList& lines = commandLine_->history();
    const QString marker = tr("Comando:");
    for (auto it = lines.crbegin(); it != lines.crend() && result.size() < max; ++it) {
        if (!it->startsWith(marker))
            continue;
        const QString word = it->mid(marker.size()).trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
        if (!word.isEmpty() && !word.startsWith(QLatin1Char('-')) && !result.contains(word))
            result << word;
    }
    return result;
}

// Menu del tasto destro senza comando attivo, con le stesse voci e lo stesso ordine.
void MainWindow::showDrawingMenu(DrawingWidget* drawing, QPoint pos)
{
    QMenu menu(this);
    const bool selected = !drawing->selection().empty();
    const auto run = [this](const char* command) { return [this, command] { runCommand(QString::fromLatin1(command)); }; };

    const QString last = QString::fromStdString(drawing->commands().lastCommand());
    if (!last.isEmpty())
        menu.addAction(tr("Ripeti %1").arg(last), this, [this, last] { runCommand(last); });
    QMenu* recent = menu.addMenu(tr("Input recente"));
    for (const QString& word : recentCommands(10))
        recent->addAction(word, this, [this, word] { runCommand(word); });
    recent->setEnabled(!recent->isEmpty());
    menu.addSeparator();

    QMenu* clip = menu.addMenu(tr("Appunti"));
    QAction* cut = clip->addAction(tr("Taglia"), this, run("CUTCLIP"));
    cut->setShortcut(QKeySequence::Cut);
    cut->setEnabled(selected);
    QAction* copy = clip->addAction(icon(Icon::Copy), tr("Copia"), this, run("COPYCLIP"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(selected);
    QAction* copyBase = clip->addAction(tr("Copia con punto base"), this, run("COPYBASE"));
    copyBase->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyBase->setEnabled(selected);
    QAction* paste = clip->addAction(icon(Icon::Paste), tr("Incolla"), this, run("PASTECLIP"));
    paste->setShortcut(QKeySequence::Paste);
    QAction* pasteBlock = clip->addAction(tr("Incolla come blocco"), this, run("PASTEBLOCK"));
    pasteBlock->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    clip->addAction(tr("Incolla sulle coordinate originali"), this, run("PASTEORIG"));

    QMenu* isolate = menu.addMenu(tr("Isola"));
    isolate->addAction(tr("Isola oggetti"), this, run("ISOLATEOBJECTS"))->setEnabled(selected);
    isolate->addAction(tr("Nascondi oggetti"), this, run("HIDEOBJECTS"))->setEnabled(selected);
    isolate->addAction(tr("Termina isolamento oggetti"), this, run("UNISOLATEOBJECTS"))
        ->setEnabled(drawing->document().hiddenCount() > 0);
    menu.addSeparator();

    if (!selected) {
        menu.addAction(undoAct_);
        menu.addAction(redoAct_);
        menu.addAction(panAct_);
        menu.addAction(zoomExtentsAct_);
        menu.addSeparator();
        menu.addAction(tr("Selezione rapida..."), this, [this] { showQuickSelect(); });
        menu.addAction(tr("Trova..."), this, [this] { showFind(); });
        menu.addAction(tr("Opzioni..."), this, [this] { showOptions(); });
        menu.exec(pos);
        return;
    }

    menu.addAction(icon(Icon::Erase), tr("Cancella"), this, run("ERASE"));
    menu.addAction(icon(Icon::Move), tr("Sposta"), this, run("MOVE"));
    menu.addAction(icon(Icon::Copy), tr("Copia selezione"), this, run("COPY"));
    menu.addAction(icon(Icon::ScaleIcon), tr("Scala"), this, run("SCALE"));
    menu.addAction(icon(Icon::Rotate), tr("Ruota"), this, run("ROTATE"));
    // Ordine di visualizzazione degli oggetti selezionati (DRAWORDER).
    QMenu* order = menu.addMenu(tr("Ordine di visualizzazione"));
    const auto reorder = [drawing](Document::DrawOrder where) {
        drawing->document().reorder(drawing->selection(), where);
        drawing->notifyChanged();
    };
    order->addAction(tr("Porta in primo piano"), this, [reorder] { reorder(Document::DrawOrder::Front); });
    order->addAction(tr("Porta dietro"), this, [reorder] { reorder(Document::DrawOrder::Back); });
    order->addAction(tr("Sopra oggetti..."), this, run("DRAWORDER"));
    order->addSeparator();
    order->addAction(tr("Testi e quote in primo piano"), this, run("TEXTTOFRONT"));
    order->addAction(tr("Tratteggi dietro"), this, run("HATCHTOBACK"));
    menu.addSeparator();
    QMenu* group = menu.addMenu(tr("Gruppo"));
    group->addAction(icon(Icon::GroupIcon), tr("Raggruppa"), this, run("GROUP"));
    group->addAction(icon(Icon::UngroupIcon), tr("Separa"), this, run("UNGROUP"));
    menu.addSeparator();
    // Blocchi: valori degli attributi ed editor blocchi.
    if (drawing->selection().size() == 1)
        if (const Entity* e = drawing->document().find(drawing->selection().front()); e && e->type() == EntityType::Insert) {
            const auto& insert = static_cast<const InsertEntity&>(*e);
            const EntityId id = e->id();
            if (!insert.attributes.empty())
                menu.addAction(tr("Modifica attributi..."), this, [this, id] { editAttributes(id); });
            const QString block = QString::fromStdString(insert.blockName);
            if (!block.startsWith(QLatin1Char('*')))
                menu.addAction(tr("Editor blocchi"), this, [this, drawing, block] { editBlock(drawing, block); });
            menu.addSeparator();
        }
    menu.addAction(tr("Aggiungi selezione"), this, run("ADDSELECTED"))->setEnabled(drawing->selection().size() == 1);
    menu.addAction(icon(Icon::MatchProp), tr("Seleziona simile"), this, run("SELECTSIMILAR"));
    menu.addAction(tr("Deseleziona tutto"), this, [drawing] { drawing->setSelection({}); });
    menu.addSeparator();
    menu.addAction(tr("Selezione rapida..."), this, [this] { showQuickSelect(); });
    menu.addAction(tr("Trova..."), this, [this] { showFind(); });
    menu.addAction(propertiesAct_);
    menu.addAction(tr("Proprietà rapide"), this, [this] {
        quickPropsForced_ = true;
        quickPropsFor_.clear();   // si riposiziona accanto al cursore
        updateQuickProperties();
    });
    menu.exec(pos);
}

// Menu del tasto destro durante un comando: Invio, Annulla, le opzioni della richiesta, Pan e Zoom.
void MainWindow::showCommandMenu(DrawingWidget* drawing, QPoint pos)
{
    QMenu menu(this);
    menu.addAction(tr("Invio"), this, [this] { submitCommandLine(); });
    menu.addAction(tr("Annulla"), this, [drawing] { drawing->cancel(); });
    // Opzioni fra parentesi quadre: "[Distanza/Raggio/ANgolo]"; si manda la sigla (le maiuscole).
    const QString prompt = drawing->prompt();
    const int open = prompt.indexOf(QLatin1Char('['));
    const int close = prompt.indexOf(QLatin1Char(']'), open + 1);
    if (open >= 0 && close > open) {
        menu.addSeparator();
        for (const QString& option : prompt.mid(open + 1, close - open - 1).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            QString keyword;
            for (QChar c : option)
                if (c.isUpper() || c.isDigit())
                    keyword += c;
            if (keyword.isEmpty())
                keyword = option.toUpper();
            QString label = option.trimmed();
            if (!label.isEmpty())
                label[0] = label[0].toUpper();
            menu.addAction(label, this, [drawing, keyword] { drawing->submit(keyword); });
        }
    }
    menu.addSeparator();
    menu.addAction(panAct_);
    menu.addAction(zoomExtentsAct_);
    menu.exec(pos);
}

int MainWindow::quickPropertiesRows() const
{
    return quickProps_ ? quickProps_->rowCount() : 0;
}

void MainWindow::updateQuickProperties()
{
    DrawingWidget* d = current();
    if (!d || d->selection().empty())
        quickPropsForced_ = false;   // aperte dal tasto destro: fino alla prossima deselezione
    const bool wanted = d && (appOptions().quickProperties || quickPropsForced_) && !d->selection().empty()
                        && !d->commands().isActive() && d->selection().size() <= 5000;
    if (!wanted) {
        if (quickPropsWindow_)
            quickPropsWindow_->hide();
        quickPropsFor_.clear();
        if (quickProps_)
            quickProps_->setDrawing(nullptr);
        return;
    }
    if (!quickPropsWindow_) {
        quickPropsWindow_ = new QFrame(this, Qt::Tool | Qt::FramelessWindowHint);
        quickPropsWindow_->setObjectName(QStringLiteral("QuickProperties"));
        quickPropsWindow_->setAttribute(Qt::WA_ShowWithoutActivating);
        static_cast<QFrame*>(quickPropsWindow_)->setFrameShape(QFrame::Box);
        quickPropsWindow_->setStyleSheet(QStringLiteral("QFrame#QuickProperties { border: 1px solid #5a6476; }"));
        auto* layout = new QVBoxLayout(quickPropsWindow_);
        layout->setContentsMargins(1, 1, 1, 1);
        quickProps_ = new PropertiesPalette(quickPropsWindow_, true);
        quickProps_->setMinimumWidth(250);
        layout->addWidget(quickProps_);
        connect(quickProps_, &PropertiesPalette::rowsChanged, quickPropsWindow_, [this] {
            const int rows = std::max(1, quickProps_->rowCount());   // riga ~26 px più la tendina in alto
            quickPropsWindow_->resize(270, std::min(46 + rows * 28, 330));
        });
    }
    const bool changed = d->selection() != quickPropsFor_;
    quickPropsFor_ = d->selection();
    quickProps_->setDrawing(d);
    if (changed || !quickPropsWindow_->isVisible()) {
        if (quickPropsWindow_->height() < 60)
            quickPropsWindow_->resize(270, 160);
        QPoint at = QCursor::pos() + QPoint(28, 20);
        if (const QScreen* screen = QGuiApplication::screenAt(QCursor::pos())) {
            const QRect area = screen->availableGeometry();
            at.setX(std::min(at.x(), area.right() - quickPropsWindow_->width()));
            at.setY(std::min(at.y(), area.bottom() - quickPropsWindow_->height()));
        }
        quickPropsWindow_->move(at);
        quickPropsWindow_->show();
    }
}

void MainWindow::updateUiState()
{
    DrawingWidget* d = current();
    updateQuickProperties();
    for (QAction* a : drawingActions_)
        a->setEnabled(d != nullptr);
    layerCombo_->setEnabled(d != nullptr);
    colorCombo_->setEnabled(d != nullptr);

    if (d) {
        undoAct_->setEnabled(d->document().canUndo());
        redoAct_->setEnabled(d->document().canRedo());
        commandLine_->setPrompt(d->commands().isActive() ? d->prompt() : QString());
        commandLine_->setCompletionEnabled(!d->commands().isActive());
        commandLine_->input()->setPlaceholderText(d->commands().isActive() ? QString() : tr("Scrivi un comando"));
        const QString name = d->displayName() + (d->isModified() ? QStringLiteral("*") : QString());
        ribbon_->setTitle(QStringLiteral("CartesioCAD   %1").arg(name));
        setWindowTitle(QStringLiteral("CartesioCAD - %1").arg(name));
    } else {
        ribbon_->setTitle(QStringLiteral("CartesioCAD"));
        setWindowTitle(QStringLiteral("CartesioCAD"));
        status_->setCoordinates(QString());
    }
    updateLayerCombo();
    refreshLayoutTabs();
    updateAnnotateCombos();
    updateColorCombo();
    updateLinetypeCombos();
    properties_->refresh();
    circuits_->refresh();
    if (canalo_ && canalo_->isVisible())
        canalo_->refresh();
    if (panelShown(MainView::Plant))
        plant_->refresh();
    if (panelShown(MainView::Library))
        library_->refresh();
    layerManager_->refresh();
    positionCommandLine();
}

void MainWindow::updateLayerCombo()
{
    updatingCombos_ = true;
    layerCombo_->clear();
    if (DrawingWidget* d = current()) {
        const Document& doc = d->document();
        const auto& layers = doc.layers();
        for (int i = 0; i < static_cast<int>(layers.size()); ++i)
            layerCombo_->addItem(layerIcon(layers[static_cast<std::size_t>(i)]),
                                 QString::fromStdString(layers[static_cast<std::size_t>(i)].name), i);

        std::set<int> used;
        for (EntityId id : d->selection())
            if (const Entity* e = doc.find(id))
                used.insert(e->layer());
        if (d->selection().empty()) {
            layerCombo_->setCurrentIndex(doc.currentLayer());
        } else if (used.size() == 1) {
            layerCombo_->setCurrentIndex(*used.begin());
        } else {
            layerCombo_->insertItem(0, QStringLiteral("*VARIA*"), -1);
            layerCombo_->setCurrentIndex(0);
        }
    }
    updatingCombos_ = false;
}

void MainWindow::updateColorCombo()
{
    updatingCombos_ = true;
    colorCombo_->clear();
    if (DrawingWidget* d = current()) {
        const Document& doc = d->document();
        std::set<int> used;
        for (EntityId id : d->selection())
            if (const Entity* e = doc.find(id))
                used.insert(e->color());
        const int shown = d->selection().empty() ? doc.currentColor() : (used.size() == 1 ? *used.begin() : -1);

        QList<int> entries = {kColorByLayer, kColorByBlock, 1, 2, 3, 4, 5, 6, 7};
        if (shown >= 0 && !entries.contains(shown))
            entries << shown;
        if (shown < 0)
            colorCombo_->addItem(QStringLiteral("*VARIA*"), -1);
        for (int c : entries)
            colorCombo_->addItem(colorIcon(c), theme::aciName(c), c);
        colorCombo_->addItem(tr("Seleziona colore..."), -2);
        colorCombo_->setCurrentIndex(std::max(0, colorCombo_->findData(shown)));
    }
    updatingCombos_ = false;
}

void MainWindow::applyColor(int color)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    if (d->selection().empty()) {
        d->document().setCurrentColor(color);
        d->notifyChanged();
    } else {
        d->modifyEntities(d->selection(), [color](Entity& e) {
            e.setColor(color);
            e.setTrueColor(-1);   // il colore scelto sostituisce quello RGB
        });
    }
}


// Tipo e spessore di linea correnti, o degli oggetti selezionati .
void MainWindow::updateLinetypeCombos()
{
    if (!linetypeCombo_ || !lineweightCombo_)
        return;
    updatingCombos_ = true;
    linetypeCombo_->clear();
    lineweightCombo_->clear();
    DrawingWidget* d = current();
    linetypeCombo_->setEnabled(d != nullptr);
    lineweightCombo_->setEnabled(d != nullptr);
    if (d) {
        const Document& doc = d->document();
        std::set<std::string> types;
        std::set<int> weights;
        for (EntityId id : d->selection())
            if (const Entity* e = doc.find(id)) {
                std::string lt = e->linetype().empty() ? std::string("ByLayer") : e->linetype();
                if (const Linetype* found = doc.findLinetype(lt))
                    lt = found->name;
                else if (QString::fromStdString(lt).compare(QStringLiteral("ByLayer"), Qt::CaseInsensitive) == 0)
                    lt = "ByLayer";
                else if (QString::fromStdString(lt).compare(QStringLiteral("ByBlock"), Qt::CaseInsensitive) == 0)
                    lt = "ByBlock";
                types.insert(lt);
                weights.insert(e->lineweight() == kLineweightUnset ? kLineweightByLayer : e->lineweight());
            }
        const bool none = d->selection().empty();
        const QString shownType = none ? QString::fromStdString(doc.currentLinetype())
                                       : (types.size() == 1 ? QString::fromStdString(*types.begin()) : QString());
        const int shownWeight = none ? doc.currentLineweight() : (weights.size() == 1 ? *weights.begin() : kLineweightUnset);

        if (shownType.isEmpty())
            linetypeCombo_->addItem(QStringLiteral("*VARIA*"), QString());
        linetypeCombo_->addItem(tr("DaLayer"), QStringLiteral("ByLayer"));
        linetypeCombo_->addItem(tr("DaBlocco"), QStringLiteral("ByBlock"));
        for (const Linetype& lt : doc.linetypes())
            linetypeCombo_->addItem(QString::fromStdString(lt.name), QString::fromStdString(lt.name));
        linetypeCombo_->addItem(tr("Altro..."), QStringLiteral("*ALTRO*"));
        int index = 0;
        for (int i = 0; i < linetypeCombo_->count(); ++i)
            if (linetypeCombo_->itemData(i).toString().compare(shownType, Qt::CaseInsensitive) == 0)
                index = i;
        linetypeCombo_->setCurrentIndex(index);

        if (shownWeight == kLineweightUnset)
            lineweightCombo_->addItem(QStringLiteral("*VARIA*"), kLineweightUnset);
        lineweightCombo_->addItem(tr("DaLayer"), kLineweightByLayer);
        lineweightCombo_->addItem(tr("DaBlocco"), kLineweightByBlock);
        lineweightCombo_->addItem(tr("Default"), kLineweightDefault);
        for (int lw : standardLineweights())
            lineweightCombo_->addItem(QStringLiteral("%1 mm").arg(lw / 100.0, 0, 'f', 2), lw);
        lineweightCombo_->setCurrentIndex(std::max(0, lineweightCombo_->findData(shownWeight)));
    }
    updatingCombos_ = false;
}

void MainWindow::applyLinetype(const QString& name)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const std::string lt = name.toStdString();
    if (d->selection().empty()) {
        d->document().setCurrentLinetype(lt);
        d->notifyChanged();
    } else {
        d->modifyEntities(d->selection(), [lt](Entity& e) { e.setLinetype(lt); });
    }
}

void MainWindow::applyLineweight(int lineweight)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    if (d->selection().empty()) {
        d->document().setCurrentLineweight(lineweight);
        d->notifyChanged();
    } else {
        d->modifyEntities(d->selection(), [lineweight](Entity& e) { e.setLineweight(lineweight); });
    }
}
void MainWindow::selectAll()
{
    DrawingWidget* d = current();
    if (!d || d->commands().isActive())
        return;
    std::vector<EntityId> ids;
    for (const auto& e : d->document().entities())
        if (d->document().isSelectable(*e))
            ids.push_back(e->id());
    d->setSelection(std::move(ids));
    print(tr("%1 oggetti selezionati.").arg(d->selection().size()));
}

void MainWindow::layerFromObject()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    if (d->selection().empty()) {
        print(tr("Scegli prima un oggetto, poi usa \"Rendi corrente\"."));
        return;
    }
    const Entity* e = d->document().find(d->selection().front());
    if (!e)
        return;
    d->document().setCurrentLayer(e->layer());
    print(tr("Layer corrente: %1").arg(QString::fromStdString(d->document().layers()[static_cast<std::size_t>(e->layer())].name)));
    d->notifyChanged();
}

void MainWindow::allLayersOn()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    Document& doc = d->document();
    for (int i = 0; i < static_cast<int>(doc.layers().size()); ++i)
        doc.layer(i).visible = true;
    doc.touch();
    print(tr("Tutti i layer sono accesi."));
    d->notifyChanged();
}

// File DWG e DXF trascinati sulla finestra.
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".scr"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        paths << url.toLocalFile();
    event->acceptProposedAction();
    QTimer::singleShot(0, this, [this, paths] {
        for (const QString& path : paths)
            if (path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)
                || path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)
                || path.endsWith(QStringLiteral(".scr"), Qt::CaseInsensitive))
                openFile(path);
    });
}

// ============================================================ documenti recenti

QStringList MainWindow::recentFiles() const
{
    QSettings store = settingsStore();
    return store.value("recentFiles").toStringList();
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings store = settingsStore();
    QStringList files = recentFiles();
    const QStringList pinned = store.value("pinnedFiles").toStringList();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    files.removeAll(absolute);
    files.prepend(absolute);
    // Oltre il numero delle Opzioni escono i più vecchi; quelli bloccati restano.
    const int limit = std::max(0, appOptions().recentFiles);
    for (qsizetype i = files.size() - 1; i >= 0 && files.size() > limit; --i)
        if (!pinned.contains(files.at(i)))
            files.removeAt(i);
    QVariantMap opened = store.value("recentOpened").toMap();
    opened.insert(absolute, QDateTime::currentDateTime());
    for (auto it = opened.begin(); it != opened.end();)
        it = files.contains(it.key()) ? std::next(it) : opened.erase(it);
    store.setValue("recentFiles", files);
    store.setValue("recentOpened", opened);
    updateRecentMenus();
}

void MainWindow::togglePinnedFile(const QString& path)
{
    QSettings store = settingsStore();
    QStringList pinned = store.value("pinnedFiles").toStringList();
    if (pinned.contains(path))
        pinned.removeAll(path);
    else
        pinned << path;
    store.setValue("pinnedFiles", pinned);
    updateRecentMenus();
}

void MainWindow::removeRecentFile(const QString& path)
{
    QSettings store = settingsStore();
    QStringList files = recentFiles();
    files.removeAll(path);
    QStringList pinned = store.value("pinnedFiles").toStringList();
    pinned.removeAll(path);
    QVariantMap opened = store.value("recentOpened").toMap();
    opened.remove(path);
    store.setValue("recentFiles", files);
    store.setValue("pinnedFiles", pinned);
    store.setValue("recentOpened", opened);
    updateRecentMenus();
}

void MainWindow::fillStartPage(StartPage* page)
{
    QSettings store = settingsStore();
    QHash<QString, QDateTime> opened;
    const QVariantMap times = store.value("recentOpened").toMap();
    for (auto it = times.begin(); it != times.end(); ++it)
        opened.insert(it.key(), it.value().toDateTime());
    page->setRecentFiles(recentFiles(), store.value("pinnedFiles").toStringList(), opened);
}

void MainWindow::connectStartPage(StartPage* page)
{
    connect(page, &StartPage::newRequested, this, &MainWindow::newDrawing);
    connect(page, &StartPage::templateRequested, this, &MainWindow::newFromTemplate);
    connect(page, &StartPage::openRequested, this, &MainWindow::open);
    connect(page, &StartPage::openManyRequested, this, &MainWindow::openMany);
    connect(page, &StartPage::recentRequested, this, [this](const QString& path) { openFile(path); });
    connect(page, &StartPage::pinToggled, this, &MainWindow::togglePinnedFile);
    connect(page, &StartPage::removeRequested, this, &MainWindow::removeRecentFile);
    connect(page, &StartPage::commandRequested, this, &MainWindow::runStartCommand);
}

// Dalla scheda APPRENDI: i comandi si provano in un disegno (se non ce n'è uno se ne apre uno nuovo).
void MainWindow::runStartCommand(const QString& name)
{
    if (!current())
        newDrawing();
    QTimer::singleShot(0, this, [this, name] { runCommand(name); });
}

void MainWindow::updateRecentMenus()
{
    const QStringList files = recentFiles();
    recentMenu_->clear();
    for (const QString& f : files) {
        QAction* a = recentMenu_->addAction(QFileInfo(f).fileName());
        a->setToolTip(QDir::toNativeSeparators(f));
        connect(a, &QAction::triggered, this, [this, f] { openFile(f); });
    }
    recentMenu_->setEnabled(!files.isEmpty());
    fillStartPage(startPage_);
}

// ================================================================== chiusura

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int i = stack_->count() - 1; i >= 1; --i) {
        auto* d = qobject_cast<DrawingWidget*>(stack_->widget(i));
        if (d && !discardOnClose_ && !maybeSave(d)) {
            event->ignore();
            return;
        }
    }
    appOptions().draft = settings_;
    appOptions().save();
    QSettings store = settingsStore();
    store.setValue("window/geometry", saveGeometry());
    if (!mainView_)
        hideDocksForStart(false);   // la disposizione si salva con le tavolozze che l'utente aveva aperte
    if (mainView_)
        store.setValue("window/panels", mainView_->saveState());
    else
        store.setValue("window/state", saveState());
    // Chiusura regolare: i salvataggi automatici di questa sessione non servono più (se restano, al prossimo
    // avvio vuol dire che il programma non si è chiuso bene).
    for (const QString& path : std::as_const(autosaved_))
        QFile::remove(path);
    autosaved_.clear();
    store.setValue("sessione/aperta", false);   // chiusura regolare
    event->accept();
}

} // namespace cartesio

namespace cartesio {

// INSERT di un file DWG/DXF: il disegno diventa un blocco (con i suoi layer, tipi di linea, stili e
// blocchi) e si posiziona con INSERT, come "Sfoglia" nel comando INSERT.
void MainWindow::insertFromFile()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Inserisci disegno come blocco"), QString(),
                                                      tr("Disegni (*.dwg *.dxf);;DWG (*.dwg);;DXF (*.dxf)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!d->insertFile(path, &error))
        QMessageBox::critical(this, tr("Inserisci"), error);
}

// XATTACH: collega un disegno come riferimento esterno e avvia INSERT per posizionarlo.
void MainWindow::attachXrefFromFile()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Riferimento esterno"), QString(),
                                                      tr("Disegni (*.dwg *.dxf);;DWG (*.dwg);;DXF (*.dxf)"));
    if (path.isEmpty())
        return;
    const auto type = askRefPathType(path);
    if (!type)
        return;
    QString error;
    if (!d->attachXref(path, &error, *type))
        QMessageBox::critical(this, tr("Riferimento esterno"), error);
}

} // namespace cartesio

namespace cartesio {

// ================================================================== opzioni

void MainWindow::showOptions(int tab)
{
    OptionsDialog dialog(this);
    if (tab >= 0)
        dialog.setCurrentTab(tab);
    connect(&dialog, &OptionsDialog::applied, this, &MainWindow::applyOptions);
    dialog.exec();
}

void MainWindow::applyOptions()
{
    // Salvataggio automatico.
    if (!autosaveTimer_) {
        autosaveTimer_ = new QTimer(this);
        connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    }
    autosaveTimer_->stop();
    if (appOptions().autosave)
        autosaveTimer_->start(std::max(1, appOptions().autosaveMinutes) * 60 * 1000);
    updateCanaloWatch();
    updateRecentMenus();
    // Mirino, maniglie e marcatori si ridisegnano con le nuove misure.
    for (int i = 0; i < stack_->count(); ++i)
        if (auto* d = qobject_cast<DrawingWidget*>(stack_->widget(i))) {
            d->document().touch();   // colori della scena (colore 7 sugli sfondi chiari) da ricalcolare
            d->view()->refresh();
        }
}

void MainWindow::autosave()
{
    const QString folder = QDir::tempPath() + QStringLiteral("/CartesioCAD");
    QDir().mkpath(folder);
    for (int i = 0; i < stack_->count(); ++i) {
        auto* d = qobject_cast<DrawingWidget*>(stack_->widget(i));
        if (!d || !d->isModified() || d->isBlockEditor())
            continue;
        // Come i .sv$, ma in DXF: si apre direttamente.
        const QString path = autosavePath(d);
        std::string failure;
        if (writeDxf(d->document(), std::filesystem::path(path.toStdWString()), &failure, nullptr, DxfVersion::R2000)) {
            autosaved_.insert(path);
            print(tr("Salvataggio automatico: %1").arg(QDir::toNativeSeparators(path)));
        }
        else
            print(tr("Salvataggio automatico non riuscito: %1").arg(QString::fromStdString(failure)));
    }
}

} // namespace cartesio
