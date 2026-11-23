#include "app/CanvasItem.h"
#include <QMimeData>

#include "app/CadView.h"
#include "app/GpuView.h"

#include <QCursor>
#include <QPainter>
#include <QQuickWindow>
#include <QSGRendererInterface>

namespace cartesio {

// ============================================================ livello sopra

CanvasOverlay::CanvasOverlay(CadCanvas& canvas) : QQuickPaintedItem(&canvas), canvas_(canvas)
{
    setAntialiasing(true);
    setFillColor(Qt::transparent);
}

void CanvasOverlay::paint(QPainter* painter)
{
    CadView* view = canvas_.view();
    if (!view)
        return;
    if (canvas_.software())
        view->paintSoftware(*painter);   // senza GPU: tutto il disegno
    else
        view->paintOverlay(*painter);    // con la GPU: solo mirino, maniglie, anteprime...
}

// ============================================================ canvas

CadCanvas::CadCanvas(QQuickItem* parent) : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsDrops, true);   // simboli dalla tavolozza Blocchi
    setFlag(ItemHasContents, false);
    setClip(true);
    overlay_ = new CanvasOverlay(*this);
    overlay_->setZ(1);
}

CadCanvas::~CadCanvas()
{
    if (view_)
        view_->detachCanvas(this);
}

QObject* CadCanvas::viewObject() const
{
    return view_.data();
}

void CadCanvas::setView(QObject* object)
{
    auto* view = qobject_cast<CadView*>(object);
    if (view == view_)
        return;
    if (view_)
        view_->detachCanvas(this);
    view_ = view;
    if (view_)
        view_->attachCanvas(this);
    rebuild();
    emit viewChanged();
}

void CadCanvas::rebuild()
{
    delete drawing_;
    drawing_ = nullptr;
    delete selectionItem_;
    selectionItem_ = nullptr;
    gpu_ = {};
    if (view_ && !software_ && window()) {
        if (window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
            software_ = true;
        } else {
            const bool layered = !qEnvironmentVariableIsSet("CARTESIO_NOLAYER");
            drawing_ = new DrawingItem(*view_, this, layered ? DrawingItem::Part::Scene : DrawingItem::Part::Full);
            drawing_->setZ(0);
            if (layered) {
                selectionItem_ = new DrawingItem(*view_, this, DrawingItem::Part::Selection);
                selectionItem_->setZ(0.5);
                connect(selectionItem_, &DrawingItem::softwareFallback, this, &CadCanvas::useSoftware, Qt::QueuedConnection);
            }
            // Il disegno va in una texture (layer di Qt Quick) riusata finché non cambia: muovendo il mouse si
            // ridisegnano solo mirino e anteprime, non le decine di migliaia di linee (fotogrammi da 8-18 ms a
            // pochi ms su una pianta grande di prova). 4 campioni come la vista a widget: linee antialias.
            if (QObject* layer = drawing_->property("layer").value<QObject*>()) {
                layer->setProperty("enabled", !qEnvironmentVariableIsSet("CARTESIO_NOLAYER"));
                layer->setProperty("samples", 4);
                layer->setProperty("smooth", true);   // pan e zoom veloci: la texture scalata sfuma invece di fare i quadretti
            }
            connect(drawing_, &DrawingItem::softwareFallback, this, &CadCanvas::useSoftware, Qt::QueuedConnection);
        }
    }
    syncSize();
    requestUpdate();
}

void CadCanvas::useSoftware()
{
    software_ = true;
    rebuild();
}

void CadCanvas::syncSize()
{
    const QSizeF s = size();
    if (view_)
        view_->resize(std::max(1, static_cast<int>(s.width())), std::max(1, static_cast<int>(s.height())));
    if (drawing_)
        drawing_->setViewSize(s, selectionItem_ ? CadView::sceneMargin() : 0.0);
    if (selectionItem_)
        selectionItem_->setSize(s);
    overlay_->setSize(s);
}

void CadCanvas::requestUpdate()
{
    if (!view_)
        return;
    // Il disegno (nella texture) solo se cambiano disegno o vista; la selezione nel suo item. Dentro una finestra
    // di layout attiva la selezione sta nel disegno.
    if (drawing_)
        view_->syncGpuItems(drawing_, selectionItem_, gpu_);
    overlay_->update();
}

void CadCanvas::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        syncSize();
        requestUpdate();
    }
}

void CadCanvas::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange && data.window)
        rebuild();
}

// ------------------------------------------------------------ eventi

void CadCanvas::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (view_)
        view_->mousePressEvent(event);
    event->accept();   // si tiene il mouse fino al rilascio
}

void CadCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (view_)
        view_->mouseMoveEvent(event);
}

void CadCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (view_)
        view_->mouseReleaseEvent(event);
}

void CadCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (view_)
        view_->mouseDoubleClickEvent(event);
    event->accept();
}

void CadCanvas::hoverEnterEvent(QHoverEvent* event)
{
    if (!view_)
        return;
    QEnterEvent enter(event->position(), mapToScene(event->position()), mapToGlobal(event->position()));
    view_->enterEvent(&enter);
}

void CadCanvas::hoverMoveEvent(QHoverEvent* event)
{
    if (!view_)
        return;
    QMouseEvent move(QEvent::MouseMove, event->position(), mapToGlobal(event->position()), Qt::NoButton, Qt::NoButton,
                     event->modifiers());
    view_->mouseMoveEvent(&move);
}

void CadCanvas::hoverLeaveEvent(QHoverEvent*)
{
    if (!view_)
        return;
    QEvent leave(QEvent::Leave);
    view_->leaveEvent(&leave);
}

void CadCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (view_ && event->mimeData()->hasFormat(CadView::blockMimeType()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void CadCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (view_ && event->mimeData()->hasFormat(CadView::blockMimeType())) {
        event->acceptProposedAction();
        QMouseEvent move(QEvent::MouseMove, event->position(), mapToGlobal(event->position()), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        view_->mouseMoveEvent(&move);   // il mirino segue il simbolo
    } else {
        event->ignore();
    }
}

void CadCanvas::dropEvent(QDropEvent* event)
{
    if (!view_ || !event->mimeData()->hasFormat(CadView::blockMimeType())) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    view_->dropBlock(QString::fromUtf8(event->mimeData()->data(CadView::blockMimeType())), event->position());
}

void CadCanvas::wheelEvent(QWheelEvent* event)
{
    if (view_)
        view_->wheelEvent(event);
    event->accept();
}

} // namespace cartesio
