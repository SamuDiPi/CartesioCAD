// DIAGGPU: scheda grafica, driver, sistema grafico, scala dello schermo e impostazioni del disegno, per capire i
// problemi che si vedono solo su un certo PC (testi fuori posto, linee spezzate...).
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/MainView.h"
#include "app/QmlUi.h"

#include <QGuiApplication>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QScreen>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>

namespace cartesio {

namespace {

QString apiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("software (senza GPU)");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("nessuno");
    default: return QStringLiteral("sconosciuto");
    }
}

} // namespace

void MainWindow::printGpuDiagnostics()
{
    print(tr("DIAGGPU - Qt %1, interfaccia %2, testi TrueType: %3")
              .arg(QString::fromLatin1(qVersion()),
                   qmlRootUi() ? tr("finestra unica QML") : tr("classica a widget"),
                   appOptions().textRendering == 1 ? tr("distance field") : tr("curve")));
    if (const QScreen* screen = this->screen())
        print(tr("  Schermo: %1, %2 x %3, scala %4% (rapporto pixel %5)")
                  .arg(screen->name())
                  .arg(screen->geometry().width())
                  .arg(screen->geometry().height())
                  .arg(qRound(screen->devicePixelRatio() * 100))
                  .arg(screen->devicePixelRatio()));
    // La finestra Qt Quick che disegna il disegno: la scena unica, o quella della vista (finestra a widget).
    QQuickWindow* window = nullptr;
    if (mainView_)
        window = mainView_->quickWindow();
    else if (DrawingWidget* d = current())
        if (auto* quick = d->view()->findChild<QQuickWidget*>())
            window = quick->quickWindow();
    if (!window) {
        print(tr("  Disegno con QPainter (senza Qt Quick)."));
        return;
    }
    print(tr("  Sistema grafico: %1").arg(apiName(window->rendererInterface()->graphicsApi())));
    if (QRhi* rhi = window->rhi()) {
        const QRhiDriverInfo info = rhi->driverInfo();
        print(tr("  Scheda grafica: %1 (%2), driver: %3")
                  .arg(QString::fromUtf8(info.deviceName))
                  .arg(info.deviceType == QRhiDriverInfo::DiscreteDevice     ? tr("dedicata")
                       : info.deviceType == QRhiDriverInfo::IntegratedDevice ? tr("integrata")
                       : info.deviceType == QRhiDriverInfo::CpuDevice        ? tr("emulata dalla CPU")
                                                                             : tr("altro"))
                  .arg(QString::fromLatin1(rhi->backendName())));
    } else {
        print(tr("  Scheda grafica: non ancora inizializzata (aprire un disegno e ripetere)."));
    }
}

} // namespace cartesio
