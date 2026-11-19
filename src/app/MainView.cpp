#include "app/MainView.h"

#include "app/CadView.h"
#include "app/QmlUi.h"
#include "app/Theme.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <QQmlContext>
#include <QQuickWindow>

#include <algorithm>

namespace cartesio {

MainView::MainView(const std::map<QString, QObject*>& models, QWidget* parent) : QQuickWidget(parent)
{
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setFocusPolicy(Qt::StrongFocus);
    // Il widget che ospita la scena: lì vanno i controlli sopra il disegno (modifica dei testi).
    quickWindow()->setProperty("hostWidget", QVariant::fromValue<QObject*>(this));
    for (const auto& [name, object] : models)
        rootContext()->setContextProperty(name, object);
    rootContext()->setContextProperty(QStringLiteral("main"), this);
    setSource(QUrl(QStringLiteral("qrc:/qml/MainView.qml")));
}

QObject* MainView::viewObject() const
{
    return view_.data();
}

void MainView::setView(CadView* view, bool start)
{
    if (view == view_ && start == start_)
        return;
    view_ = view;
    start_ = start;
    emit viewChanged();
}

void MainView::setViews(const std::vector<CadView*>& views, int tileMode)
{
    QVariantList list;
    for (CadView* v : views)
        list.append(QVariant::fromValue(static_cast<QObject*>(v)));
    views_ = list;
    tileMode_ = tileMode;
    emit viewChanged();
}

void MainView::setCommandLineVisible(bool on)
{
    if (on != commandLine_) {
        commandLine_ = on;
        emit panelsChanged();
    }
}

void MainView::setPanelVisible(Panel panel, bool on)
{
    if (panels_[panel] == on)
        return;
    panels_[panel] = on;
    emit panelsChanged();
    emit panelVisibilityChanged(panel, on);
}

bool MainView::panelVisible(Panel panel) const
{
    return panels_[panel];
}

void MainView::setLeftWidth(int w)
{
    w = std::clamp(w, 200, std::max(200, width() / 2));
    if (w != leftWidth_) {
        leftWidth_ = w;
        emit panelsChanged();
    }
}

void MainView::setRightWidth(int w)
{
    w = std::clamp(w, 260, std::max(260, width() / 2));
    if (w != rightWidth_) {
        rightWidth_ = w;
        emit panelsChanged();
    }
}

void MainView::setCommandBar(int x, int y, int w, int h)
{
    const QRect r(x, y, w, h);
    if (r != commandBar_) {
        commandBar_ = r;
        emit commandBarChanged();
    }
}

void MainView::focusCommandLine()
{
    setFocus();
    emit focusCommandLineRequested();
}

QByteArray MainView::saveState() const
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s << quint8(4) << panels_[0] << panels_[1] << panels_[2] << qint32(leftWidth_) << qint32(rightWidth_) << panels_[3] << panels_[4]
      << panels_[5];
    return out;
}

void MainView::restoreState(const QByteArray& state)
{
    QDataStream s(state);
    quint8 version = 0;
    s >> version;
    if (version < 1 || version > 4)
        return;
    bool p0 = false, p1 = false, p2 = false, p3 = false, p4 = false, p5 = false;
    qint32 l = leftWidth_, r = rightWidth_;
    s >> p0 >> p1 >> p2 >> l >> r;
    if (version >= 2)
        s >> p3;   // tavolozza Blocchi (dalla versione 2)
    if (version >= 3)
        s >> p4;   // tavolozza Impianto (dalla versione 3)
    if (version >= 4)
        s >> p5;   // tavolozza Guida (dalla versione 4)
    if (s.status() != QDataStream::Ok)
        return;
    leftWidth_ = std::clamp<int>(l, 200, 1200);
    rightWidth_ = std::clamp<int>(r, 260, 1200);
    setPanelVisible(Properties, p0);
    setPanelVisible(Layers, p1);
    setPanelVisible(Circuits, p2);
    setPanelVisible(Library, p3);
    setPanelVisible(Plant, p4);
    setPanelVisible(Guide, p5);
    emit panelsChanged();
}

} // namespace cartesio

#include <QQmlComponent>
#include <QQuickItem>
#include <QElapsedTimer>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <rhi/qrhi.h>

#include <memory>

namespace cartesio {

// Diagnosi senza schermo: tutta la scena principale disegnata con Direct3D 11 (come a schermo con la GPU).
QImage MainView::renderOffscreenGpu(QSize size, QString* error, const std::function<void(int)>& step, int frames,
                                    double* msPerFrame)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QImage();
    };
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setProperty("hostWidget", QVariant::fromValue<QObject*>(this));
    window.setGeometry(0, 0, size.width(), size.height());
    window.contentItem()->setSize(size);
    QQmlComponent component(engine(), QUrl(QStringLiteral("qrc:/qml/MainView.qml")));
    std::unique_ptr<QObject> object(component.create(rootContext()));
    auto* item = qobject_cast<QQuickItem*>(object.get());
    if (!item)
        return fail(component.errorString());
    item->setParentItem(window.contentItem());
    item->setSize(size);
    if (!control.initialize())
        return fail(QStringLiteral("Direct3D 11 non disponibile"));
    QRhi* rhi = control.rhi();
    std::unique_ptr<QRhiTexture> texture(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    texture->create();
    std::unique_ptr<QRhiRenderBuffer> depth(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    depth->create();
    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(texture.get())};
    description.setDepthStencilBuffer(depth.get());
    std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget(description));
    std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(pass.get());
    target->create();
    window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target.get()));
    QImage image;
    // Due fotogrammi: il primo crea i nodi (il disegno si aggancia alla finestra), il secondo è quello completo.
    QElapsedTimer timer;
    double total = 0.0;
    double phase[4] = {0.0, 0.0, 0.0, 0.0};   // eventi, polish, sync, render (prove della reattività)
    std::vector<double> each;                 // tempo di ogni fotogramma: la mediana regge al PC occupato
    for (int frame = 0; frame < 2 + frames; ++frame) {
        const bool last = frame == 1 + frames;
        if (frame >= 2 && step)
            step(frame - 2);
        timer.start();
        QElapsedTimer part;
        part.start();
        QCoreApplication::processEvents();
        const double tEvents = part.nsecsElapsed() / 1e6;
        part.restart();
        control.polishItems();
        const double tPolish = part.nsecsElapsed() / 1e6;
        part.restart();
        control.beginFrame();
        control.sync();
        const double tSync = part.nsecsElapsed() / 1e6;
        part.restart();
        control.render();
        QRhiReadbackResult result;
        if (last) {
            QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
            readback->readBackTexture(QRhiReadbackDescription(texture.get()), &result);
            control.commandBuffer()->resourceUpdate(readback);
        }
        control.endFrame();
        rhi->finish();
        if (frame >= 2) {
            total += timer.nsecsElapsed() / 1e6;
            each.push_back(timer.nsecsElapsed() / 1e6);
            phase[0] += tEvents;
            phase[1] += tPolish;
            phase[2] += tSync;
            phase[3] += part.nsecsElapsed() / 1e6;
        }
        if (msPerFrame && frames > 0)
            *msPerFrame = total / frames;
        if (!last)
            continue;
        image = QImage(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(),
                       result.pixelSize.height(), QImage::Format_RGBA8888_Premultiplied)
                    .copy();
        if (rhi->isYUpInFramebuffer())
            image = image.flipped(Qt::Vertical);
    }
    object.reset();
    if (error) {
        *error = QString::fromLatin1(rhi->backendName());
        if (frames > 0)
            *error += QStringLiteral("; eventi %1, polish %2, sync %3, render %4 ms")
                          .arg(phase[0] / frames, 0, 'f', 1)
                          .arg(phase[1] / frames, 0, 'f', 1)
                          .arg(phase[2] / frames, 0, 'f', 1)
                          .arg(phase[3] / frames, 0, 'f', 1);
        if (!each.empty()) {
            std::sort(each.begin(), each.end());
            *error += QStringLiteral("; mediana %1, minimo %2 ms").arg(each[each.size() / 2], 0, 'f', 1).arg(each.front(), 0, 'f', 1);
        }
    }
    return image;
}

} // namespace cartesio
