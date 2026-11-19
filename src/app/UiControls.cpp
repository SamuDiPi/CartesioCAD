#include "app/UiControls.h"

#include <QAction>
#include <QApplication>
#include <QMenu>

#include <algorithm>
#include <cmath>

namespace cartesio {

// ================================================================ UiCheck

void UiCheck::setChecked(bool on)
{
    if (on == checked_)
        return;
    checked_ = on;
    emit changed();
    if (on)
        for (UiCheck* other : group_)
            if (other != this)
                other->setChecked(false);
    emit toggled(on);
}

void UiCheck::makeGroup(const std::vector<UiCheck*>& checks)
{
    for (UiCheck* c : checks)
        c->group_ = checks;
}

void UiCheck::toggle()
{
    if (!isEnabled())
        return;
    if (isRadio())
        setChecked(true);
    else
        setChecked(!checked_);
}

// ================================================================ UiSpin

void UiSpin::setValue(double v)
{
    v = std::clamp(v, min_, max_);
    const double k = std::pow(10.0, decimals_);
    v = std::round(v * k) / k;
    if (v == value_) {
        emit valueChangedSignal();   // il campo torna a mostrare il valore
        return;
    }
    value_ = v;
    emit valueChangedSignal();
    emit valueChanged(v);
}

QString UiSpin::display() const
{
    return QString::number(value_, 'f', decimals_) + suffix_;
}

void UiSpin::commit(const QString& text)
{
    QString t = text.trimmed();
    if (!suffix_.trimmed().isEmpty() && t.endsWith(suffix_.trimmed()))
        t.chop(suffix_.trimmed().size());
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = t.trimmed().toDouble(&ok);
    setValue(ok ? v : value_);
}

void UiSpin::step(int steps)
{
    if (isEnabled())
        setValue(value_ + steps * (decimals_ > 0 ? std::pow(10.0, -std::min(decimals_, 1)) : 1.0));
}

// ================================================================ UiCombo

void UiCombo::clear()
{
    items_.clear();
    const bool had = current_ != -1;
    current_ = -1;
    emit itemsChanged();
    if (had)
        emit currentIndexChanged(-1);
}

void UiCombo::addItem(const QString& text, const QVariant& data)
{
    items_.push_back({text, data});
    if (current_ < 0) {
        current_ = 0;
        emit itemsChanged();
        emit currentIndexChanged(0);
        return;
    }
    emit itemsChanged();
}

void UiCombo::addItems(const QStringList& texts)
{
    for (const QString& t : texts)
        addItem(t);
}

void UiCombo::insertItem(int index, const QString& text, const QVariant& data)
{
    index = std::clamp(index, 0, count());
    items_.insert(items_.begin() + index, Item{text, data});
    if (current_ < 0)
        current_ = 0;
    else if (index <= current_)
        ++current_;
    emit itemsChanged();
}

void UiCombo::setItemEnabled(int index, bool on)
{
    if (index >= 0 && index < count())
        items_[static_cast<std::size_t>(index)].enabled = on;
}

void UiCombo::setItemToolTip(int index, const QString& tip)
{
    if (index >= 0 && index < count())
        items_[static_cast<std::size_t>(index)].tooltip = tip;
}

QVariant UiCombo::itemData(int index) const
{
    return index >= 0 && index < count() ? items_[static_cast<std::size_t>(index)].data : QVariant();
}

QString UiCombo::itemText(int index) const
{
    return index >= 0 && index < count() ? items_[static_cast<std::size_t>(index)].text : QString();
}

int UiCombo::findData(const QVariant& data) const
{
    for (int i = 0; i < count(); ++i)
        if (items_[static_cast<std::size_t>(i)].data == data)
            return i;
    return -1;
}

int UiCombo::findDataNoCase(const QString& data) const
{
    for (int i = 0; i < count(); ++i)
        if (items_[static_cast<std::size_t>(i)].data.toString().compare(data, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

void UiCombo::setCurrentIndex(int index)
{
    if (index < -1 || index >= count() || index == current_)
        return;
    current_ = index;
    emit itemsChanged();
    emit currentIndexChanged(index);
}

void UiCombo::popup(int globalX, int globalY, int width)
{
    if (!isEnabled() || items_.empty())
        return;
    QMenu menu(QApplication::activeWindow());
    menu.setMinimumWidth(width);
    menu.setToolTipsVisible(true);
    for (int i = 0; i < count(); ++i) {
        const Item& item = items_[static_cast<std::size_t>(i)];
        QAction* a = menu.addAction(item.text);
        a->setCheckable(true);
        a->setChecked(i == current_);
        a->setEnabled(item.enabled);
        a->setToolTip(item.tooltip);
        a->setData(i);
    }
    if (current_ >= 0)
        menu.setActiveAction(menu.actions().value(current_));
    if (QAction* chosen = menu.exec(QPoint(globalX, globalY))) {
        const int index = chosen->data().toInt();
        setCurrentIndex(index);
        emit activated(index);
    }
}

// ================================================================ UiSelectList

void UiSelectList::addItem(const QString& text, const QColor& color, const QVariant& data)
{
    items_ << QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("color"), color}};
    data_.push_back(data);
    selected_.push_back(false);
    emit itemsChanged();
}

std::vector<int> UiSelectList::selectedRows() const
{
    std::vector<int> out;
    for (std::size_t i = 0; i < selected_.size(); ++i)
        if (selected_[i])
            out.push_back(static_cast<int>(i));
    return out;
}

QVariantList UiSelectList::selectedList() const
{
    QVariantList out;
    for (bool b : selected_)
        out << b;
    return out;
}

void UiSelectList::setCurrentRow(int row)
{
    std::fill(selected_.begin(), selected_.end(), false);
    if (row >= 0 && row < static_cast<int>(selected_.size()))
        selected_[static_cast<std::size_t>(row)] = true;
    current_ = anchor_ = row;
    emit selectionChanged();
}

void UiSelectList::click(int row, int modifiers)
{
    if (row < 0 || row >= static_cast<int>(selected_.size()))
        return;
    if ((modifiers & Qt::ShiftModifier) && anchor_ >= 0) {
        if (!(modifiers & Qt::ControlModifier))
            std::fill(selected_.begin(), selected_.end(), false);
        for (int i = std::min(anchor_, row); i <= std::max(anchor_, row); ++i)
            selected_[static_cast<std::size_t>(i)] = true;
        current_ = row;
    } else if (modifiers & Qt::ControlModifier) {
        selected_[static_cast<std::size_t>(row)] = !selected_[static_cast<std::size_t>(row)];
        current_ = anchor_ = row;
    } else {
        std::fill(selected_.begin(), selected_.end(), false);
        selected_[static_cast<std::size_t>(row)] = true;
        current_ = anchor_ = row;
    }
    emit selectionChanged();
}

// ================================================================ UiSheet

void UiSheet::setFrame(const SheetSize& sheet, const BBox& printable, std::optional<BBox> placed, bool outside)
{
    sheet_ = sheet;
    printable_ = printable;
    placed_ = placed;
    outside_ = outside;
    emit changed();
}

QVariantList UiSheet::printable() const
{
    return {printable_.min.x, printable_.min.y, printable_.max.x, printable_.max.y};
}

QVariantList UiSheet::placed() const
{
    if (!placed_ || !placed_->isValid())
        return {};
    // Si stampa solo dentro l'area stampabile.
    const double x0 = std::max(placed_->min.x, printable_.min.x), y0 = std::max(placed_->min.y, printable_.min.y);
    const double x1 = std::min(placed_->max.x, printable_.max.x), y1 = std::min(placed_->max.y, printable_.max.y);
    if (x1 <= x0 || y1 <= y0)
        return {};
    return {x0, y0, x1, y1};
}

} // namespace cartesio
