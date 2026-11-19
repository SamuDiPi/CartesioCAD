#pragma once

// Il disegno dentro la scena QML della finestra principale (interfaccia tutta in QML).
//
// CadCanvas (tipo QML "CadCanvas") mostra la vista di un disegno (CadView, che resta la logica: zoom, snap,
// comandi, mirino) con il disegno su GPU (DrawingItem) e sopra un livello disegnato con QPainter (mirino,
// maniglie, anteprime, pulsanti di navigazione). Mouse e rotella arrivano alla CadView come prima. Senza GPU (scene graph
// software) il livello disegna anche il disegno.

#include "app/CadView.h"

#include <QPointer>
#include <QQuickItem>
#include <QQuickPaintedItem>

namespace cartesio {

class CadView;
class DrawingItem;
class CadCanvas;

class CanvasOverlay : public QQuickPaintedItem {
    Q_OBJECT

public:
    explicit CanvasOverlay(CadCanvas& canvas);
    void paint(QPainter* painter) override;

private:
    CadCanvas& canvas_;
};

class CadCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* view READ viewObject WRITE setView NOTIFY viewChanged)

public:
    explicit CadCanvas(QQuickItem* parent = nullptr);
    ~CadCanvas() override;

    QObject* viewObject() const;
    void setView(QObject* view);
    CadView* view() const { return view_; }
    bool software() const { return software_; }

    // Dalla CadView: è cambiato qualcosa da ridisegnare (scena se serve, livello sopra sempre).
    void requestUpdate();

signals:
    void viewChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    void rebuild();
    void useSoftware();
    void syncSize();

    QPointer<CadView> view_;
    DrawingItem* drawing_ = nullptr;

public:
    DrawingItem* drawingItem() const { return drawing_; }

private:
    CanvasOverlay* overlay_ = nullptr;
    bool software_ = false;
    CadView::GpuItemsSync gpu_;
    DrawingItem* selectionItem_ = nullptr;   // selezione sopra la texture del disegno
};

} // namespace cartesio
