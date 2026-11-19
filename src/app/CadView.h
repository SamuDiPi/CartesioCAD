#pragma once

#include "app/DraftSettings.h"
#include "core/Entity.h"
#include "core/Vec2.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QWidget>
#include <unordered_set>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QPainter;
class QQuickWidget;

namespace cartesio {

class DrawingItem;
class OverlayWidget;
class CadCanvas;
class GpuSceneBuilder;
struct GpuScene;
struct GpuContent;
struct PlacedText;
class CommandManager;
class Document;
class SelectionPicker;
struct PlotStyleTable;

// Come disegnare una stampa.
struct PrintOptions {
    double dotsPerMm = 0.0;           // punti del dispositivo per millimetro di carta
    bool lineweights = true;          // spessori degli oggetti (altrimenti tutti sottili)
    double lineweightScale = 1.0;     // "scala spessori di linea" con la scala di stampa
    std::shared_ptr<const PlotStyleTable> styles;   // tabella stili (nullptr: colori degli oggetti)
    bool transparency = false;        // stampa la trasparenza degli oggetti
    bool paperSpaceLast = true;       // layout: prima le finestre, poi gli oggetti del foglio
};

// Area di disegno: zoom/pan, griglia, snap, ortho, puntamento polare,
// input dinamico, anteprime dei comandi e pulsanti di navigazione.
// Modifica di un testo direttamente nel disegno: mentre è attiva riceve per primo il mouse della vista e
// disegna l'oggetto modificato, la selezione e il cursore sopra la scena.
class ViewTextEditor {
public:
    virtual ~ViewTextEditor() = default;
    // True se l'evento è suo (la vista non fa altro).
    virtual bool viewMousePress(QMouseEvent* event) = 0;
    virtual bool viewMouseMove(QMouseEvent* event) = 0;
    virtual bool viewMouseRelease(QMouseEvent* event) = 0;
    virtual bool viewMouseDoubleClick(QMouseEvent* event) = 0;
    // Cursore a I sopra il testo.
    virtual bool viewIBeam(QPointF screen) const = 0;
    virtual void paintInView(QPainter& p) = 0;
};

class CadView : public QWidget {
    Q_OBJECT
    // Disegno su GPU (GpuView.cpp): usano stato, colori e tipi di linea della vista.
    friend class GpuSceneBuilder;
    friend class DrawingItem;
    friend class OverlayWidget;
    friend class CadCanvas;
    friend GpuScene buildGpuScene(const CadView& view, const GpuScene* previous);
    friend GpuContent buildGpuSelection(const CadView& view);

public:
    // Dallo schermo alle coordinate in cui si lavora (modello attraverso la finestra attiva, o foglio).
    Vec2 worldFromScreen(QPointF screen) const { return toWorld(screen); }

    // Confronto con un'altra revisione (scheda Confronto): nel modello gli oggetti uguali in grigio, quelli solo qui
    // in verde, quelli solo nell'altro disegno sopra in rosso; le zone delle differenze riquadrate.
    struct CompareView {
        std::unordered_set<EntityId> onlyHere;
        std::vector<std::shared_ptr<const Entity>> onlyThere;
        std::vector<BBox> regions;
        int current = -1;
        bool showHere = true;
        bool showThere = true;
        QString other;   // nome del file confrontato
        int same = 0;
    };
    void setCompare(std::optional<CompareView> compare);
    const CompareView* compare() const { return compare_ ? &*compare_ : nullptr; }
    void setCompareShown(bool here, bool there);

    // Finestre del modello affiancate: questa vista è una di più (bordo azzurro se è quella attiva).
    void setTileState(bool tiled, bool active)
    {
        tiled_ = tiled;
        tileActive_ = active;
        update();
    }
    // Zona successiva (+1) o precedente (-1): si inquadra.
    void compareGo(int delta);
    // Stato degli item GPU di una vista (disegno nella texture e selezione): ultime scene mandate e vista con cui
    // sono state disegnate, per spostare e scalare le texture pronte durante pan e zoom.
    struct GpuItemsSync {
        std::uint64_t sceneKey = 0, selectionKey = 0;
        std::uint64_t sceneContent = 0, selectionContent = 0;
        QTransform sceneView, selectionView;
        bool moved = false;
    };
    // Aggiorna gli item GPU (selection può essere nullptr): durante pan e zoom, se è cambiata solo la vista,
    // sposta e scala le texture già pronte; il ridisegno vero arriva quando il movimento si ferma.
    void syncGpuItems(DrawingItem* scene, DrawingItem* selection, GpuItemsSync& sync);
    // Margine (frazione della vista per lato) della texture della scena: zoomando indietro niente bordi vuoti.
    static double sceneMargin();
    // Interfaccia tutta in QML: la vista si mostra in un CadCanvas della scena principale (nullptr = nessuno).
    // La CadView resta un widget nascosto che fa da logica (dimensioni, eventi, stato).
    void attachCanvas(CadCanvas* canvas);
    void detachCanvas(CadCanvas* canvas);
    CadCanvas* canvas() const { return canvases_.empty() ? nullptr : canvases_.back(); }
    // Nascondono quelle di QWidget: con il canvas il ridisegno e il cursore vanno lì.
    void update();
    void setCursor(const QCursor& cursor);
    // Widget su cui mettere i controlli sopra il disegno (modifica dei testi) e posizione della vista in esso.
    QWidget* overlayHost();
    QPoint overlayOffset() const;
    // Disegno con QPainter: tutto (senza GPU) o solo il livello sopra (mirino, maniglie, anteprime).
    void paintSoftware(QPainter& p);
    void paintOverlay(QPainter& p);

    // Disegno su GPU (Qt Quick) attivo; false con CARTESIO_RENDER=software o senza GPU utilizzabile.
    bool gpuRendering() const { return item_ != nullptr; }
    // Immagine della vista com'è a schermo (disegno e livello sopra), anche con il disegno su GPU.
    QImage grabImage();
    // Miniatura della vista attuale (senza griglia, pulsanti di navigazione e maniglie) per la pagina Inizia.
    QImage renderThumbnail(QSize size) const;
    // Diagnosi: tempo medio (ms) di un movimento del mouse (evidenziazione, snap) su n punti della vista.
    double benchmarkCursor(int n);
    // Diagnosi di OTRACK: punti agganciati e cursore; restituisce dove va il cursore.
    QString simulateTracking(const std::vector<Vec2>& acquired, Vec2 at);
    // Oltre questo numero di oggetti selezionati le maniglie non si mostrano (GRIPOBJLIMIT).
    static constexpr std::size_t kGripObjectLimit = 100;
    // Diagnosi senza schermo: la vista disegnata con Direct3D 11 in una texture, con il livello sopra.
    QImage grabGpuImage(QString* info);
    // Diagnosi: la stessa scena GPU a più scale (fattori sulla vista attuale), un'immagine per fattore.
    std::vector<QImage> grabGpuZoomSequence(const std::vector<double>& factors, QString* info);
    CadView(const Document& doc, const CommandManager& commands, const std::vector<EntityId>& selection,
            const SelectionPicker& idlePicker, const DraftSettings& settings, QWidget* parent = nullptr);

    // Posizione del cursore con snap/ortho/polare applicati.
    Vec2 cursorWorld() const { return cursor_; }
    // Posizione del cursore senza snap (per la selezione).
    Vec2 rawCursorWorld() const { return rawCursor_; }
    double pixelsToWorld(double px) const { return px / scale_; }
    // Punto dello schermo (coordinate della vista) di un punto del disegno su cui si lavora.
    QPointF screenPoint(Vec2 world) const { return toScreen(world); }
    Vec2 worldPoint(QPointF screen) const { return toWorld(screen); }
    bool selectionCornerOpen() const { return selectionCorner().has_value(); }
    // Modifica di un testo nel disegno in corso (nullptr = nessuna).
    void setTextEditor(ViewTextEditor* editor)
    {
        textEditor_ = editor;
        update();
    }
    ViewTextEditor* textEditor() const { return textEditor_; }
    // Disegna un oggetto (coordinate del disegno su cui si lavora) con i suoi colori, sopra la scena.
    void paintEntityWorld(QPainter& p, const Entity& e) const;
    // Tolleranza di selezione (mirino) in unità del disegno su cui si lavora: la stessa per l'evidenziazione e
    // per il clic, così si seleziona sempre l'oggetto evidenziato.
    double pickTolerance() const;
    // Ciclo di selezione: con più oggetti sovrapposti sotto il mirino, Maiusc+Spazio evidenzia il successivo.
    void cycleHover();
    // Oggetto scelto con il ciclo (0 = nessuna scelta: vale quello che vince da solo).
    EntityId pickHint() const { return hoverCandidates_.size() > 1 && cycleIndex_ > 0 ? hover_ : 0; }
    // Diagnosi: oggetti sovrapposti sotto il mirino e menu della maniglia calda.
    std::size_t hoverCandidateCount() const { return hoverCandidates_.size(); }
    EntityId hoveredEntity() const { return hover_; }
    bool gripMenuOpen() const { return gripMenuOpen_ && hotGrip_.has_value(); }
    void openGripMenuNow();
    // Diagnosi: centro della voce `index` del menu della maniglia (coordinate della vista).
    QPointF gripMenuItemCenter(int index) const { return gripMenuItemRect(index).center(); }

    void zoomExtents();
    // Diagnosi: tempo medio (ms) per ridisegnare la scena corrente.
    double benchmarkRender(int times);
    // ZOOM Finestra (rettangolo nelle coordinate in cui si lavora) e Precedente.
    void zoomWindow(const BBox& box);
    bool zoomPrevious();
    void zoomBy(double factor);
    // Testo digitato nella riga di comando, mostrato dall'input dinamico.
    void setTypedText(const QString& text);
    // Ultime righe della cronologia dei comandi, disegnate sopra la barra della riga di comando (`bar`,
    // coordinate della vista); i clic passano comunque al disegno.
    void setCommandHistory(const QStringList& lines, const QRect& bar);
    // Da chiamare quando cambiano documento, selezione o impostazioni.
    void refresh();
    // Stampa (PDF o stampante) di uno spazio: `world` porta le coordinate dello spazio sul dispositivo.
    void renderForPrint(QPainter& p, const QTransform& world, const BBox& area, int space,
                        const PrintOptions& options) const;
    // Parte dello spazio mostrato che si vede nella finestra (per la stampa "Schermo").
    BBox visibleArea() const;
    // Formato dei simboli trascinati dalla tavolozza Blocchi; li accetta anche il CadCanvas della scena unica.
    static QString blockMimeType() { return QStringLiteral("application/x-cartesio-block"); }
    // Rilascio di un simbolo nel punto `screen` della vista.
    void dropBlock(const QString& payload, QPointF screen) { emit blockDropped(payload, toWorld(screen)); }
    // Finestra del layout attivata con un doppio clic (zoom e pan agiscono sul modello al suo interno).
    EntityId activeViewport() const { return activeViewport_; }
    // PAN in tempo reale (la "manina"): trascinando con il tasto sinistro si sposta la vista.
    bool panMode() const { return panMode_; }
    void setPanMode(bool on);
    void clearActiveViewport() { activeViewport_ = 0; update(); }
    // Layout mostrato (0 = modello) e finestra attraverso cui si lavora sul modello (0 = nessuna).
    void setShownSpace(int space) { shownSpace_ = space; }
    int shownSpace() const { return shownSpace_; }
    void setActiveViewport(EntityId id) { activeViewport_ = id; refresh(); }
    // Da chiamare dopo aver cambiato lo spazio attivo: salva lo zoom del precedente e ripristina
    // quello del nuovo (o inquadra tutto la prima volta).
    void onSpaceChanged();
    // Dopo aver caricato un altro documento.
    void resetSpaces()
    {
        savedViews_.clear();
        viewSpace_ = 0;
        shownSpace_ = 0;
        activeViewport_ = 0;
    }

signals:
    // Clic nella vista (finestre del modello affiancate: diventa quella attiva).
    void activated();
    void cursorMoved(cartesio::Vec2 world);
    void pointPicked(cartesio::Vec2 world);
    void enterRequested();
    void viewportViewChanged(cartesio::EntityId id, cartesio::Vec2 viewCenter, double viewHeight);
    // Doppio clic su una finestra del layout (0 = fuori da tutte le finestre).
    void viewportActivated(cartesio::EntityId id);
    // Doppio clic fuori dai layout (es. su una tabella per modificarne le celle).
    void worldDoubleClicked(cartesio::Vec2 world);
    // Doppio clic su un oggetto (fuori dai comandi).
    void entityDoubleClicked(cartesio::EntityId id);
    // Tasto destro senza comando attivo, con il menu di scelta rapida attivato nelle opzioni.
    void contextMenuRequested(QPoint globalPos);
    // Tasto destro durante un comando che propone opzioni: menu con Invio, Annulla e le opzioni.
    void commandMenuRequested(QPoint globalPos);
    // Un simbolo della tavolozza Blocchi rilasciato sul disegno (`payload`: tipo, sorgente e nome).
    void blockDropped(const QString& payload, cartesio::Vec2 world);
    // Lazo disegnato trascinando con il tasto sinistro (punti nel disegno su cui si lavora).
    void lassoSelected(const std::vector<cartesio::Vec2>& polygon, bool crossing);
    // Voce scelta nel menu di una maniglia multifunzione (`action`: valore di GripAction).
    void gripActionRequested(cartesio::EntityId id, int gripIndex, int action, cartesio::Vec2 from);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Chiavi per il disegno su GPU: contenuto (senza vista né selezione) e selezione.
    std::uint64_t contentKey() const;
    std::uint64_t selectionKey() const;
    void startGpuRendering();
    void useSoftwareRendering();
    QQuickWidget* quick_ = nullptr;
    DrawingItem* item_ = nullptr;
    DrawingItem* selectionItem_ = nullptr;   // selezione sopra la texture del disegno (un clic non ridisegna tutto)
    OverlayWidget* overlay_ = nullptr;

    struct PolarHit {
        Vec2 base;
        double angle;      // radianti
        double distance;
    };

    // Stato ereditato quando si disegna il contenuto di blocchi e quote.
    struct Style {
        int layer = 0;            // layer dell'inserimento (per gli oggetti sul layer "0")
        int aci = 7;              // colore dell'inserimento (per gli oggetti DaBlocco)
        std::optional<QColor> override;   // colore forzato (evidenziazione)
        double width = 1.0;
        Qt::PenStyle penStyle = Qt::SolidLine;
        std::string blockLinetype;                  // per gli oggetti DaBlocco
        int blockLineweight = kLineweightDefault;   // per gli oggetti DaBlocco
        // Zona visibile e pixel per unità, nelle coordinate dell'oggetto (dentro un blocco: quelle del
        // blocco): gli oggetti di un blocco fuori vista o più piccoli di un pixel non si disegnano.
        BBox view;
        double pixelsPerUnit = 0.0;
    };

    QPointF toScreen(Vec2 world) const;
    Vec2 toWorld(QPointF screen) const;
    QTransform worldTransform() const;
    QTransform paperTransform() const;
    Vec2 toPaper(QPointF screen) const;
    double effectiveScale() const;
    double gridSpacing() const;
    void updateCursor(QPointF screenPos);
    std::optional<Vec2> selectionCorner() const;
    bool overNavigation(QPointF pos) const;
    QRectF navButtonRect(int index) const;
    std::uint64_t sceneKey(bool withView = true, bool withSelection = true) const;
    // Come contentKey, ma senza le modifiche ai soli oggetti: se è uguale, la scena GPU riusa i blocchi.
    std::uint64_t tableKey() const;
    // Pan e zoom veloci: durante il movimento si sposta e scala l'immagine già pronta, poi si ridisegna.
    void beginInteraction();
    void drawOverlays(QPainter& p);
    QTimer* settleTimer_ = nullptr;
    bool interacting_ = false;
    Vec2 cacheCenter_;
    double cacheScale_ = 1.0;
    double lastRenderMs_ = 0.0;   // durata dell'ultimo ridisegno completo (decide come fare pan e zoom)
    std::uint64_t cacheContentKey_ = 0;

    void renderScene(QPainter& p) const;
    void renderViewports(QPainter& p, int space) const;
    // annoFactor: scala di annotazione della finestra del layout (0 = modello, la scala corrente).
    void renderEntities(QPainter& p, int space, const BBox& view, bool interactive,
                        const std::vector<int>* frozen = nullptr, double annoFactor = 0.0) const;
    void drawPaperSheet(QPainter& p) const;
    QColor aciColor(int aci) const;
    const ViewportEntity* viewportAt(Vec2 paperPoint) const;
    BBox paperExtents() const;
    const ViewportEntity* activeViewportEntity() const;
    void drawGrid(QPainter& p) const;
    // Disegna un'entità in coordinate mondo (il QPainter ha già la trasformazione della vista).
    void paintEntity(QPainter& p, const Entity& e, const Style& style, int depth = 0) const;
    QColor resolveColor(const Entity& e, const Style& style) const;
    QColor resolveColorRaw(const Entity& e, const Style& style) const;
    // A schermo un colore (quasi) uguale allo sfondo - es. RGB nero nel modello - si mostra come il colore 7,
    // altrimenti l'oggetto sparisce; in stampa i colori restano quelli veri.
    QColor visibleOnScreen(QColor c) const;
    // Sfondo su cui si disegna ora (foglio o modello, dalle Opzioni) e se è chiaro (colore 7 nero).
    QColor screenBackground() const;
    bool lightBackground() const;
public:
    // TESTINASCOSTI: perché i testi nella vista non si vedono (righe per la riga di comando).
    QStringList diagnoseTexts() const;
private:
    // Colore con cui si disegna: in stampa passa per la tabella stili (colore, grigi, retinatura).
    QColor plotColor(const Entity& e, const Style& style) const;
    // Colore ACI effettivo (DaLayer e DaBlocco risolti), per la tabella stili.
    int effectiveAci(const Entity& e, const Style& style) const;
    const Linetype* linetypeOf(const Entity& e, const Style& style) const;
    int lineweightOf(const Entity& e, const Style& style) const;
    // Penna con spessore e tipo di linea dell'oggetto.
    QPen entityPen(QPainter& p, const Entity& e, const Style& style, const QColor& color) const;
    void paintText(QPainter& p, const TextEntity& t) const;
    void paintMText(QPainter& p, const MTextEntity& m, bool ownColors = true) const;
    // `ownColors`: i colori dei pezzi (\C) dei testi multilinea; falso se il testo è evidenziato.
    void paintPlaced(QPainter& p, const std::vector<PlacedText>& lines, bool dashOnly, bool ownColors = false) const;
    void drawGrips(QPainter& p, const Entity& e) const;
    void drawSelectionWindow(QPainter& p) const;
    void drawPolarTracking(QPainter& p) const;
    void drawSnapMarker(QPainter& p) const;
    void drawCrosshair(QPainter& p) const;
    void drawUcsIcon(QPainter& p) const;
    void drawNavigationBar(QPainter& p) const;
    void drawDynamicInput(QPainter& p) const;
    void drawTooltip(QPainter& p, QPointF anchor, const QString& text) const;
    void drawCommandHistory(QPainter& p) const;

    const Document& doc_;
    const CommandManager& commands_;
    const std::vector<EntityId>& selection_;
    const SelectionPicker& idlePicker_;
    const DraftSettings& settings_;

    double scale_ = 4.0;        // pixel per unità di disegno
    Vec2 center_;               // punto del mondo al centro della vista

    Vec2 cursor_;
    Vec2 rawCursor_;
    QPointF mouseScreen_;
    bool mouseInside_ = false;
    // Mirino come cursore di Windows: lo muove il sistema operativo (stessa velocità e DPI del puntatore, nessun
    // fotogramma di ritardo). Non con lo snap a griglia, dove il mirino salta sui passi: lì lo disegna la vista.
    bool systemCrosshair() const;
    QCursor idleCursor() const;
public:
    // Diagnosi: il cursore che la vista usa ora (mirino di Windows).
    QCursor currentIdleCursor() const { return idleCursor(); }
private:
    void applyIdleCursor();
    mutable QString cursorKey_;
    mutable QCursor cursorCache_;
    QString appliedCursor_;
    std::optional<SnapPoint> snap_;
    std::optional<PolarHit> polar_;
    // Puntamento snap a oggetto (OTRACK): punti agganciati (fermando il cursore su un punto di snap) e la linea di
    // allineamento (o l'incrocio di due) su cui sta il cursore.
    struct TrackHit {
        Vec2 point;                  // punto sulla linea (o incrocio)
        std::vector<std::pair<Vec2, double>> lines;   // punto agganciato e angolo di ogni linea usata
    };
    std::vector<Vec2> tracked_;
    std::optional<TrackHit> track_;
    std::optional<Vec2> dwellPoint_;
    qint64 dwellStart_ = 0;
    class QTimer* dwellTimer_ = nullptr;
    void updateTracking(const std::optional<Vec2>& base, bool selecting);
    void drawObjectTracking(QPainter& p) const;
    EntityId hover_ = 0;
    ViewTextEditor* textEditor_ = nullptr;
    // Oggetti sotto il mirino (il primo è quello che vince da solo) e posizione nel ciclo di selezione.
    std::vector<EntityId> hoverCandidates_;
    std::size_t cycleIndex_ = 0;
    bool overlapBadge_ = false;   // oggetti davvero sovrapposti sotto il mirino: piccola icona accanto
    void drawCycleBadge(QPainter& p) const;
    // Lazo: dopo un clic nel vuoto (finestra aperta), trascinando col tasto premuto si disegna un lazo.
    bool lassoArmed_ = false;
    bool lassoDragging_ = false;
    std::vector<Vec2> lassoPath_;
    // Maniglia sotto il cursore (calda, rossa) e il suo menu, che si apre fermandosi un attimo.
    struct HotGrip {
        EntityId id = 0;
        std::size_t index = 0;
        Vec2 position;
    };
    std::optional<HotGrip> hotGrip_;
    std::optional<CompareView> compare_;
    bool tiled_ = false;
    bool tileActive_ = false;
    std::uint64_t compareRevision_ = 0;
    // Colore del confronto per un oggetto del disegno (nessuno: confronto spento o fuori dal modello).
    std::optional<QColor> compareColor(const Entity& e) const;
    bool gripDragArmed_ = false;   // premuto su una maniglia: il rilascio dopo un trascinamento conferma
    bool gripMenuOpen_ = false;
    int gripMenuHover_ = -1;
    QTimer* gripMenuTimer_ = nullptr;
    void updateHotGrip();
    std::vector<int> gripMenuActions() const;
    QRectF gripMenuItemRect(int index) const;
    QRectF gripMenuRect() const;
    int gripMenuItemAt(QPointF pos) const;
    bool overGripMenu(QPointF pos) const { return gripMenuOpen() && gripMenuRect().contains(pos); }
    void drawGripMenu(QPainter& p) const;
    // MEASUREGEOM rapido: distanze fra gli oggetti in orizzontale e in verticale attorno al cursore.
    void drawQuickMeasure(QPainter& p) const;
    QString typedText_;
    QStringList commandHistory_;
    QRect commandBar_;

    bool panning_ = false;
    QPointF panLast_;
    QPointF leftPressPos_;
    // Ultime pressioni del tasto sinistro: ora (del sistema) e oggetto sotto il mirino. Servono al doppio clic:
    // con Qt Quick la seconda pressione arriva prima del doppio clic (non va ripetuta), e il doppio clic apre la
    // modifica solo se i due clic sono sullo stesso oggetto (clic rapidi su oggetti vicini = selezione).
    quint64 lastLeftPressTime_ = 0;
    EntityId lastLeftPressHit_ = 0;
    EntityId prevLeftPressHit_ = 0;

    // Immagine del disegno, rigenerata solo quando cambia qualcosa (non ad ogni movimento del mouse).
    mutable QPixmap sceneCache_;
    mutable std::uint64_t sceneCacheKey_ = 0;
    GpuItemsSync gpu_;
    std::vector<CadCanvas*> canvases_;   // interfaccia QML: dove si mostra la vista (di solito uno)   // ultima vista/scena mandata alla GPU (i movimenti del mouse non la cambiano)
    mutable bool paperMode_ = false;     // si disegna su carta bianca: il colore 7 diventa nero
    mutable double lineScale_ = 1.0;     // spessore delle linee in pixel del dispositivo
    mutable double printDotsPerMm_ = 0.0;   // > 0 durante la stampa: spessori di linea reali
    mutable const PrintOptions* print_ = nullptr;   // durante la stampa
    mutable double ltPaperScale_ = 0.0;     // > 0 nelle finestre: tipi di linea in unità della carta (PSLTSCALE)
    EntityId activeViewport_ = 0;
    bool panMode_ = false;
    // Viste precedenti per ZOOM P (centro, scala); le rotellate vicine nel tempo contano una volta.
    std::vector<std::pair<Vec2, double>> viewHistory_;
    qint64 lastHistoryPush_ = 0;
    void rememberView(bool coalesce = false);
    int shownSpace_ = 0;
    int viewSpace_ = 0;
    std::map<int, std::pair<Vec2, double>> savedViews_;   // spazio -> (centro, scala)
};

} // namespace cartesio
