#include "app/FormDialog.h"

#include "app/QmlUi.h"
#include "app/Theme.h"

#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QTimer>
#include <QQmlContext>
#include <QQuickWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace cartesio {

FormDialog::FormDialog(const QString& title, int buttons, QWidget* parent) : QDialog(parent), buttons_(buttons)
{
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = makeQuickWidget(this, QColor(0x3b, 0x44, 0x53));
    view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view_->rootContext()->setContextProperty(QStringLiteral("form"), this);
    layout->addWidget(view_);
    resize(520, 400);
}

namespace {
QString& grabPath()
{
    static QString path;
    return path;
}
} // namespace

void FormDialog::grabNext(const QString& path)
{
    grabPath() = path;
}

void FormDialog::showEvent(QShowEvent* event)
{
    if (!grabPath().isEmpty()) {
        const QString path = grabPath();
        grabPath().clear();
        QTimer::singleShot(400, this, [this, path] {
            grab().save(path);
            reject();
        });
    }
    if (!loaded_) {
        loaded_ = true;
        view_->setSource(QUrl(QStringLiteral("qrc:/qml/FormDialog.qml")));
    }
    QDialog::showEvent(event);
}

void FormDialog::setSize(int width, int height)
{
    resize(width, height);
}

void FormDialog::page(const QString& title)
{
    pageTitles_ << title;
}

int FormDialog::add(QVariantMap data, std::function<void(const QVariant&)> set)
{
    if (pageTitles_.isEmpty())
        pageTitles_ << QString();
    Field f;
    f.page = static_cast<int>(pageTitles_.size()) - 1;
    data.insert(QStringLiteral("id"), static_cast<int>(fields_.size()));
    f.data = std::move(data);
    f.set = std::move(set);
    fields_.push_back(std::move(f));
    return static_cast<int>(fields_.size()) - 1;
}

void FormDialog::group(const QString& title)
{
    add({{QStringLiteral("kind"), QStringLiteral("group")}, {QStringLiteral("label"), title}}, {});
}

int FormDialog::check(const QString& label, bool& value, int indent)
{
    return check(label, value, [&value](bool on) { value = on; }, indent);
}

int FormDialog::check(const QString& label, bool value, std::function<void(bool)> set, int indent)
{
    return add({{QStringLiteral("kind"), QStringLiteral("check")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), value},
                {QStringLiteral("indent"), indent}},
               [set](const QVariant& v) { set(v.toBool()); });
}

int FormDialog::integer(const QString& label, int lo, int hi, int& value, const QString& suffix)
{
    return add({{QStringLiteral("kind"), QStringLiteral("number")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), value},
                {QStringLiteral("min"), lo},
                {QStringLiteral("max"), hi},
                {QStringLiteral("decimals"), 0},
                {QStringLiteral("suffix"), suffix}},
               [&value](const QVariant& v) { value = qRound(v.toDouble()); });
}

int FormDialog::number(const QString& label, double lo, double hi, int decimals, double& value, const QString& suffix)
{
    return number(label, lo, hi, decimals, value, [&value](double v) { value = v; }, suffix);
}

int FormDialog::number(const QString& label, double lo, double hi, int decimals, double value,
                       std::function<void(double)> set, const QString& suffix)
{
    return add({{QStringLiteral("kind"), QStringLiteral("number")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), value},
                {QStringLiteral("min"), lo},
                {QStringLiteral("max"), hi},
                {QStringLiteral("decimals"), decimals},
                {QStringLiteral("suffix"), suffix}},
               [set](const QVariant& v) { set(v.toDouble()); });
}

int FormDialog::slider(const QString& label, int lo, int hi, int& value, const QString& suffix)
{
    return add({{QStringLiteral("kind"), QStringLiteral("slider")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), value},
                {QStringLiteral("min"), lo},
                {QStringLiteral("max"), hi},
                {QStringLiteral("suffix"), suffix}},
               [&value](const QVariant& v) { value = v.toInt(); });
}

int FormDialog::color(const QString& label, QColor& value)
{
    return add({{QStringLiteral("kind"), QStringLiteral("color")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), value.name()}},
               [&value](const QVariant& v) { value = QColor(v.toString()); });
}

int FormDialog::choice(const QString& label, const QList<QPair<QString, QVariant>>& items, const QVariant& current,
                       std::function<void(const QVariant&)> set)
{
    QStringList texts;
    QVariantList values;
    int index = 0;
    for (int i = 0; i < items.size(); ++i) {
        texts << items[i].first;
        values << items[i].second;
        if (items[i].second == current)
            index = i;
    }
    return add({{QStringLiteral("kind"), QStringLiteral("choice")},
                {QStringLiteral("label"), label},
                {QStringLiteral("items"), texts},
                {QStringLiteral("values"), values},
                {QStringLiteral("index"), index},
                {QStringLiteral("value"), items.isEmpty() ? QString() : texts[index]}},
               std::move(set));
}

int FormDialog::text(const QString& label, QString& value, const QString& placeholder)
{
    return text(label, value, [&value](const QString& t) { value = t.trimmed(); }, placeholder);
}

int FormDialog::text(const QString& label, const QString& value, std::function<void(const QString&)> set,
                     const QString& placeholder)
{
    return add({{QStringLiteral("kind"), QStringLiteral("text")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), value},
                {QStringLiteral("placeholder"), placeholder}},
               [set](const QVariant& v) { set(v.toString()); });
}

int FormDialog::path(const QString& label, QString& value, PathMode mode, const QString& placeholder, const QString& filter)
{
    return add({{QStringLiteral("kind"), QStringLiteral("path")},
                {QStringLiteral("label"), label},
                {QStringLiteral("value"), QDir::toNativeSeparators(value)},
                {QStringLiteral("placeholder"), placeholder},
                {QStringLiteral("mode"), static_cast<int>(mode)},
                {QStringLiteral("filter"), filter},
                {QStringLiteral("browseText"), mode == AddFolder ? tr("Aggiungi...") : tr("Sfoglia...")}},
               [&value](const QVariant& v) { value = v.toString().trimmed(); });
}

int FormDialog::note(const QString& text)
{
    return add({{QStringLiteral("kind"), QStringLiteral("note")}, {QStringLiteral("label"), text}}, {});
}

int FormDialog::list(const QStringList& items, int current, std::function<void(int)> select,
                     std::function<void(int)> activated)
{
    const int id = add({{QStringLiteral("kind"), QStringLiteral("list")},
                        {QStringLiteral("items"), items},
                        {QStringLiteral("index"), current}},
                       [select](const QVariant& v) { select(v.toInt()); });
    fields_[static_cast<std::size_t>(id)].activated = std::move(activated);
    return id;
}

int FormDialog::gallery(const QStringList& names, const QStringList& images, int current, std::function<void(int)> select,
                        std::function<void(int)> activated)
{
    const int id = add({{QStringLiteral("kind"), QStringLiteral("gallery")},
                        {QStringLiteral("items"), names},
                        {QStringLiteral("images"), images},
                        {QStringLiteral("index"), current}},
                       [select](const QVariant& v) { select(v.toInt()); });
    fields_[static_cast<std::size_t>(id)].activated = std::move(activated);
    return id;
}

int FormDialog::buttonsRow(const QList<QPair<QString, std::function<void()>>>& buttons)
{
    QStringList texts;
    for (const auto& b : buttons)
        texts << b.first;
    const int id = add({{QStringLiteral("kind"), QStringLiteral("buttons")}, {QStringLiteral("items"), texts}}, {});
    for (const auto& b : buttons)
        fields_[static_cast<std::size_t>(id)].actions.push_back(b.second);
    return id;
}

int FormDialog::aci(int value, bool byLayer, std::function<void(int)> set)
{
    QStringList palette, names;
    for (int i = 0; i <= 256; ++i) {
        palette << (i >= 1 && i <= 255 ? theme::aciColor(i).name() : QString());
        names << theme::aciName(i);
    }
    return add({{QStringLiteral("kind"), QStringLiteral("aci")},
                {QStringLiteral("value"), value},
                {QStringLiteral("byLayer"), byLayer},
                {QStringLiteral("palette"), palette},
                {QStringLiteral("names"), names},
                {QStringLiteral("byLayerValue"), 256},
                {QStringLiteral("byBlockValue"), 0}},
               [set](const QVariant& v) { set(v.toInt()); });
}

void FormDialog::enableWhen(int field, int checkField)
{
    fields_[static_cast<std::size_t>(field)].enabledBy = checkField;
}

void FormDialog::changed(int field)
{
    emit fieldChanged(field, fields_[static_cast<std::size_t>(field)].data);
}

void FormDialog::setItems(int field, const QStringList& items, int current)
{
    auto& d = fields_[static_cast<std::size_t>(field)].data;
    d.insert(QStringLiteral("items"), items);
    d.insert(QStringLiteral("index"), current);
    changed(field);
}

void FormDialog::setChoices(int field, const QList<QPair<QString, QVariant>>& items, const QVariant& current)
{
    QStringList texts;
    QVariantList values;
    int index = 0;
    for (int i = 0; i < items.size(); ++i) {
        texts << items[i].first;
        values << items[i].second;
        if (items[i].second == current)
            index = i;
    }
    auto& d = fields_[static_cast<std::size_t>(field)].data;
    d.insert(QStringLiteral("items"), texts);
    d.insert(QStringLiteral("values"), values);
    d.insert(QStringLiteral("index"), index);
    d.insert(QStringLiteral("value"), texts.value(index));
    changed(field);
}

void FormDialog::setFieldText(int field, const QString& text)
{
    fields_[static_cast<std::size_t>(field)].data.insert(QStringLiteral("label"), text);
    changed(field);
}

void FormDialog::setFieldValue(int field, const QVariant& value)
{
    fields_[static_cast<std::size_t>(field)].data.insert(QStringLiteral("value"), value);
    changed(field);
}

QVariantList FormDialog::pages() const
{
    QVariantList out;
    for (int p = 0; p < pageTitles_.size(); ++p) {
        QVariantList rows;
        for (const Field& f : fields_)
            if (f.page == p)
                rows << f.data;
        out << QVariantMap{{QStringLiteral("title"), pageTitles_[p]}, {QStringLiteral("rows"), rows}};
    }
    return out;
}

void FormDialog::setCurrentPage(int index)
{
    index = std::clamp(index, 0, std::max(0, static_cast<int>(pageTitles_.size()) - 1));
    if (index != currentPage_) {
        currentPage_ = index;
        emit currentPageChanged();
    }
}

QVariantList FormDialog::buttonList() const
{
    QVariantList out;
    const auto addButton = [&](int which, const QString& text) {
        if (buttons_ & which)
            out << QVariantMap{{QStringLiteral("id"), which}, {QStringLiteral("text"), text}};
    };
    addButton(Ok, tr("OK"));
    addButton(Cancel, tr("Annulla"));
    addButton(Apply, tr("Applica"));
    addButton(Close, tr("Chiudi"));
    return out;
}

QVariantMap FormDialog::enabledFields() const
{
    QVariantMap out;
    for (std::size_t i = 0; i < fields_.size(); ++i)
        if (fields_[i].enabledBy >= 0)
            out.insert(QString::number(i),
                       fields_[static_cast<std::size_t>(fields_[i].enabledBy)].data.value(QStringLiteral("value")).toBool());
    return out;
}

void FormDialog::edit(int field, const QVariant& value)
{
    if (field < 0 || field >= static_cast<int>(fields_.size()))
        return;
    Field& f = fields_[static_cast<std::size_t>(field)];
    const QString kind = f.data.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("choice")) {
        const int index = value.toInt();
        const QStringList items = f.data.value(QStringLiteral("items")).toStringList();
        const QVariantList values = f.data.value(QStringLiteral("values")).toList();
        if (index < 0 || index >= values.size())
            return;
        f.data.insert(QStringLiteral("index"), index);
        f.data.insert(QStringLiteral("value"), items[index]);
        if (f.set)
            f.set(values[index]);
        changed(field);
        return;
    }
    if (kind == QLatin1String("list")) {
        f.data.insert(QStringLiteral("index"), value.toInt());
        if (f.set)
            f.set(value);
        return;
    }
    f.data.insert(QStringLiteral("value"), value);
    if (f.set)
        f.set(value);
    if (kind == QLatin1String("check"))
        emit enabledChanged();
    if (kind == QLatin1String("aci"))
        changed(field);
}

void FormDialog::browse(int field)
{
    const Field& f = fields_[static_cast<std::size_t>(field)];
    const QString current = f.data.value(QStringLiteral("value")).toString();
    const QString filter = f.data.value(QStringLiteral("filter")).toString();
    const QString title = f.data.value(QStringLiteral("label")).toString().remove(QLatin1Char(':'));
    QString chosen;
    switch (f.data.value(QStringLiteral("mode")).toInt()) {
    case Folder:
        chosen = QFileDialog::getExistingDirectory(this, title, current);
        break;
    case OpenFile:
        chosen = QFileDialog::getOpenFileName(this, title, current, filter);
        break;
    case SaveFile:
        chosen = QFileDialog::getSaveFileName(this, title, current, filter, nullptr, QFileDialog::DontConfirmOverwrite);
        break;
    case AddFolder: {
        const QString dir = QFileDialog::getExistingDirectory(this, title);
        if (!dir.isEmpty())
            chosen = current.isEmpty() ? QDir::toNativeSeparators(dir) : current + QLatin1Char(';') + QDir::toNativeSeparators(dir);
        break;
    }
    }
    if (chosen.isEmpty())
        return;
    chosen = QDir::toNativeSeparators(chosen);
    edit(field, chosen);
    changed(field);
}

void FormDialog::pickColor(int field)
{
    const QColor current(fields_[static_cast<std::size_t>(field)].data.value(QStringLiteral("value")).toString());
    const QColor c = QColorDialog::getColor(current, this, tr("Colore"));
    if (!c.isValid())
        return;
    edit(field, c.name());
    changed(field);
}

void FormDialog::activate(int field, int index)
{
    const Field& f = fields_[static_cast<std::size_t>(field)];
    if (f.activated)
        f.activated(index);
}

void FormDialog::press(int field, int index)
{
    const Field& f = fields_[static_cast<std::size_t>(field)];
    if (index >= 0 && index < static_cast<int>(f.actions.size()) && f.actions[static_cast<std::size_t>(index)])
        f.actions[static_cast<std::size_t>(index)]();
}

void FormDialog::popupChoice(int field, int globalX, int globalY, int width)
{
    const Field& f = fields_[static_cast<std::size_t>(field)];
    const QStringList items = f.data.value(QStringLiteral("items")).toStringList();
    const int current = f.data.value(QStringLiteral("index")).toInt();
    QMenu menu(this);
    menu.setMinimumWidth(width);
    for (int i = 0; i < items.size(); ++i) {
        QAction* a = menu.addAction(items[i]);
        a->setCheckable(true);
        a->setChecked(i == current);
        a->setData(i);
    }
    if (QAction* chosen = menu.exec(QPoint(globalX, globalY)))
        edit(field, chosen->data().toInt());
}

void FormDialog::button(int which)
{
    if ((which == Ok || which == Apply) && onApply)
        onApply();
    if (which == Ok || which == Close)
        accept();
    else if (which == Cancel)
        reject();
}

void FormDialog::openLink(const QString& url)
{
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace cartesio
