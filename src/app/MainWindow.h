#pragma once

#include "app/AppOptions.h"
#include "app/ClaudeLink.h"
#include "app/DraftSettings.h"
#include "core/DataExport.h"
#include "core/Switchboard.h"
#include "app/PlotDialog.h"
#include "app/Updater.h"
#include "core/Entity.h"
#include "core/Vec2.h"

#include <QDateTime>
#include <QImage>
#include <QJsonValue>
#include <QMainWindow>
#include <QUrl>
#include <QPointer>
#include <QSet>
#include <QMap>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabBar;
class QTimer;
class QFileSystemWatcher;
class QToolButton;
class QPagedPaintDevice;

namespace cartesio {

class CadView;
class Document;
struct Layout;

class CommandLine;
class ComboModel;
class TabsModel;
class StatusBar;
struct OpenJob;
class DrawingWidget;
class LayerManager;
class PropertiesPalette;
class CircuitsPanel;
class PlantPanel;
class CanaloPanel;
class GuidePanel;
class Ribbon;
class StartPage;
class CanaloTab;
class BlockLibrary;
struct DocSummary;

// FRONTEQUADRO: cosa leggere e cosa produrre (vedi MainWindowSwitchboard.cpp).
struct SwitchboardRequest {
    QString pdf;            // schema unifilare
    QString boards;         // sigle dei quadri separate da virgola (vuoto = tutti)
    QString catalog;        // catalogo CSV delle carpenterie (vuoto = solo i tipi generici)
    QString devices;        // catalogo CSV degli apparecchi con i blocchi dei costruttori (vuoto = ingombri tipici)
    QString family;         // marca (famiglia di blocchi del catalogo) per tutti i quadri; vuoto = dallo schema
    BoardOptions options;
    bool draw = true;
    bool newDrawing = true; // altrimenti nel disegno aperto, scalato sulle sue unità
    QString ifc, csv;       // vuoti = non si scrivono
};

// Finestra principale: barra di accesso rapido, barra multifunzione,
// schede dei disegni, riga di comando mobile, tavolozze e barra di stato.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Conversione in serie (DWGCONVERT).
    enum ConvertFormat { ConvertDxf2000 = 0, ConvertDxfR12 = 1, ConvertDwg = 2, ConvertPdf = 3 };
    struct BatchConvertOptions {
        QString source;
        QString output;   // vuoto = sottocartella "Convertiti" della sorgente
        bool recursive = false;
        int format = ConvertDxf2000;
        bool overwrite = false;
        bool verify = true;
    };

    bool openFile(const QString& path);
    // All'avvio senza file: salvataggi automatici rimasti da una chiusura anomala, da recuperare.
    void checkAutosaveRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // --- costruzione dell'interfaccia
    void createActions();
    QWidget* buildTopArea();
    QWidget* buildCentralArea();
    void buildPalettes();
    void buildStatusBar();
    QAction* makeAction(const QString& text, const QIcon& icon, const QKeySequence& shortcut = {},
                        const QString& tip = {});
    QAction* makeCommand(const QString& text, const QIcon& icon, const QString& command,
                         const QKeySequence& shortcut = {});
    QAction* makeToggle(const QString& text, const QIcon& icon, const QKeySequence& shortcut, bool DraftSettings::*field);

    // --- disegni
    DrawingWidget* current() const;
    DrawingWidget* createDrawing(const QString& name = {});
    // Editor blocchi: BEDIT apre una scheda con gli oggetti del blocco, BSAVE lo ridefinisce, BCLOSE chiude.
    void editBlock(DrawingWidget* host, const QString& name);
    void chooseBlockToEdit();
    // Gestione stati dei layer (LAYERSTATE).
    void showLayerStates();
    void showBatchConvert();
    // Restituisce il percorso del rapporto. `interactive`: finestra di avanzamento e letture in un altro thread.
    QString runBatchConvert(const BatchConvertOptions& options, bool interactive);
    // ESTRAIDATI: dati del disegno in CSV (core/DataExport).
    void showDataExport();
    bool writeDataExport(DrawingWidget* d, const QString& path, const DataExportOptions& options);
    // FRONTEQUADRO: fronte dei quadri dallo schema unifilare in PDF. Restituisce l'errore (vuoto se riuscito).
    void showSwitchboardFront();
    QString runSwitchboardFront(const SwitchboardRequest& request);
    bool saveBlockEdit(DrawingWidget* editor);
    void closeBlockEditor();
    void newDrawing();
    // Disegno nuovo da un modello (.dwt/.dwg/.dxf): copia senza nome del file.
    void newFromTemplate(const QString& path);
    void open();
    void openMany();
    bool openFileAs(const QString& path, bool asTemplate);
    // Miniatura per la pagina Inizia (al salvataggio, o all'apertura se il file non ne ha una).
    void saveThumbnail(DrawingWidget* drawing, const QString& path);
    void scheduleThumbnail(DrawingWidget* drawing, const QString& path);
    bool save();
    bool saveAs();
    bool saveDrawing(DrawingWidget* drawing, bool askName);
    // Dopo un salvataggio DWG: il file si rilegge in un altro thread e si confronta con il disegno; se manca
    // qualcosa (LibreDWG non scrive tutto) si avvisa subito.
    void verifySavedDwg(const QString& path, const QString& summaryOwner);
    bool maybeSave(DrawingWidget* drawing);
    bool closeDrawingAt(int tabIndex);
    void onTabChanged(int index);

    // --- riga di comando
    void submitCommandLine();
    void runCommand(const QString& name);
    bool handleUiCommand(const QString& word);
    void print(const QString& text);
    // LOGFILEON: la cronologia della riga di comando va anche in questo file (vuoto = spento).
    QString logFile_;
    void positionCommandLine();
    void showCommandHistory();
    void showHistory();
    void showHelp();
    // Informazioni su CartesioCAD: versione, licenza, nessuna garanzia, componenti e sorgenti.
    void showAbout();
    static QString aboutText(bool html);
    // Segnala un problema: pagina "nuova segnalazione" di GitHub già compilata (MainWindowReport.cpp).
    void showReportDialog();
    static QString anonymizeForReport(QString text);
    QString reportTechnical(bool withHistory) const;
    QString reportBody(const QString& what, const QString& steps, bool technical, bool withHistory) const;
    QUrl reportUrl(const QString& kind, const QString& title, const QString& what, const QString& steps, bool technical,
                   bool withHistory, QString* overflow) const;
    bool writeReportForTest(const QString& path, const QString& what);

    // --- layout, stampa, script (MainWindowLayouts.cpp)
    void buildLayoutTabs();
    void refreshLayoutTabs();
    // Gruppi delle schede dei layout (MainWindowLayouts.cpp).
    QString layoutGroupOf(const Document& doc, const Layout& layout) const;
    void showLayoutGroupMenu(const QString& tabText, QPoint globalPos);
    void showLayoutList(QPoint globalPos);
    void setLayoutGroup(const std::vector<int>& ids, const QString& group);
    void updateViewportStatus();
    EntityId statusViewport() const;
    void setViewportScale(double scale);
    void showLayoutMenu(int index, const QPoint& globalPos);
    // Tasti speciali della riga di comando (Esc, spazio, Ctrl+Z con il campo vuoto...).
    bool handleCommandKey(QKeyEvent& key);
    void newLayout();
    void renameLayout(int id);
    void deleteLayout(int id);
    void pageSetup(int id);
    // Stampa (PLOT): dialogo. `state` riapre il dialogo dopo la scelta della finestra.
    void plotDialog(std::optional<PlotDialogState> state = std::nullopt);
    // Anteprima di stampa dello spazio mostrato con le sue impostazioni (PREVIEW).
    void plotPreview();
    // Tutti i layout in un PDF, ciascuno con le sue impostazioni (PUBLISH).
    void publish();
    // Stampa lo spazio mostrato in PDF con le impostazioni salvate (EXPORTPDF negli script).
    bool exportPdfTo(DrawingWidget* drawing, const QString& path, QString* error);
    // -PLOT negli script: opzioni chiave=valore su una riga.
    bool plotFromOptions(DrawingWidget* drawing, const QString& line, QString* error);
    // Esegue la stampa scelta (PDF, stampante o file) e salva le preferenze.
    void runPlot(DrawingWidget* drawing, const PlotDialogState& state);
    void runScriptDialog();
    bool runScript(const QString& path);
    QString systemVariable(const QString& name) const;
    bool setSystemVariable(const QString& name, const QString& value);

    // --- aggiornamento dell'interfaccia
    void onDrawingChanged();
    void updateUiState();
    void updateLayerCombo();
    void updateColorCombo();
    void applyColor(int color);
    void updateLinetypeCombos();
    void insertFromFile();
    void attachXrefFromFile();
    // Tavolozze e riga di comando: nella scena QML unica (MainView) o nei dock della finestra a widget.
    void showPanel(int panel, bool on = true);
    bool panelShown(int panel) const;
    void setCommandLineShown(bool on);
    bool commandLineShown() const;
    // Riferimenti esterni (XRIF): disegni collegati e immagini; IMAGEATTACH di un file.
    void showExternalReferences();
    // Attributi dei blocchi: valori di un inserimento (EATTEDIT) e definizioni di un blocco (BATTMAN).
    void editAttributes(EntityId id);
    void showAttributeManager(const QString& blockName = {});
    // FIND: trova e sostituisci.
    void showFind(const QString& initial = {});
    // QSELECT: selezione rapida per tipo e proprietà.
    void showQuickSelect();
    // DIAGGPU: scheda grafica, driver, scala dello schermo, impostazioni del disegno.
    void printGpuDiagnostics();
    bool attachImage(const QString& path, int pathType = -1);
    // Tipo di percorso per un file da collegare (dialogo); nullopt se annullato.
    std::optional<int> askRefPathType(const QString& file);
    void applyLinetype(const QString& name);
    void applyLineweight(int lineweight);
    void selectAll();
    // Finestra Opzioni (OPZIONI) e applicazione delle opzioni cambiate.
    void showOptions(int tab = -1);
    void applyOptions();
    // Salvataggio automatico dei disegni modificati (opzione), in una cartella temporanea.
    void autosave();
    // File del salvataggio automatico di un disegno. Si cancella salvando il disegno e chiudendo il programma:
    // se all'avvio ce n'è ancora, il programma non si era chiuso bene e si propone il recupero.
    QString autosavePath(const DrawingWidget* drawing) const;
    // RECOVER: apre il file e lo corregge con AUDIT (MainWindowTransmit.cpp).
    void recoverFile(const QString& path = {});
    void auditAfterOpen(const QString& path);
    // ETRANSMIT: archivio ZIP con il disegno e i file collegati (riferimenti, immagini, font, tabelle stili).
    bool etransmit(const QString& target = {});
    // Aggiornamenti (MainWindowUpdate.cpp): controllo, download firmato e installazione sul posto, ritorno indietro.
    Updater* updater();
    void startupUpdates();
    void checkForUpdates(bool interactive);
    void showUpdateOffer(const UpdateOffer& offer);
    void downloadAndInstall(const UpdateOffer& offer);
    void rollbackUpdate();
    void askRestart(const QString& question);
    void scriptUpdateCheck();
    void scriptUpdateLocal(const QString& manifest, const QString& archive, const QString& folder);
    void scriptUpdateRollback(const QString& folder);
    void scriptUpdateDownload(const QString& folder);
    // Connettore per Claude Desktop (MainWindowClaude.cpp): canale locale, strumenti sul disegno aperto,
    // conferma delle modifiche proposte, finestra ASSISTENTE (Assistente IA).
    bool startClaude(QString* error);
    void stopClaude();
    void startupClaude();
    void handleClaudeTool(const QString& tool, const QJsonObject& args, ClaudeLink::Reply reply);
    void proposeClaudeChanges(DrawingWidget* d, const QJsonObject& args, ClaudeLink::Reply reply);
    // Strumenti canalo_* (CanaloCAD): true se "tool" è uno di questi (la risposta la dà lui).
    bool handleCanaloTool(const QString& tool, const QJsonObject& args, DrawingWidget* d, ClaudeLink::Reply& reply);
    QString configureClaudeDesktop();
    static QString claudeCodeCommand();
    void showClaudeDialog();
    // Plugin CanaloCAD (MainWindowCanalo.cpp).
    void setupCanalo(Ribbon* ribbon);
    // Scheda contestuale "Editor di testo" (MainWindowTextEditor.cpp) e il suo aggiornamento dallo stato dell'editor.
    void setupTextEditorTab(Ribbon* ribbon);
    // Schede Inserisci, Annota, Vista, Gestisci, Output e Layout (MainWindowRibbonTabs.cpp).
    void setupRibbonTabs(Ribbon* ribbon);
    // Finestre del modello affiancate.
    void setupView(DrawingWidget* drawing, CadView* view);
    void syncMainViews();
    void chooseTiles();
    // Confronto di revisioni (MainWindowCompare.cpp).
    void setupCompareTab(Ribbon* ribbon);
    void startCompare(QString path = {});
    void compareStep(int delta);
    void closeCompare();
    QAction* compareHereAct_ = nullptr;
    QAction* compareThereAct_ = nullptr;
    void updateAnnotateCombos();
    void restoreNamedView(const QString& name);
    void manageNamedViews();
    ComboModel* annoTextStyleCombo_ = nullptr;
    ComboModel* annoTextHeightCombo_ = nullptr;
    ComboModel* annoDimStyleCombo_ = nullptr;
    ComboModel* annoLeaderStyleCombo_ = nullptr;
    ComboModel* viewsCombo_ = nullptr;
    void refreshTextEditorTab(bool lists = false);
    ComboModel* textStyleCombo_ = nullptr;
    ComboModel* textHeightCombo_ = nullptr;
    ComboModel* textFontCombo_ = nullptr;
    ComboModel* textColorCombo_ = nullptr;
    QAction* textAnnotativeAct_ = nullptr;
    QAction* textMaskAct_ = nullptr;
    QAction* textBoldAct_ = nullptr;
    QAction* textItalicAct_ = nullptr;
    QAction* textUnderlineAct_ = nullptr;
    QAction* textOverlineAct_ = nullptr;
    QAction* textStrikeAct_ = nullptr;
    QAction* textAlignActs_[5] = {};
    QAction* textRulerAct_ = nullptr;
    QAction* textSpellAct_ = nullptr;
    QMenu* textJustifyMenu_ = nullptr;
    QMenu* textSpacingMenu_ = nullptr;
    bool updatingTextTab_ = false;
    void canaloOpen();
    bool canaloSend(const QString& target = {});
    // Calcoli di CanaloCAD sulla sezione selezionata (riempimento, portata, segregazione, staffaggio, distinta).
    void canaloCheck(bool askData, bool sheet = false);
    // Collega la finestra Canalina (messaggi, guida, cavi propri del progetto dell'impianto).
    void wireCanalo();
    // Sezione composta con i cavi del catalogo: disegnata al vero e verificata (CANALOSEZIONE).
    // Negli script `scriptCables` sono i codici del catalogo (FG16_4G10*2,DATA_CAT6...): niente finestra.
    void canaloCompose(const QString& scriptCables = {});
    // Impianto: tavolozza, nuova tratta dalla selezione, verifica di tutte le tratte.
    void plantNewRun();
    void plantCheck();
    void linkPlantAndCircuits();
    // Guida: "Fammi vedere" (scheda, tavolozza, comando).
    void guideAction(const QString& action);
    // Percorsi guidati: passo nuovo (evidenzia il pulsante, fotografa lo stato) e controllo della condizione.
    void guideStepEntered();
    void guideCheckStep();
    // F1: la guida; durante un comando la sua pagina del manuale.
    void showGuide();
    bool canaloInsert(const QString& source = {});
    void canaloInsertLatest();
    void updateCanaloWatch();
    // Scheda CanaloCAD (WebView2, solo Windows): la pagina tra le schede dei disegni. "then" parte a pagina pronta.
    bool canaloEmbedded() const;
    void canaloShowTab(std::function<void()> then = {});
    bool isCanaloTab(int index) const;
    void closeCanaloTab();
    void positionCanaloTab();
    void canaloMessage(const QJsonValue& value);
    // Tavola DXF di CanaloCAD negli appunti del programma come blocco (con stili di testo e caratteri): Ctrl+V la
    // incolla in qualsiasi disegno aperto. Restituisce il nome del blocco (vuoto se non riesce).
    QString canaloCopyAsBlock(const QByteArray& dxf, const QString& fileName, QString* error = nullptr);
    // Tavola DXF di CanaloCAD come immagine a 300 dpi su fondo bianco (per gli appunti di Windows).
    QImage canaloSheetImage(const QByteArray& dxf, QString* error = nullptr);
    // Prova senza schermo (-CANALOSCHEDA sezione.dxf tavola.dxf): la pagina in una finestra nascosta importa la
    // sezione e restituisce la tavola, come fanno "Invia sezione" e "Inserisci nel disegno" nella scheda.
    void canaloTabSelfTest(const QString& section, const QString& sheet);
    QWidget* canaloPlaceholder_ = nullptr;     // pagina dello stack per la scheda CanaloCAD
    CanaloTab* canaloTab_ = nullptr;           // finestra nativa con la pagina
    QPointer<DrawingWidget> canaloFrom_;       // disegno in cui inserire la tavola
    bool canaloReady_ = false;
    // Disegno di cui la pagina mostra il progetto (salvato nel disegno); l'ultimo disegno attivo prima della scheda.
    QPointer<DrawingWidget> canaloOwner_;
    bool canaloOwnerLoaded_ = false;
    QPointer<DrawingWidget> lastDrawing_;
    // La pagina mostra il progetto del disegno canaloFrom_ (quello salvato nel disegno, o uno nuovo).
    void canaloSyncOwner();
    std::vector<std::function<void()>> canaloPending_;
    QFileSystemWatcher* canaloWatcher_ = nullptr;
    QDateTime canaloSeen_;
    void layerFromObject();
    void allLayersOn();
    void addRecentFile(const QString& path);
    QStringList recentFiles() const;
    void updateRecentMenus();
    // Pagina Inizia: documenti recenti (con bloccati e ore di apertura), segnali collegati.
    void fillStartPage(StartPage* page);
    void connectStartPage(StartPage* page);
    void togglePinnedFile(const QString& path);
    void removeRecentFile(const QString& path);
    void runStartCommand(const QString& name);

    DraftSettings settings_ = appOptions().draft;
    QTimer* autosaveTimer_ = nullptr;
    QSet<QString> autosaved_;      // salvataggi automatici scritti in questa sessione
    QSet<QString> recoverPaths_;   // file aperti con RECOVER: alla fine della lettura si corregge il disegno
    QSet<QString> recoveredAutosaves_;   // salvataggi automatici riaperti all'avvio (diventano disegni da salvare)
    int untitledCounter_ = 0;

    // Azioni
    QAction* newAct_ = nullptr;
    QAction* openAct_ = nullptr;
    QAction* saveAct_ = nullptr;
    QAction* saveAsAct_ = nullptr;
    QAction* closeAct_ = nullptr;
    QAction* exitAct_ = nullptr;
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QAction* lineAct_ = nullptr;
    QAction* circleAct_ = nullptr;
    QAction* arcAct_ = nullptr;
    QAction* polylineAct_ = nullptr;
    QAction* rectangleAct_ = nullptr;
    QAction* ellipseAct_ = nullptr;
    QAction* pointAct_ = nullptr;
    QAction* hatchAct_ = nullptr;
    QAction* textAct_ = nullptr;
    QAction* mtextAct_ = nullptr;
    QAction* dimLinearAct_ = nullptr;
    QAction* dimAlignedAct_ = nullptr;
    QAction* dimRadiusAct_ = nullptr;
    QAction* dimAngularAct_ = nullptr;
    QAction* dimOrdinateAct_ = nullptr;
    QAction* dimContinueAct_ = nullptr;
    QAction* dimBaselineAct_ = nullptr;
    QAction* qdimAct_ = nullptr;
    QAction* peditAct_ = nullptr;
    QAction* dimDiameterAct_ = nullptr;
    QAction* blockAct_ = nullptr;
    QAction* insertAct_ = nullptr;
    QAction* explodeAct_ = nullptr;
    QAction* moveAct_ = nullptr;
    QAction* copyAct_ = nullptr;
    QAction* offsetAct_ = nullptr;
    QAction* trimAct_ = nullptr;
    QAction* eraseAct_ = nullptr;
    QAction* selectAllAct_ = nullptr;
    QAction* layerPropsAct_ = nullptr;
    QAction* layerFromObjectAct_ = nullptr;
    QAction* layerAllOnAct_ = nullptr;
    QAction* zoomExtentsAct_ = nullptr;
    QAction* zoomInAct_ = nullptr;
    QAction* zoomOutAct_ = nullptr;
    QAction* propertiesAct_ = nullptr;
    QAction* commandLineAct_ = nullptr;
    QAction* historyAct_ = nullptr;
    QAction* helpAct_ = nullptr;
    QAction* nextTabAct_ = nullptr;
    QAction* gridAct_ = nullptr;
    QAction* snapAct_ = nullptr;
    QAction* orthoAct_ = nullptr;
    QAction* polarAct_ = nullptr;
    QAction* osnapAct_ = nullptr;
    QAction* otrackAct_ = nullptr;
    QAction* dynAct_ = nullptr;
    QAction* lwAct_ = nullptr;
    QList<QAction*> drawingActions_;   // disabilitate quando nessun disegno è aperto

    // Widget
    Ribbon* ribbon_ = nullptr;
    TabsModel* fileTabs_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget* drawingArea_ = nullptr;
    StartPage* startPage_ = nullptr;
    CommandLine* commandLine_ = nullptr;
    QDockWidget* propertiesDock_ = nullptr;
    QDockWidget* layersDock_ = nullptr;
    PropertiesPalette* properties_ = nullptr;
    // Proprietà rapide: finestrella accanto al cursore quando si selezionano oggetti senza comando attivo.
    QWidget* quickPropsWindow_ = nullptr;
    PropertiesPalette* quickProps_ = nullptr;
    std::vector<EntityId> quickPropsFor_;
    bool quickPropsForced_ = false;   // aperte dal tasto destro: restano finché c'è una selezione
    void updateQuickProperties();
    // Menu del tasto destro: senza comando (con o senza selezione) e durante un comando.
    void showDrawingMenu(DrawingWidget* drawing, QPoint pos);
    void showCommandMenu(DrawingWidget* drawing, QPoint pos);
    QStringList recentCommands(int max) const;
public:
    // Diagnosi: Proprietà rapide visibili e quante righe mostrano.
    bool quickPropertiesVisible() const { return quickPropsWindow_ && quickPropsWindow_->isVisible(); }
    int quickPropertiesRows() const;
private:
    QDockWidget* circuitsDock_ = nullptr;
    QDockWidget* plantDock_ = nullptr;
    QDockWidget* guideDock_ = nullptr;
    // Interfaccia a widget: tavolozze nascoste mentre c'è la pagina Inizia (tornano con un disegno).
    QList<QDockWidget*> startHiddenDocks_;
    void hideDocksForStart(bool start);
    CircuitsPanel* circuits_ = nullptr;
    PlantPanel* plant_ = nullptr;
    CanaloPanel* canalo_ = nullptr;   // finestra Canalina (CanaloPanel.h)
    GuidePanel* guide_ = nullptr;
    QAction* guideAct_ = nullptr;
    QAction* updateAct_ = nullptr;
    Updater* updater_ = nullptr;
    std::optional<UpdateOffer> pendingUpdate_;
    bool updateBusy_ = false;
    QTimer* guideTimer_ = nullptr;
    int canaloChecks_ = 0;
    // Attività del connettore per Claude (percorsi guidati): richieste, modifiche applicate, versioni salvate.
    int claudeCalls_ = 0, claudeApplied_ = 0, claudeSaved_ = 0;
    // Stato all'inizio del passo del percorso guidato.
    struct GuideSnapshot {
        std::map<int, int> entities;   // per tipo di oggetto
        int total = 0;
        int runs = 0, circuits = 0, verify = 0, canalo = 0;
        std::map<std::string, int> tagged;
        std::string activeCommand;
        std::uint64_t commandRevision = 0;
        std::map<std::string, int> finishedCommands;
        int claudeCalls = 0, claudeApplied = 0, claudeSaved = 0;
    } guideSnap_;
    void guideCommandFinished(const QString& name, bool cancelled);
    BlockLibrary* library_ = nullptr;
    QDockWidget* libraryDock_ = nullptr;
    QAction* libraryAct_ = nullptr;
    std::map<QString, std::shared_ptr<DocSummary>> pendingSummaries_;   // riepiloghi dei DWG da verificare
    ClaudeLink* claude_ = nullptr;
    QPointer<QDialog> claudeProposal_;   // proposta di Claude in attesa della scelta dell'utente
    bool claudeAutoApprove_ = false;     // solo script di prova: -ASSISTENTEATTIVA AUTO
    // Libreria simboli: inserisce un blocco (dal disegno o da un file libreria già letto).
    // `at`: rilasciato con il trascinamento in quel punto (scala 1, rotazione 0); altrimenti INSERT.
    void insertFromLibrary(const QString& source, const QString& name, std::optional<Vec2> at = {});
    void dropLibraryItem(DrawingWidget* drawing, const QString& payload, Vec2 world);
    LayerManager* layerManager_ = nullptr;
    ComboModel* layerCombo_ = nullptr;
    bool layerListChanged_ = false;   // layer cambiati dall'elenco aperto
    ComboModel* colorCombo_ = nullptr;
    ComboModel* linetypeCombo_ = nullptr;
    ComboModel* lineweightCombo_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    bool updatingCombos_ = false;
    TabsModel* layoutTabs_ = nullptr;   // schede Modello/layout nella barra di stato
    StatusBar* status_ = nullptr;
    QMenu* vpScaleMenu_ = nullptr;
    std::vector<std::shared_ptr<OpenJob>> opening_;   // disegni in lettura in un altro thread
    bool forceAsyncOpen_ = false;   // diagnosi: apertura asincrona anche dentro uno script
    void finishOpen(const std::shared_ptr<OpenJob>& job);
    void updateLoadingStatus();
    bool updatingLayoutTabs_ = false;
    bool scriptRunning_ = false;
    bool textInPlaceInScripts_ = false;   // diagnosi: TEXT/MTEXT scrivono nel disegno anche negli script
    bool discardOnClose_ = false;
    bool previousCrash_ = false;   // la sessione precedente non si è chiusa regolarmente
    class MainView* mainView_ = nullptr;   // interfaccia tutta in QML (nullptr con CARTESIO_UI=widgets)
    QString dialogGrab_;   // diagnosi: il prossimo dialogo XRIF si salva come immagine invece di aspettare   // QUIT da script: chiude senza chiedere di salvare
    QMap<QString, QString> extraVariables_;   // FILEDIA, CMDECHO, ATTREQ...
    QAction* layoutNewAct_ = nullptr;
    QAction* mviewAct_ = nullptr;
    QAction* vpclipAct_ = nullptr;
    QAction* titleBlockAct_ = nullptr;
    QAction* titleUpdateAct_ = nullptr;
    QAction* pageSetupAct_ = nullptr;
    QAction* pdfAct_ = nullptr;
    QAction* plotAct_ = nullptr;
    QAction* previewAct_ = nullptr;
    QAction* publishAct_ = nullptr;
    QAction* scriptAct_ = nullptr;
    QAction* attdefAct_ = nullptr;
    QAction* rotateAct_ = nullptr;
    QAction* scaleAct_ = nullptr;
    QAction* mirrorAct_ = nullptr;
    QAction* arrayAct_ = nullptr;
    QAction* extendAct_ = nullptr;
    QAction* filletAct_ = nullptr;
    QAction* dimStyleAct_ = nullptr;
    QAction* panAct_ = nullptr;
    QAction* pickStyleAct_ = nullptr;
    QAction* leaderAct_ = nullptr;
    QAction* chamferAct_ = nullptr;
    QAction* stretchAct_ = nullptr;
    QAction* breakAct_ = nullptr;
    QAction* joinAct_ = nullptr;
    QAction* distAct_ = nullptr;
    QAction* areaAct_ = nullptr;
    QAction* idAct_ = nullptr;
    QAction* listAct_ = nullptr;
    QAction* matchPropAct_ = nullptr;
    QAction* copyClipAct_ = nullptr;
    QAction* pasteAct_ = nullptr;
};

} // namespace cartesio
