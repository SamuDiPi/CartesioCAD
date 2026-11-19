#include "app/BlockLibrary.h"

#include "app/DrawingWidget.h"
#include "app/QmlUi.h"
#include "app/Theme.h"
#include "app/Thumbnails.h"
#include "core/Document.h"
#include "core/Import.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include <cmath>

namespace cartesio {

namespace {

constexpr int kPreviewSize = 96;
constexpr int kMaxRecent = 30;

QSettings librarySettings()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

QString previewFolder()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/CartesioCAD");
    return base + QStringLiteral("/Miniature/Blocchi");
}

// Disegno ricorsivo di un blocco: `t` porta le coordinate del blocco in quelle dell'anteprima (prima del
// ribaltamento dell'asse Y, fatto dal QPainter).
class PreviewPainter {
public:
    PreviewPainter(const Document& doc, QPainter& p) : doc_(doc), p_(p) {}

    void collect(const BlockDef& def, const Affine& t, BBox& box, int depth = 0) const
    {
        if (depth > 8)
            return;
        for (const auto& e : def.entities) {
            if (e->type() == EntityType::AttDef)
                continue;
            if (e->type() == EntityType::Insert) {
                const auto& ins = static_cast<const InsertEntity&>(*e);
                if (ins.block)
                    collect(*ins.block, t * ins.affine(), box, depth + 1);
                continue;
            }
            const BBox b = e->bounds();
            if (!b.isValid())
                continue;
            box.expand(t.apply(b.min));
            box.expand(t.apply(b.max));
            box.expand(t.apply({b.min.x, b.max.y}));
            box.expand(t.apply({b.max.x, b.min.y}));
        }
    }

    void paint(const BlockDef& def, const Affine& t, QColor blockColor, int depth = 0)
    {
        if (depth > 8)
            return;
        for (const auto& e : def.entities) {
            QColor c = colorOf(*e, blockColor);
            switch (e->type()) {
            case EntityType::AttDef:
                break;
            case EntityType::Insert: {
                const auto& ins = static_cast<const InsertEntity&>(*e);
                if (ins.block)
                    paint(*ins.block, t * ins.affine(), c, depth + 1);
                break;
            }
            case EntityType::Text:
                text(static_cast<const TextEntity&>(*e).displayText(), static_cast<const TextEntity&>(*e).origin(),
                     static_cast<const TextEntity&>(*e).height, static_cast<const TextEntity&>(*e).effectiveRotation(), t, c);
                break;
            case EntityType::MText: {
                const auto& m = static_cast<const MTextEntity&>(*e);
                const auto lines = m.lines();
                for (std::size_t i = 0; i < lines.size() && i < 4; ++i)
                    text(lines[i], m.position - Vec2::polar(m.lineHeight() * (static_cast<double>(i) + 1.0), m.rotation + kPi / 2.0),
                         m.height, m.rotation, t, c);
                break;
            }
            case EntityType::Hatch: {
                const auto& h = static_cast<const HatchEntity&>(*e);
                if (!h.solid)
                    break;   // i motivi a righe in 96 pixel sono solo rumore
                QPainterPath path;
                path.setFillRule(Qt::OddEvenFill);
                for (const auto& loop : h.tessellatedLoops()) {
                    QPolygonF poly;
                    for (Vec2 v : loop) {
                        const Vec2 w = t.apply(v);
                        poly << QPointF(w.x, w.y);
                    }
                    path.addPolygon(poly);
                }
                p_.fillPath(path, c);
                break;
            }
            case EntityType::Solid: {
                QPolygonF poly;
                for (Vec2 v : static_cast<const SolidEntity&>(*e).corners) {
                    const Vec2 w = t.apply(v);
                    poly << QPointF(w.x, w.y);
                }
                p_.setPen(Qt::NoPen);
                p_.setBrush(c);
                p_.drawPolygon(poly);
                p_.setBrush(Qt::NoBrush);
                break;
            }
            default: {
                if (e->type() == EntityType::Polyline) {
                    const auto& pl = static_cast<const PolylineEntity&>(*e);
                    if (pl.hasWidth()) {
                        p_.setPen(Qt::NoPen);
                        p_.setBrush(c);
                        for (const auto& poly : pl.widthPolygons()) {
                            QPolygonF q;
                            for (Vec2 v : poly) {
                                const Vec2 w = t.apply(v);
                                q << QPointF(w.x, w.y);
                            }
                            p_.drawPolygon(q);
                        }
                        p_.setBrush(Qt::NoBrush);
                    }
                }
                QPen pen(c);
                pen.setCosmetic(true);
                pen.setWidthF(1.2);
                p_.setPen(pen);
                std::vector<Prim> prims;
                e->decompose(prims);
                transformPrims(prims, t);
                QPainterPath path;
                for (const Prim& pr : prims) {
                    if (pr.kind == Prim::Kind::Segment) {
                        path.moveTo(pr.a.x, pr.a.y);
                        path.lineTo(pr.b.x, pr.b.y);
                    } else {
                        const int steps = std::max(6, static_cast<int>(pr.sweep / (kPi / 24.0)));
                        for (int k = 0; k <= steps; ++k) {
                            const Vec2 v = pr.pointAt(pr.start + pr.sweep * k / steps);
                            if (k == 0)
                                path.moveTo(v.x, v.y);
                            else
                                path.lineTo(v.x, v.y);
                        }
                    }
                }
                p_.drawPath(path);
                break;
            }
            }
        }
    }

private:
    QColor colorOf(const Entity& e, QColor blockColor) const
    {
        if (e.color() == kColorByBlock)
            return blockColor;
        const int aci = doc_.displayColor(e);
        if (aci <= 0 || aci == 7)
            return QColor(235, 238, 242);
        return theme::aciColor(aci);
    }

    void text(const std::string& s, Vec2 origin, double height, double rotation, const Affine& t, QColor c)
    {
        if (s.empty() || height <= 0.0)
            return;
        const Vec2 o = t.apply(origin);
        const Vec2 up = t.applyVector(Vec2::polar(height, rotation + kPi / 2.0));
        const double px = std::hypot(up.x, up.y);
        if (px < 2.5)
            return;   // troppo piccolo per leggerlo: niente macchie
        const Vec2 dir = t.applyVector(Vec2::polar(1.0, rotation));
        p_.save();
        p_.translate(o.x, o.y);
        p_.rotate(std::atan2(dir.y, dir.x) * 180.0 / kPi);
        p_.scale(1.0, -1.0);   // il testo si legge dritto anche con l'asse Y rovesciato
        QFont font(QStringLiteral("Arial"));
        font.setPixelSize(std::max(3, static_cast<int>(std::lround(px))));
        p_.setFont(font);
        p_.setPen(c);
        p_.drawText(QPointF(0.0, 0.0), QString::fromStdString(s));
        p_.restore();
    }

    const Document& doc_;
    QPainter& p_;
};

} // namespace

QImage renderBlockPreview(const Document& doc, const BlockDef& def, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0x1b, 0x20, 0x27));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    PreviewPainter painter(doc, p);
    BBox box;
    painter.collect(def, Affine{}, box);
    if (!box.isValid())
        return image;
    const double w = std::max(box.width(), 1e-9), h = std::max(box.height(), 1e-9);
    const double margin = 6.0;
    const double s = std::min((size - 2.0 * margin) / w, (size - 2.0 * margin) / h);
    // Y verso l'alto: si ribalta il disegno, centrato.
    p.translate(size / 2.0, size / 2.0);
    p.scale(1.0, -1.0);
    Affine t;
    t.m11 = s;
    t.m22 = s;
    t.dx = -box.center().x * s;
    t.dy = -box.center().y * s;
    painter.paint(def, t, QColor(235, 238, 242));
    return image;
}

BlockLibrary::BlockLibrary(QWidget* parent) : QQuickWidget(parent)
{
    pool_ = new QThreadPool(this);
    pool_->setMaxThreadCount(2);
    QSettings s = librarySettings();
    source_ = s.value(QStringLiteral("BlockLibrary/source")).toString();
    recent_ = s.value(QStringLiteral("BlockLibrary/recent")).toStringList();
    tab_ = std::clamp(s.value(QStringLiteral("BlockLibrary/tab"), 0).toInt(), 0, 2);
    setupQuickEngine(this);
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    rootContext()->setContextProperty(QStringLiteral("library"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/BlockLibrary.qml")));
}

BlockLibrary::~BlockLibrary()
{
    pool_->clear();
    pool_->waitForDone(3000);
}

void BlockLibrary::setDrawing(DrawingWidget* drawing)
{
    if (drawing == drawing_)
        return;
    drawing_ = drawing;
    signature_ = 0;
    refresh();
}

void BlockLibrary::refresh()
{
    // Impronta dei blocchi del disegno: definizioni ridefinite = puntatori nuovi.
    quint64 sig = drawing_ ? 1469598103934665603ull : 0;
    if (drawing_)
        for (const auto& b : drawing_->document().blocks())
            sig = (sig ^ reinterpret_cast<quintptr>(b.get()) ^ b->entities.size()) * 1099511628211ull;
    if (sig == signature_ && !items_.isEmpty() && tab_ != Recent)
        return;
    signature_ = sig;
    rebuild();
}

void BlockLibrary::setTab(int tab)
{
    tab = std::clamp(tab, 0, 2);
    if (tab == tab_)
        return;
    tab_ = tab;
    librarySettings().setValue(QStringLiteral("BlockLibrary/tab"), tab_);
    if (tab_ == Library && !source_.isEmpty() && QFileInfo(source_).isFile() && sourceDocPath_ != source_)
        loadSource(source_);
    rebuild();
    emit changed();
}

void BlockLibrary::setFilter(const QString& text)
{
    if (text == filter_)
        return;
    filter_ = text;
    rebuild();
    emit changed();
}

QString BlockLibrary::sourceText() const
{
    if (source_.isEmpty())
        return tr("Nessuna libreria: scegliere un file DWG/DXF con i simboli o una cartella.");
    return QDir::toNativeSeparators(source_);
}

void BlockLibrary::setLibrarySource(const QString& path)
{
    source_ = path;
    librarySettings().setValue(QStringLiteral("BlockLibrary/source"), source_);
    sourceDoc_.reset();
    sourceDocPath_.clear();
    if (QFileInfo(path).isFile())
        loadSource(path);
    rebuild();
    emit changed();
}

std::shared_ptr<const Document> BlockLibrary::loadedDocument(const QString& path) const
{
    return sourceDocPath_ == path ? sourceDoc_ : nullptr;
}

void BlockLibrary::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Cartella dei simboli (ogni disegno è un simbolo)"),
                                                          QFileInfo(source_).isDir() ? source_ : QFileInfo(source_).path());
    if (!dir.isEmpty()) {
        setLibrarySource(dir);
        setTab(Library);
    }
}

void BlockLibrary::chooseFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Disegno libreria (i suoi blocchi sono i simboli)"),
                                                      QFileInfo(source_).isDir() ? source_ : QFileInfo(source_).path(),
                                                      tr("Disegni (*.dwg *.dxf *.dwt)"));
    if (!file.isEmpty()) {
        setLibrarySource(file);
        setTab(Library);
    }
}

void BlockLibrary::loadSource(const QString& path, const QString& thenInsert)
{
    if (loading_)
        return;
    loading_ = true;
    status_ = tr("Lettura di %1...").arg(QFileInfo(path).fileName());
    emit changed();
    QPointer<BlockLibrary> self(this);
    QThread* thread = QThread::create([self, path, thenInsert] {
        auto doc = std::make_shared<Document>();
        QString error;
        const bool ok = DrawingWidget::readFile(path, *doc, &error);
        QMetaObject::invokeMethod(qApp, [self, path, thenInsert, doc, ok, error] {
            if (!self)
                return;
            self->loading_ = false;
            if (!ok) {
                self->status_ = tr("Impossibile leggere %1: %2").arg(QFileInfo(path).fileName(), error.left(200));
                emit self->changed();
                return;
            }
            self->sourceDoc_ = doc;
            self->sourceDocPath_ = path;
            self->status_.clear();
            self->rebuild();
            emit self->changed();
            if (!thenInsert.isEmpty())
                emit self->insertBlockRequested(path, thenInsert);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QString BlockLibrary::previewFor(const QString& source, const Document& doc, const BlockDef& def)
{
    // La chiave cambia con la definizione (ridefinire un blocco crea una definizione nuova).
    const QString key = source + QLatin1Char('|') + QString::fromStdString(def.name) + QLatin1Char('|')
                        + QString::number(reinterpret_cast<quintptr>(&def)) + QLatin1Char('|')
                        + QString::number(def.entities.size());
    if (const auto it = previews_.constFind(key); it != previews_.constEnd())
        return *it;
    QDir().mkpath(previewFolder());
    const QString name = QString::fromLatin1(
        QCryptographicHash::hash((source + QLatin1Char('|') + QString::fromStdString(def.name)).toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString file = previewFolder() + QLatin1Char('/') + name + QStringLiteral(".png");
    renderBlockPreview(doc, def, kPreviewSize * 2).save(file, "PNG");
    QUrl url = QUrl::fromLocalFile(file);
    url.setQuery(QStringLiteral("v=%1").arg(previews_.size() + 1));
    previews_.insert(key, url.toString());
    return url.toString();
}

void BlockLibrary::loadFileThumbnail(const QString& path)
{
    if (fileThumbs_.contains(path))
        return;
    fileThumbs_.insert(path, QString());
    QPointer<BlockLibrary> self(this);
    pool_->start([self, path] {
        const QString file = thumbnailFile(path);
        QMetaObject::invokeMethod(qApp, [self, path, file] {
            if (!self || file.isEmpty())
                return;
            QUrl url = QUrl::fromLocalFile(file);
            url.setQuery(QStringLiteral("v=%1").arg(QFileInfo(file).lastModified().toMSecsSinceEpoch()));
            self->fileThumbs_.insert(path, url.toString());
            if (self->tab_ != Current)
                self->rebuild();
        }, Qt::QueuedConnection);
    });
}

QVariantMap BlockLibrary::item(const QString& kind, const QString& name, const QString& source, const QString& image) const
{
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("name"), name},
            {QStringLiteral("source"), source},
            {QStringLiteral("image"), image},
            {QStringLiteral("tip"), source.isEmpty() ? name : name + QStringLiteral("\n") + QDir::toNativeSeparators(source)}};
}

void BlockLibrary::rebuild()
{
    items_.clear();
    // I blocchi tecnici (frecce delle quote _ArchTick, _Dot...) non sono simboli: si nascondono.
    const auto matches = [this](const QString& name) {
        if (name.startsWith(QLatin1Char('_')))
            return false;
        return filter_.trimmed().isEmpty() || name.contains(filter_.trimmed(), Qt::CaseInsensitive);
    };
    if (tab_ == Current && drawing_) {
        const Document& doc = drawing_->document();
        for (const std::string& n : userBlockNames(doc)) {
            const QString name = QString::fromStdString(n);
            if (!matches(name))
                continue;
            if (const auto def = doc.findBlock(n))
                items_ << item(QStringLiteral("block"), name, QString(), previewFor(QString(), doc, *def));
        }
        status_ = drawing_ ? (items_.isEmpty() && filter_.isEmpty() ? tr("Il disegno non ha blocchi.") : QString()) : QString();
    } else if (tab_ == Library && !source_.isEmpty()) {
        const QFileInfo info(source_);
        if (info.isDir()) {
            const QDir dir(source_);
            const auto files = dir.entryInfoList({QStringLiteral("*.dwg"), QStringLiteral("*.dxf")}, QDir::Files,
                                                 QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo& f : files) {
                if (!matches(f.completeBaseName()))
                    continue;
                loadFileThumbnail(f.absoluteFilePath());
                items_ << item(QStringLiteral("file"), f.completeBaseName(), f.absoluteFilePath(),
                               fileThumbs_.value(f.absoluteFilePath()));
            }
            if (!loading_)
                status_ = files.isEmpty() ? tr("Nessun disegno DWG/DXF nella cartella.") : QString();
        } else if (sourceDoc_ && sourceDocPath_ == source_) {
            for (const std::string& n : userBlockNames(*sourceDoc_)) {
                const QString name = QString::fromStdString(n);
                if (!matches(name))
                    continue;
                if (const auto def = sourceDoc_->findBlock(n))
                    items_ << item(QStringLiteral("block"), name, source_, previewFor(source_, *sourceDoc_, *def));
            }
            if (!loading_)
                status_ = items_.isEmpty() && filter_.isEmpty() ? tr("Il file non ha blocchi.") : QString();
        } else if (!info.exists()) {
            status_ = tr("Libreria non trovata: %1").arg(QDir::toNativeSeparators(source_));
        }
    } else if (tab_ == Recent) {
        for (const QString& r : std::as_const(recent_)) {
            const QString source = r.section(QLatin1Char('\t'), 0, 0);
            const QString name = r.section(QLatin1Char('\t'), 1);
            const QString shown = name.isEmpty() ? QFileInfo(source).completeBaseName() : name;
            if (!matches(shown))
                continue;
            QString image;
            if (name.isEmpty()) {
                loadFileThumbnail(source);
                image = fileThumbs_.value(source);
            } else {
                // L'anteprima salvata l'ultima volta (stesso nome di file della cache).
                const QString file = previewFolder() + QLatin1Char('/')
                                     + QString::fromLatin1(QCryptographicHash::hash((source + QLatin1Char('|') + name).toUtf8(),
                                                                                    QCryptographicHash::Sha1).toHex())
                                     + QStringLiteral(".png");
                if (QFileInfo::exists(file))
                    image = QUrl::fromLocalFile(file).toString();
            }
            items_ << item(name.isEmpty() ? QStringLiteral("file") : QStringLiteral("block"), shown, source, image);
            items_.last() = [&] {
                QVariantMap m = items_.last().toMap();
                m.insert(QStringLiteral("blockName"), name);
                return m;
            }();
        }
        status_ = recent_.isEmpty() ? tr("Nessun simbolo inserito di recente.") : QString();
    }
    emit itemsChanged();
    emit changed();
}

void BlockLibrary::activate(int index)
{
    if (index < 0 || index >= items_.size())
        return;
    const QVariantMap m = items_.at(index).toMap();
    const QString kind = m.value(QStringLiteral("kind")).toString();
    const QString source = m.value(QStringLiteral("source")).toString();
    const QString name = m.contains(QStringLiteral("blockName")) ? m.value(QStringLiteral("blockName")).toString()
                                                                 : m.value(QStringLiteral("name")).toString();
    if (kind == QLatin1String("file")) {
        emit insertFileRequested(source);
        return;
    }
    if (source.isEmpty() || loadedDocument(source)) {
        emit insertBlockRequested(source, name);
        return;
    }
    if (!QFileInfo::exists(source)) {
        emit message(tr("File non trovato: %1").arg(QDir::toNativeSeparators(source)));
        return;
    }
    loadSource(source, name);   // si inserisce appena letto
}

QString BlockLibrary::dragPayload(int index) const
{
    if (index < 0 || index >= items_.size())
        return {};
    const QVariantMap m = items_.at(index).toMap();
    const QString name = m.contains(QStringLiteral("blockName")) ? m.value(QStringLiteral("blockName")).toString()
                                                                 : m.value(QStringLiteral("name")).toString();
    return m.value(QStringLiteral("kind")).toString() + QLatin1Char('\t') + m.value(QStringLiteral("source")).toString()
           + QLatin1Char('\t') + name;
}

void BlockLibrary::popupMenu(int index, int x, int y)
{
    if (index < 0 || index >= items_.size())
        return;
    const QVariantMap m = items_.at(index).toMap();
    const QString kind = m.value(QStringLiteral("kind")).toString();
    const QString source = m.value(QStringLiteral("source")).toString();
    const QString name = m.value(QStringLiteral("name")).toString();
    QMenu menu(this);
    connect(menu.addAction(tr("Inserisci")), &QAction::triggered, this, [this, index] { activate(index); });
    if (tab_ == Current)
        connect(menu.addAction(tr("Modifica blocco (BEDIT)")), &QAction::triggered, this,
                [this, name] { emit editBlockRequested(name); });
    if (!source.isEmpty())
        connect(menu.addAction(kind == QLatin1String("file") ? tr("Apri il disegno") : tr("Apri il disegno libreria")),
                &QAction::triggered, this, [this, source] { emit openFileRequested(source); });
    if (tab_ == Recent) {
        menu.addSeparator();
        connect(menu.addAction(tr("Togli dai recenti")), &QAction::triggered, this, [this, index] {
            if (index < recent_.size()) {
                recent_.removeAt(index);
                librarySettings().setValue(QStringLiteral("BlockLibrary/recent"), recent_);
                rebuild();
            }
        });
    }
    menu.exec(QPoint(x, y));
}

void BlockLibrary::addRecent(const QString& source, const QString& name)
{
    const QString entry = source + QLatin1Char('\t') + name;
    recent_.removeAll(entry);
    recent_.prepend(entry);
    while (recent_.size() > kMaxRecent)
        recent_.removeLast();
    librarySettings().setValue(QStringLiteral("BlockLibrary/recent"), recent_);
    if (tab_ == Recent)
        rebuild();
}

} // namespace cartesio
