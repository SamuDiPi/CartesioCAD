#include "app/QmlUi.h"

#include "app/AppOptions.h"
#include "app/CanvasItem.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>

#include <mutex>

namespace cartesio {

namespace {

std::mutex& iconMutex()
{
    static std::mutex m;
    return m;
}

std::map<qint64, std::pair<int, QIcon>>& iconsByKey()
{
    static std::map<qint64, std::pair<int, QIcon>> icons;
    return icons;
}

std::map<int, QIcon>& iconsById()
{
    static std::map<int, QIcon> icons;
    return icons;
}

} // namespace

QString IconProvider::url(const QIcon& icon)
{
    if (icon.isNull())
        return {};
    const std::lock_guard<std::mutex> lock(iconMutex());
    auto& byKey = iconsByKey();
    auto it = byKey.find(icon.cacheKey());
    if (it == byKey.end()) {
        const int id = static_cast<int>(byKey.size()) + 1;
        it = byKey.emplace(icon.cacheKey(), std::make_pair(id, icon)).first;
        iconsById()[id] = icon;
    }
    return QStringLiteral("image://cad/%1").arg(it->second.first);
}

QPixmap IconProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize)
{
    QIcon icon;
    {
        const std::lock_guard<std::mutex> lock(iconMutex());
        const auto it = iconsById().find(id.section(QLatin1Char('/'), 0, 0).toInt());
        if (it != iconsById().end())
            icon = it->second;
    }
    const QSize wanted = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(32, 32);
    QPixmap pixmap = icon.pixmap(wanted);
    if (size)
        *size = pixmap.size();
    return pixmap;
}

QQuickWidget* makeQuickWidget(QWidget* parent, const QColor& background)
{
    auto* w = new QQuickWidget(parent);
    setupQuickEngine(w);
    w->setClearColor(background);
    return w;
}

// ============================================================ ActionItem

ActionItem::ActionItem(QAction* action, QObject* parent) : QObject(parent), action_(action)
{
    if (action)
        connect(action, &QAction::changed, this, &ActionItem::changed);
}

QString ActionItem::text() const
{
    if (!action_)
        return {};
    QString t = action_->text();
    t.remove(QLatin1Char('&'));
    return t;
}

QString ActionItem::shortText() const
{
    return text().replace(QLatin1Char('\n'), QLatin1Char(' '));
}

QString ActionItem::tooltip() const
{
    if (!action_)
        return {};
    QString tip = action_->toolTip();
    if (!action_->shortcut().isEmpty())
        tip += QStringLiteral("  (%1)").arg(action_->shortcut().toString(QKeySequence::NativeText));
    return tip;
}

QString ActionItem::icon() const
{
    if (!action_)
        return {};
    const QIcon i = action_->icon();
    if (i.cacheKey() != iconKey_) {
        iconKey_ = i.cacheKey();
        iconUrl_ = IconProvider::url(i);
    }
    return iconUrl_;
}

void ActionItem::trigger()
{
    if (!action_ || !action_->isEnabled())
        return;
    // Le azioni con un menu e senza un comando proprio aprono il menu (lo fa il QML con popup()).
    QPointer<QAction> a = action_;
    QTimer::singleShot(0, this, [a] {
        if (a)
            a->trigger();
    });
}

void ActionItem::popup(int globalX, int globalY)
{
    if (action_ && action_->menu())
        action_->menu()->popup(QPoint(globalX, globalY));
}

// ============================================================ ComboModel

ComboModel::ComboModel(int width, QObject* parent) : QObject(parent), width_(width) {}

void ComboModel::notify()
{
    if (pending_)
        return;
    pending_ = true;
    QMetaObject::invokeMethod(this, [this] {
        pending_ = false;
        emit itemsChanged();
    }, Qt::QueuedConnection);
}

void ComboModel::clear()
{
    items_.clear();
    current_ = -1;
    notify();
}

void ComboModel::addItem(const QIcon& icon, const QString& text, const QVariant& data)
{
    items_.push_back({icon, text, data});
    if (current_ < 0)
        current_ = 0;
    notify();
}

void ComboModel::insertItem(int index, const QString& text, const QVariant& data)
{
    index = std::clamp(index, 0, count());
    items_.insert(items_.begin() + index, Item{QIcon(), text, data});
    if (current_ >= index)
        ++current_;
    if (current_ < 0)
        current_ = 0;
    notify();
}

void ComboModel::setItemIcon(int index, const QIcon& icon)
{
    if (index >= 0 && index < count()) {
        items_[static_cast<std::size_t>(index)].icon = icon;
        notify();
    }
}

QVariant ComboModel::itemData(int index) const
{
    return index >= 0 && index < count() ? items_[static_cast<std::size_t>(index)].data : QVariant();
}

int ComboModel::findData(const QVariant& data) const
{
    for (int i = 0; i < count(); ++i)
        if (items_[static_cast<std::size_t>(i)].data == data)
            return i;
    return -1;
}

void ComboModel::setCurrentIndex(int index)
{
    current_ = index >= 0 && index < count() ? index : -1;
    notify();
}

void ComboModel::setEnabled(bool on)
{
    if (enabled_ != on) {
        enabled_ = on;
        notify();
    }
}

QString ComboModel::currentText() const
{
    return current_ >= 0 && current_ < count() ? items_[static_cast<std::size_t>(current_)].text : QString();
}

QString ComboModel::currentIcon() const
{
    return current_ >= 0 && current_ < count() ? IconProvider::url(items_[static_cast<std::size_t>(current_)].icon)
                                               : QString();
}

namespace {

// Riga dell'elenco dei layer: le icone sono pulsanti (acceso, congelato, bloccato, colore), il nome sceglie.
class LayerRow : public QWidget {
public:
    LayerRow(const QIcon& icon, int iconWidth, int slotCount, const QString& text, std::function<void(int)> onSlot,
             std::function<void()> onChoose, QWidget* parent)
        : QWidget(parent), iconWidth_(iconWidth), slots_(slotCount), onSlot_(std::move(onSlot)), onChoose_(std::move(onChoose))
    {
        setMouseTracking(true);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(6, 2, 10, 2);
        row->setSpacing(6);
        icon_ = new QLabel(this);
        icon_->setPixmap(icon.pixmap(QSize(iconWidth, 16)));
        icon_->setFixedSize(iconWidth, 16);
        row->addWidget(icon_);
        auto* label = new QLabel(text, this);
        row->addWidget(label, 1);
        setAttribute(Qt::WA_Hover);
        setAutoFillBackground(true);
    }
    void setIcon(const QIcon& icon) { icon_->setPixmap(icon.pixmap(QSize(iconWidth_, 16))); }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const int x = static_cast<int>(event->position().x()) - icon_->x();
        const int slot = x >= 0 && x < slots_ * 18 ? x / 18 : -1;
        if (slot >= 0)
            onSlot_(slot);
        else
            onChoose_();
    }
    void enterEvent(QEnterEvent*) override { setStyleSheet(QStringLiteral("background: #2f5d94;")); }
    void leaveEvent(QEvent*) override { setStyleSheet(QString()); }

private:
    QLabel* icon_;
    int iconWidth_;
    int slots_;
    std::function<void(int)> onSlot_;
    std::function<void()> onChoose_;
};

} // namespace

void ComboModel::popup(int globalX, int globalY)
{
    if (!enabled_ || items_.empty())
        return;
    if (menu_)
        menu_->deleteLater();
    auto* menu = new QMenu;
    menu_ = menu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setMinimumWidth(width_);
    for (int i = 0; i < count(); ++i) {
        const Item& item = items_[static_cast<std::size_t>(i)];
        if (slots_ > 0) {
            auto* action = new QWidgetAction(menu);
            QPointer<QMenu> guard(menu);
            auto* row = new LayerRow(
                item.icon, iconWidth_, slots_, item.text,
                [this, i](int slot) { emit slotClicked(i, slot); },
                [this, i, guard] {
                    if (guard)
                        guard->close();
                    emit activated(i);
                },
                menu);
            // Dopo il clic su un'icona la riga mostra subito lo stato nuovo.
            connect(this, &ComboModel::itemsChanged, row, [this, row, i] {
                if (i < count())
                    row->setIcon(items_[static_cast<std::size_t>(i)].icon);
            });
            action->setDefaultWidget(row);
            menu->addAction(action);
        } else {
            QAction* a = menu->addAction(item.icon, item.text);
            a->setCheckable(true);
            a->setChecked(i == current_);
            connect(a, &QAction::triggered, this, [this, i] { emit activated(i); });
        }
    }
    connect(menu, &QMenu::aboutToHide, this, &ComboModel::popupClosed);
    menu->popup(QPoint(globalX, globalY));
}

// ============================================================ TabsModel

int TabsModel::addTab(const QString& text, bool closable)
{
    tabs_.push_back({text, {}, closable});
    emit tabsChanged();
    if (current_ < 0)
        setCurrentIndex(0);
    return count() - 1;
}

void TabsModel::removeTab(int index)
{
    if (index < 0 || index >= count())
        return;
    tabs_.erase(tabs_.begin() + index);
    const int before = current_;
    if (current_ >= count())
        current_ = count() - 1;
    else if (current_ > index)
        --current_;
    emit tabsChanged();
    if (current_ != before || index == before)
        emit currentChanged(current_);
}

void TabsModel::setTabText(int index, const QString& text)
{
    if (index >= 0 && index < count() && tabs_[static_cast<std::size_t>(index)].text != text) {
        tabs_[static_cast<std::size_t>(index)].text = text;
        emit tabsChanged();
    }
}

QString TabsModel::tabText(int index) const
{
    return index >= 0 && index < count() ? tabs_[static_cast<std::size_t>(index)].text : QString();
}

void TabsModel::setTabData(int index, const QVariant& data)
{
    if (index >= 0 && index < count())
        tabs_[static_cast<std::size_t>(index)].data = data;
}

QVariant TabsModel::tabData(int index) const
{
    return index >= 0 && index < count() ? tabs_[static_cast<std::size_t>(index)].data : QVariant();
}

void TabsModel::setCurrentIndex(int index)
{
    if (index < 0 || index >= count() || index == current_)
        return;
    current_ = index;
    emit currentChanged(index);
}

QVariantList TabsModel::tabs() const
{
    QVariantList out;
    for (const Tab& t : tabs_)
        out.append(QVariantMap{{QStringLiteral("text"), t.text}, {QStringLiteral("closable"), t.closable}});
    return out;
}

} // namespace cartesio

#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QToolTip>

namespace cartesio {

void UiHelper::showToolTip(int globalX, int globalY, const QString& text)
{
    if (!text.isEmpty())
        QToolTip::showText(QPoint(globalX, globalY), text);
}

void UiHelper::hideToolTip()
{
    QToolTip::hideText();
}

bool qmlRootUi()
{
    // La variabile d'ambiente CARTESIO_UI (qml / widgets) vale più dell'opzione; letta una volta all'avvio.
    static const bool root = [] {
        const QByteArray env = qgetenv("CARTESIO_UI").toLower();
        if (env == "widgets")
            return false;
        if (env == "qml")
            return true;
        return appOptions().uiMode == 0;
    }();
    return root;
}

void setupQuickEngine(QQuickWidget* widget)
{
    static const int theme = qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Theme.qml")), "CartesioCAD", 1, 0, "Theme");
    static const int canvas = qmlRegisterType<CadCanvas>("CartesioCAD", 1, 0, "CadCanvas");
    Q_UNUSED(theme);
    Q_UNUSED(canvas);
    widget->engine()->addImageProvider(QStringLiteral("cad"), new IconProvider);
    widget->engine()->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    widget->rootContext()->setContextProperty(QStringLiteral("ui"), new UiHelper(widget));
    widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
}

} // namespace cartesio

#include "app/Theme.h"

namespace cartesio {

StatusBar::StatusBar(QWidget* parent) : QQuickWidget(parent), layoutTabs_(new TabsModel(this))
{
    setupQuickEngine(this);
    setClearColor(theme::kPanelDark);
    setFocusPolicy(Qt::NoFocus);
    rootContext()->setContextProperty(QStringLiteral("sbar"), this);   // "status" è già una proprietà di Image
    setFixedHeight(30);
}

void StatusBar::setToggles(const QList<QAction*>& actions)
{
    toggles_.clear();
    for (QAction* a : actions)
        toggles_.append(QVariant::fromValue(static_cast<QObject*>(new ActionItem(a, this))));
    // Il QML si carica dopo: gli interruttori sono costanti.
    if (!qmlRootUi())
        setSource(QUrl(QStringLiteral("qrc:/qml/StatusBar.qml")));
}

void StatusBar::setSpace(const QString& text, bool enabled)
{
    if (spaceText_ != text || spaceEnabled_ != enabled) {
        spaceText_ = text;
        spaceEnabled_ = enabled;
        emit changed();
    }
}

void StatusBar::setViewport(bool visible, const QString& scale, bool scaleEnabled, const QIcon& lock, const QString& tip)
{
    viewportVisible_ = visible;
    viewportScale_ = scale;
    viewportScaleEnabled_ = scaleEnabled;
    lockIcon_ = IconProvider::url(lock);
    lockTip_ = tip;
    emit changed();
}

} // namespace cartesio
