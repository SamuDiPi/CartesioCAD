#include "app/LayerManager.h"

#include "app/CadView.h"
#include "app/ColorDialog.h"
#include "app/DrawingWidget.h"
#include "app/Icons.h"
#include "app/QmlUi.h"
#include "app/Theme.h"
#include "core/Document.h"

#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QQmlContext>
#include <QTimer>

#include <algorithm>

namespace cartesio {

LayerManager::LayerManager(QWidget* parent) : QQuickWidget(parent)
{
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    rootContext()->setContextProperty(QStringLiteral("layers"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/LayerManager.qml")));
    setMinimumSize(320, 160);
}

QVariantMap LayerManager::icons() const
{
    const auto url = [](Icon i) { return IconProvider::url(icon(i)); };
    return {{QStringLiteral("current"), url(Icon::LayerCurrentMark)}, {QStringLiteral("on"), url(Icon::BulbOn)},
            {QStringLiteral("off"), url(Icon::BulbOff)},              {QStringLiteral("sun"), url(Icon::Sun)},
            {QStringLiteral("snow"), url(Icon::Snowflake)},           {QStringLiteral("lockOpen"), url(Icon::LockOpen)},
            {QStringLiteral("lockClosed"), url(Icon::LockClosed)},    {QStringLiteral("new"), url(Icon::LayerNew)},
            {QStringLiteral("delete"), url(Icon::LayerDelete)},       {QStringLiteral("makeCurrent"), url(Icon::LayerMakeCurrent)}};
}

void LayerManager::setDrawing(DrawingWidget* drawing)
{
    drawing_ = drawing;
    selected_ = -1;
    refresh();
}

void LayerManager::refresh()
{
    if (pending_)
        return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] {
        pending_ = false;
        rebuild();
    });
}

void LayerManager::rebuild()
{
    rows_.clear();
    if (drawing_) {
        const Document& doc = drawing_->document();
        const ViewportEntity* vp = activeViewport();
        const auto& layers = doc.layers();
        for (int row = 0; row < static_cast<int>(layers.size()); ++row) {
            const Layer& l = layers[static_cast<std::size_t>(row)];
            rows_.append(QVariantMap{
                {QStringLiteral("name"), QString::fromStdString(l.name)},
                {QStringLiteral("current"), row == doc.currentLayer()},
                {QStringLiteral("renamable"), row != 0},
                {QStringLiteral("on"), l.visible},
                {QStringLiteral("frozen"), l.frozen},
                {QStringLiteral("vpFrozen"), vp ? (vp->isLayerFrozen(row) ? 1 : 0) : -1},
                {QStringLiteral("locked"), l.locked},
                {QStringLiteral("color"), (l.trueColor >= 0 ? QColor::fromRgb(static_cast<QRgb>(l.trueColor)) : theme::aciColor(l.color)).name()},
                {QStringLiteral("colorName"), theme::aciName(l.color)},
                {QStringLiteral("linetype"), QString::fromStdString(l.linetype)},
                {QStringLiteral("lineweight"), l.lineweight < 0 ? tr("Default") : QStringLiteral("%1 mm").arg(l.lineweight / 100.0, 0, 'f', 2)},
            });
        }
        if (selected_ >= static_cast<int>(layers.size()))
            selected_ = -1;
    }
    emit rowsChanged();
    emit selectedRowChanged();
}

// La notifica è differita: il disegno si aggiorna dopo il ritorno al QML.
void LayerManager::changed()
{
    if (!drawing_)
        return;
    Document& doc = drawing_->document();
    doc.touch();
    // Gli oggetti dei layer non più selezionabili escono dalla selezione.
    auto& sel = drawing_->selection();
    std::erase_if(sel, [&](EntityId id) {
        const Entity* e = doc.find(id);
        return !e || !doc.isLayerSelectable(e->layer());
    });
    QPointer<DrawingWidget> drawing = drawing_;
    QTimer::singleShot(0, this, [drawing] {
        if (drawing)
            drawing->notifyChanged();
    });
    refresh();
}

void LayerManager::select(int row)
{
    if (row != selected_) {
        selected_ = row;
        emit selectedRowChanged();
    }
}

void LayerManager::toggle(int row, const QString& column)
{
    if (!drawing_ || row < 0 || row >= static_cast<int>(drawing_->document().layers().size()))
        return;
    Document& doc = drawing_->document();
    Layer& layer = doc.layer(row);
    if (column == QLatin1String("on")) {
        layer.visible = !layer.visible;
    } else if (column == QLatin1String("freeze")) {
        if (row == doc.currentLayer() && !layer.frozen) {
            QMessageBox::information(this, tr("Layer"), tr("Impossibile congelare il layer corrente."));
            return;
        }
        layer.frozen = !layer.frozen;
    } else if (column == QLatin1String("vpfreeze")) {
        const ViewportEntity* vp = activeViewport();
        if (!vp)
            return;
        // Modifica annullabile della finestra.
        const EntityId id = vp->id();
        QPointer<DrawingWidget> drawing = drawing_;
        QTimer::singleShot(0, this, [drawing, id, row] {
            if (!drawing)
                return;
            drawing->modifyEntities({id}, [row](Entity& e) {
                auto& frozen = static_cast<ViewportEntity&>(e).frozenLayers;
                if (std::find(frozen.begin(), frozen.end(), row) != frozen.end())
                    std::erase(frozen, row);
                else
                    frozen.push_back(row);
            });
        });
        return;
    } else if (column == QLatin1String("lock")) {
        layer.locked = !layer.locked;
    } else {
        return;
    }
    changed();
}

void LayerManager::pickColor(int row)
{
    if (!drawing_ || row < 0 || row >= static_cast<int>(drawing_->document().layers().size()))
        return;
    QTimer::singleShot(0, this, [this, row] {
        if (!drawing_ || row >= static_cast<int>(drawing_->document().layers().size()))
            return;
        Layer& layer = drawing_->document().layer(row);
        const auto color = ColorDialog::pick(layer.color, false, this);
        if (!color)
            return;
        layer.color = *color;
        layer.trueColor = -1;
        changed();
    });
}

void LayerManager::popupLinetype(int row, int globalX, int globalY)
{
    if (!drawing_)
        return;
    const Document& doc = drawing_->document();
    // Tipi di linea caricati, poi quelli standard da caricare.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto choose = [this, row](const std::string& name) {
        if (!drawing_ || row < 0 || row >= static_cast<int>(drawing_->document().layers().size()))
            return;
        drawing_->document().loadStandardLinetype(name);
        drawing_->document().layer(row).linetype = name;
        changed();
    };
    for (const cartesio::Linetype& lt : doc.linetypes())
        connect(menu->addAction(QString::fromStdString(lt.name)), &QAction::triggered, this,
                [choose, name = lt.name] { choose(name); });
    QMenu* more = menu->addMenu(tr("Carica"));
    for (const cartesio::Linetype& lt : standardLinetypes())
        if (!doc.findLinetype(lt.name))
            connect(more->addAction(QString::fromStdString(lt.name + "   " + lt.description)), &QAction::triggered, this,
                    [choose, name = lt.name] { choose(name); });
    menu->popup(QPoint(globalX, globalY));
}

void LayerManager::popupLineweight(int row, int globalX, int globalY)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto choose = [this, row](int lw) {
        if (!drawing_ || row < 0 || row >= static_cast<int>(drawing_->document().layers().size()))
            return;
        drawing_->document().layer(row).lineweight = lw;
        changed();
    };
    connect(menu->addAction(tr("Default")), &QAction::triggered, this, [choose] { choose(kLineweightDefault); });
    for (int lw : standardLineweights())
        connect(menu->addAction(QStringLiteral("%1 mm").arg(lw / 100.0, 0, 'f', 2)), &QAction::triggered, this,
                [choose, lw] { choose(lw); });
    menu->popup(QPoint(globalX, globalY));
}

void LayerManager::rename(int row, const QString& name)
{
    if (!drawing_ || row <= 0)
        return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QString::fromStdString(drawing_->document().layers()[static_cast<std::size_t>(row)].name))
        return;
    std::string error;
    if (!drawing_->document().renameLayer(row, trimmed.toStdString(), &error)) {
        QMessageBox::warning(this, tr("Rinomina layer"), QString::fromStdString(error));
        refresh();
        return;
    }
    changed();
}

void LayerManager::newLayer()
{
    if (!drawing_)
        return;
    Document& doc = drawing_->document();
    QString name;
    for (int n = 1;; ++n) {
        name = QStringLiteral("Layer%1").arg(n);
        if (doc.findLayer(name.toStdString()) < 0)
            break;
    }
    const int index = doc.addLayer(Layer{name.toStdString(), 7, true});
    selected_ = index;
    changed();
    QTimer::singleShot(50, this, [this, index] { emit editNameRequested(index); });
}

void LayerManager::deleteLayer()
{
    if (!drawing_ || selected_ < 0)
        return;
    std::string error;
    if (!drawing_->document().removeLayer(selected_, &error)) {
        QMessageBox::warning(this, tr("Elimina layer"), QString::fromStdString(error));
        return;
    }
    selected_ = -1;
    changed();
}

void LayerManager::makeCurrent()
{
    if (!drawing_ || selected_ < 0)
        return;
    Document& doc = drawing_->document();
    if (doc.layers()[static_cast<std::size_t>(selected_)].frozen) {
        QMessageBox::information(this, tr("Layer"), tr("Un layer congelato non può diventare corrente."));
        return;
    }
    doc.setCurrentLayer(selected_);
    changed();
}

const ViewportEntity* LayerManager::activeViewport() const
{
    if (!drawing_ || !drawing_->inViewport())
        return nullptr;
    const Entity* e = drawing_->document().find(drawing_->view()->activeViewport());
    return e && e->type() == EntityType::Viewport ? static_cast<const ViewportEntity*>(e) : nullptr;
}

} // namespace cartesio
