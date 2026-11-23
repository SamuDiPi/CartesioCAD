#include "app/CadView.h"
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>

#include "app/AppOptions.h"

#include "app/GpuView.h"
#include "app/CanvasItem.h"
#include "app/QmlUi.h"
#include "app/Icons.h"
#include "app/ImageLibrary.h"
#include "app/TextRender.h"
#include "app/Theme.h"
#include "core/Document.h"
#include "core/Geometry.h"
#include "core/commands/CommandManager.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QFile>
#include <QTimer>
#include <QEnterEvent>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cartesio {

namespace {

const QColor kPreviewColor(255, 255, 255, 200);
const QColor kTrackingColor(110, 220, 130);
// Fondo intorno al foglio dei layout (Opzioni > Visualizza); bianco se lo sfondo carta è spento.
QColor layoutBackground()
{
    const AppOptions& o = appOptions();
    return o.showPaperBackground ? o.layoutBackground : QColor(Qt::white);
}

constexpr double kPolarRadiusPx = 10.0;
constexpr double kMinScale = 1e-6;
constexpr double kMaxScale = 1e6;

QString snapName(SnapKind kind)
{
    switch (kind) {
    case SnapKind::Endpoint: return QStringLiteral("Punto finale");
    case SnapKind::Midpoint: return QStringLiteral("Punto medio");
    case SnapKind::Center: return QStringLiteral("Centro");
    case SnapKind::Quadrant: return QStringLiteral("Quadrante");
    case SnapKind::Node: return QStringLiteral("Nodo");
    case SnapKind::Insertion: return QStringLiteral("Inserimento");
    case SnapKind::Intersection: return QStringLiteral("Intersezione");
    case SnapKind::Perpendicular: return QStringLiteral("Perpendicolare");
    case SnapKind::Tangent: return QStringLiteral("Tangente");
    case SnapKind::Nearest: return QStringLiteral("Vicino");
    }
    return {};
}

QString number(double v, int decimals = 4)
{
    return QString::number(v, 'f', decimals);
}

} // namespace

CadView::CadView(const Document& doc, const CommandManager& commands, const std::vector<EntityId>& selection,
                 const SelectionPicker& idlePicker, const DraftSettings& settings, QWidget* parent)
    : QWidget(parent)
    , doc_(doc)
    , commands_(commands)
    , selection_(selection)
    , idlePicker_(idlePicker)
    , settings_(settings)
{
    setMouseTracking(true);
    setCursor(Qt::BlankCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    // Disegno su GPU (Qt Quick); CARTESIO_RENDER=software torna al disegno con QPainter.
    // Con l'interfaccia tutta in QML la vista si mostra in un CadCanvas: qui niente QQuickWidget proprio.
    setAcceptDrops(true);   // simboli dalla tavolozza Blocchi
    if (qgetenv("CARTESIO_RENDER").toLower() != "software" && !qmlRootUi())
        startGpuRendering();
}

void CadView::startGpuRendering()
{
    quick_ = new QQuickWidget(this);
    QSurfaceFormat format = quick_->format();
    format.setSamples(4);   // linee antialias
    quick_->setFormat(format);
    quick_->setAttribute(Qt::WA_TransparentForMouseEvents);
    quick_->setFocusPolicy(Qt::NoFocus);
    quick_->setClearColor(appOptions().modelBackground);
    // Il disegno in una texture (layer) e la selezione in un item sopra: un clic ridisegna solo la selezione
    // (su una pianta grande di prova da 80-140 ms a pochi ms). CARTESIO_NOLAYER=1 torna al disegno unico.
    const bool layered = !qEnvironmentVariableIsSet("CARTESIO_NOLAYER");
    item_ = new DrawingItem(*this, nullptr, layered ? DrawingItem::Part::Scene : DrawingItem::Part::Full);
    item_->setParent(quick_);
    item_->setParentItem(quick_->quickWindow()->contentItem());
    connect(item_, &DrawingItem::softwareFallback, this, &CadView::useSoftwareRendering);
    if (layered) {
        if (QObject* layer = item_->property("layer").value<QObject*>()) {
            layer->setProperty("enabled", true);
            layer->setProperty("samples", 4);
            layer->setProperty("smooth", true);   // pan e zoom veloci: la texture scalata sfuma invece di fare i quadretti
        }
        selectionItem_ = new DrawingItem(*this, nullptr, DrawingItem::Part::Selection);
        selectionItem_->setParent(quick_);
        selectionItem_->setParentItem(quick_->quickWindow()->contentItem());
        selectionItem_->setZ(0.5);
    }
    overlay_ = new OverlayWidget(*this);
    overlay_->raise();
    quick_->setGeometry(rect());
    overlay_->setGeometry(rect());
    item_->setViewSize(QSizeF(width(), height()), selectionItem_ ? sceneMargin() : 0.0);
    if (selectionItem_)
        selectionItem_->setSize(QSizeF(width(), height()));
}

// Qt Quick senza GPU utilizzabile: si torna al disegno con QPainter.
void CadView::useSoftwareRendering()
{
    if (!quick_)
        return;
    delete overlay_;
    overlay_ = nullptr;
    item_ = nullptr;
    selectionItem_ = nullptr;
    quick_->deleteLater();
    quick_ = nullptr;
    sceneCacheKey_ = 0;
    update();
}

void CadView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (quick_) {
        quick_->setGeometry(rect());
        overlay_->setGeometry(rect());
        item_->setViewSize(QSizeF(width(), height()), selectionItem_ ? sceneMargin() : 0.0);
        if (selectionItem_)
            selectionItem_->setSize(QSizeF(width(), height()));
    }
}

QImage CadView::grabImage()
{
    if (!item_)
        return grab().toImage();
    item_->refresh();
    if (selectionItem_)
        selectionItem_->refresh();
    QImage image = quick_->grabFramebuffer();
    image.setDevicePixelRatio(1.0);
    if (const QByteArray debug = qgetenv("CARTESIO_GRABDEBUG"); !debug.isEmpty()) {
        image.save(QString::fromLocal8Bit(debug) + QStringLiteral("-gpu.png"));
        overlay_->grab().save(QString::fromLocal8Bit(debug) + QStringLiteral("-overlay.png"));
    }
    QPainter p(&image);
    p.scale(static_cast<double>(image.width()) / std::max(1, width()), static_cast<double>(image.height()) / std::max(1, height()));
    p.drawPixmap(0, 0, overlay_->grab());
    return image;
}

// ------------------------------------------------------------ trasformazioni

QPointF CadView::toScreen(Vec2 w) const
{
    return worldTransform().map(QPointF(w.x, w.y));
}

Vec2 CadView::toWorld(QPointF s) const
{
    const QPointF w = worldTransform().inverted().map(s);
    return {w.x(), w.y()};
}

Vec2 CadView::toPaper(QPointF s) const
{
    const QPointF w = paperTransform().inverted().map(s);
    return {w.x(), w.y()};
}

// Pixel per unità del disegno su cui si lavora (il modello, se si lavora attraverso una finestra).
double CadView::effectiveScale() const
{
    const ViewportEntity* vp = activeViewportEntity();
    return vp ? scale_ * vp->scale() : scale_;
}

// Passo della griglia adattivo (1, 2, 5 x 10^n) in modo che le linee distino almeno ~20 pixel.
double CadView::gridSpacing() const
{
    const double target = 20.0 / scale_;
    const double power = std::pow(10.0, std::floor(std::log10(target)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (power * m >= target)
            return power * m;
    return power * 10.0;
}

void CadView::zoomExtents()
{
    rememberView();
    const BBox box = paperExtents();
    if (!box.isValid()) {
        center_ = {};
        scale_ = 4.0;
    } else {
        center_ = box.center();
        const double w = std::max(box.width(), 1e-9);
        const double h = std::max(box.height(), 1e-9);
        if (box.width() > 0.0 || box.height() > 0.0)
            scale_ = std::clamp(std::min(width() * 0.9 / w, height() * 0.85 / h), kMinScale, kMaxScale);
    }
    updateCursor(mouseScreen_);
    update();
}

void CadView::zoomBy(double factor)
{
    rememberView();
    scale_ = std::clamp(scale_ * factor, kMinScale, kMaxScale);
    updateCursor(mouseScreen_);
    update();
}

void CadView::setTypedText(const QString& text)
{
    typedText_ = text;
    update();
}

void CadView::refresh()
{
    updateCursor(mouseScreen_);
    if (mouseInside_)
        applyIdleCursor();   // es. un comando che inizia o finisce: mirino con o senza quadratino
    update();
}

// --------------------------------------------------------------- cursore e snap

std::optional<Vec2> CadView::selectionCorner() const
{
    if (const Command* command = commands_.active())
        return command->selectionCorner();
    return idlePicker_.corner();
}

void CadView::updateCursor(QPointF screenPos)
{
    mouseScreen_ = screenPos;
    rawCursor_ = toWorld(screenPos);
    snap_.reset();
    polar_.reset();
    hover_ = 0;

    const Command* command = commands_.active();
    const bool selecting = !command || command->wantsSelection();
    const std::optional<Vec2> base = command ? command->basePoint() : std::nullopt;

    // Anteprima di selezione: l'oggetto sotto il cursore viene evidenziato, con la stessa tolleranza del clic.
    // Con più oggetti sovrapposti si tiene l'elenco: Maiusc+Spazio passa al successivo (ciclo di selezione).
    if (selecting && !selectionCorner()) {
        std::vector<EntityId> ids;
        for (const Entity* e : doc_.pickAll(rawCursor_, pickTolerance()))
            ids.push_back(e->id());
        if (ids != hoverCandidates_) {
            hoverCandidates_ = std::move(ids);
            cycleIndex_ = 0;
        }
        if (!hoverCandidates_.empty())
            hover_ = hoverCandidates_[std::min(cycleIndex_, hoverCandidates_.size() - 1)];
        // L'icona solo se anche il secondo oggetto passa proprio sotto il mirino (1,5 px): non a ogni spigolo.
        overlapBadge_ = false;
        if (hoverCandidates_.size() > 1)
            if (const Entity* second = doc_.find(hoverCandidates_[1]))
                overlapBadge_ = second->distanceTo(rawCursor_) * effectiveScale() <= 1.5;
    } else {
        hoverCandidates_.clear();
        cycleIndex_ = 0;
        overlapBadge_ = false;
    }
    const bool measuring = command && command->quickMeasure();

    // Snap ad oggetto: solo mentre un comando chiede un punto.
    if (settings_.osnap && command && !selecting && !measuring) {
        const double radius = appOptions().snapAperture / effectiveScale();
        snap_ = findSnap(doc_.entitiesNear(rawCursor_, radius), rawCursor_, radius, settings_.osmode, base);
    }

    Vec2 p = rawCursor_;
    if (snap_) {
        p = snap_->position;
    } else {
        // Puntamento polare: aggancia il cursore alle direzioni multiple dell'angolo impostato.
        if (settings_.polar && !settings_.ortho && base && !selecting) {
            const Vec2 d = rawCursor_ - *base;
            if (d.length() * effectiveScale() > 1.0) {
                const double inc = degToRad(settings_.polarAngleDeg);
                const double angle = std::round(angleOf(d) / inc) * inc;
                const Vec2 dir = Vec2::polar(1.0, angle);
                const double along = d.dot(dir);
                if (along > 0.0 && std::abs(cross(dir, d)) * effectiveScale() <= kPolarRadiusPx) {
                    p = *base + dir * along;
                    polar_ = PolarHit{*base, angle, along};
                }
            }
        }
        if (!polar_) {
            if (settings_.snap) {
                const double g = gridSpacing();
                p = {std::round(p.x / g) * g, std::round(p.y / g) * g};
            }
            if (settings_.ortho && base && !selecting) {
                const Vec2 d = p - *base;
                if (std::abs(d.x) >= std::abs(d.y))
                    p.y = base->y;
                else
                    p.x = base->x;
            }
        }
    }

    // Puntamento snap a oggetto: senza snap, il cursore si allinea ai punti agganciati.
    track_.reset();
    if (settings_.otrack && command && !selecting && !measuring) {
        updateTracking(base, selecting);
        if (!snap_ && track_)
            p = track_->point;
    } else {
        tracked_.clear();
        dwellPoint_.reset();
    }

    cursor_ = p;
    updateHotGrip();
    emit cursorMoved(p);
}

void CadView::paintEntityWorld(QPainter& p, const Entity& e) const
{
    p.save();
    p.setTransform(worldTransform());
    Style style;
    paintEntity(p, e, style);
    p.restore();
}

double CadView::pickTolerance() const
{
    return appOptions().pickboxSize / effectiveScale();
}

void CadView::cycleHover()
{
    if (hoverCandidates_.size() < 2)
        return;
    cycleIndex_ = (cycleIndex_ + 1) % hoverCandidates_.size();
    hover_ = hoverCandidates_[cycleIndex_];
    update();
}

// Oggetti sovrapposti sotto il mirino: piccola icona (due quadrati) accanto; Maiusc+Spazio
// passa al successivo. Niente testo: non deve dare fastidio mentre si seleziona.
void CadView::drawCycleBadge(QPainter& p) const
{
    if (!overlapBadge_ || hoverCandidates_.size() < 2 || !mouseInside_ || panning_)
        return;
    const QPointF i = mouseScreen_ + QPointF(14.0, 10.0);
    p.setPen(QPen(QColor(150, 200, 255), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(i, QSizeF(8, 8)));
    p.setBrush(QColor(33, 40, 48));
    p.drawRect(QRectF(i + QPointF(4, 3), QSizeF(8, 8)));
    p.setBrush(Qt::NoBrush);
}

// ------------------------------------------------------ maniglie multifunzione

void CadView::updateHotGrip()
{
    // Con il menu aperto non cambia niente finché il cursore resta sulla maniglia o sul menu.
    if (gripMenuOpen()) {
        const QPointF g = toScreen(hotGrip_->position);
        const double t = appOptions().gripSize + 3.0;
        const QRectF bridge = gripMenuRect().united(QRectF(g.x() - t, g.y() - t, 2 * t, 2 * t));
        if (bridge.contains(mouseScreen_)) {
            gripMenuHover_ = gripMenuItemAt(mouseScreen_);
            return;
        }
    }
    std::optional<HotGrip> found;
    if (!commands_.isActive() && !idlePicker_.corner() && !selection_.empty()
        && selection_.size() <= kGripObjectLimit) {
        // Lo stesso quadratino disegnato e usato dal clic (DrawingWidget::startGripEdit).
        const double tol = (appOptions().gripSize + 1.0) / effectiveScale();
        for (EntityId id : selection_) {
            const Entity* e = doc_.find(id);
            if (!e || !doc_.isSelectable(*e))
                continue;
            const auto points = e->gripPoints();
            const std::size_t count = e->type() == EntityType::Insert ? std::min<std::size_t>(1, points.size()) : points.size();
            for (std::size_t i = 0; i < count && !found; ++i) {
                if (points[i].kind == SnapKind::Quadrant && e->type() == EntityType::Arc)
                    continue;
                if (std::abs(points[i].position.x - rawCursor_.x) <= tol && std::abs(points[i].position.y - rawCursor_.y) <= tol)
                    found = HotGrip{id, i, points[i].position};
            }
            if (found)
                break;
        }
    }
    const bool same = found && hotGrip_ && found->id == hotGrip_->id && found->index == hotGrip_->index;
    if (same)
        return;
    hotGrip_ = found;
    gripMenuOpen_ = false;
    gripMenuHover_ = -1;
    if (!gripMenuTimer_) {
        gripMenuTimer_ = new QTimer(this);
        gripMenuTimer_->setSingleShot(true);
        gripMenuTimer_->setInterval(350);
        connect(gripMenuTimer_, &QTimer::timeout, this, &CadView::openGripMenuNow);
    }
    if (hotGrip_ && !gripMenuActions().empty())
        gripMenuTimer_->start();
    else
        gripMenuTimer_->stop();
}

void CadView::openGripMenuNow()
{
    if (!hotGrip_ || gripMenuActions().empty())
        return;
    gripMenuOpen_ = true;
    gripMenuHover_ = gripMenuItemAt(mouseScreen_);
    update();
}

std::vector<int> CadView::gripMenuActions() const
{
    std::vector<int> result;
    if (!hotGrip_)
        return result;
    if (const Entity* e = doc_.find(hotGrip_->id))
        for (GripAction a : gripActions(*e, hotGrip_->index))
            result.push_back(static_cast<int>(a));
    return result;
}

namespace {

QString gripActionLabel(int action)
{
    switch (static_cast<GripAction>(action)) {
    case GripAction::Stretch: return QCoreApplication::translate("CadView", "Stira");
    case GripAction::AddVertex: return QCoreApplication::translate("CadView", "Aggiungi vertice");
    case GripAction::RemoveVertex: return QCoreApplication::translate("CadView", "Rimuovi vertice");
    case GripAction::ToArc: return QCoreApplication::translate("CadView", "Converti in arco");
    case GripAction::ToLine: return QCoreApplication::translate("CadView", "Converti in linea");
    case GripAction::Lengthen: return QCoreApplication::translate("CadView", "Allunga");
    }
    return {};
}

} // namespace

QRectF CadView::gripMenuItemRect(int index) const
{
    if (!hotGrip_)
        return {};
    const QFontMetricsF fm(font());
    double w = 0.0;
    for (int a : gripMenuActions())
        w = std::max(w, fm.horizontalAdvance(gripActionLabel(a)));
    const double h = fm.height() + 8.0;
    const QPointF g = toScreen(hotGrip_->position);
    const QPointF origin(g.x() + appOptions().gripSize + 8.0, g.y() - appOptions().gripSize);
    return QRectF(origin.x(), origin.y() + index * h, w + 24.0, h);
}

QRectF CadView::gripMenuRect() const
{
    const auto actions = gripMenuActions();
    if (actions.empty())
        return {};
    return gripMenuItemRect(0).united(gripMenuItemRect(static_cast<int>(actions.size()) - 1));
}

int CadView::gripMenuItemAt(QPointF pos) const
{
    const auto actions = gripMenuActions();
    for (int i = 0; i < static_cast<int>(actions.size()); ++i)
        if (gripMenuItemRect(i).contains(pos))
            return i;
    return -1;
}

void CadView::drawGripMenu(QPainter& p) const
{
    if (!gripMenuOpen())
        return;
    const auto actions = gripMenuActions();
    p.setPen(QPen(QColor(90, 100, 118), 1.0));
    p.setBrush(QColor(44, 50, 60, 245));
    p.drawRect(gripMenuRect());
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        const QRectF r = gripMenuItemRect(i);
        if (i == gripMenuHover_) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::kAccent);
            p.drawRect(r.adjusted(1, 1, -1, -1));
        }
        p.setPen(QColor(230, 233, 238));
        p.drawText(r.adjusted(12, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, gripActionLabel(actions[static_cast<std::size_t>(i)]));
    }
    p.setBrush(Qt::NoBrush);
}

// ------------------------------------------------------------- misura rapida

namespace {

// Intersezioni della retta orizzontale (vertical = false: y = c.y) o verticale (x = c.x) con una primitiva:
// la coordinata lungo la retta.
void lineHits(const Prim& pr, Vec2 c, bool vertical, std::vector<double>& out)
{
    const auto across = [vertical](Vec2 v) { return vertical ? v.x : v.y; };
    const auto along = [vertical](Vec2 v) { return vertical ? v.y : v.x; };
    const double k = across(c);
    if (pr.kind == Prim::Kind::Segment) {
        const double a = across(pr.a) - k, b = across(pr.b) - k;
        if ((a > 0) == (b > 0) || a == b)
            return;
        const double t = a / (a - b);
        out.push_back(along(pr.a) + t * (along(pr.b) - along(pr.a)));
        return;
    }
    const double d = k - across(pr.center);
    if (std::abs(d) > pr.radius)
        return;
    const double h = std::sqrt(pr.radius * pr.radius - d * d);
    for (double s : {-h, h}) {
        const Vec2 q = vertical ? Vec2{c.x, pr.center.y + s} : Vec2{pr.center.x + s, c.y};
        if (pr.containsAngle(angleOf(q - pr.center)))
            out.push_back(along(q));
    }
}

} // namespace

void CadView::drawQuickMeasure(QPainter& p) const
{
    if (!mouseInside_ || panning_)
        return;
    const Vec2 c = rawCursor_;
    const BBox view = visibleArea();
    if (!view.isValid())
        return;
    const double band = 1.0 / effectiveScale();
    const QColor color(255, 170, 40);
    const QFontMetricsF fm(font());
    for (bool vertical : {false, true}) {
        BBox strip;
        strip.expand(vertical ? Vec2{c.x - band, view.min.y} : Vec2{view.min.x, c.y - band});
        strip.expand(vertical ? Vec2{c.x + band, view.max.y} : Vec2{view.max.x, c.y + band});
        std::vector<double> hits;
        int checked = 0;
        for (const Entity* e : doc_.entitiesInRect(strip, 0.0)) {
            if (++checked > 4000)   // disegni enormi: basta questo per restare fluidi
                break;
            for (const Prim& pr : e->prims())
                lineHits(pr, c, vertical, hits);
        }
        const double here = vertical ? c.y : c.x;
        std::optional<double> before, after;
        for (double h : hits) {
            if (h < here - 1e-9 && (!before || h > *before))
                before = h;
            if (h > here + 1e-9 && (!after || h < *after))
                after = h;
        }
        if (!before && !after)
            continue;
        const double from = before ? *before : here, to = after ? *after : here;
        const auto at = [&](double v) { return toScreen(vertical ? Vec2{c.x, v} : Vec2{v, c.y}); };
        const QPointF a = at(from), b = at(to);
        p.setPen(QPen(color, 1.5));
        p.drawLine(a, b);
        for (const QPointF& t : {a, b}) {   // tacche agli estremi
            if (vertical)
                p.drawLine(t + QPointF(-5, 0), t + QPointF(5, 0));
            else
                p.drawLine(t + QPointF(0, -5), t + QPointF(0, 5));
        }
        const QString text = QString::number(to - from, 'f', 2);
        const QPointF mid = (a + b) / 2.0;
        const QRectF box(mid + (vertical ? QPointF(8, -fm.height() / 2.0 - 3) : QPointF(-fm.horizontalAdvance(text) / 2.0 - 6, -fm.height() - 12)),
                         QSizeF(fm.horizontalAdvance(text) + 12, fm.height() + 6));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 45, 20, 230));
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
        p.setPen(color);
        p.drawText(box, Qt::AlignCenter, text);
    }
}

// ------------------------------------------------------------------- disegno

// Dalle coordinate dello spazio mostrato (modello o foglio) allo schermo.
QTransform CadView::paperTransform() const
{
    return QTransform(scale_, 0.0, 0.0, -scale_, width() / 2.0 - center_.x * scale_,
                      height() / 2.0 + center_.y * scale_);
}

// Dalle coordinate in cui si lavora allo schermo: con una finestra attiva, il modello passa
// prima per la trasformazione della finestra.
QTransform CadView::worldTransform() const
{
    if (const ViewportEntity* vp = activeViewportEntity()) {
        const Affine t = vp->modelToPaper();
        return QTransform(t.m11, t.m12, t.m21, t.m22, t.dx, t.dy) * paperTransform();
    }
    return paperTransform();
}

// Chiave che cambia quando l'immagine del disegno va rigenerata.
std::uint64_t CadView::sceneKey(bool withView, bool withSelection) const
{
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    const auto bits = [](double d) {
        std::uint64_t u;
        std::memcpy(&u, &d, sizeof u);
        return u;
    };
    if (withView) {
        mix(bits(scale_));
        mix(bits(center_.x));
        mix(bits(center_.y));
    }
    mix(static_cast<std::uint64_t>(width()) << 32 | static_cast<std::uint64_t>(height()));
    mix(doc_.revision());
    mix(doc_.visibilityRevision() + 0x51u);   // oggetti isolati o nascosti
    mix(reinterpret_cast<std::uintptr_t>(&doc_));
    mix(settings_.grid ? 1 : 2);
    mix(settings_.lwDisplay ? 3 : 4);
    mix(static_cast<std::uint64_t>(shownSpace_));
    mix(static_cast<std::uint64_t>(doc_.activeSpace()));
    mix(activeViewport_);
    mix(compareRevision_ + 0x77u);   // confronto con un'altra revisione
    mix(static_cast<std::uint64_t>(appOptions().modelBackground.rgb()) << 4 ^ appOptions().paperColor.rgb());
    // Opzioni di visualizzazione dei layout (cambiano con la finestra Opzioni).
    const AppOptions& options = appOptions();
    mix((options.showPrintableArea ? 1u : 0u) | (options.showPaperBackground ? 2u : 0u)
        | (options.showPaperShadow ? 4u : 0u) | (static_cast<std::uint64_t>(options.layoutBackground.rgb()) << 8));
    if (!withSelection)
        return h;
    mix(selection_.size());
    for (EntityId id : selection_)
        mix(id);
    return h;
}

std::uint64_t CadView::contentKey() const
{
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    mix(doc_.revision());
    mix(doc_.visibilityRevision() + 0x51u);   // oggetti isolati o nascosti
    mix(reinterpret_cast<std::uintptr_t>(&doc_));
    mix(static_cast<std::uint64_t>(shownSpace_));
    mix(static_cast<std::uint64_t>(doc_.activeSpace()));
    mix(activeViewport_);   // gli oggetti dei layer bloccati sono attenuati dove si lavora
    mix(static_cast<std::uint64_t>(appOptions().textRendering) + 7);   // cambio del metodo dei testi: scena nuova
    return h;
}

std::uint64_t CadView::tableKey() const
{
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    mix(doc_.tableRevision());
    // Niente visibilityRevision: gli oggetti nascosti (isolamento, testo in modifica) escono dall'elenco degli
    // oggetti e si ricostruisce solo il loro blocco.
    mix(reinterpret_cast<std::uintptr_t>(&doc_));
    mix(static_cast<std::uint64_t>(shownSpace_));
    mix(static_cast<std::uint64_t>(doc_.activeSpace()));
    mix(activeViewport_);
    mix(static_cast<std::uint64_t>(appOptions().textRendering) + 7);
    mix(compareRevision_);
    return h;
}

void CadView::setCompare(std::optional<CompareView> compare)
{
    compare_ = std::move(compare);
    ++compareRevision_;
    refresh();
}

void CadView::setCompareShown(bool here, bool there)
{
    if (!compare_)
        return;
    compare_->showHere = here;
    compare_->showThere = there;
    ++compareRevision_;
    refresh();
}

void CadView::compareGo(int delta)
{
    if (!compare_ || compare_->regions.empty())
        return;
    const int n = static_cast<int>(compare_->regions.size());
    compare_->current = ((compare_->current < 0 ? (delta > 0 ? -1 : 0) : compare_->current) + delta + n) % n;
    BBox box = compare_->regions[static_cast<std::size_t>(compare_->current)];
    const double m = std::max(box.width(), box.height()) * 0.25;
    box.expand(box.min - Vec2{m, m});
    box.expand(box.max + Vec2{m, m});
    zoomWindow(box);
}

std::optional<QColor> CadView::compareColor(const Entity& e) const
{
    if (!compare_ || e.space() != 0 || shownSpace_ != 0)
        return std::nullopt;
    if (compare_->onlyHere.count(e.id()))
        return compare_->showHere ? QColor(60, 190, 240) : QColor(105, 110, 120);   // aggiunto: azzurro
    return QColor(105, 110, 120);   // uguale nei due disegni
}

std::uint64_t CadView::selectionKey() const
{
    std::uint64_t h = contentKey();
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    mix(selection_.size());
    for (EntityId id : selection_)
        mix(id);
    return h;
}

void CadView::paintEvent(QPaintEvent*)
{
    // Con la GPU il disegno lo fa DrawingItem e il livello sopra OverlayWidget: qui si avvisa solo la scena.
    if (item_) {
        syncGpuItems(item_, selectionItem_, gpu_);
        return;
    }
    QPainter p(this);
    paintSoftware(p);
}

// Disegno con QPainter (senza GPU): l'immagine della scena, rigenerata solo se serve, e il livello sopra.
void CadView::paintSoftware(QPainter& p)
{
    // Il disegno vero e proprio viene rigenerato solo se è cambiato qualcosa.
    const std::uint64_t key = sceneKey();
    const qreal dpr = devicePixelRatioF();
    const bool sameSize = sceneCache_.size() == size() * dpr;
    // Durante pan e zoom, se è cambiata solo la vista, l'immagine pronta viene spostata e scalata.
    // Se il ridisegno è veloce (meno di 20 ms) si ridisegna davvero anche durante pan e zoom: niente bordi
    // vuoti né immagine sgranata. Con i disegni pesanti si sposta e si scala l'immagine pronta.
    if (interacting_ && lastRenderMs_ > 20.0 && key != sceneCacheKey_ && sameSize && !sceneCache_.isNull()
        && sceneKey(false) == cacheContentKey_ && cacheScale_ > 0.0) {
        const double k = scale_ / cacheScale_;
        const double w = width(), h = height();
        const QTransform t(k, 0, 0, k, w / 2.0 + (cacheCenter_.x - center_.x) * scale_ - w / 2.0 * k,
                           h / 2.0 - (cacheCenter_.y - center_.y) * scale_ - h / 2.0 * k);
        p.fillRect(rect(), shownSpace_ != 0 ? layoutBackground() : appOptions().modelBackground);
        p.save();
        p.setTransform(t);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawPixmap(0, 0, sceneCache_);
        p.restore();
        drawOverlays(p);
        return;
    }
    if (key != sceneCacheKey_ || !sameSize) {
        sceneCache_ = QPixmap(size() * dpr);
        sceneCache_.setDevicePixelRatio(dpr);
        QPainter cache(&sceneCache_);
        QElapsedTimer timer;
        timer.start();
        renderScene(cache);
        sceneCacheKey_ = key;
        cacheContentKey_ = sceneKey(false);
        cacheCenter_ = center_;
        cacheScale_ = scale_;
        lastRenderMs_ = timer.nsecsElapsed() / 1e6;
        // Diagnosi delle prestazioni: CARTESIO_PROFILE=file registra il tempo di ogni ridisegno.
        static const QByteArray profile = qgetenv("CARTESIO_PROFILE");
        if (!profile.isEmpty()) {
            QFile log(QString::fromLocal8Bit(profile));
            if (log.open(QIODevice::Append | QIODevice::Text))
                log.write(QByteArray("ridisegno ") + QByteArray::number(timer.nsecsElapsed() / 1e6, 'f', 1) + " ms\n");
        }
    }
    p.drawPixmap(0, 0, sceneCache_);
    drawOverlays(p);
}

// Elementi dinamici sopra il disegno: evidenziazione, anteprima, mirino, pulsanti di navigazione...
void CadView::drawOverlays(QPainter& p)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    if (tiled_ && shownSpace_ == 0) {   // finestre del modello affiancate: bordo, azzurro e spesso sulla vista attiva
        p.save();
        p.setPen(QPen(tileActive_ ? QColor(61, 139, 253) : QColor(90, 98, 112), tileActive_ ? 3.0 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, width() - 3.0, height() - 3.0));
        p.restore();
    }
    if (compare_ && shownSpace_ == 0) {
        p.save();
        p.setTransform(worldTransform());
        if (compare_->showThere) {
            Style removed;
            removed.override = QColor(225, 85, 205);   // tolto: magenta
            for (const auto& e : compare_->onlyThere)
                paintEntity(p, *e, removed);
        }
        p.restore();
        // Zone delle differenze: riquadri arancioni, quella scelta più marcata.
        for (std::size_t i = 0; i < compare_->regions.size(); ++i) {
            const BBox& b = compare_->regions[i];
            const QRectF r = QRectF(toScreen(b.min), toScreen(b.max)).normalized();
            const bool chosen = static_cast<int>(i) == compare_->current;
            p.setPen(QPen(QColor(255, 160, 40, chosen ? 255 : 170), chosen ? 2.5 : 1.2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        }
    }
    if (hover_ != 0) {
        if (const Entity* e = doc_.find(hover_)) {
            p.save();
            p.setTransform(worldTransform());
            Style style;
            const int aci = doc_.displayColor(*e);
            style.override = shownSpace_ != 0 && aci == 7 ? QColor(60, 60, 60) : theme::aciColor(aci).lighter(115);
            style.width = 3.0;
            paintEntity(p, *e, style);
            p.restore();
        }
    }

    // Bordo evidenziato della finestra di layout attiva.
    if (const ViewportEntity* vp = activeViewportEntity()) {
        QPolygonF outline;
        for (Vec2 v : vp->outline())
            outline << paperTransform().map(QPointF(v.x, v.y));
        p.setPen(QPen(theme::kAccent, 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(outline);
    }

    if (textEditor_)
        textEditor_->paintInView(p);   // il testo che si sta modificando, con selezione e cursore

    if (const Command* command = commands_.active()) {
        p.save();
        p.setTransform(worldTransform());
        Style style;
        style.override = shownSpace_ != 0 ? QColor(0, 0, 0, 200) : kPreviewColor;
        for (const auto& e : command->preview(cursor_))
            paintEntity(p, *e, style);
        p.restore();
    }

    if (const Command* command = commands_.active(); command && command->quickMeasure())
        drawQuickMeasure(p);
    drawSelectionWindow(p);
    drawPolarTracking(p);
    drawObjectTracking(p);
    if (appOptions().showUcsIcon)
        drawUcsIcon(p);
    if (appOptions().showNavigationBar)
        drawNavigationBar(p);
    drawSnapMarker(p);
    drawCommandHistory(p);

    if (mouseInside_ && !panning_ && !panMode_ && !overNavigation(mouseScreen_) && !overGripMenu(mouseScreen_)) {
        drawCrosshair(p);
        drawDynamicInput(p);
        drawCycleBadge(p);
    }
    drawGripMenu(p);
}

void CadView::renderScene(QPainter& p) const
{
    const bool paper = shownSpace_ != 0;
    paperMode_ = paper;
    lineScale_ = 1.0;
    p.fillRect(rect(), paper ? layoutBackground() : appOptions().modelBackground);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    if (paper)
        drawPaperSheet(p);
    else if (settings_.grid)
        drawGrid(p);

    // Solo le entità che cadono nella vista (in coordinate dello spazio mostrato).
    const Vec2 a = toPaper({0.0, 0.0});
    const Vec2 b = toPaper({static_cast<double>(width()), static_cast<double>(height())});
    BBox view;
    view.expand(a);
    view.expand(b);

    p.save();
    p.setTransform(paperTransform());
    if (paper)
        renderViewports(p, shownSpace_);
    // Con una finestra attiva si lavora sul modello: gli oggetti del foglio non sono selezionabili.
    renderEntities(p, shownSpace_, view, activeViewport_ == 0);
    p.restore();

    if (selection_.size() <= kGripObjectLimit)   // GRIPOBJLIMIT: oltre, niente maniglie
        for (EntityId id : selection_)
            if (const Entity* e = doc_.find(id))
                drawGrips(p, *e);
}

QImage CadView::renderThumbnail(QSize size) const
{
    QImage image(size, QImage::Format_RGB32);
    const bool paper = shownSpace_ != 0;
    image.fill(paper ? layoutBackground() : appOptions().modelBackground);
    if (width() < 2 || height() < 2)
        return image;
    paperMode_ = paper;
    lineScale_ = 1.0;
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    // La vista riempie la miniatura (il centro resta al centro, i bordi in più si tagliano).
    const double s = std::max(static_cast<double>(size.width()) / width(), static_cast<double>(size.height()) / height());
    p.translate((size.width() - width() * s) / 2.0, (size.height() - height() * s) / 2.0);
    p.scale(s, s);
    if (paper)
        drawPaperSheet(p);
    BBox view;
    view.expand(toPaper({0.0, 0.0}));
    view.expand(toPaper({static_cast<double>(width()), static_cast<double>(height())}));
    p.setTransform(paperTransform(), true);
    if (paper)
        renderViewports(p, shownSpace_);
    renderEntities(p, shownSpace_, view, false);
    return image;
}

// Contenuto delle finestre di un layout: lo spazio modello in scala, ritagliato sul riquadro.
void CadView::renderViewports(QPainter& p, int space) const
{
    for (const Entity* e : doc_.entitiesNear({0.0, 0.0}, 1e12, space)) {
        if (e->type() != EntityType::Viewport)
            continue;
        const auto& vp = static_cast<const ViewportEntity&>(*e);
        p.save();
        QPolygonF outline;
        for (Vec2 v : vp.outline())
            outline << QPointF(v.x, v.y);
        QPainterPath clip;
        clip.addPolygon(outline);
        clip.closeSubpath();
        p.setClipPath(clip, Qt::IntersectClip);
        // Tipi di linea in unità della carta (PSLTSCALE): scala carta -> dispositivo.
        ltPaperScale_ = std::sqrt(std::abs(p.transform().determinant()));
        const Affine t = vp.modelToPaper();
        p.setTransform(QTransform(t.m11, t.m12, t.m21, t.m22, t.dx, t.dy), true);
        renderEntities(p, 0, vp.modelRect(), vp.id() == activeViewport_, &vp.frozenLayers,
                       vp.scale() > 0.0 ? 1.0 / vp.scale() : 1.0);
        ltPaperScale_ = 0.0;
        p.restore();
    }
}

void CadView::renderEntities(QPainter& p, int space, const BBox& view, bool interactive,
                             const std::vector<int>* frozen, double annoFactor) const
{
    const auto& layers = doc_.layers();
    // Ciò che sullo schermo è più piccolo di circa 0,7 pixel non si disegna (in stampa: di 0,7 punti).
    const double minSize = 0.7 / std::max(std::sqrt(std::abs(p.transform().determinant())), 1e-12);
    for (const Entity* e : doc_.entitiesInRect(view, minSize, space)) {
        if (frozen && std::find(frozen->begin(), frozen->end(), e->layer()) != frozen->end())
            continue;   // congelato in questa finestra
        if (printDotsPerMm_ > 0.0 && e->layer() >= 0 && e->layer() < static_cast<int>(layers.size())
            && !layers[static_cast<std::size_t>(e->layer())].plot)
            continue;   // layer non stampabile
        Style style;
        style.layer = e->layer();
        style.aci = doc_.displayColor(*e);
        style.view = view;
        style.pixelsPerUnit = 0.7 / minSize;
        const int li = e->layer();
        if (interactive && li >= 0 && li < static_cast<int>(layers.size()) && layers[static_cast<std::size_t>(li)].locked) {
            const QColor c = aciColor(style.aci);
            style.override = QColor::fromRgbF(c.redF() * 0.5f + 0.1f, c.greenF() * 0.5f + 0.1f, c.blueF() * 0.5f + 0.1f);
        }
        if (const auto c = compareColor(*e))
            style.override = c;
        if (interactive && std::find(selection_.begin(), selection_.end(), e->id()) != selection_.end()) {
            style.override = QColor(110, 170, 255);
            style.penStyle = Qt::DashLine;
            style.width = 1.5;
        }
        // Annotativi del modello nelle finestre dei layout: alla scala della finestra, se la supportano; altrimenti
        // come sono.
        if (e->annotative() && e->space() == 0 && annoFactor > 0.0) {
            const double f = annoFactor;
            if (!sameFactor(e->annoFactor(), f) && doc_.supportsFactor(*e, f)) {
                paintEntity(p, *annotationAt(*e, f), style);
                continue;
            }
        }
        paintEntity(p, *e, style);
    }
}

// Foglio bianco con ombra e area stampabile tratteggiata.
void CadView::drawPaperSheet(QPainter& p) const
{
    const Layout* layout = doc_.findLayout(shownSpace_);
    if (!layout)
        return;
    const Vec2 o = layout->origin;
    const AppOptions& options = appOptions();
    const QRectF sheet = paperTransform().mapRect(QRectF(o.x, o.y, layout->paperWidth, layout->paperHeight));
    if (options.showPaperBackground) {
        if (options.showPaperShadow)
            p.fillRect(sheet.translated(5, 5), QColor(0, 0, 0, 90));
        p.fillRect(sheet, appOptions().paperColor);
    }
    if (!options.showPrintableArea)
        return;
    const double m = layout->margin;
    const QRectF printable = paperTransform().mapRect(
        QRectF(o.x + m, o.y + m, layout->paperWidth - 2 * m, layout->paperHeight - 2 * m));
    QPen dash(QColor(150, 150, 150), 1.0);
    dash.setStyle(Qt::DashLine);
    p.setPen(dash);
    p.setBrush(Qt::NoBrush);
    p.drawRect(printable);
}

QColor CadView::aciColor(int aci) const
{
    // Il colore 7 è nero sugli sfondi chiari (foglio, o modello chiaro dalle Opzioni) e in stampa.
    if (aci == 7 && (lightBackground() || print_ || printDotsPerMm_ > 0.0))
        return Qt::black;
    return theme::aciColor(aci);
}

void CadView::renderForPrint(QPainter& p, const QTransform& world, const BBox& area, int space,
                             const PrintOptions& options) const
{
    const bool savedPaper = paperMode_;
    const double savedScale = lineScale_;
    paperMode_ = true;
    lineScale_ = 1.0;
    printDotsPerMm_ = options.dotsPerMm;
    print_ = &options;
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.save();
    p.setTransform(world);
    if (space != 0 && options.paperSpaceLast)
        renderViewports(p, space);
    renderEntities(p, space, area, false);
    if (space != 0 && !options.paperSpaceLast)
        renderViewports(p, space);
    p.restore();
    paperMode_ = savedPaper;
    lineScale_ = savedScale;
    printDotsPerMm_ = 0.0;
    print_ = nullptr;
}

BBox CadView::visibleArea() const
{
    BBox box;
    box.expand(toPaper({0.0, 0.0}));
    box.expand(toPaper({static_cast<double>(width()), static_cast<double>(height())}));
    return box;
}


namespace {
bool sameText(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}
} // namespace

// Tipo di linea effettivo (nullptr = continua). Gli oggetti sul layer "0" dentro un blocco
// prendono il layer dell'inserimento, come per il colore.
const Linetype* CadView::linetypeOf(const Entity& e, const Style& style) const
{
    std::string name = e.linetype();
    if (name.empty() || sameText(name, "ByLayer")) {
        const int layer = e.layer() <= 0 ? style.layer : e.layer();
        const auto& layers = doc_.layers();
        name = layer >= 0 && layer < static_cast<int>(layers.size()) ? layers[static_cast<std::size_t>(layer)].linetype
                                                                     : std::string();
    } else if (sameText(name, "ByBlock")) {
        name = style.blockLinetype;
    }
    if (name.empty())
        return nullptr;
    const Linetype* lt = doc_.findLinetype(name);
    return lt && !lt->pattern.empty() ? lt : nullptr;
}

int CadView::lineweightOf(const Entity& e, const Style& style) const
{
    int lw = e.lineweight();
    if (lw == kLineweightByLayer || lw == kLineweightUnset) {
        const int layer = e.layer() <= 0 ? style.layer : e.layer();
        const auto& layers = doc_.layers();
        lw = layer >= 0 && layer < static_cast<int>(layers.size()) ? layers[static_cast<std::size_t>(layer)].lineweight
                                                                   : kLineweightDefault;
    } else if (lw == kLineweightByBlock) {
        lw = style.blockLineweight;
    }
    return lw < 0 ? kDefaultLineweight : lw;
}

QPen CadView::entityPen(QPainter& p, const Entity& e, const Style& style, const QColor& color) const
{
    // Spessore: reale in stampa; a schermo solo con LWDISPLAY (1 px = circa 0,25 mm).
    double width = style.width * lineScale_;
    const int lw = lineweightOf(e, style);
    if (printDotsPerMm_ > 0.0) {
        // Senza spessori: tutte le linee sottili. La tabella stili può imporre lo spessore.
        double mm = print_ && !print_->lineweights ? 0.18 : lw / 100.0;
        if (print_ && print_->styles && !style.override) {
            const double styleMm = print_->styles->lineweightMm(print_->styles->forAci(effectiveAci(e, style)));
            if (styleMm > 0.0)
                mm = styleMm;
        }
        if (print_)
            mm *= print_->lineweightScale;
        width = std::max(1.0, mm * printDotsPerMm_);
    } else if (settings_.lwDisplay && style.width <= 1.0)
        width = std::max(1.0, std::round(lw / 100.0 * 96.0 / 25.4));

    QPen pen(color, width);
    pen.setCosmetic(true);
    pen.setStyle(style.penStyle);
    if (print_ && print_->styles && !style.override) {   // estremità e giunzioni della tabella stili
        const PlotStyle& ps = print_->styles->forAci(effectiveAci(e, style));
        if (ps.endStyle >= 0 && ps.endStyle <= 2)
            pen.setCapStyle(ps.endStyle == 0 ? Qt::FlatCap : ps.endStyle == 1 ? Qt::SquareCap : Qt::RoundCap);
        if (ps.joinStyle >= 0 && ps.joinStyle <= 2)
            pen.setJoinStyle(ps.joinStyle == 0 ? Qt::MiterJoin : ps.joinStyle == 1 ? Qt::BevelJoin : Qt::RoundJoin);
    }
    if (style.penStyle != Qt::SolidLine)
        return pen;   // evidenziazione (selezione): tratteggio dell'interfaccia

    const Linetype* lt = linetypeOf(e, style);
    if (!lt)
        return pen;
    // Lunghezze del motivo in pixel del dispositivo. Nelle finestre dei layout il motivo è in unità
    // della carta (PSLTSCALE = 1), altrimenti segue la scala di disegno (anche dei blocchi).
    const double device = ltPaperScale_ > 0.0 ? ltPaperScale_ : std::sqrt(std::abs(p.transform().determinant()));
    const double k = doc_.ltscale() * e.linetypeScale() * device;
    if (lt->length() * k < 4.0 * width)
        return pen;   // motivo troppo fitto: si vede come una linea continua

    // Qt vuole tratto/spazio alternati, in multipli dello spessore della penna.
    QList<qreal> dashes;
    bool wantDash = true;
    for (double d : lt->pattern) {
        const bool isDash = d >= 0.0;
        double len = std::abs(d) * k / width;
        if (isDash && len < 0.5)
            len = 0.5;   // punto
        if (isDash != wantDash) {
            if (dashes.isEmpty()) {
                dashes << 0.0;   // il motivo inizia con uno spazio
                wantDash = false;
            }
            else {
                dashes.back() += len;   // due elementi dello stesso tipo di seguito
                continue;
            }
        }
        dashes << len;
        wantDash = !wantDash;
    }
    if (dashes.size() % 2 == 1)
        dashes << 0.0001;
    if (dashes.size() >= 2) {
        pen.setDashPattern(dashes);
        pen.setCapStyle(Qt::FlatCap);
    }
    return pen;
}
QColor CadView::visibleOnScreen(QColor c) const
{
    if (print_ || printDotsPerMm_ > 0.0)
        return c;
    const QColor bg = screenBackground();
    const int dr = c.red() - bg.red(), dg = c.green() - bg.green(), db = c.blue() - bg.blue();
    if (dr * dr + dg * dg + db * db < 40 * 40) {
        const int alpha = c.alpha();
        c = lightBackground() ? QColor(Qt::black) : theme::aciColor(7);
        c.setAlpha(alpha);
    }
    return c;
}

QColor CadView::screenBackground() const
{
    return paperMode_ ? appOptions().paperColor : appOptions().modelBackground;
}

bool CadView::lightBackground() const
{
    return screenBackground().lightnessF() > 0.55;
}

QColor CadView::resolveColor(const Entity& e, const Style& style) const
{
    return style.override ? *style.override : visibleOnScreen(resolveColorRaw(e, style));
}

QColor CadView::resolveColorRaw(const Entity& e, const Style& style) const
{
    if (style.override)
        return *style.override;
    // Trasparenza dell'oggetto (440): 0x02000000 | alfa.
    const auto withAlpha = [&e](QColor c) {
        if (e.transparency() >= 0 && (e.transparency() & 0x02000000))
            c.setAlpha(e.transparency() & 0xFF);
        return c;
    };
    // Colore RGB dell'oggetto.
    if (e.trueColor() >= 0)
        return withAlpha(QColor::fromRgb(static_cast<QRgb>(e.trueColor())));
    const int c = e.color();
    if (c >= 1 && c <= 255)
        return withAlpha(aciColor(c));
    if (c == kColorByBlock)
        return withAlpha(aciColor(style.aci));
    // DaLayer: gli oggetti sul layer "0" dentro un blocco prendono il layer dell'inserimento.
    const int layer = e.layer() <= 0 ? style.layer : e.layer();
    const auto& layers = doc_.layers();
    if (layer >= 0 && layer < static_cast<int>(layers.size())) {
        const Layer& l = layers[static_cast<std::size_t>(layer)];
        return withAlpha(l.trueColor >= 0 ? QColor::fromRgb(static_cast<QRgb>(l.trueColor)) : aciColor(l.color));
    }
    return withAlpha(aciColor(7));
}

int CadView::effectiveAci(const Entity& e, const Style& style) const
{
    const int c = e.color();
    if (c >= 1 && c <= 255)
        return c;
    if (c == kColorByBlock)
        return style.aci;
    const int layer = e.layer() <= 0 ? style.layer : e.layer();
    const auto& layers = doc_.layers();
    if (layer >= 0 && layer < static_cast<int>(layers.size()))
        return std::abs(layers[static_cast<std::size_t>(layer)].color);
    return 7;
}

QColor CadView::plotColor(const Entity& e, const Style& style) const
{
    QColor color = resolveColor(e, style);
    if (!print_ || style.override)
        return color;
    if (!print_->transparency)
        color.setAlpha(255);
    if (!print_->styles)
        return color;
    const PlotStyle& ps = print_->styles->forAci(effectiveAci(e, style));
    if (!ps.hasObjectColor()) {
        const int alpha = color.alpha();
        color = QColor::fromRgb(static_cast<QRgb>(ps.rgb()));
        color.setAlpha(alpha);
    }
    if (ps.grayscale()) {   // luminanza
        const int g = qRound(0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue());
        color.setRgb(g, g, g, color.alpha());
    }
    if (ps.screen < 100) {   // retinatura: verso il bianco della carta
        const double k = std::clamp(ps.screen, 0, 100) / 100.0;
        const auto toWhite = [k](int v) { return qRound(255.0 - (255.0 - v) * k); };
        color.setRgb(toWhite(color.red()), toWhite(color.green()), toWhite(color.blue()), color.alpha());
    }
    return color;
}

namespace {

// Percorso di Qt dalle primitive, in coordinate mondo (Y verso l'alto).
QPainterPath pathOf(const std::vector<Prim>& prims)
{
    QPainterPath path;
    for (const Prim& pr : prims) {
        if (pr.kind == Prim::Kind::Segment) {
            const QPointF a(pr.a.x, pr.a.y);
            if (path.elementCount() == 0 || path.currentPosition() != a)
                path.moveTo(a);
            path.lineTo(pr.b.x, pr.b.y);
        } else {
            const QRectF rect(pr.center.x - pr.radius, pr.center.y - pr.radius, 2 * pr.radius, 2 * pr.radius);
            // QPainterPath misura gli angoli con Y verso il basso: in un mondo con Y in su vanno negati.
            const double startDeg = -radToDeg(pr.start);
            const QPointF startPoint(pr.pointAt(pr.start).x, pr.pointAt(pr.start).y);
            if (path.elementCount() == 0 || QLineF(path.currentPosition(), startPoint).length() > 1e-9 * (1 + pr.radius))
                path.arcMoveTo(rect, startDeg);
            path.arcTo(rect, startDeg, -radToDeg(pr.sweep));
        }
    }
    return path;
}

QPainterPath loopsPath(const HatchEntity& h)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const auto& loop : h.tessellatedLoops()) {
        QPolygonF poly;
        for (Vec2 v : loop)
            poly << QPointF(v.x, v.y);
        poly << poly.front();
        path.addPolygon(poly);
    }
    return path;
}


// Linee vere del motivo, ritagliate sul contorno: ogni famiglia di linee parallele
// con i suoi tratti, spazi e punti. Se il motivo è troppo fitto per lo zoom si riempie a tinta tenue.
void paintHatchPattern(QPainter& p, const HatchEntity& h, const QPainterPath& area, const QColor& color)
{
    std::vector<HatchPatternLine> lines = h.patternLines;
    if (lines.empty())
        lines = standardHatchPattern(h.pattern, h.patternScale, h.patternAngle);
    if (lines.empty())
        lines = standardHatchPattern("ANSI31", h.patternScale, h.patternAngle);

    p.save();
    p.setClipPath(area, Qt::IntersectClip);
    const QRectF box = area.boundingRect().intersected(p.clipBoundingRect());
    if (box.isEmpty()) {
        p.restore();
        return;
    }
    const double device = std::sqrt(std::abs(p.transform().determinant()));   // pixel per unità
    const Vec2 corners[] = {{box.left(), box.top()}, {box.right(), box.top()}, {box.left(), box.bottom()},
                            {box.right(), box.bottom()}};
    const auto dense = [&] {
        QColor c = color;
        c.setAlphaF(c.alphaF() * 0.35);
        p.fillPath(area, c);
        p.restore();
    };

    QPainterPath path;
    std::size_t segments = 0;
    constexpr std::size_t kMaxSegments = 200000;
    for (const HatchPatternLine& line : lines) {
        const Vec2 u = Vec2::polar(1.0, line.angle);
        const Vec2 n{-u.y, u.x};
        const double spacing = line.offset.dot(n);
        if (std::abs(spacing) < 1e-12)
            continue;
        if (std::abs(spacing) * device < 1.2)
            return dense();
        double dmin = 1e300, dmax = -1e300, tmin = 1e300, tmax = -1e300;
        for (Vec2 c : corners) {
            dmin = std::min(dmin, c.dot(n));
            dmax = std::max(dmax, c.dot(n));
            tmin = std::min(tmin, c.dot(u));
            tmax = std::max(tmax, c.dot(u));
        }
        const double d0 = line.base.dot(n);
        double k0 = (dmin - d0) / spacing;
        double k1 = (dmax - d0) / spacing;
        if (k0 > k1)
            std::swap(k0, k1);
        const auto kFirst = static_cast<long long>(std::floor(k0));
        const auto kLast = static_cast<long long>(std::ceil(k1));
        if (kLast - kFirst > 20000)
            return dense();

        double period = 0.0;
        for (double d : line.dashes)
            period += std::abs(d);
        const bool solidLine = line.dashes.empty() || period * device < 3.0;
        const double dot = 0.6 / std::max(device, 1e-12);   // un punto: circa mezzo pixel

        for (long long k = kFirst; k <= kLast; ++k) {
            const Vec2 origin = line.base + line.offset * static_cast<double>(k);
            const double s0 = origin.dot(u);
            const auto at = [&](double t) {
                const Vec2 q = origin + u * (t - s0);
                return QPointF(q.x, q.y);
            };
            if (solidLine) {
                path.moveTo(at(tmin));
                path.lineTo(at(tmax));
                ++segments;
            } else {
                double t = s0 + std::floor((tmin - s0) / period) * period;
                while (t < tmax && segments < kMaxSegments) {
                    for (double d : line.dashes) {
                        const double len = std::abs(d);
                        if (d >= 0.0) {
                            const double a = std::max(t, tmin);
                            const double b = std::min(t + (d > 0.0 ? len : dot), tmax);
                            if (b > a) {
                                path.moveTo(at(a));
                                path.lineTo(at(b));
                                ++segments;
                            }
                        }
                        t += len;
                        if (t >= tmax)
                            break;
                    }
                }
            }
            if (segments >= kMaxSegments)
                return dense();
        }
    }
    p.drawPath(path);
    p.restore();
}
QTransform toQt(const Affine& a)
{
    return QTransform(a.m11, a.m12, a.m21, a.m22, a.dx, a.dy);
}

} // namespace

void CadView::paintEntity(QPainter& p, const Entity& e, const Style& style, int depth) const
{
    if (depth > 16)
        return;
    const QColor color = plotColor(e, style);
    const QPen pen = entityPen(p, e, style, color);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Polilinea con spessore: aree piene, e i tratti senza spessore come linee normali. Evidenziata o
    // selezionata resta la linea di mezzeria (come i tratteggi).
    if (e.type() == EntityType::Polyline && !style.override && style.penStyle != Qt::DashLine && style.width <= 1.0) {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        if (pl.hasWidth()) {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            for (const auto& poly : pl.widthPolygons()) {
                QPolygonF q;
                for (Vec2 v : poly)
                    q << QPointF(v.x, v.y);
                p.drawPolygon(q);
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(pen);
            for (const auto& part : pl.thinParts())
                paintEntity(p, *part, style, depth + 1);
            return;
        }
    }

    switch (e.type()) {
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        p.drawEllipse(QPointF(c.center.x, c.center.y), c.radius, c.radius);
        break;
    }
    case EntityType::Point: {
        const auto& pt = static_cast<const PointEntity&>(e);
        QPen dot = pen;
        dot.setWidthF(4.0 * lineScale_);
        dot.setCapStyle(Qt::RoundCap);
        p.setPen(dot);
        p.drawPoint(QPointF(pt.position.x, pt.position.y));
        break;
    }
    case EntityType::Solid: {
        const auto& s = static_cast<const SolidEntity&>(e);
        QPolygonF poly;
        for (Vec2 c : s.corners)
            poly << QPointF(c.x, c.y);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(poly);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case EntityType::Hatch: {
        const auto& h = static_cast<const HatchEntity&>(e);
        const QPainterPath path = loopsPath(h);
        if (style.penStyle == Qt::DashLine || style.width > 1.0) {
            p.drawPath(path);   // selezionato o evidenziato: solo il contorno
            break;
        }
        if (h.solid || !h.gradient.empty()) {
            p.fillPath(path, color);   // pieno (le sfumature si vedono con il primo colore)
            break;
        }
        QPen hatchPen = pen;
        hatchPen.setStyle(Qt::SolidLine);
        p.setPen(hatchPen);
        paintHatchPattern(p, h, path, color);
        break;
    }
    case EntityType::Text:
        p.setPen(color);
        paintText(p, static_cast<const TextEntity&>(e));
        break;
    case EntityType::AttDef:
        if (depth == 0) {   // fuori dai blocchi si vede l'etichetta
            p.setPen(color);
            paintText(p, static_cast<const AttDefEntity&>(e).tagText());
        }
        break;
    case EntityType::MText: {
        const auto& m = static_cast<const MTextEntity&>(e);
        if (m.backgroundFill) {   // maschera: riquadro del colore dello sfondo (bianco in stampa)
            const auto b = m.box();
            const double k = (m.fillScale - 1.0) * m.height;
            const Vec2 dir = Vec2::polar(1.0, m.rotation), up = Vec2::polar(1.0, m.rotation + kPi / 2.0);
            const Vec2 corners[] = {b[0] - dir * k - up * k, b[1] + dir * k - up * k, b[2] + dir * k + up * k, b[3] - dir * k + up * k};
            QPolygonF poly;
            for (Vec2 c : corners)
                poly << QPointF(c.x, c.y);
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(printDotsPerMm_ > 0.0 ? QColor(Qt::white) : screenBackground());
            p.drawPolygon(poly);
            p.restore();
        }
        p.setPen(color);
        paintMText(p, m, !style.override);
        break;
    }
    case EntityType::Insert: {
        const auto& in = static_cast<const InsertEntity&>(e);
        if (!in.block)
            break;
        Style child = style;
        child.layer = e.layer();
        child.aci = style.aci;
        // Tipo e spessore di linea per gli oggetti DaBlocco: quelli effettivi dell'inserimento.
        if (const Linetype* lt = linetypeOf(e, style))
            child.blockLinetype = lt->name;
        else
            child.blockLinetype.clear();
        child.blockLineweight = lineweightOf(e, style);
        // Colore da trasmettere agli oggetti DaBlocco: quello effettivo dell'inserimento.
        if (e.color() >= 1 && e.color() <= 255)
            child.aci = e.color();
        else if (e.color() == kColorByLayer || e.color() < 0) {
            const auto& layers = doc_.layers();
            const int l = e.layer() <= 0 ? style.layer : e.layer();
            if (l >= 0 && l < static_cast<int>(layers.size()))
                child.aci = layers[static_cast<std::size_t>(l)].color;
        }
        const Affine a = in.affine();
        // Zona visibile nelle coordinate del blocco: si disegna solo ciò che si vede.
        child.view = BBox{};
        child.pixelsPerUnit = 0.0;
        const double det = a.m11 * a.m22 - a.m12 * a.m21;
        if (style.view.isValid() && style.pixelsPerUnit > 0.0 && std::abs(det) > 1e-300) {
            const auto toBlock = [&a, det](Vec2 w) {
                const double x = w.x - a.dx, y = w.y - a.dy;
                return Vec2{(a.m22 * x - a.m21 * y) / det, (-a.m12 * x + a.m11 * y) / det};
            };
            const BBox& v = style.view;
            for (Vec2 c : {v.min, v.max, Vec2{v.min.x, v.max.y}, Vec2{v.max.x, v.min.y}})
                child.view.expand(toBlock(c));
            child.pixelsPerUnit = style.pixelsPerUnit * std::sqrt(std::abs(det));
        }
        const bool cull = child.view.isValid();
        const std::vector<BBox>* bounds = cull ? &in.block->childBounds() : nullptr;
        p.save();
        p.setTransform(toQt(a), true);
        if (in.clipped()) {   // XCLIP: si vede solo dentro il contorno (nelle coordinate del blocco)
            QPolygonF frame;
            for (Vec2 v : in.clipPolygon())
                frame << QPointF(v.x, v.y);
            QPainterPath path;
            path.addPolygon(frame);
            p.setClipPath(path, Qt::IntersectClip);
        }
        for (std::size_t i = 0; i < in.block->entities.size(); ++i) {
            const Entity* c = in.block->entities[i].get();
            // Le definizioni di attributo non si vedono negli inserimenti: si vedono i valori.
            if (c->type() == EntityType::AttDef)
                continue;
            if (c->layer() > 0 && !doc_.isLayerVisible(c->layer()))
                continue;
            if (cull) {
                const BBox& b = (*bounds)[i];
                if (b.isValid()) {
                    const BBox& v = child.view;
                    if (b.max.x < v.min.x || b.min.x > v.max.x || b.max.y < v.min.y || b.min.y > v.max.y)
                        continue;   // fuori vista
                    const double size = std::max(b.width(), b.height()) * child.pixelsPerUnit;
                    if (size > 0.0 && size < 0.7)
                        continue;   // più piccolo di un pixel
                }
            }
            paintEntity(p, *c, child, depth + 1);
        }
        p.restore();
        for (const Attribute& a : in.attributes) {
            if ((a.flags & AttInvisible) != 0 || (a.text.layer() > 0 && !doc_.isLayerVisible(a.text.layer())))
                continue;
            p.setPen(plotColor(a.text, child));
            paintText(p, a.text);
        }
        break;
    }
    case EntityType::Dimension: {
        Style child = style;
        child.layer = e.layer();
        child.aci = doc_.displayColor(e);
        for (const auto& part : static_cast<const DimensionEntity&>(e).geometry())
            paintEntity(p, *part, child, depth + 1);
        break;
    }
    case EntityType::Image: {
        // Immagine raster dal suo file; se manca (o l'oggetto è evidenziato) la cornice, con le diagonali se manca.
        const auto& im = static_cast<const ImageEntity&>(e);
        const QImage image = im.show ? ImageLibrary::instance().image(ImageLibrary::instance().source(doc_, im)) : QImage();
        if (image.isNull() || style.override) {
            p.drawPath(pathOf(e.prims()));
            if (image.isNull() && !style.override) {
                const auto c = im.corners();
                p.drawLine(QPointF(c[0].x, c[0].y), QPointF(c[2].x, c[2].y));
                p.drawLine(QPointF(c[1].x, c[1].y), QPointF(c[3].x, c[3].y));
            }
            break;
        }
        p.save();
        p.setTransform(toQt(im.pixelToWorld()), true);   // ora le coordinate sono i pixel dell'immagine
        if (im.clipping && im.clip.size() >= 2) {
            QPolygonF clip;
            if (im.clip.size() == 2)
                clip = QPolygonF(QRectF(QPointF(im.clip[0].x + 0.5, im.clip[0].y + 0.5),
                                        QPointF(im.clip[1].x + 0.5, im.clip[1].y + 0.5)).normalized());
            else
                for (Vec2 v : im.clip)
                    clip << QPointF(v.x + 0.5, v.y + 0.5);
            QPainterPath path;
            path.addPolygon(clip);
            p.setClipPath(path, Qt::IntersectClip);
        }
        p.setOpacity(p.opacity() * (1.0 - std::clamp(im.fade, 0, 100) / 100.0));
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QRectF(0.0, 0.0, im.width, im.height), image);
        p.restore();
        break;
    }
    case EntityType::Wipeout: {
        // Area piena del colore dello sfondo: nasconde gli oggetti disegnati prima.
        const auto& w = static_cast<const WipeoutEntity&>(e);
        QPolygonF poly;
        for (Vec2 v : w.outline)
            poly << QPointF(v.x, v.y);
        p.setPen(Qt::NoPen);
        p.setBrush(printDotsPerMm_ > 0.0 || print_ ? QColor(Qt::white) : screenBackground());
        p.drawPolygon(poly);
        p.setBrush(Qt::NoBrush);
        const int frame = doc_.wipeoutFrame;
        if (frame == 1 || (frame == 2 && printDotsPerMm_ <= 0.0) || style.override) {
            p.setPen(pen);
            p.drawPolygon(poly);
        }
        break;
    }
    case EntityType::Table: {
        Style child = style;
        child.layer = e.layer();
        child.aci = doc_.displayColor(e);
        for (const auto& part : static_cast<const TableEntity&>(e).geometry())
            paintEntity(p, *part, child, depth + 1);
        break;
    }
    case EntityType::XLine: {
        // Solo il tratto visibile: dal punto base fino oltre i bordi della vista.
        const auto& x = static_cast<const XLineEntity&>(e);
        const QRectF view = p.transform().inverted().mapRect(QRectF(0, 0, p.device()->width(), p.device()->height()));
        const double reach = distance(x.base, {view.center().x(), view.center().y()}) + std::hypot(view.width(), view.height());
        const Vec2 a = x.ray ? x.base : x.base - x.direction * reach;
        const Vec2 b = x.base + x.direction * reach;
        p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
        break;
    }
    case EntityType::Leader: {
        Style child = style;
        child.layer = e.layer();
        child.aci = doc_.displayColor(e);
        for (const auto& part : static_cast<const LeaderEntity&>(e).geometry())
            paintEntity(p, *part, child, depth + 1);
        break;
    }
    default: {
        p.drawPath(pathOf(e.prims()));
        break;
    }
    }
}

void CadView::paintText(QPainter& p, const TextEntity& t) const
{
    const double pixelHeight = t.height * std::sqrt(std::abs(p.transform().determinant()));
    if (pixelHeight < 0.25)
        return;   // meno di un quarto di pixel: niente
    // A schermo, un testo alto meno di 1 pixel diventa un trattino (come la GPU); in stampa mai.
    paintPlaced(p, layoutText(doc_, t), pixelHeight < 1.0 && printDotsPerMm_ <= 0.0);
}

void CadView::paintMText(QPainter& p, const MTextEntity& m, bool ownColors) const
{
    const double pixelHeight = m.height * std::sqrt(std::abs(p.transform().determinant()));
    if (pixelHeight < 0.25)
        return;
    paintPlaced(p, layoutMText(doc_, m), pixelHeight < 1.0 && printDotsPerMm_ <= 0.0, ownColors);
}

// Righe impaginate (TextRender): tratti per i font SHX, testo per i TrueType. Il colore è quello della penna.
void CadView::paintPlaced(QPainter& p, const std::vector<PlacedText>& lines, bool dashOnly, bool ownColors) const
{
    const QPen saved = p.pen();
    QPen stroke(saved.color(), printDotsPerMm_ > 0.0 ? std::max(1.0, 0.18 * printDotsPerMm_) : 1.0);
    stroke.setCosmetic(true);
    stroke.setCapStyle(Qt::RoundCap);
    stroke.setJoinStyle(Qt::RoundJoin);
    for (const PlacedText& line : lines) {
        // Colore del pezzo (\C, \c dei testi multilinea), se il testo non è evidenziato.
        QColor color = saved.color();
        if (ownColors && line.rgb >= 0)
            color = visibleOnScreen(QColor::fromRgb(static_cast<QRgb>(line.rgb)));
        else if (ownColors && line.aci >= 1 && line.aci <= 255)
            color = aciColor(line.aci);
        stroke.setColor(color);
        if (dashOnly) {
            const auto [a, b] = line.dash();
            p.setPen(stroke);
            p.drawLine(a, b);
        } else if (line.shx) {
            p.setPen(stroke);
            for (const auto& s : line.strokes())
                p.drawPolyline(s.data(), static_cast<int>(s.size()));
            for (const auto& [a, b] : line.decorations)
                p.drawLine(a, b);
        } else {
            if (!line.decorations.empty()) {
                p.setPen(stroke);
                for (const auto& [a, b] : line.decorations)
                    p.drawLine(a, b);
            }
            p.save();
            p.setTransform(line.fontToEntity(), true);
            p.setFont(line.font);
            p.setPen(color);
            p.drawText(QPointF(0.0, 0.0), line.text);
            p.restore();
        }
    }
    p.setPen(saved);
}

void CadView::drawGrid(QPainter& p) const
{
    const Vec2 topLeft = toWorld({0.0, 0.0});
    const Vec2 bottomRight = toWorld({static_cast<double>(width()), static_cast<double>(height())});
    const double g = gridSpacing();

    const auto x0 = static_cast<long long>(std::ceil(topLeft.x / g));
    const auto x1 = static_cast<long long>(std::floor(bottomRight.x / g));
    const auto y0 = static_cast<long long>(std::ceil(bottomRight.y / g));
    const auto y1 = static_cast<long long>(std::floor(topLeft.y / g));

    QColor minorColor = appOptions().gridColor, majorColor = appOptions().gridColor;
    minorColor.setAlpha(lightBackground() ? 30 : 12);
    majorColor.setAlpha(lightBackground() ? 60 : 30);
    const QPen minor(minorColor, 1.0);
    const QPen major(majorColor, 1.0);
    p.setRenderHint(QPainter::Antialiasing, false);

    for (long long i = x0; i <= x1; ++i) {
        p.setPen(i % 5 == 0 ? major : minor);
        const double x = toScreen({static_cast<double>(i) * g, 0.0}).x();
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    for (long long j = y0; j <= y1; ++j) {
        p.setPen(j % 5 == 0 ? major : minor);
        const double y = toScreen({0.0, static_cast<double>(j) * g}).y();
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    // Assi X (rosso) e Y (verde) passanti per l'origine.
    const QPointF origin = toScreen({0.0, 0.0});
    p.setPen(QPen(QColor(200, 60, 60, 100), 1.0));
    p.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
    p.setPen(QPen(QColor(60, 200, 60, 100), 1.0));
    p.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));

    p.setRenderHint(QPainter::Antialiasing, true);
}

void CadView::drawGrips(QPainter& p, const Entity& e) const
{
    p.setPen(QPen(QColor(10, 40, 90), 1.0));
    p.setBrush(appOptions().gripColor);
    const double g = appOptions().gripSize;
    const auto points = e.gripPoints();
    // I blocchi mostrano solo il punto di inserimento.
    const std::size_t count = e.type() == EntityType::Insert ? std::min<std::size_t>(1, points.size()) : points.size();
    for (std::size_t i = 0; i < count; ++i) {
        const SnapPoint& sp = points[i];
        if (sp.kind == SnapKind::Quadrant && e.type() == EntityType::Arc)
            continue;
        const QPointF s = toScreen(sp.position);
        const bool hot = hotGrip_ && hotGrip_->id == e.id() && hotGrip_->index == i;
        p.setBrush(hot ? QColor(255, 70, 70) : appOptions().gripColor);   // maniglia sotto il cursore: rossa
        if (e.type() == EntityType::MText && i > 0) {
            // Frecce della larghezza del testo multilinea: triangoli che puntano verso l'esterno.
            const auto box = static_cast<const MTextEntity&>(e).box();
            QPointF out = toScreen(i == 1 ? box[1] : box[0]) - toScreen(i == 1 ? box[0] : box[1]);
            const double len = std::hypot(out.x(), out.y());
            out = len > 0.0 ? out / len : QPointF(1.0, 0.0);
            const QPointF side(-out.y(), out.x());
            const double r = g * 1.4;
            p.drawPolygon(QPolygonF{s + out * r, s - out * r * 0.6 + side * r, s - out * r * 0.6 - side * r});
            continue;
        }
        p.drawRect(QRectF(s.x() - g, s.y() - g, 2 * g, 2 * g));
    }
    p.setBrush(Qt::NoBrush);
}


// Finestra (sinistra -> destra): azzurra, bordo continuo.
// Intersezione (destra -> sinistra): verde, bordo tratteggiato.
void CadView::drawSelectionWindow(QPainter& p) const
{
    const auto corner = selectionCorner();
    if (!corner)
        return;
    if (lassoDragging_ && lassoPath_.size() >= 2) {
        // Lazo: in senso orario finestra (azzurra), antiorario intersezione (verde tratteggiata).
        QPolygonF poly;
        double area = 0.0;
        std::vector<Vec2> pts = lassoPath_;
        pts.push_back(rawCursor_);
        for (std::size_t i = 0; i < pts.size(); ++i) {
            poly << toScreen(pts[i]);
            const Vec2 a = pts[i], b = pts[(i + 1) % pts.size()];
            area += a.x * b.y - b.x * a.y;
        }
        const bool crossingLasso = area > 0.0;
        QPen pen(crossingLasso ? QColor(90, 220, 120) : QColor(90, 150, 255), 1.0);
        if (crossingLasso)
            pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(crossingLasso ? QColor(90, 220, 120, 40) : QColor(90, 150, 255, 40));
        p.drawPolygon(poly);
        p.setBrush(Qt::NoBrush);
        return;
    }
    const bool crossing = rawCursor_.x < corner->x;
    const QRectF rect = QRectF(toScreen(*corner), toScreen(rawCursor_)).normalized();

    QPen pen(crossing ? QColor(90, 220, 120) : QColor(90, 150, 255), 1.0);
    if (crossing)
        pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.setBrush(crossing ? QColor(90, 220, 120, 40) : QColor(90, 150, 255, 40));
    p.drawRect(rect);
    p.setBrush(Qt::NoBrush);
}

void CadView::drawPolarTracking(QPainter& p) const
{
    if (!polar_ || !mouseInside_)
        return;
    const Vec2 dir = Vec2::polar(1.0, polar_->angle);
    const double reach = polar_->distance + 3.0 * std::max(width(), height()) / effectiveScale();
    QPen pen(kTrackingColor, 1.0);
    pen.setDashPattern({4, 4});
    p.setPen(pen);
    p.drawLine(toScreen(polar_->base), toScreen(polar_->base + dir * reach));

    const double deg = normalizeAngle(polar_->angle) * 180.0 / kPi;
    drawTooltip(p, toScreen(cursor_) + QPointF(18, 26),
                QStringLiteral("Polare: %1 < %2°").arg(number(polar_->distance), number(deg, 0)));
}

void CadView::drawSnapMarker(QPainter& p) const
{
    if (!snap_ || !appOptions().snapMarker)
        return;
    const QPointF s = toScreen(snap_->position);
    const double r = appOptions().snapMarkerSize;
    p.setPen(QPen(appOptions().snapMarkerColor, 2.0));
    p.setBrush(Qt::NoBrush);

    switch (snap_->kind) {
    case SnapKind::Endpoint:
        p.drawRect(QRectF(s.x() - r, s.y() - r, 2 * r, 2 * r));
        break;
    case SnapKind::Midpoint: {
        const QPointF triangle[] = {QPointF(s.x(), s.y() - r), QPointF(s.x() + r, s.y() + r),
                                    QPointF(s.x() - r, s.y() + r)};
        p.drawPolygon(triangle, 3);
        break;
    }
    case SnapKind::Center:
        p.drawEllipse(s, r, r);
        break;
    case SnapKind::Node:
        p.drawEllipse(s, r, r);
        p.drawLine(s + QPointF(-r, -r), s + QPointF(r, r));
        p.drawLine(s + QPointF(-r, r), s + QPointF(r, -r));
        break;
    case SnapKind::Insertion:
        p.drawRect(QRectF(s.x() - r, s.y() - r, r, r));
        p.drawRect(QRectF(s.x(), s.y(), r, r));
        break;
    case SnapKind::Quadrant: {
        const QPointF diamond[] = {QPointF(s.x(), s.y() - r), QPointF(s.x() + r, s.y()),
                                   QPointF(s.x(), s.y() + r), QPointF(s.x() - r, s.y())};
        p.drawPolygon(diamond, 4);
        break;
    }
    case SnapKind::Intersection:
        p.drawLine(s + QPointF(-r, -r), s + QPointF(r, r));
        p.drawLine(s + QPointF(-r, r), s + QPointF(r, -r));
        break;
    case SnapKind::Perpendicular:
        p.drawLine(s + QPointF(-r, -r), s + QPointF(-r, r));
        p.drawLine(s + QPointF(-r, r), s + QPointF(r, r));
        p.drawLine(s + QPointF(-r, 0), s + QPointF(0, 0));
        p.drawLine(s + QPointF(0, 0), s + QPointF(0, r));
        break;
    case SnapKind::Tangent:
        p.drawEllipse(s, r, r);
        p.drawLine(s + QPointF(-r, -r), s + QPointF(r, -r));
        break;
    case SnapKind::Nearest: {
        const QPointF hourglass[] = {QPointF(s.x() - r, s.y() - r), QPointF(s.x() + r, s.y() - r),
                                     QPointF(s.x() - r, s.y() + r), QPointF(s.x() + r, s.y() + r)};
        p.drawPolygon(hourglass, 4);
        break;
    }
    }
    drawTooltip(p, s + QPointF(14, 14), snapName(snap_->kind));
}

bool CadView::systemCrosshair() const
{
    return appOptions().systemCursor && !settings_.snap && !qEnvironmentVariableIsSet("CARTESIO_SOFTCURSOR");
}

// Il mirino (con il quadratino di selezione o il mirino dello snap) disegnato in un cursore di Windows.
QCursor CadView::idleCursor() const
{
    if (!systemCrosshair())
        return QCursor(Qt::BlankCursor);
    const int arm = static_cast<int>(std::max(24.0, 0.03 * std::max(width(), height())));
    const Command* command = commands_.active();
    const bool pickbox = !command || command->wantsSelection();
    const bool aperture = !pickbox && appOptions().showAperture && settings_.osnap;
    const int box = pickbox ? appOptions().pickboxSize : aperture ? appOptions().snapAperture : 0;
    const bool paper = shownSpace_ != 0 && appOptions().showPaperBackground;
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(arm).arg(box).arg(paper).arg(dpr);
    if (key == cursorKey_)
        return cursorCache_;
    const int size = 2 * arm + 1;
    QPixmap pm(QSize(qRound(size * dpr), qRound(size * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(lightBackground() ? QColor(0, 0, 0) : QColor(255, 255, 255));
        pen.setWidthF(1.0 / dpr);   // una linea di un pixel vero anche al 125%
        pen.setCosmetic(true);
        p.setPen(pen);
        const qreal c = arm + 0.5;
        p.drawLine(QPointF(0.0, c), QPointF(size, c));
        p.drawLine(QPointF(c, 0.0), QPointF(c, size));
        if (box > 0)
            p.drawRect(QRectF(c - box, c - box, 2.0 * box, 2.0 * box));
    }
    cursorKey_ = key;
    cursorCache_ = QCursor(pm, arm, arm);
    return cursorCache_;
}

// Cursore della vista quando il mouse è sul disegno (non sulla navigazione, non in PAN).
void CadView::applyIdleCursor()
{
    if (panning_ || panMode_ || overNavigation(mouseScreen_)) {
        appliedCursor_.clear();
        return;
    }
    const QCursor c = idleCursor();
    const QString key = systemCrosshair() ? cursorKey_ : QStringLiteral("blank");
    if (key == appliedCursor_)
        return;   // stesso cursore: niente chiamate al sistema a ogni movimento
    appliedCursor_ = key;
    setCursor(c);
}

void CadView::drawCrosshair(QPainter& p) const
{
    if (systemCrosshair())
        return;   // lo disegna Windows (cursore)
    // Con snap alla griglia o puntamento polare il mirino segue il punto agganciato.
    const QPointF c = ((settings_.snap || polar_) && !snap_) ? toScreen(cursor_) : mouseScreen_;
    const double arm = std::max(24.0, 0.03 * std::max(width(), height()));

    p.setRenderHint(QPainter::Antialiasing, false);
    // Sul foglio bianco dei layout il mirino è nero.
    p.setPen(QPen(shownSpace_ != 0 && appOptions().showPaperBackground ? QColor(0, 0, 0) : QColor(255, 255, 255), 1.0));
    p.drawLine(QPointF(c.x() - arm, c.y()), QPointF(c.x() + arm, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - arm), QPointF(c.x(), c.y() + arm));

    const Command* command = commands_.active();
    const double box = appOptions().pickboxSize;
    if (!command || command->wantsSelection())
        p.drawRect(QRectF(c.x() - box, c.y() - box, 2 * box, 2 * box));
    else if (appOptions().showAperture && settings_.osnap) {   // mirino dello snap
        const double a = appOptions().snapAperture;
        p.drawRect(QRectF(c.x() - a, c.y() - a, 2 * a, 2 * a));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
}

void CadView::drawUcsIcon(QPainter& p) const
{
    const QPointF o(36.0, height() - 36.0);
    constexpr double len = 44.0;

    p.setPen(QPen(QColor(230, 90, 90), 2.0));
    p.drawLine(o, o + QPointF(len, 0));
    p.setPen(QPen(QColor(90, 210, 90), 2.0));
    p.drawLine(o, o - QPointF(0, len));

    p.setPen(QPen(QColor(200, 205, 215), 1.2));
    p.drawRect(QRectF(o.x() - 4, o.y() - 4, 8, 8));

    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(230, 90, 90));
    p.drawText(o + QPointF(len + 5, 5), QStringLiteral("X"));
    p.setPen(QColor(90, 210, 90));
    p.drawText(o + QPointF(-4, -len - 6), QStringLiteral("Y"));
    p.setFont(font());
}

// Pulsanti di navigazione in alto a destra: Pan, Zoom estensioni, Zoom avanti, Zoom indietro.
QRectF CadView::navButtonRect(int index) const
{
    return QRectF(width() - 44, 30 + index * 28, 26, 26);
}

void CadView::drawNavigationBar(QPainter& p) const
{
    const QRectF first = navButtonRect(0);
    const QRectF bar(first.left() - 2, first.top() - 3, first.width() + 4, 4 * 28 + 4);
    p.setPen(QPen(QColor(80, 90, 105), 1.0));
    p.setBrush(QColor(44, 51, 62, 220));
    p.drawRoundedRect(bar, 4, 4);
    p.setBrush(Qt::NoBrush);

    const Icon icons[] = {Icon::Pan, Icon::ZoomExtents, Icon::ZoomIn, Icon::ZoomOut};
    for (int i = 0; i < 4; ++i) {
        const QRectF r = navButtonRect(i);
        if ((mouseInside_ && r.contains(mouseScreen_)) || (i == 0 && panMode_)) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(74, 85, 103));
            p.drawRoundedRect(r, 3, 3);
            p.setBrush(Qt::NoBrush);
        }
        icon(icons[i]).paint(&p, r.adjusted(4, 4, -4, -4).toRect());
    }
}

bool CadView::overNavigation(QPointF pos) const
{
    if (!appOptions().showNavigationBar)
        return false;
    const QRectF nav = navButtonRect(0).united(navButtonRect(3)).adjusted(-3, -3, 3, 3);
    return nav.contains(pos);
}

void CadView::drawTooltip(QPainter& p, QPointF anchor, const QString& text) const
{
    const QFontMetricsF fm(font());
    const QRectF box(anchor, QSizeF(fm.horizontalAdvance(text) + 12, fm.height() + 6));
    p.setPen(QPen(QColor(90, 100, 118), 1.0));
    p.setBrush(QColor(44, 50, 60, 235));
    p.drawRect(box);
    p.setBrush(Qt::NoBrush);
    p.setPen(QColor(230, 233, 238));
    p.drawText(box, Qt::AlignCenter, text);
}

// Input dinamico: richiesta del comando e campi con i valori correnti, accanto al cursore.
void CadView::drawDynamicInput(QPainter& p) const
{
    if (!settings_.dynInput)
        return;
    const Command* command = commands_.active();
    if (!command && typedText_.isEmpty())
        return;
    if (snap_ || polar_)
        return; // il cursore mostra già un suggerimento

    const QFontMetricsF fm(font());
    const QPointF anchor = mouseScreen_ + QPointF(22, 22);
    const double rowH = fm.height() + 6;

    QStringList fields;
    int active = 0;
    if (!typedText_.isEmpty()) {
        fields << typedText_ + QStringLiteral("|");
    } else if (command) {
        const auto base = command->basePoint();
        if (base) {
            const Vec2 d = cursor_ - *base;
            fields << number(d.length()) << number(normalizeAngle(angleOf(d)) * 180.0 / kPi, 0) + QStringLiteral("°");
        } else {
            fields << number(cursor_.x) << number(cursor_.y);
        }
    }

    const QString prompt = command ? QString::fromStdString(commands_.prompt()) : QString();
    double x = anchor.x();
    double y = anchor.y();
    if (!prompt.isEmpty()) {
        const QRectF box(x, y, fm.horizontalAdvance(prompt) + 12, rowH);
        p.setPen(QPen(QColor(90, 100, 118), 1.0));
        p.setBrush(QColor(44, 50, 60, 235));
        p.drawRect(box);
        p.setPen(QColor(230, 233, 238));
        p.drawText(box, Qt::AlignCenter, prompt);
        y += rowH + 3;
    }
    for (int i = 0; i < fields.size(); ++i) {
        const QRectF box(x, y, std::max(70.0, fm.horizontalAdvance(fields[i]) + 14), rowH);
        p.setPen(QPen(i == active ? theme::kAccent : QColor(90, 100, 118), 1.0));
        p.setBrush(QColor(30, 34, 42, 240));
        p.drawRect(box);
        p.setPen(QColor(240, 242, 246));
        p.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, fields[i]);
        x = box.right() + 4;
    }
    p.setBrush(Qt::NoBrush);
}

// --------------------------------------------------------------------- mouse

void CadView::mousePressEvent(QMouseEvent* event)
{
    emit activated();   // finestre affiancate: la vista cliccata diventa quella attiva
    const QPointF pos = event->position();
    // Modifica di un testo nel disegno: il clic sposta il cursore (o chiude la modifica, fuori dal testo).
    if (textEditor_ && event->button() != Qt::MiddleButton) {
        if (event->type() == QEvent::MouseButtonDblClick ? textEditor_->viewMouseDoubleClick(event)
                                                         : textEditor_->viewMousePress(event)) {
            update();
            return;
        }
    }
    // Menu della maniglia multifunzione: la voce scelta avvia l'operazione; un clic altrove lo chiude.
    if (event->button() == Qt::LeftButton && gripMenuOpen()) {
        const int item = gripMenuItemAt(pos);
        if (item >= 0) {
            const auto actions = gripMenuActions();
            const HotGrip grip = *hotGrip_;
            gripMenuOpen_ = false;
            hotGrip_.reset();
            emit gripActionRequested(grip.id, static_cast<int>(grip.index), actions[static_cast<std::size_t>(item)],
                                     grip.position);
            update();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && event->type() == QEvent::MouseButtonPress) {
        lastLeftPressTime_ = event->timestamp();
        prevLeftPressHit_ = lastLeftPressHit_;
        const Entity* hit = doc_.pick(toWorld(pos), pickTolerance());
        lastLeftPressHit_ = hit ? hit->id() : 0;
    }

    if (event->button() == Qt::LeftButton && overNavigation(pos)) {
        if (navButtonRect(0).contains(pos))
            setPanMode(!panMode_);
        else if (navButtonRect(1).contains(pos))
            zoomExtents();
        else if (navButtonRect(2).contains(pos))
            zoomBy(1.5);
        else if (navButtonRect(3).contains(pos))
            zoomBy(1.0 / 1.5);
        return;
    }

    // Modalità PAN: il tasto sinistro trascina la vista, il destro esce.
    if (panMode_ && event->button() == Qt::LeftButton) {
        panning_ = true;
        panLast_ = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (panMode_ && event->button() == Qt::RightButton) {
        setPanMode(false);
        return;
    }

    switch (event->button()) {
    case Qt::MiddleButton:
        panning_ = true;
        panLast_ = pos;
        setCursor(Qt::ClosedHandCursor);
        break;
    case Qt::LeftButton: {
        leftPressPos_ = pos;
        updateCursor(pos);
        const bool onGrip = !commands_.isActive() && hotGrip_.has_value();
        emit pointPicked(cursor_);
        // Premuto su una maniglia: trascinando e rilasciando il punto si conferma al rilascio (oltre al clic e clic).
        gripDragArmed_ = onGrip && commands_.isActive();
        // Clic nel vuoto che ha aperto una finestra di selezione: trascinando col tasto premuto diventa un lazo.
        lassoArmed_ = appOptions().pressAndDrag && selectionCorner().has_value();
        lassoDragging_ = false;
        lassoPath_.clear();
        if (lassoArmed_)
            lassoPath_.push_back(*selectionCorner());
        break;
    }
    case Qt::RightButton: {
        // Senza comando il menu di scelta rapida; durante un comando che propone opzioni il menu
        // del comando (Invio, Annulla, opzioni); mentre si selezionano oggetti o senza opzioni, Invio.
        const QPoint global = event->globalPosition().toPoint();
        const Command* command = commands_.active();
        if (!appOptions().rightClickMenu)
            emit enterRequested();
        else if (!command)
            emit contextMenuRequested(global);
        else if (!command->wantsSelection() && commands_.prompt().find('[') != std::string::npos)
            emit commandMenuRequested(global);
        else
            emit enterRequested();
        break;
    }
    default:
        break;
    }
    update();
}

void CadView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(blockMimeType()))
        event->acceptProposedAction();
    else
        event->ignore();   // i file trascinati li apre la finestra principale
}

void CadView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(blockMimeType())) {
        event->acceptProposedAction();
        updateCursor(event->position());
        update();
    } else {
        event->ignore();
    }
}

void CadView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(blockMimeType())) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    dropBlock(QString::fromUtf8(event->mimeData()->data(blockMimeType())), event->position());
}

void CadView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (textEditor_ && !panning_ && textEditor_->viewMouseMove(event)) {   // selezione trascinando sul testo
        update();
        return;
    }
    if (panning_) {
        const QPointF delta = pos - panLast_;
        if (const ViewportEntity* vp = activeViewportEntity(); vp && !vp->locked) {
            const Vec2 d{delta.x() / scale_ / vp->scale(), -delta.y() / scale_ / vp->scale()};
            emit viewportViewChanged(vp->id(), vp->viewCenter - d, vp->viewHeight);
        } else {
            beginInteraction();
            center_.x -= delta.x() / scale_;
            center_.y += delta.y() / scale_;
        }
        panLast_ = pos;
    } else if (overNavigation(pos) || panMode_ || overGripMenu(pos)) {
        setCursor(overNavigation(pos) || overGripMenu(pos) ? Qt::ArrowCursor : Qt::OpenHandCursor);
        appliedCursor_.clear();
    } else {
        mouseScreen_ = pos;
        applyIdleCursor();
    }
    if (textEditor_ && !panning_ && textEditor_->viewIBeam(pos)) {
        setCursor(Qt::IBeamCursor);
        appliedCursor_.clear();
    }
    // Lazo: i punti del trascinamento (dopo qualche pixel, per non confonderlo con un clic).
    if (lassoArmed_ && (event->buttons() & Qt::LeftButton) && selectionCorner()) {
        if (!lassoDragging_) {
            const QPointF moved = pos - leftPressPos_;
            lassoDragging_ = std::abs(moved.x()) + std::abs(moved.y()) > 6.0;
        }
        if (lassoDragging_ && (lassoPath_.empty() || QLineF(toScreen(lassoPath_.back()), pos).length() >= 3.0))
            lassoPath_.push_back(toWorld(pos));
    } else if (!(event->buttons() & Qt::LeftButton)) {
        lassoArmed_ = false;
        lassoDragging_ = false;
    }
    updateCursor(pos);
    update();
}

void CadView::mouseReleaseEvent(QMouseEvent* event)
{
    if (textEditor_ && event->button() == Qt::LeftButton && textEditor_->viewMouseRelease(event))
        return;
    if (panning_ && (event->button() == Qt::MiddleButton || (panMode_ && event->button() == Qt::LeftButton))) {
        panning_ = false;
        interacting_ = false;   // fine del pan: ridisegno a piena qualità
        appliedCursor_.clear();
        if (panMode_)
            setCursor(Qt::OpenHandCursor);
        else
            applyIdleCursor();
        update();
        return;
    }

    // Maniglia trascinata: al rilascio (se il mouse si è spostato) il punto d'arrivo conferma la modifica.
    if (event->button() == Qt::LeftButton && gripDragArmed_) {
        gripDragArmed_ = false;
        const QPointF moved = event->position() - leftPressPos_;
        if (commands_.isActive() && std::hypot(moved.x(), moved.y()) > 4.0) {
            updateCursor(event->position());
            emit pointPicked(cursor_);
            update();
            return;
        }
    }
    // Trascinamento con il tasto sinistro dopo un clic nel vuoto: rilasciando si seleziona con il lazo.
    // Un trascinamento quasi diritto (senza area) resta una finestra rettangolare fra i due punti.
    if (event->button() == Qt::LeftButton && lassoArmed_) {
        const bool dragged = lassoDragging_;
        lassoArmed_ = false;
        lassoDragging_ = false;
        if (dragged && selectionCorner()) {
            std::vector<Vec2> polygon = std::move(lassoPath_);
            lassoPath_.clear();
            polygon.push_back(toWorld(event->position()));
            double area = 0.0;
            BBox box;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
                area += a.x * b.y - b.x * a.y;
                box.expand(a);
            }
            area /= 2.0;
            if (std::abs(area) < 0.2 * box.width() * box.height()) {
                updateCursor(event->position());
                emit pointPicked(cursor_);
            } else {
                emit lassoSelected(polygon, area > 0.0);   // antiorario = intersezione
            }
            update();
        }
    }
}

void CadView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (textEditor_ && event->button() == Qt::LeftButton) {   // doppio clic su una parola: la seleziona
        if (textEditor_->viewMouseDoubleClick(event)) {
            update();
            return;
        }
    }
    // Doppio clic con la rotellina = zoom estensioni.
    if (event->button() == Qt::MiddleButton) {
        zoomExtents();
        return;
    }
    // Con i widget Qt manda il doppio clic al posto della seconda pressione; con Qt Quick (interfaccia QML) la
    // pressione è già arrivata (stessa ora) e ripeterla raddoppierebbe punti e clic di selezione.
    const bool pressDelivered = event->timestamp() == lastLeftPressTime_ && event->button() == Qt::LeftButton;
    const auto secondPress = [&] {
        if (!pressDelivered)
            mousePressEvent(event);
    };
    // Durante un comando, in modalità PAN, sui pulsanti di navigazione o con il tasto destro il secondo clic
    // rapido è un clic normale: Qt lo manda come doppio clic, e senza questo andrebbe perso (punti cliccati in
    // fretta, Invio con il tasto destro ripetuto).
    if (commands_.isActive() || panMode_ || overNavigation(event->position()) || event->button() == Qt::RightButton) {
        secondPress();
        return;
    }
    // Oggetto sotto il primo dei due clic: la modifica col doppio clic vale solo se è lo stesso del secondo.
    const EntityId firstHit = pressDelivered ? prevLeftPressHit_ : lastLeftPressHit_;
    // In un layout, doppio clic dentro una finestra la attiva (zoom e pan sul modello),
    // fuori la disattiva. Altrimenti il doppio clic sinistro è ignorato.
    // Doppio clic su una tabella (nel modello o sul foglio): modifica delle celle.
    if (event->button() == Qt::LeftButton && !commands_.isActive()) {
        const Vec2 world = toWorld(event->position());
        for (const Entity* e : doc_.entitiesNear(world, 0.0))
            if (e->type() == EntityType::Table
                && static_cast<const TableEntity*>(e)->cellAt(world).first >= 0) {
                emit worldDoubleClicked(world);
                return;
            }
    }
    // Doppio clic su un oggetto: Proprietà (opzione "Modifica mediante doppio clic").
    if (event->button() == Qt::LeftButton && !commands_.isActive() && appOptions().doubleClickEdit) {
        const Vec2 world = toWorld(event->position());
        if (const Entity* e = doc_.pick(world, pickTolerance());
            e && e->type() != EntityType::Viewport && e->id() == firstHit) {
            emit entityDoubleClicked(e->id());
            return;
        }
    }
    if (event->button() == Qt::LeftButton && shownSpace_ != 0 && !commands_.isActive()) {
        const ViewportEntity* vp = viewportAt(toPaper(event->position()));
        emit viewportActivated(vp ? vp->id() : 0);
        return;
    }
    // Nel modello, un doppio clic che non apre niente vale come un secondo clic (selezione rapida).
    secondPress();
}

const ViewportEntity* CadView::activeViewportEntity() const
{
    if (activeViewport_ == 0)
        return nullptr;
    const Entity* e = doc_.find(activeViewport_);
    return e && e->type() == EntityType::Viewport ? static_cast<const ViewportEntity*>(e) : nullptr;
}

void CadView::wheelEvent(QWheelEvent* event)
{
    const QPointF pos = event->position();
    // Finestra attiva: lo zoom agisce sulla vista del modello, con il punto sotto il mouse fermo.
    // Finestra bloccata: lo zoom agisce sul foglio.
    if (const ViewportEntity* vp = activeViewportEntity(); vp && !vp->locked) {
        const double factor = std::pow(1.2, event->angleDelta().y() / 120.0);
        const Vec2 paper = toPaper(pos);
        const Vec2 model = vp->viewCenter + (paper - vp->center) / vp->scale();
        const double newHeight = vp->viewHeight / factor;
        const double newScale = vp->height / newHeight;
        emit viewportViewChanged(vp->id(), model - (paper - vp->center) / newScale, newHeight);
        event->accept();
        return;
    }
    beginInteraction();
    rememberView(true);
    const Vec2 before = toPaper(pos);
    const double factor = std::pow(1.2, event->angleDelta().y() / 120.0);
    scale_ = std::clamp(scale_ * factor, kMinScale, kMaxScale);
    const Vec2 after = toPaper(pos);
    center_ = center_ + (before - after);   // il punto sotto il mouse resta fermo

    updateCursor(pos);
    update();
    event->accept();
}

void CadView::enterEvent(QEnterEvent* event)
{
    mouseInside_ = true;
    updateCursor(event->position());
    update();
}

void CadView::leaveEvent(QEvent*)
{
    mouseInside_ = false;
    update();
}

} // namespace cartesio

namespace cartesio {

// Estensioni da inquadrare: nel modello il disegno, in un layout il foglio.
BBox CadView::paperExtents() const
{
    if (const Layout* layout = doc_.findLayout(shownSpace_); layout && shownSpace_ != 0) {
        BBox b;
        const Vec2 o = layout->origin;
        b.expand(o + Vec2{-layout->paperWidth * 0.03, -layout->paperHeight * 0.03});
        b.expand(o + Vec2{layout->paperWidth * 1.03, layout->paperHeight * 1.03});
        return b;
    }
    return doc_.extents();
}

const ViewportEntity* CadView::viewportAt(Vec2 paperPoint) const
{
    if (shownSpace_ == 0)
        return nullptr;
    for (const Entity* e : doc_.entitiesNear(paperPoint, 0.0, shownSpace_)) {
        if (e->type() != EntityType::Viewport)
            continue;
        if (static_cast<const ViewportEntity*>(e)->containsPaperPoint(paperPoint))
            return static_cast<const ViewportEntity*>(e);
    }
    return nullptr;
}

} // namespace cartesio

namespace cartesio {

void CadView::onSpaceChanged()
{
    savedViews_[viewSpace_] = {center_, scale_};
    viewSpace_ = shownSpace_;
    activeViewport_ = 0;
    if (const auto it = savedViews_.find(viewSpace_); it != savedViews_.end()) {
        center_ = it->second.first;
        scale_ = it->second.second;
        refresh();
    } else {
        zoomExtents();
    }
}

} // namespace cartesio

namespace cartesio {

void CadView::setPanMode(bool on)
{
    if (panMode_ == on)
        return;
    panMode_ = on;
    panning_ = false;
    appliedCursor_.clear();
    if (on)
        setCursor(Qt::OpenHandCursor);
    else
        applyIdleCursor();
    update();
}

} // namespace cartesio

namespace cartesio {

void CadView::rememberView(bool coalesce)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (coalesce && now - lastHistoryPush_ < 1000)
        return;   // una serie di rotellate è un solo passo
    lastHistoryPush_ = now;
    if (!viewHistory_.empty() && viewHistory_.back().first.x == center_.x && viewHistory_.back().first.y == center_.y
        && viewHistory_.back().second == scale_)
        return;
    viewHistory_.emplace_back(center_, scale_);
    if (viewHistory_.size() > 10)   // se ne ricordano 10
        viewHistory_.erase(viewHistory_.begin());
}

void CadView::zoomWindow(const BBox& box)
{
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return;
    rememberView();
    // Con una finestra attiva il rettangolo è nel modello: si porta sul foglio.
    BBox paper = box;
    if (const ViewportEntity* vp = activeViewportEntity()) {
        const Affine t = vp->modelToPaper();
        paper = BBox{};
        paper.expand(t.apply(box.min));
        paper.expand(t.apply(box.max));
    }
    center_ = paper.center();
    scale_ = std::clamp(std::min(width() / paper.width(), height() / paper.height()), kMinScale, kMaxScale);
    updateCursor(mouseScreen_);
    update();
}

bool CadView::zoomPrevious()
{
    if (viewHistory_.empty())
        return false;
    center_ = viewHistory_.back().first;
    scale_ = viewHistory_.back().second;
    viewHistory_.pop_back();
    updateCursor(mouseScreen_);
    update();
    return true;
}

} // namespace cartesio

namespace cartesio {

double CadView::benchmarkRender(int times)
{
    if (item_)   // GPU: tempo di costruzione delle geometrie (il disegno vero lo fa la scheda video)
        return item_->rebuildNow();
    QPixmap target(size());
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < times; ++i) {
        QPainter p(&target);
        renderScene(p);
    }
    return timer.nsecsElapsed() / 1e6 / std::max(1, times);
}

} // namespace cartesio

namespace cartesio {

// Pan e zoom in corso: per ~150 ms dopo l'ultimo movimento si usa l'immagine pronta spostata e scalata,
// poi la scena viene ridisegnata a piena qualità.
double CadView::sceneMargin()
{
    // Frazione della vista disegnata in più su ogni lato della texture della scena (CARTESIO_SCENEMARGIN per prova).
    static const double margin = [] {
        bool ok = false;
        const double v = qEnvironmentVariable("CARTESIO_SCENEMARGIN").toDouble(&ok);
        return ok ? std::clamp(v, 0.0, 1.0) : 0.25;
    }();
    return margin;
}

void CadView::syncGpuItems(DrawingItem* scene, DrawingItem* selection, GpuItemsSync& sync)
{
    // Solo se è cambiato il disegno, la vista o la selezione: un movimento del mouse ridisegna soltanto il
    // livello sopra (mirino, snap, evidenziazione), non tutta la scena sulla GPU.
    const bool selectionInScene = !selection || activeViewport_ != 0;
    const std::uint64_t key = sceneKey(true, selectionInScene);
    const std::uint64_t content = sceneKey(false, selectionInScene);
    const std::uint64_t selKey = selection ? sceneKey(true, true) : 0;
    const std::uint64_t selContent = selection ? sceneKey(false, true) : 0;
    const QTransform now = worldTransform();
    // La texture (con il suo margine attorno alla vista) spostata e scalata: punto (x, y) della texture a riposo ->
    // scala * (x, y) + spostamento. L'item ha l'origine in alto a sinistra e a riposo sta in baseOffset().
    const auto place = [](DrawingItem* item, const QTransform& t) {
        if (!item)
            return;
        item->setTransformOrigin(QQuickItem::TopLeft);
        item->setScale(t.m11());
        item->setPosition(item->baseOffset() * t.m11() + QPointF(t.dx(), t.dy()));
    };

    // Pan e zoom (rotellina, tasto centrale): sui disegni grandi ridisegnare tutto nella texture costa decine di
    // ms (scatti). Se è cambiata solo la vista e l'ultimo ridisegno vero è costato più di 25 ms, la texture pronta
    // si sposta e si scala (costa niente) e il ridisegno vero arriva quando ci si ferma. Sotto i 25 ms si
    // ridisegna sempre davvero: zoom nitido.
    static const bool fastView = !qEnvironmentVariableIsSet("CARTESIO_NOFASTVIEW");   // prova: ridisegno vero
    const bool slow = scene && scene->lastRenderMs() > 25.0;
    if (fastView && slow && interacting_ && sync.sceneContent == content && (!selection || sync.selectionContent == selContent)
        && key != sync.sceneKey && sync.sceneKey != 0) {
        const QTransform d = sync.sceneView.inverted() * now;
        const QTransform ds = sync.selectionView.inverted() * now;
        // Oltre 2 volte avanti la texture sarebbe troppo sgranata; se copre meno del 70% della vista (zoom indietro,
        // pan lungo: il margine attorno alla vista copre i primi scatti) resterebbe vuoto: si ridisegna davvero.
        const double w = width(), h = height();
        const double k = d.m11();
        const QPointF at = scene->baseOffset() * k + QPointF(d.dx(), d.dy());
        const double tw = scene->width() * k, th = scene->height() * k;
        const double coverX = std::max(0.0, std::min(w, at.x() + tw) - std::max(0.0, at.x()));
        const double coverY = std::max(0.0, std::min(h, at.y() + th) - std::max(0.0, at.y()));
        const bool covered = w > 0.0 && h > 0.0 && coverX * coverY >= 0.98 * w * h;
        if (std::abs(d.m12()) < 1e-9 && std::abs(d.m21()) < 1e-9 && k > 0.0 && k <= 2.0 && covered
            && std::abs(d.m11() - d.m22()) < 1e-9 * std::max(1.0, d.m11())) {
            place(scene, d);
            place(selection, ds);
            sync.moved = true;
            return;
        }
    }
    if (sync.moved) {   // fine del movimento: le texture tornano al loro posto, ridisegnate con la vista vera
        place(scene, QTransform());
        place(selection, QTransform());
        sync.moved = false;
    }
    if (key != sync.sceneKey) {
        sync.sceneKey = key;
        sync.sceneContent = content;
        sync.sceneView = now;
        scene->refresh();
    }
    if (selection && selKey != sync.selectionKey) {
        sync.selectionKey = selKey;
        sync.selectionContent = selContent;
        sync.selectionView = now;
        selection->refresh();
    }
}

void CadView::beginInteraction()
{
    if (!settleTimer_) {
        settleTimer_ = new QTimer(this);
        settleTimer_->setSingleShot(true);
        settleTimer_->setInterval(150);
        connect(settleTimer_, &QTimer::timeout, this, [this] {
            interacting_ = false;
            update();
        });
    }
    interacting_ = true;
    settleTimer_->start();
}

} // namespace cartesio

namespace cartesio {

QImage CadView::grabGpuImage(QString* info)
{
    QImage image = renderGpuImage(*this, size(), info);
    if (image.isNull())
        return image;
    QPainter p(&image);
    if (selection_.size() <= kGripObjectLimit)   // GRIPOBJLIMIT: oltre, niente maniglie
        for (EntityId id : selection_)
            if (const Entity* e = doc_.find(id))
                drawGrips(p, *e);
    drawOverlays(p);
    return image;
}

} // namespace cartesio

namespace cartesio {

void CadView::setCommandHistory(const QStringList& lines, const QRect& bar)
{
    if (lines == commandHistory_ && bar == commandBar_)
        return;
    commandHistory_ = lines;
    commandBar_ = bar;
    update();
}

void CadView::drawCommandHistory(QPainter& p) const
{
    if (commandHistory_.isEmpty() || commandBar_.isEmpty())
        return;
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(12);
    p.save();
    p.setFont(font);
    const int lineHeight = 18;
    const QRect area(commandBar_.left(), commandBar_.top() - lineHeight * static_cast<int>(commandHistory_.size()) - 1,
                     commandBar_.width(), lineHeight * static_cast<int>(commandHistory_.size()));
    p.fillRect(area, QColor(30, 35, 45, 170));
    p.setPen(QColor(210, 216, 225));
    for (int i = 0; i < commandHistory_.size(); ++i)
        p.drawText(QRect(area.left() + 8, area.top() + i * lineHeight, area.width() - 16, lineHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, commandHistory_[i]);
    p.restore();
}

} // namespace cartesio

namespace cartesio {

double CadView::benchmarkCursor(int n)
{
    QElapsedTimer timer;
    timer.start();
    const int steps = std::max(1, n);
    for (int i = 0; i < steps; ++i) {
        // Percorso a zig-zag su tutta la vista, come un mouse che si muove.
        const double fx = (i % 37) / 36.0, fy = ((i * 7) % 29) / 28.0;
        updateCursor(QPointF(fx * width(), fy * height()));
    }
    return timer.nsecsElapsed() / 1e6 / steps;
}

} // namespace cartesio

namespace cartesio {

std::vector<QImage> CadView::grabGpuZoomSequence(const std::vector<double>& factors, QString* info)
{
    const double base = scale_;
    std::vector<QImage> images = renderGpuSequence(
        *this, size(), static_cast<int>(factors.size()),
        [&](int i) { scale_ = std::clamp(base * factors[static_cast<std::size_t>(i)], kMinScale, kMaxScale); }, info);
    scale_ = base;
    return images;
}

} // namespace cartesio

namespace cartesio {

void CadView::attachCanvas(CadCanvas* canvas)
{
    if (canvas && std::find(canvases_.begin(), canvases_.end(), canvas) == canvases_.end())
        canvases_.push_back(canvas);
    gpu_ = {};
}

void CadView::detachCanvas(CadCanvas* canvas)
{
    canvases_.erase(std::remove(canvases_.begin(), canvases_.end(), canvas), canvases_.end());
}

void CadView::update()
{
    if (canvases_.empty()) {
        QWidget::update();
        return;
    }
    for (CadCanvas* c : canvases_)
        c->requestUpdate();
}

void CadView::setCursor(const QCursor& cursor)
{
    if (canvases_.empty()) {
        QWidget::setCursor(cursor);
        return;
    }
    for (CadCanvas* c : canvases_)
        c->setCursor(cursor);
}

QWidget* CadView::overlayHost()
{
    if (CadCanvas* c = canvas(); c && c->window())
        if (QWidget* host = qobject_cast<QWidget*>(c->window()->property("hostWidget").value<QObject*>()))
            return host;
    return this;
}

QPoint CadView::overlayOffset() const
{
    if (CadCanvas* c = canvas())
        return c->mapToScene(QPointF(0, 0)).toPoint();
    return {};
}

void CadView::paintOverlay(QPainter& p)
{
    if (selection_.size() <= kGripObjectLimit)   // GRIPOBJLIMIT
        for (EntityId id : selection_)
            if (const Entity* e = doc_.find(id))
                drawGrips(p, *e);
    drawOverlays(p);
}

} // namespace cartesio

namespace cartesio {

// TESTINASCOSTI: per i testi che cadono nella vista, perché non si vedono, detto in modo comprensibile:
// i layer spenti o congelati per nome (con il comando per riaccenderli), poi le altre cause con qualche esempio.
QStringList CadView::diagnoseTexts() const
{
    const BBox area = visibleArea();
    const double pxPerUnit = scale_;
    std::map<QString, int> offLayers, frozenLayers, counts;
    QStringList examples;
    int inView = 0, drawn = 0;
    const auto& layers = doc_.layers();
    const auto plain = [](const std::string& s) { return plainMText(s); };
    for (const auto& e : doc_.entities()) {
        if (e->type() != EntityType::Text && e->type() != EntityType::MText)
            continue;
        if ((e->space() < 0 ? 0 : e->space()) != shownSpace_)
            continue;
        const BBox b = e->bounds();
        if (b.isValid() && area.isValid()
            && (b.max.x < area.min.x || b.min.x > area.max.x || b.max.y < area.min.y || b.min.y > area.max.y))
            continue;
        ++inView;
        const bool mtext = e->type() == EntityType::MText;
        const double height = mtext ? static_cast<const MTextEntity&>(*e).height : static_cast<const TextEntity&>(*e).height;
        const std::string& text = mtext ? static_cast<const MTextEntity&>(*e).text : static_cast<const TextEntity&>(*e).text;
        const int li = e->layer();
        const Layer* layer = li >= 0 && li < static_cast<int>(layers.size()) ? &layers[static_cast<std::size_t>(li)] : nullptr;
        if (layer && !layer->visible) {
            ++offLayers[QString::fromStdString(layer->name)];
            continue;
        }
        if (layer && layer->frozen) {
            ++frozenLayers[QString::fromStdString(layer->name)];
            continue;
        }
        QString reason;
        if (plain(text).isEmpty())
            reason = tr("testo vuoto");
        else if (height * pxPerUnit < 1.0)
            reason = tr("troppo piccoli a questo zoom (meno di 1 pixel): ingrandire per leggerli");
        else if ((mtext ? layoutMText(doc_, static_cast<const MTextEntity&>(*e)) : layoutText(doc_, static_cast<const TextEntity&>(*e))).empty())
            reason = tr("font non trovato");
        else {
            Style style;
            style.layer = li;
            style.aci = doc_.displayColor(*e);
            if (resolveColorRaw(*e, style) != resolveColor(*e, style))
                reason = tr("colore uguale allo sfondo (ora mostrato come il colore 7)");
        }
        if (reason.isEmpty()) {
            ++drawn;
            continue;
        }
        ++counts[reason];
        if (examples.size() < 5)
            examples << tr("    \"%1\" (layer %2) - %3")
                            .arg(plain(text).left(40), layer ? QString::fromStdString(layer->name) : QStringLiteral("0"), reason);
    }
    QStringList out;
    out << tr("Testi in questa vista: %1, visibili: %2.").arg(inView).arg(drawn);
    if (inView == drawn) {
        out << tr("Tutti i testi della vista sono disegnati.");
        return out;
    }
    for (const auto& [name, n] : offLayers)
        out << tr("  %1 testi sul layer \"%2\", che è SPENTO nel disegno (nessun programma li mostra). Per vederli: "
                  "accenderlo nel Gestore proprietà layer, oppure -LAYER ON %3")
                   .arg(n)
                   .arg(name, QString(name).replace(QLatin1Char(' '), QLatin1Char('?')));
    for (const auto& [name, n] : frozenLayers)
        out << tr("  %1 testi sul layer \"%2\", che è CONGELATO. Per vederli: scongelarlo nel Gestore proprietà layer, "
                  "oppure -LAYER SC %3")
                   .arg(n)
                   .arg(name, QString(name).replace(QLatin1Char(' '), QLatin1Char('?')));
    for (const auto& [reason, n] : counts)
        out << tr("  %1 testi: %2").arg(n).arg(reason);
    if (!examples.isEmpty()) {
        out << tr("  Per esempio:");
        out << examples;
    }
    return out;
}

} // namespace cartesio


namespace cartesio {

// OTRACK: fermandosi su un punto di snap per mezzo secondo lo si aggancia (di nuovo: lo si sgancia; al massimo 7).
// Dai punti agganciati partono linee orizzontali e verticali (e polari, se il polare è attivo): vicino a una linea
// il cursore ci si allinea, vicino a due si va all'incrocio.
void CadView::updateTracking(const std::optional<Vec2>& base, bool)
{
    constexpr double kAcquireMs = 450.0;
    constexpr double kTrackPx = 7.0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (snap_) {
        const Vec2 sp = snap_->position;
        if (!dwellPoint_ || distance(*dwellPoint_, sp) * effectiveScale() > 1.0) {
            dwellPoint_ = sp;
            dwellStart_ = now;
            if (!dwellTimer_) {
                dwellTimer_ = new QTimer(this);
                dwellTimer_->setSingleShot(true);
                connect(dwellTimer_, &QTimer::timeout, this, [this] { refresh(); });
            }
            dwellTimer_->start(static_cast<int>(kAcquireMs) + 30);
        } else if (now - dwellStart_ >= kAcquireMs && dwellStart_ > 0) {
            dwellStart_ = 0;   // una volta per sosta
            const auto it = std::find_if(tracked_.begin(), tracked_.end(),
                                         [&](Vec2 t) { return distance(t, sp) * effectiveScale() <= 1.0; });
            if (it != tracked_.end())
                tracked_.erase(it);
            else {
                tracked_.push_back(sp);
                if (tracked_.size() > 7)
                    tracked_.erase(tracked_.begin());
            }
        }
    } else {
        dwellPoint_.reset();
    }
    if (tracked_.empty())
        return;

    // Direzioni: 0° e 90°, più quelle polari.
    std::vector<double> angles{0.0, kPi / 2.0};
    if (settings_.polar && settings_.polarAngleDeg > 0.0)
        for (double a = settings_.polarAngleDeg; a < 180.0 - 1e-6; a += settings_.polarAngleDeg)
            if (std::abs(a - 90.0) > 1e-6)
                angles.push_back(degToRad(a));
    struct Line {
        Vec2 origin;
        double angle;
        double distancePx;
    };
    std::vector<Line> near;
    const double scale = effectiveScale();
    for (Vec2 t : tracked_)
        for (double a : angles) {
            const Vec2 dir = Vec2::polar(1.0, a);
            const Vec2 d = rawCursor_ - t;
            const double off = std::abs(cross(dir, d)) * scale;
            if (off <= kTrackPx)
                near.push_back({t, a, off});
        }
    if (base)   // anche dal punto base del comando (come per il polare)
        for (double a : angles) {
            const Vec2 dir = Vec2::polar(1.0, a);
            const double off = std::abs(cross(dir, rawCursor_ - *base)) * scale;
            if (off <= kTrackPx && !tracked_.empty())
                near.push_back({*base, a, off});
        }
    if (near.empty())
        return;
    std::sort(near.begin(), near.end(), [](const Line& a, const Line& b) { return a.distancePx < b.distancePx; });
    const Line& best = near.front();
    // Incrocio con un'altra linea (di un altro punto o un'altra direzione) vicino al cursore.
    for (std::size_t i = 1; i < near.size(); ++i) {
        const Line& other = near[i];
        if (std::abs(std::sin(other.angle - best.angle)) < 1e-6)
            continue;
        const Vec2 d1 = Vec2::polar(1.0, best.angle), d2 = Vec2::polar(1.0, other.angle);
        const double denom = cross(d1, d2);
        const double s = cross(other.origin - best.origin, d2) / denom;
        const Vec2 x = best.origin + d1 * s;
        if (distance(x, rawCursor_) * scale <= kTrackPx * 1.5) {
            track_ = TrackHit{x, {{best.origin, best.angle}, {other.origin, other.angle}}};
            return;
        }
    }
    const Vec2 dir = Vec2::polar(1.0, best.angle);
    const double along = (rawCursor_ - best.origin).dot(dir);
    track_ = TrackHit{best.origin + dir * along, {{best.origin, best.angle}}};
}

void CadView::drawObjectTracking(QPainter& p) const
{
    if (!mouseInside_)
        return;
    // Punti agganciati: una piccola croce verde.
    p.setPen(QPen(kTrackingColor, 1.5));
    for (Vec2 t : tracked_) {
        const QPointF s = toScreen(t);
        p.drawLine(s + QPointF(-4, 0), s + QPointF(4, 0));
        p.drawLine(s + QPointF(0, -4), s + QPointF(0, 4));
    }
    if (!track_)
        return;
    QPen pen(kTrackingColor, 1.0);
    pen.setDashPattern({4, 4});
    p.setPen(pen);
    const double reach = 3.0 * std::max(width(), height()) / effectiveScale();
    for (const auto& [origin, angle] : track_->lines) {
        const Vec2 dir = Vec2::polar(1.0, angle);
        p.drawLine(toScreen(origin - dir * reach), toScreen(origin + dir * reach));
    }
    QString tip;
    if (track_->lines.size() >= 2) {
        tip = QStringLiteral("Intersezione: %1 < %2°, %3 < %4°")
                  .arg(number(distance(track_->lines[0].first, track_->point)))
                  .arg(number(normalizeAngle(angleOf(track_->point - track_->lines[0].first)) * 180.0 / kPi, 0))
                  .arg(number(distance(track_->lines[1].first, track_->point)))
                  .arg(number(normalizeAngle(angleOf(track_->point - track_->lines[1].first)) * 180.0 / kPi, 0));
    } else {
        const Vec2 o = track_->lines[0].first;
        tip = QStringLiteral("Puntamento: %1 < %2°")
                  .arg(number(distance(o, track_->point)))
                  .arg(number(normalizeAngle(angleOf(track_->point - o)) * 180.0 / kPi, 0));
    }
    if (!snap_)
        drawTooltip(p, toScreen(track_->point) + QPointF(18, 26), tip);
}

} // namespace cartesio

namespace cartesio {

// Diagnosi di OTRACK: aggancia i punti indicati e mette il cursore (coordinate del disegno) dove dice `at`.
QString CadView::simulateTracking(const std::vector<Vec2>& acquired, Vec2 at)
{
    tracked_ = acquired;
    dwellPoint_.reset();
    updateCursor(toScreen(at));
    if (!track_)
        return QStringLiteral("nessun allineamento; cursore %1, %2").arg(cursor_.x).arg(cursor_.y);
    return QStringLiteral("%1 linee; cursore %2, %3").arg(track_->lines.size()).arg(cursor_.x).arg(cursor_.y);
}

} // namespace cartesio
