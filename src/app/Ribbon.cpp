#include "app/Ribbon.h"

#include "app/Theme.h"

#include <QQmlContext>
#include <QPointer>
#include <QMenu>

#include <functional>

namespace cartesio {

QObject* RibbonPanel::item(QAction* action)
{
    return new ActionItem(action, this);
}

void RibbonPanel::addLargeButton(QAction* action)
{
    actionColumns_.push_back({action});
    comboColumns_.emplace_back();
    columns_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("large")},
                                {QStringLiteral("action"), QVariant::fromValue(item(action))}});
    emit changed();
}

void RibbonPanel::addSmallColumn(std::initializer_list<QAction*> actions, bool showText)
{
    QVariantList list;
    actionColumns_.emplace_back(actions);
    comboColumns_.emplace_back();
    for (QAction* a : actions)
        list.append(QVariant::fromValue(item(a)));
    columns_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("small")},
                                {QStringLiteral("actions"), list},
                                {QStringLiteral("showText"), showText}});
    emit changed();
}

void RibbonPanel::addComboColumn(std::initializer_list<ComboModel*> combos, std::vector<std::vector<QAction*>> rows)
{
    QVariantList comboList;
    comboColumns_.emplace_back(combos);
    actionColumns_.emplace_back();
    for (const auto& row : rows)
        actionColumns_.back().insert(actionColumns_.back().end(), row.begin(), row.end());
    for (ComboModel* c : combos) {
        c->setParent(this);
        comboList.append(QVariant::fromValue(static_cast<QObject*>(c)));
    }
    QVariantList rowList;
    for (const auto& row : rows) {
        QVariantList r;
        for (QAction* a : row)
            r.append(QVariant::fromValue(item(a)));
        rowList.append(QVariant(r));
    }
    columns_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("combos")},
                                {QStringLiteral("combos"), comboList},
                                {QStringLiteral("rows"), rowList}});
    emit changed();
}

QString RibbonPanel::iconUrl() const
{
    for (const auto& column : actionColumns_)
        for (QAction* a : column)
            if (a && !a->icon().isNull())
                return IconProvider::url(a->icon());
    return {};
}

void RibbonPanel::popup(int globalX, int globalY)
{
    auto* menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QPoint at(globalX, globalY);
    for (std::size_t c = 0; c < actionColumns_.size(); ++c) {
        if (c > 0 && !menu->isEmpty())
            menu->addSeparator();
        // Elenchi a discesa: una voce che apre l'elenco al posto del menu.
        for (ComboModel* combo : comboColumns_[c]) {
            const QString text = combo->tooltip().isEmpty() ? combo->currentText() : combo->tooltip();
            QAction* open = menu->addAction(text + QStringLiteral(": ") + combo->currentText() + QStringLiteral(" \u25BE"));
            QPointer<ComboModel> guard(combo);
            connect(open, &QAction::triggered, combo, [guard, at] {
                if (guard)
                    guard->popup(at.x(), at.y());
            });
        }
        for (QAction* a : actionColumns_[c])
            if (a && a->isVisible())
                menu->addAction(a);   // con il suo menu (varianti) diventa un sottomenu
    }
    menu->popup(at);
}

Ribbon::Ribbon(QWidget* parent) : QQuickWidget(parent)
{
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    setFocusPolicy(Qt::NoFocus);
    rootContext()->setContextProperty(QStringLiteral("ribbon"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/Ribbon.qml")));
    setFixedHeight(166);
}

RibbonPanel* Ribbon::addPanel(const QString& tab, const QString& title)
{
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [&tab](const Tab& t) { return t.title == tab; });
    if (it == tabs_.end()) {
        tabs_.push_back({tab, {}});
        it = tabs_.end() - 1;
    }
    auto* panel = new RibbonPanel(title, this);
    it->panels.append(panel);
    emit changed();
    return panel;
}

void Ribbon::setQuickAccess(QAction* app, const QList<QAction*>& actions)
{
    app_ = new ActionItem(app, this);
    quick_.clear();
    for (QAction* a : actions)
        quick_.append(QVariant::fromValue(static_cast<QObject*>(new ActionItem(a, this))));
    emit changed();
}

void Ribbon::setTitle(const QString& title)
{
    if (title_ != title) {
        title_ = title;
        emit titleChanged();
    }
}

bool Ribbon::setHighlight(const QString& label)
{
    auto norm = [](const QAction* a) {
        QString t = a->text();
        return t.replace(u'\n', u' ').remove(u'&');
    };
    QString found, tab;
    if (!label.isEmpty())
        for (const Tab& t : tabs_) {
            if (!t.shown || !found.isEmpty())
                continue;
            for (RibbonPanel* p : t.panels)
                for (const auto& column : p->actionColumns())
                    for (QAction* a : column) {
                        if (!a || !found.isEmpty())
                            continue;
                        bool match = norm(a) == label;
                        if (!match && a->menu())
                            for (QAction* sub : a->menu()->actions())
                                match = match || norm(sub) == label;
                        if (match) {
                            found = norm(a);
                            tab = t.title;
                        }
                    }
        }
    highlight_ = found;
    if (!tab.isEmpty())
        selectTab(tab);
    emit highlightChanged();
    return !found.isEmpty() || label.isEmpty();
}

QStringList Ribbon::describe() const
{
    QStringList out;
    std::function<void(const QString&, QAction*)> add = [&](const QString& where, QAction* a) {
        if (!a)
            return;
        QString text = a->text();
        text.replace(u'\n', u' ').remove(u'&');
        const QString tip = a->toolTip();
        const int c = tip.indexOf(QLatin1String("Comando: "));
        const QString command = c >= 0 ? tip.mid(c + 9).section(u'\n', 0, 0).trimmed() : QString();
        out << QStringLiteral("%1 > %2%3").arg(where, text, command.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(command));
        // Le varianti del menu (il pulsante stesso può esserci di nuovo: una volta sola, e solo un livello).
        if (a->menu() && !where.contains(QLatin1String(" > ") + text))
            for (QAction* sub : a->menu()->actions())
                if (!sub->isSeparator() && sub != a)
                    add(where + QStringLiteral(" > ") + text, sub);
    };
    for (const Tab& t : tabs_)
        for (RibbonPanel* p : t.panels)
            for (const auto& column : p->actionColumns())
                for (QAction* a : column)
                    add(t.title + QStringLiteral(" > ") + p->title(), a);
    return out;
}

QVariantList Ribbon::tabs() const
{
    QVariantList out;
    for (const Tab& t : tabs_) {
        if (!t.shown)
            continue;
        QVariantList panels;
        for (RibbonPanel* p : t.panels)
            panels.append(QVariant::fromValue(static_cast<QObject*>(p)));
        out.append(QVariantMap{{QStringLiteral("title"), t.title},
                               {QStringLiteral("panels"), panels},
                               {QStringLiteral("contextual"), t.contextual}});
    }
    return out;
}

int Ribbon::visibleIndex(const QString& title) const
{
    int index = 0;
    for (const Tab& t : tabs_) {
        if (!t.shown)
            continue;
        if (t.title == title)
            return index;
        ++index;
    }
    return -1;
}

QString Ribbon::visibleTitle(int index) const
{
    for (const Tab& t : tabs_)
        if (t.shown && index-- == 0)
            return t.title;
    return {};
}

void Ribbon::setContextual(const QString& tab)
{
    for (Tab& t : tabs_)
        if (t.title == tab) {
            t.contextual = true;
            t.shown = false;
        }
    emit changed();
}

void Ribbon::showContextual(const QString& tab, bool on, bool select)
{
    for (Tab& t : tabs_)
        if (t.title == tab && t.contextual && t.shown != on) {
            // La scheda scelta resta quella (le posizioni cambiano: si ritrova per nome).
            const QString keep = visibleTitle(currentShown_);
            t.shown = on;
            emit changed();
            int index = select ? (on ? visibleIndex(tab) : visibleIndex(lastUserTab_)) : visibleIndex(keep);
            if (index < 0)
                index = visibleIndex(lastUserTab_);
            selectedTab_ = std::max(0, index);
            currentShown_ = selectedTab_;
            emit selectedTabChanged();
        }
}

} // namespace cartesio
