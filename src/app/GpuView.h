#pragma once

// Disegno su GPU con Qt Quick (scene graph, Direct3D/Vulkan/OpenGL tramite RHI).
//
// Il documento viene tradotto una volta in geometrie (linee e triangoli con il colore nei vertici) e
// testi, raggruppati nell'ordine di disegno; pan e zoom cambiano solo la trasformazione della vista, senza
// rigenerare nulla. Le geometrie si rigenerano quando cambia il disegno (revisione del documento, spazio,
// layer...); la selezione ha un livello a parte, così selezionare non rigenera il resto.
//
// CadView resta il "controllore" (mouse, comandi, snap, zoom): ospita un QQuickWidget con DrawingItem per
// il disegno e, sopra, un livello sottile dipinto con QPainter per mirino, snap, maniglie e anteprime.
// La stampa continua a usare il percorso vettoriale di CadView (renderForPrint).

#include "core/Entity.h"
#include "core/Prim.h"
#include "core/Vec2.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPointF>
#include <QPointer>
#include <QQuickWindow>
#include <QRectF>
#include <QSizeF>
#include <QQuickItem>
#include <QSGGeometry>
#include <QString>
#include <QTransform>
#include <QWidget>

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace cartesio {

class CadView;

using GpuVertex = QSGGeometry::ColoredPoint2D;

// Linee (a coppie di vertici) o triangoli, nell'ordine di disegno.
struct GpuBatch {
    bool triangles = false;
    std::vector<GpuVertex> vertices;
    // Tratteggio a motivo: linee del motivo da vicino, area a tinta tenue (`denseFill`, triangoli) quando
    // le linee distano meno di un pixel e mezzo. 0 = batch normale.
    double lodSpacing = 0.0;
    double lodPeriod = 0.0;               // motivo dei tratti più corto (0 = linee continue)
    std::vector<GpuVertex> solidLines;    // le stesse linee senza tratti, per quando il motivo è troppo fitto
    std::vector<GpuVertex> denseFill;
    // Immagine raster: file, trasformazione dai suoi pixel alle coordinate locali, parte da mostrare (in pixel
    // dell'immagine originale) e opacità (dissolvenza). Vuoto = batch di linee o triangoli.
    QString imageFile;
    QTransform imageToLocal;
    QRectF imageRect;
    QSizeF imagePixels;
    double imageOpacity = 1.0;
    // Ingombro in coordinate locali (costruzione): un oggetto può finire in un batch precedente dello stesso tipo
    // se non si sovrappone a niente di quello che viene dopo. Non valido = si sovrappone a tutto.
    BBox box;
};

// Un testo: righe nel sistema del font (pixel del font, Y verso il basso) e trasformazione verso le
// coordinate locali. Se è troppo piccolo sullo schermo si mostra un trattino al suo posto.
struct GpuText {
    // Riga TrueType: testo, font e trasformazione dai pixel del font (Y in basso) alle coordinate locali.
    struct Run {
        QString text;
        QFont font;
        QTransform toLocal;
    };
    std::vector<Run> runs;
    std::vector<GpuVertex> strokes;    // tratti dei font SHX (coppie di vertici, coordinate locali)
    QColor color;
    double height = 0.0;               // altezza in unità dello spazio
    std::vector<GpuVertex> dashes;     // trattini (coppie di vertici) per i testi minuscoli
};

// Contenuto di uno spazio, in coordinate locali (float) relative a `origin` per non perdere precisione.
struct GpuContent {
    Vec2 origin;
    std::vector<GpuBatch> batches;
    std::vector<GpuText> texts;
    std::vector<std::pair<Vec2, QColor>> points;   // oggetti PUNTO: marcatori di dimensione fissa a schermo
    bool empty() const { return batches.empty() && texts.empty() && points.empty(); }
};

struct GpuViewport {
    std::vector<Vec2> outline;   // contorno sul foglio
    Affine modelToPaper;
    double scale = 1.0;          // unità del foglio per unità del modello
    EntityId id = 0;
    GpuContent model;
};

// Blocco di oggetti consecutivi nell'ordine di disegno, con le sue geometrie (coordinate locali rispetto a
// content.origin). Dopo una modifica si ricostruiscono solo i blocchi con oggetti cambiati: l'ordine di disegno
// resta quello del documento (coperture, tratteggi pieni...).
struct GpuChunk {
    std::vector<std::uint64_t> serials;   // Entity::serial() degli oggetti, nell'ordine
    GpuContent content;
};

struct GpuScene {
    int space = 0;
    bool paper = false;
    GpuContent content;   // origine della scena e oggetti PUNTO (coordinate locali della scena); geometrie nei blocchi
    std::vector<std::shared_ptr<const GpuChunk>> chunks;
    std::uint64_t chunkKey = 0;   // tutto ciò che non sono gli oggetti (tabelle, vista): se cambia, niente riuso
    std::vector<GpuViewport> viewports;
};

// Costruisce le geometrie dalla vista (documento, spazio mostrato, layer congelati, colori...). Con `previous`
// riusa i blocchi i cui oggetti non sono cambiati.
GpuScene buildGpuScene(const CadView& view, const GpuScene* previous = nullptr);
// Solo gli oggetti selezionati, evidenziati (livello sopra il disegno).
GpuContent buildGpuSelection(const CadView& view);

// Disegna la vista con Direct3D 11 in una texture e la legge (diagnosi senza schermo: -GPUPNG).
QImage renderGpuImage(CadView& view, QSize size, QString* error = nullptr);
std::vector<QImage> renderGpuSequence(CadView& view, QSize size, int frames, const std::function<void(int)>& step,
                                      QString* error = nullptr);

// Elemento Qt Quick che disegna la scena.
class DrawingItem : public QQuickItem {
    Q_OBJECT

public:
    // Full: disegno e selezione (vista a widget). Scene + Selection: due item della scena unica, il disegno in una
    // texture (layer) e la selezione sopra: un clic ridisegna solo la selezione, non decine di migliaia di linee.
    // Lavorando dentro una finestra di layout attiva la selezione resta nel disegno (deve stare nel suo ritaglio).
    enum class Part { Full, Scene, Selection };
    explicit DrawingItem(CadView& view, QQuickItem* parent = nullptr, Part part = Part::Full);
    ~DrawingItem() override;

    // Da chiamare a ogni cambiamento: rigenera le geometrie solo se serve, altrimenti aggiorna la vista.
    void refresh();
    // Millisecondi dell'ultima costruzione della scena (diagnosi).
    double lastBuildMs() const { return lastBuildMs_; }
    // Dimensione della vista e margine attorno (frazione per lato): la scena si disegna anche un po' fuori dalla
    // vista, così zoomando indietro con la texture pronta non compaiono bordi vuoti. L'item resta centrato sulla
    // vista; baseOffset() è la sua posizione a riposo.
    void setViewSize(QSizeF view, double margin);
    QPointF baseOffset() const { return base_; }
    double margin() const { return margin_; }
    // Millisecondi dell'ultimo ridisegno vero della scena (thread di disegno): decide se pan e zoom ridisegnano
    // davvero o spostano e scalano la texture pronta.
    double lastRenderMs() const { return renderMs_.load(std::memory_order_relaxed); }
    // Ricostruisce subito le geometrie e restituisce i millisecondi impiegati.
    double rebuildNow();

signals:
    // Qt Quick è partito con il disegno software (nessuna GPU utilizzabile): la vista torna a QPainter.
    void softwareFallback();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void updatePolish() override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    bool selectionHere() const;

    CadView& view_;
    Part part_ = Part::Full;
    bool selectionWasHere_ = true;
    GpuScene scene_;
    GpuContent selection_;
    std::uint64_t sceneKey_ = 0;
    std::uint64_t selectionKey_ = 0;
    bool sceneChanged_ = true;
    bool selectionChanged_ = true;
    bool fallbackSent_ = false;
    double lastBuildMs_ = 0.0;
    int lastChunksBuilt_ = 0;   // blocchi ricostruiti nell'ultima costruzione (diagnosi)
    QPointF base_;
    double margin_ = 0.0;
    std::atomic<bool> measureNext_{false};   // il prossimo fotogramma ridisegna la scena: si misura
    std::atomic<bool> measuring_{false};
    std::atomic<double> renderMs_{0.0};
    QElapsedTimer renderTimer_;
    QPointer<QQuickWindow> watched_;
    void watchRendering(QQuickWindow* window);

public:
    int lastChunksBuilt() const { return lastChunksBuilt_; }
};

// Livello trasparente sopra il disegno: mirino, snap, maniglie, anteprime (QPainter, solo ciò che si muove).
class OverlayWidget : public QWidget {
public:
    explicit OverlayWidget(CadView& view);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    CadView& view_;
};

} // namespace cartesio
