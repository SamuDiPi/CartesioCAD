#include "app/StartPage.h"

#include "app/AppOptions.h"
#include "app/QmlUi.h"
#include "app/Thumbnails.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QPointer>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

namespace cartesio {

namespace {

QVariant startValue(const QString& key, const QVariant& fallback = {})
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")).value(QStringLiteral("StartPage/") + key, fallback);
}

void setStartValue(const QString& key, const QVariant& value)
{
    QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")).setValue(QStringLiteral("StartPage/") + key, value);
}

QString kindOf(const QFileInfo& info)
{
    const QString suffix = info.suffix().toUpper();
    return suffix.isEmpty() ? QStringLiteral("?") : suffix.left(3);
}

// Gruppo per data dell'ultima apertura, come nell'elenco.
QString groupOf(const QDate& date)
{
    if (!date.isValid())
        return QObject::tr("MENO RECENTI");
    const qint64 days = date.daysTo(QDate::currentDate());
    if (days <= 0)
        return QObject::tr("OGGI");
    if (days == 1)
        return QObject::tr("IERI");
    if (days < 7)
        return QObject::tr("ULTIMA SETTIMANA");
    if (days < 31)
        return QObject::tr("ULTIMO MESE");
    return QObject::tr("MENO RECENTI");
}

} // namespace

QString defaultTemplatesFolder()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/CartesioCAD");
    return base + QStringLiteral("/Modelli");
}

QString templatesFolder()
{
    return appOptions().templatesFolder.isEmpty() ? defaultTemplatesFolder() : appOptions().templatesFolder;
}

QStringList templateFiles()
{
    QStringList files;
    const QDir dir(templatesFolder());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.dwt"), QStringLiteral("*.dwg"), QStringLiteral("*.dxf")},
                                                   QDir::Files, QDir::Name | QDir::IgnoreCase))
        files << info.absoluteFilePath();
    return files;
}

StartPage::StartPage(QWidget* parent) : QQuickWidget(parent)
{
    pool_ = new QThreadPool(this);
    pool_->setMaxThreadCount(2);   // i file possono stare in cartelle di rete: pochi alla volta
    viewMode_ = std::clamp(startValue(QStringLiteral("viewMode"), 0).toInt(), 0, 2);
    setupQuickEngine(this);
    setClearColor(QColor(0x3b, 0x44, 0x53));
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    rootContext()->setContextProperty(QStringLiteral("start"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/StartPage.qml")));
}

StartPage::~StartPage()
{
    pool_->clear();
    pool_->waitForDone(3000);
}

QString StartPage::version() const
{
    return QString::fromLatin1(CARTESIO_VERSION);
}

void StartPage::setRecentFiles(const QStringList& files, const QStringList& pinned, const QHash<QString, QDateTime>& opened)
{
    const QLocale italian(QLocale::Italian);
    recent_.clear();
    const auto add = [&](const QString& path, bool isPinned) {
        const QFileInfo info(path);
        const QDateTime when = opened.value(path);
        QString whenText;
        if (when.isValid())
            whenText = tr("Ultima apertura %1").arg(italian.toString(when, QStringLiteral("dddd d MMMM yyyy HH:mm:ss")));
        else if (info.exists())
            whenText = tr("Modificato %1").arg(italian.toString(info.lastModified(), QStringLiteral("dddd d MMMM yyyy HH:mm:ss")));
        recent_ << QVariantMap{
            {QStringLiteral("name"), info.completeBaseName()},
            {QStringLiteral("file"), info.fileName()},
            {QStringLiteral("folder"), QDir::toNativeSeparators(info.path())},
            {QStringLiteral("path"), path},
            {QStringLiteral("exists"), info.exists()},
            {QStringLiteral("kind"), kindOf(info)},
            {QStringLiteral("pinned"), isPinned},
            {QStringLiteral("group"), isPinned ? tr("BLOCCATI") : groupOf(when.isValid() ? when.date() : info.lastModified().date())},
            {QStringLiteral("when"), whenText},
            {QStringLiteral("shortWhen"), when.isValid() ? italian.toString(when, QLocale::ShortFormat)
                                          : info.exists() ? italian.toString(info.lastModified(), QLocale::ShortFormat) : QString()},
        };
    };
    for (const QString& path : files)
        if (pinned.contains(path))
            add(path, true);
    // Gli altri dal più recente: l'ultima apertura o, per quelli registrati prima, la data del file
    // (così i gruppi per data restano uniti).
    QList<QPair<QDateTime, QString>> others;
    for (const QString& path : files)
        if (!pinned.contains(path)) {
            const QDateTime when = opened.value(path);
            others.append({when.isValid() ? when : QFileInfo(path).lastModified(), path});
        }
    std::stable_sort(others.begin(), others.end(), [](const auto& a, const auto& b) {
        if (a.first.isValid() != b.first.isValid())
            return a.first.isValid();
        return a.first > b.first;
    });
    for (const auto& [when, path] : others)
        add(path, false);
    emit recentChanged();
    for (const QString& path : files)
        loadThumbnail(path);
}

void StartPage::loadThumbnail(const QString& path)
{
    if (loading_.contains(path))
        return;
    loading_.insert(path);
    QPointer<StartPage> self(this);
    pool_->start([self, path] {
        const QString file = thumbnailFile(path);
        QString url;
        if (!file.isEmpty()) {
            // La data nella query fa ricaricare l'immagine quando la miniatura cambia.
            QUrl u = QUrl::fromLocalFile(file);
            u.setQuery(QStringLiteral("v=%1").arg(QFileInfo(file).lastModified().toMSecsSinceEpoch()));
            url = u.toString();
        }
        QMetaObject::invokeMethod(qApp, [self, path, url] {
            if (!self)
                return;
            self->loading_.remove(path);
            if (self->thumbs_.value(path).toString() == url)
                return;
            if (url.isEmpty())
                self->thumbs_.remove(path);
            else
                self->thumbs_.insert(path, url);
            emit self->thumbsChanged();
        }, Qt::QueuedConnection);
    });
}

QString StartPage::templatePath() const
{
    const QString chosen = startValue(QStringLiteral("template")).toString();
    return !chosen.isEmpty() && QFileInfo::exists(chosen) ? chosen : QString();
}

QString StartPage::templateName() const
{
    const QString path = templatePath();
    return path.isEmpty() ? tr("Nessun modello") : QFileInfo(path).fileName();
}

void StartPage::requestNew()
{
    const QString path = templatePath();
    if (path.isEmpty())
        emit newRequested();
    else
        emit templateRequested(path);
}

void StartPage::setViewMode(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == viewMode_)
        return;
    viewMode_ = mode;
    setStartValue(QStringLiteral("viewMode"), mode);
    emit viewModeChanged();
}

void StartPage::setPage(int page)
{
    if (page == page_)
        return;
    page_ = page;
    emit pageChanged();
}

void StartPage::popupTemplates(int x, int y, int width)
{
    QMenu menu(this);
    menu.setMinimumWidth(width);
    const QString current = templatePath();
    const auto choose = [this](const QString& path) {
        setStartValue(QStringLiteral("template"), path);
        emit templateChanged();
    };
    QAction* none = menu.addAction(tr("Nessun modello (disegno vuoto, unità delle Opzioni)"));
    none->setCheckable(true);
    none->setChecked(current.isEmpty());
    connect(none, &QAction::triggered, this, [choose] { choose(QString()); });
    const QStringList files = templateFiles();
    if (!files.isEmpty())
        menu.addSeparator();
    for (const QString& file : files) {
        QAction* a = menu.addAction(QFileInfo(file).fileName());
        a->setCheckable(true);
        a->setChecked(QFileInfo(file) == QFileInfo(current));
        a->setToolTip(QDir::toNativeSeparators(file));
        connect(a, &QAction::triggered, this, [choose, file] { choose(file); });
    }
    menu.addSeparator();
    connect(menu.addAction(tr("Apri la cartella dei modelli...")), &QAction::triggered, this, &StartPage::openTemplatesFolder);
    menu.exec(QPoint(x, y));
}

void StartPage::popupRecentMenu(const QString& path, int x, int y)
{
    bool pinned = false;
    for (const QVariant& v : recent_) {
        const QVariantMap item = v.toMap();
        if (item.value(QStringLiteral("path")).toString() == path)
            pinned = item.value(QStringLiteral("pinned")).toBool();
    }
    QMenu menu(this);
    QAction* open = menu.addAction(tr("Apri"));
    open->setEnabled(QFileInfo::exists(path));
    connect(open, &QAction::triggered, this, [this, path] { emit recentRequested(path); });
    connect(menu.addAction(pinned ? tr("Sblocca dall'elenco") : tr("Blocca nell'elenco")), &QAction::triggered, this,
            [this, path] { emit pinToggled(path); });
    QAction* folder = menu.addAction(tr("Apri la cartella del file"));
    folder->setEnabled(QFileInfo::exists(QFileInfo(path).path()));
    connect(folder, &QAction::triggered, this,
            [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).path())); });
    menu.addSeparator();
    connect(menu.addAction(tr("Rimuovi dall'elenco")), &QAction::triggered, this, [this, path] { emit removeRequested(path); });
    menu.exec(QPoint(x, y));
}

void StartPage::openTemplatesFolder()
{
    const QString folder = templatesFolder();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void StartPage::openLink(const QString& url)
{
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace cartesio
