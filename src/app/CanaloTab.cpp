#include "app/CanaloTab.h"

#include "app/WebView2Host.h"

#include <QUrl>
#include <QWindow>

#include <cmath>

namespace cartesio {

CanaloTab::CanaloTab(QWidget* parent) : QWidget(parent), host_(new WebView2Host(this))
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setFocusPolicy(Qt::StrongFocus);
    connect(host_, &WebView2Host::ready, this, [this] {
        updateBounds();
        host_->setVisible(isVisible());
    });
}

void CanaloTab::start(const QUrl& page, const QString& dataFolder)
{
    host_->start(static_cast<quintptr>(winId()), dataFolder, page);
}

void CanaloTab::updateBounds()
{
    // WebView2 lavora in pixel fisici della finestra nativa.
    const qreal ratio = windowHandle() ? windowHandle()->devicePixelRatio() : devicePixelRatioF();
    host_->setBounds(0, 0, int(std::lround(width() * ratio)), int(std::lround(height() * ratio)));
}

void CanaloTab::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateBounds();
}

void CanaloTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateBounds();
    host_->setVisible(true);
}

void CanaloTab::hideEvent(QHideEvent* event)
{
    host_->setVisible(false);
    QWidget::hideEvent(event);
}

void CanaloTab::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    host_->focus();
}

} // namespace cartesio
