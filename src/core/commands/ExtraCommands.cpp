// Altri comandi: POLYGON, REVCLOUD, XLINE, RAY, DIVIDE, MEASURE, LENGTHEN, ALIGN, OVERKILL,
// PURGE, SELECTSIMILAR.
#include "core/commands/CommandManager.h"

#include "core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cartesio {

namespace {

std::string fmtNumber(double v)
{
    std::ostringstream out;
    out << std::round(v * 10000.0) / 10000.0;
    return out.str();
}

// ------------------------------------------------- misure lungo un oggetto

double primLength(const Prim& p)
{
    return p.kind == Prim::Kind::Segment ? distance(p.a, p.b) : p.radius * p.sweep;
}

double totalLength(const std::vector<Prim>& prims)
{
    double l = 0.0;
    for (const Prim& p : prims)
        l += primLength(p);
    return l;
}

// Punto (e direzione) alla distanza s dall'inizio, lungo i tratti nell'ordine.
Vec2 pointAtLength(const std::vector<Prim>& prims, double s)
{
    for (const Prim& p : prims) {
        const double l = primLength(p);
        if (s <= l || &p == &prims.back()) {
            const double f = l > 0.0 ? std::clamp(s / l, 0.0, 1.0) : 0.0;
            if (p.kind == Prim::Kind::Segment)
                return p.a + (p.b - p.a) * f;
            // Archi: i tratti delle polilinee orarie hanno a = punto finale dell'arco.
            const bool reversed = distance(p.pointAt(p.start), p.a) > 1e-9 * std::max(1.0, p.radius);
            return p.pointAt(p.start + p.sweep * (reversed ? 1.0 - f : f));
        }
        s -= l;
    }
    return prims.empty() ? Vec2{} : prims.back().b;
}

// Tratti di un oggetto misurabile (linee, archi, cerchi, polilinee, ellissi, spline).
std::vector<Prim> measurablePrims(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Line:
    case EntityType::Arc:
    case EntityType::Circle:
    case EntityType::Polyline:
    case EntityType::Ellipse:
    case EntityType::Spline:
        return e.prims();
    default:
        return {};
    }
}

std::unique_ptr<PolylineEntity> closedPolyline(const std::vector<Vec2>& points)
{
    std::vector<PolyVertex> v;
    for (Vec2 p : points)
        v.push_back({p, 0.0});
    return std::make_unique<PolylineEntity>(std::move(v), true);
}

// Nuvola di revisione lungo un contorno chiuso: archi verso l'esterno, lunghi circa `arc`.
std::unique_ptr<PolylineEntity> cloudAlong(std::vector<Vec2> outline, double arc)
{
    double area = 0.0;
    for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
        area += cross(outline[j], outline[i]);
    if (area < 0.0)
        std::reverse(outline.begin(), outline.end());   // antiorario
    std::vector<Prim> prims;
    appendPolyline(prims, outline, true);
    const double perimeter = totalLength(prims);
    const int n = std::max(3, static_cast<int>(std::round(perimeter / arc)));
    std::vector<PolyVertex> v;
    const double bulge = -std::tan(degToRad(110.0) / 4.0);   // archi orari = verso l'esterno
    for (int i = 0; i < n; ++i)
        v.push_back({pointAtLength(prims, perimeter * i / n), bulge});
    return std::make_unique<PolylineEntity>(std::move(v), true);
}

// ----------------------------------------------------------------- POLYGON

int gPolygonSides = 4;
bool gPolygonInscribed = true;

class PolygonCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (step_) {
        case Step::Sides: return "Immettere numero dei lati <" + std::to_string(gPolygonSides) + ">:";
        case Step::Center: return "Specificare centro del poligono o [Lato]:";
        case Step::Mode:
            return std::string("Immettere un'opzione [Inscritto nel cerchio/Circoscritto attorno al cerchio] <")
                + (gPolygonInscribed ? "I" : "C") + ">:";
        case Step::Radius: return "Specificare raggio del cerchio:";
        case Step::Edge1: return "Specificare primo punto finale del lato:";
        case Step::Edge2: return "Specificare secondo punto finale del lato:";
        }
        return {};
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (step_ == Step::Sides) {
            if (v < 3 || v > 1024) {
                ctx.message("Il numero dei lati deve essere fra 3 e 1024.");
                return CommandStatus::Continue;
            }
            gPolygonSides = static_cast<int>(v);
            step_ = Step::Center;
            return CommandStatus::Continue;
        }
        if (step_ == Step::Radius && v > 0.0)
            return create(ctx, *center_ + Vec2{v, 0.0});
        return Command::onValue(ctx, v);
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (step_ == Step::Sides) {
            step_ = Step::Center;
            return CommandStatus::Continue;
        }
        if (step_ == Step::Mode) {
            step_ = Step::Radius;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (step_ == Step::Center && (w == "L" || w == "LATO" || w == "E" || w == "EDGE")) {
            step_ = Step::Edge1;
            return CommandStatus::Continue;
        }
        if (step_ == Step::Mode) {
            if (w == "I" || w == "INSCRITTO")
                gPolygonInscribed = true;
            else if (w == "C" || w == "CIRCOSCRITTO")
                gPolygonInscribed = false;
            else
                return Command::onKeyword(ctx, w);
            step_ = Step::Radius;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        switch (step_) {
        case Step::Sides:
        case Step::Center:
            center_ = p;
            ctx.lastPoint = p;
            step_ = Step::Mode;
            return CommandStatus::Continue;
        case Step::Radius:
            return create(ctx, p);
        case Step::Edge1:
            edge_ = p;
            ctx.lastPoint = p;
            step_ = Step::Edge2;
            return CommandStatus::Continue;
        case Step::Edge2: {
            ctx.document.add(byEdge(*edge_, p));
            return CommandStatus::Finished;
        }
        case Step::Mode:
            step_ = Step::Radius;
            return create(ctx, p);
        }
        return CommandStatus::Continue;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (step_ == Step::Radius && center_ && distance(*center_, cursor) > 0.0)
            result.push_back(byRadius(*center_, cursor));
        if (step_ == Step::Edge2 && edge_ && distance(*edge_, cursor) > 0.0)
            result.push_back(byEdge(*edge_, cursor));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return step_ == Step::Edge2 ? edge_ : center_; }

private:
    enum class Step { Sides, Center, Mode, Radius, Edge1, Edge2 };

    // Inscritto: il punto è un vertice; circoscritto: il punto è il centro di un lato.
    static std::unique_ptr<PolylineEntity> byRadius(Vec2 c, Vec2 p)
    {
        const int n = gPolygonSides;
        double r = distance(c, p);
        double a0 = angleOf(p - c);
        if (!gPolygonInscribed) {
            r /= std::cos(kPi / n);
            a0 += kPi / n;
        }
        std::vector<Vec2> pts;
        for (int i = 0; i < n; ++i)
            pts.push_back(c + Vec2::polar(r, a0 + 2.0 * kPi * i / n));
        return closedPolyline(pts);
    }

    static std::unique_ptr<PolylineEntity> byEdge(Vec2 a, Vec2 b)
    {
        const int n = gPolygonSides;
        std::vector<Vec2> pts{a, b};
        double angle = angleOf(b - a);
        const double side = distance(a, b);
        for (int i = 2; i < n; ++i) {
            angle += 2.0 * kPi / n;
            pts.push_back(pts.back() + Vec2::polar(side, angle));
        }
        return closedPolyline(pts);
    }

    CommandStatus create(CommandContext& ctx, Vec2 p)
    {
        if (distance(*center_, p) <= 0.0)
            return CommandStatus::Continue;
        ctx.document.add(byRadius(*center_, p));
        return CommandStatus::Finished;
    }

    Step step_ = Step::Sides;
    std::optional<Vec2> center_;
    std::optional<Vec2> edge_;
};

// ----------------------------------------------------------------- REVCLOUD

double gRevcloudArc = 0.0;   // 0 = automatica

class RevcloudCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (askingArc_)
            return "Specificare lunghezza dell'arco <" + (gRevcloudArc > 0 ? fmtNumber(gRevcloudArc) : std::string("auto"))
                + ">:";
        if (object_)
            return "Selezionare oggetto chiuso (polilinea, cerchio, ellisse):";
        if (first_)
            return "Specificare angolo opposto:";
        return "Specificare primo angolo o [Lunghezza arco/Oggetto]:";
    }
    bool wantsSelection() const override { return object_; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!first_ && (w == "L" || w == "LUNGHEZZA" || w == "A" || w == "ARC")) {
            askingArc_ = true;
            return CommandStatus::Continue;
        }
        if (!first_ && (w == "O" || w == "OGGETTO" || w == "OBJECT")) {
            object_ = true;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (askingArc_ && v >= 0.0) {
            gRevcloudArc = v;
            askingArc_ = false;
            return CommandStatus::Continue;
        }
        return Command::onValue(ctx, v);
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (askingArc_) {
            askingArc_ = false;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (object_) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            const auto outline = e ? closedOutline(*e) : std::vector<Vec2>{};
            if (outline.size() < 3) {
                ctx.message("Serve un oggetto chiuso.");
                return CommandStatus::Continue;
            }
            auto cloud = cloud_(outline);
            cloud->copyStyleFrom(*e);
            cloud->setId(e->id());
            std::vector<std::unique_ptr<Entity>> added;
            added.push_back(std::move(cloud));
            ctx.document.commit(std::move(added), {e->id()});
            return CommandStatus::Finished;
        }
        if (!first_) {
            first_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        if (std::abs(p.x - first_->x) <= 0.0 || std::abs(p.y - first_->y) <= 0.0)
            return CommandStatus::Continue;
        ctx.document.add(cloud_(rect(*first_, p)));
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (first_ && std::abs(cursor.x - first_->x) > 0.0 && std::abs(cursor.y - first_->y) > 0.0)
            result.push_back(cloud_(rect(*first_, cursor)));
        return result;
    }

private:
    static std::vector<Vec2> rect(Vec2 a, Vec2 b) { return {a, {b.x, a.y}, b, {a.x, b.y}}; }

    static std::unique_ptr<PolylineEntity> cloud_(const std::vector<Vec2>& outline)
    {
        double arc = gRevcloudArc;
        if (arc <= 0.0) {   // automatica: circa 40 archi sul perimetro, come le proporzioni
            std::vector<Prim> prims;
            appendPolyline(prims, outline, true);
            arc = std::max(1e-6, totalLength(prims) / 40.0);
        }
        return cloudAlong(outline, arc);
    }

    bool askingArc_ = false;
    bool object_ = false;
    std::optional<Vec2> first_;
};

// ------------------------------------------------------------ XLINE / RAY

class XlineCommand final : public Command {
public:
    explicit XlineCommand(bool ray) : ray_(ray) {}

    std::string prompt() const override
    {
        if (askingAngle_)
            return "Specificare angolo della linea di costruzione:";
        if (!base_)
            return ray_ ? "Specificare punto iniziale:" : "Specificare un punto o [Orizz/Vert/Ang]:";
        return "Specificare punto di passaggio:";
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!ray_ && !base_) {
            if (w == "O" || w == "ORIZZ" || w == "H" || w == "HOR")
                fixed_ = Vec2{1.0, 0.0};
            else if (w == "V" || w == "VERT" || w == "VER")
                fixed_ = Vec2{0.0, 1.0};
            else if (w == "A" || w == "ANG")
                askingAngle_ = true;
            else
                return Command::onKeyword(ctx, w);
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (askingAngle_) {
            fixed_ = Vec2::polar(1.0, degToRad(v));
            askingAngle_ = false;
            return CommandStatus::Continue;
        }
        return Command::onValue(ctx, v);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (fixed_) {   // direzione fissa: ogni punto è una nuova linea
            ctx.document.add(std::make_unique<XLineEntity>(p, *fixed_, false));
            return CommandStatus::Continue;
        }
        if (!base_) {
            base_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        if (distance(*base_, p) <= 0.0)
            return CommandStatus::Continue;
        ctx.document.add(std::make_unique<XLineEntity>(*base_, p - *base_, ray_));
        return CommandStatus::Continue;   // altre linee dallo stesso punto, Invio per finire
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (fixed_)
            result.push_back(std::make_unique<XLineEntity>(cursor, *fixed_, false));
        else if (base_ && distance(*base_, cursor) > 0.0)
            result.push_back(std::make_unique<XLineEntity>(*base_, cursor - *base_, ray_));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return base_; }

private:
    bool ray_;
    bool askingAngle_ = false;
    std::optional<Vec2> base_;
    std::optional<Vec2> fixed_;
};

// ------------------------------------------------------ DIVIDE / MEASURE

class DivideCommand final : public Command {
public:
    explicit DivideCommand(bool measure) : measure_(measure) {}

    std::string prompt() const override
    {
        if (!target_)
            return measure_ ? "Selezionare oggetto da misurare:" : "Selezionare oggetto da dividere:";
        return measure_ ? "Specificare lunghezza del segmento:" : "Immettere il numero di segmenti:";
    }
    bool wantsSelection() const override { return !target_; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (target_)
            return Command::onPoint(ctx, p);
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e || measurablePrims(*e).empty()) {
            ctx.message("Selezionare linee, archi, cerchi, polilinee, ellissi o spline.");
            return CommandStatus::Continue;
        }
        target_ = e->id();
        pick_ = p;
        ctx.selection = {e->id()};
        return CommandStatus::Continue;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!target_)
            return Command::onValue(ctx, v);
        const Entity* e = ctx.document.find(*target_);
        std::vector<Prim> prims = e ? measurablePrims(*e) : std::vector<Prim>{};
        const double length = totalLength(prims);
        std::vector<double> at;
        if (measure_) {
            if (v <= 0.0)
                return CommandStatus::Continue;
            // MEASURE parte dall'estremo più vicino al punto con cui si è selezionato l'oggetto.
            const bool fromEnd = !prims.empty() && distance(pick_, prims.back().b) < distance(pick_, prims.front().a);
            for (double s = v; s < length - 1e-9; s += v)
                at.push_back(fromEnd ? length - s : s);
        } else {
            const int n = static_cast<int>(v);
            if (n < 2 || n > 32767) {
                ctx.message("Il numero di segmenti deve essere fra 2 e 32767.");
                return CommandStatus::Continue;
            }
            const bool closed = e && (e->type() == EntityType::Circle
                                      || (e->type() == EntityType::Polyline && static_cast<const PolylineEntity*>(e)->closed));
            for (int i = 1; i < n; ++i)
                at.push_back(length * i / n);
            if (closed)
                at.insert(at.begin(), 0.0);   // sugli oggetti chiusi c'è un punto anche all'inizio
        }
        std::vector<std::unique_ptr<Entity>> points;
        for (double s : at) {
            auto pt = std::make_unique<PointEntity>(pointAtLength(prims, s));
            if (e)
                pt->setLayer(e->layer());
            points.push_back(std::move(pt));
        }
        ctx.selection.clear();
        ctx.message(std::to_string(points.size()) + " punti inseriti (si agganciano con lo snap Nodo).");
        if (!points.empty())
            ctx.document.commit(std::move(points));
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

private:
    bool measure_;
    std::optional<EntityId> target_;
    Vec2 pick_;
};

// ----------------------------------------------------------------- LENGTHEN

class LengthenCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (mode_) {
        case Mode::AskDelta: return "Immettere lunghezza incremento <" + fmtNumber(delta_) + ">:";
        case Mode::AskPercent: return "Immettere percentuale di lunghezza <" + fmtNumber(percent_) + ">:";
        case Mode::AskTotal: return "Specificare lunghezza totale <" + fmtNumber(total_) + ">:";
        case Mode::None: return "Selezionare un oggetto da misurare o [DElta/Percentuale/Totale]:";
        default: return "Selezionare un oggetto da modificare (Invio per finire):";
        }
    }
    bool wantsSelection() const override
    {
        return mode_ == Mode::None || mode_ == Mode::Delta || mode_ == Mode::Percent || mode_ == Mode::Total;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (w == "DE" || w == "DELTA")
            mode_ = Mode::AskDelta;
        else if (w == "P" || w == "PERCENTUALE" || w == "PERCENT")
            mode_ = Mode::AskPercent;
        else if (w == "T" || w == "TOTALE" || w == "TOTAL")
            mode_ = Mode::AskTotal;
        else
            return Command::onKeyword(ctx, w);
        return CommandStatus::Continue;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        switch (mode_) {
        case Mode::AskDelta: delta_ = v; mode_ = Mode::Delta; break;
        case Mode::AskPercent:
            if (v <= 0.0)
                return CommandStatus::Continue;
            percent_ = v;
            mode_ = Mode::Percent;
            break;
        case Mode::AskTotal:
            if (v <= 0.0)
                return CommandStatus::Continue;
            total_ = v;
            mode_ = Mode::Total;
            break;
        default: return Command::onValue(ctx, v);
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        switch (mode_) {
        case Mode::AskDelta: mode_ = Mode::Delta; return CommandStatus::Continue;
        case Mode::AskPercent: mode_ = Mode::Percent; return CommandStatus::Continue;
        case Mode::AskTotal: mode_ = Mode::Total; return CommandStatus::Continue;
        default: return CommandStatus::Finished;
        }
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e || (e->type() != EntityType::Line && e->type() != EntityType::Arc)) {
            ctx.message("Selezionare una linea o un arco.");
            return CommandStatus::Continue;
        }
        const double current = totalLength(e->prims());
        if (mode_ == Mode::None) {
            ctx.message("Lunghezza corrente: " + fmtNumber(current)
                        + (e->type() == EntityType::Arc
                               ? ", angolo incluso: " + fmtNumber(radToDeg(static_cast<const ArcEntity*>(e)->sweep()))
                               : std::string()));
            return CommandStatus::Continue;
        }
        double wanted = current;
        if (mode_ == Mode::Delta)
            wanted = current + delta_;
        else if (mode_ == Mode::Percent)
            wanted = current * percent_ / 100.0;
        else if (mode_ == Mode::Total)
            wanted = total_;
        if (wanted <= 1e-9) {
            ctx.message("La lunghezza risultante deve essere positiva.");
            return CommandStatus::Continue;
        }
        auto copy = e->clone();
        // Si allunga o accorcia l'estremo più vicino al punto indicato.
        if (e->type() == EntityType::Line) {
            auto& l = static_cast<LineEntity&>(*copy);
            const Vec2 u = (l.end - l.start) / current;
            if (distance(p, l.end) <= distance(p, l.start))
                l.end = l.start + u * wanted;
            else
                l.start = l.end - u * wanted;
        } else {
            auto& a = static_cast<ArcEntity&>(*copy);
            const double sweep = wanted / a.radius;
            if (sweep >= 2.0 * kPi) {
                ctx.message("L'arco risultante supererebbe il cerchio intero.");
                return CommandStatus::Continue;
            }
            if (distance(p, a.endPoint()) <= distance(p, a.startPoint()))
                a.endAngle = a.startAngle + sweep;
            else
                a.startAngle = a.endAngle - sweep;
        }
        std::vector<std::unique_ptr<Entity>> modified;
        modified.push_back(std::move(copy));
        ctx.document.commit(std::move(modified), {e->id()});
        return CommandStatus::Continue;
    }

private:
    enum class Mode { None, AskDelta, Delta, AskPercent, Percent, AskTotal, Total };
    Mode mode_ = Mode::None;
    double delta_ = 0.0;
    double percent_ = 100.0;
    double total_ = 1.0;
};

// -------------------------------------------------------------------- ALIGN

class AlignCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        ids_ = ctx.selection;
        return CommandStatus::Continue;
    }

    std::string actionPrompt() const override
    {
        switch (points_.size()) {
        case 0: return "Specificare primo punto di origine:";
        case 1: return "Specificare primo punto di destinazione:";
        case 2: return "Specificare secondo punto di origine (Invio per solo spostamento):";
        case 3: return "Specificare secondo punto di destinazione:";
        default: return "Scalare gli oggetti in base ai punti di allineamento? [Sì/No] <N>:";
        }
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (points_.size() >= 4)
            return Command::onPoint(ctx, p);
        points_.push_back(p);
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (points_.size() == 4) {
            if (w == "S" || w == "SI" || w == "Y" || w == "YES")
                return apply(ctx, true);
            if (w == "N" || w == "NO")
                return apply(ctx, false);
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus actionEnter(CommandContext& ctx) override
    {
        if (points_.size() == 2 || points_.size() == 4)
            return apply(ctx, false);
        return CommandStatus::Finished;
    }

private:
    CommandStatus apply(CommandContext& ctx, bool scale)
    {
        Affine t = Affine::translation(points_[1] - points_[0]);
        if (points_.size() == 4) {
            const Vec2 s = points_[2] - points_[0];
            const Vec2 d = points_[3] - points_[1];
            if (s.length() <= 0.0 || d.length() <= 0.0)
                return CommandStatus::Finished;
            t = Affine::rotation(points_[1], angleOf(d) - angleOf(s)) * t;
            if (scale)
                t = Affine::scaling(points_[1], d.length() / s.length()) * t;
        }
        std::vector<std::unique_ptr<Entity>> moved;
        for (EntityId id : ids_)
            if (const Entity* e = ctx.document.find(id)) {
                auto c = e->clone();
                c->transform(t);
                moved.push_back(std::move(c));
            }
        ctx.selection.clear();
        ctx.document.commit(std::move(moved), ids_);
        return CommandStatus::Finished;
    }

    std::vector<EntityId> ids_;
    std::vector<Vec2> points_;
};

// ----------------------------------------------------------------- OVERKILL

// Oggetti doppi: stesso tipo, layer, colore e geometria (tratti uguali, anche in ordine inverso).
bool sameGeometry(const Entity& a, const Entity& b, double tol)
{
    if (a.type() != b.type() || a.layer() != b.layer() || a.color() != b.color())
        return false;
    if (a.type() == EntityType::Text || a.type() == EntityType::MText || a.type() == EntityType::Insert
        || a.type() == EntityType::Dimension || a.type() == EntityType::Hatch || a.type() == EntityType::Viewport
        || a.type() == EntityType::Leader)
        return false;   // per questi conta anche il contenuto: si lasciano stare
    if (a.type() == EntityType::Point)
        return distance(static_cast<const PointEntity&>(a).position, static_cast<const PointEntity&>(b).position) <= tol;
    const auto pa = a.prims();
    const auto pb = b.prims();
    if (pa.size() != pb.size() || pa.empty())
        return false;
    const auto samePrim = [tol](const Prim& x, const Prim& y) {
        if (x.kind != y.kind)
            return false;
        const bool direct = distance(x.a, y.a) <= tol && distance(x.b, y.b) <= tol;
        const bool reverse = distance(x.a, y.b) <= tol && distance(x.b, y.a) <= tol;
        if (x.kind == Prim::Kind::Segment)
            return direct || reverse;
        return (direct || reverse) && distance(x.center, y.center) <= tol && std::abs(x.radius - y.radius) <= tol;
    };
    bool forward = true, backward = true;
    for (std::size_t i = 0; i < pa.size(); ++i) {
        forward = forward && samePrim(pa[i], pb[i]);
        backward = backward && samePrim(pa[i], pb[pb.size() - 1 - i]);
    }
    return forward || backward;
}

class OverkillCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        std::vector<const Entity*> list;
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id))
                list.push_back(e);
        ctx.selection.clear();
        const double tol = 1e-6;
        std::vector<EntityId> removed;
        std::vector<bool> gone(list.size(), false);
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (gone[i])
                continue;
            for (std::size_t j = i + 1; j < list.size(); ++j)
                if (!gone[j] && sameGeometry(*list[i], *list[j], tol)) {
                    gone[j] = true;
                    removed.push_back(list[j]->id());
                }
        }
        // Linee sovrapposte: una linea interamente contenuta in un'altra sulla stessa retta è di troppo.
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (gone[i] || list[i]->type() != EntityType::Line)
                continue;
            const auto& a = static_cast<const LineEntity&>(*list[i]);
            const double len = distance(a.start, a.end);
            if (len <= tol)
                continue;
            const Vec2 u = (a.end - a.start) / len;
            for (std::size_t j = 0; j < list.size(); ++j) {
                if (j == i || gone[j] || list[j]->type() != EntityType::Line || list[j]->layer() != a.layer())
                    continue;
                const auto& b = static_cast<const LineEntity&>(*list[j]);
                if (std::abs(cross(u, b.start - a.start)) > tol || std::abs(cross(u, b.end - a.start)) > tol)
                    continue;
                const double t0 = (b.start - a.start).dot(u);
                const double t1 = (b.end - a.start).dot(u);
                if (std::min(t0, t1) >= -tol && std::max(t0, t1) <= len + tol) {
                    gone[j] = true;
                    removed.push_back(b.id());
                }
            }
        }
        const std::size_t total = removed.size();
        std::vector<std::unique_ptr<Entity>> joined;
        if (removed.empty()) {
            ctx.message("Nessun oggetto doppio o sovrapposto.");
            return CommandStatus::Finished;
        }
        ctx.message(std::to_string(total) + " oggetti doppi o sovrapposti eliminati o uniti.");
        ctx.document.commit(std::move(joined), removed);
        return CommandStatus::Finished;
    }
};

// -------------------------------------------------------------------- PURGE

class PurgeCommand final : public Command {
public:
    std::string prompt() const override
    {
        return "Tipo di elementi da eliminare [Blocchi/LAyer/TIpi di linea/Stili di quota/Tutto] <Tutto>:";
    }

    CommandStatus onEnter(CommandContext& ctx) override { return purge(ctx, true, true, true, true); }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (w == "T" || w == "TUTTO" || w == "A" || w == "ALL")
            return purge(ctx, true, true, true, true);
        if (w == "B" || w == "BLOCCHI" || w == "BLOCKS")
            return purge(ctx, true, false, false, false);
        if (w == "LA" || w == "LAYER" || w == "LAYERS")
            return purge(ctx, false, true, false, false);
        if (w == "TI" || w == "TIPI" || w == "LT" || w == "LTYPES")
            return purge(ctx, false, false, true, false);
        if (w == "S" || w == "STILI" || w == "D" || w == "DIMSTYLES")
            return purge(ctx, false, false, false, true);
        return Command::onKeyword(ctx, w);
    }

private:
    static CommandStatus purge(CommandContext& ctx, bool blocks, bool layers, bool linetypes, bool dims)
    {
        Document& doc = ctx.document;
        std::vector<std::string> all;
        const auto report = [&](const char* what, const std::vector<std::string>& names) {
            if (names.empty())
                return;
            std::string list;
            for (const auto& n : names)
                list += (list.empty() ? "" : ", ") + n;
            ctx.message(std::string(what) + " eliminati: " + list + ".");
            all.insert(all.end(), names.begin(), names.end());
        };
        if (blocks)
            report("Blocchi", doc.purgeBlocks());
        if (layers)
            report("Layer", doc.purgeLayers());
        if (linetypes)
            report("Tipi di linea", doc.purgeLinetypes());
        if (dims)
            report("Stili di quota", doc.purgeDimStyles());
        if (all.empty())
            ctx.message("Nessun elemento inutilizzato da eliminare.");
        return CommandStatus::Finished;
    }
};

// ------------------------------------------------------------ SELECTSIMILAR

class SelectSimilarCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        const Document& doc = ctx.document;
        std::vector<const Entity*> models;
        for (EntityId id : ctx.selection)
            if (const Entity* e = doc.find(id))
                models.push_back(e);
        const auto similar = [&](const Entity& e) {
            for (const Entity* m : models) {
                if (m->type() != e.type() || m->layer() != e.layer())
                    continue;
                if (e.type() == EntityType::Insert
                    && static_cast<const InsertEntity&>(e).blockName != static_cast<const InsertEntity*>(m)->blockName)
                    continue;
                return true;
            }
            return false;
        };
        std::vector<EntityId> result;
        const int space = doc.activeSpace();
        for (const auto& e : doc.entities())
            if (e->space() == space && doc.isSelectable(*e) && similar(*e))
                result.push_back(e->id());
        ctx.selection = result;
        ctx.message(std::to_string(result.size()) + " oggetti simili selezionati.");
        return CommandStatus::Finished;
    }
};


// ------------------------------------------------------------ GROUP / UNGROUP

class GroupCommand final : public SelectingCommand {
public:
    bool wantsText() const override { return naming_; }
    bool textIsLine() const override { return false; }

protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        ids_ = ctx.selection;
        naming_ = true;
        return CommandStatus::Continue;
    }
    std::string actionPrompt() const override { return "Nome del gruppo (Invio = senza nome):"; }

    CommandStatus actionEnter(CommandContext& ctx) override { return create(ctx, {}); }

public:
    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        if (!naming_)
            return Command::onText(ctx, text);
        for (const auto& g : ctx.document.groups())
            if (g.name == text) {
                ctx.message("Esiste già un gruppo \"" + text + "\".");
                return CommandStatus::Continue;
            }
        return create(ctx, text);
    }

private:
    CommandStatus create(CommandContext& ctx, const std::string& name)
    {
        const std::string created = ctx.document.addGroup(ids_, name);
        ctx.selection.clear();
        ctx.message("Gruppo " + (name.empty() ? std::string("senza nome") : "\"" + created + "\"") + " creato con "
                    + std::to_string(ids_.size()) + " oggetti.");
        return CommandStatus::Finished;
    }

    std::vector<EntityId> ids_;
    bool naming_ = false;
};

class UngroupCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        const int n = ctx.document.removeGroupsOf(ctx.selection);
        ctx.selection.clear();
        ctx.message(n == 0 ? "Gli oggetti non appartengono a gruppi." : "Gruppi sciolti: " + std::to_string(n) + ".");
        return CommandStatus::Finished;
    }
};

// ------------------------------------------------------------------- WIPEOUT

class WipeoutCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (asking_ == 1)
            return "Immettere modalità [ON/OFF/Visualizza ma non stampare] <ON>:";
        if (asking_ == 2)
            return "Selezionare una polilinea chiusa:";
        if (points_.empty())
            return "Specificare primo punto o [Cornici/Polilinea] <Polilinea>:";
        return points_.size() < 3 ? "Specificare punto successivo:" : "Specificare punto successivo o [Chiudi]:";
    }
    bool wantsSelection() const override { return asking_ == 2; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (asking_ == 1) {
            if (w == "ON")
                ctx.document.wipeoutFrame = 1;
            else if (w == "OFF")
                ctx.document.wipeoutFrame = 0;
            else if (w == "V" || w == "VISUALIZZA" || w == "D" || w == "DISPLAY")
                ctx.document.wipeoutFrame = 2;
            else
                return Command::onKeyword(ctx, w);
            ctx.document.touch();
            return CommandStatus::Finished;
        }
        if (points_.empty() && (w == "C" || w == "CORNICI" || w == "F" || w == "FRAMES")) {
            asking_ = 1;
            return CommandStatus::Continue;
        }
        if (points_.empty() && (w == "P" || w == "POLILINEA" || w == "POLYLINE")) {
            asking_ = 2;
            return CommandStatus::Continue;
        }
        if (points_.size() >= 3 && (w == "C" || w == "CHIUDI" || w == "CLOSE"))
            return onEnter(ctx);
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (asking_ == 2) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (!e || e->type() != EntityType::Polyline || !static_cast<const PolylineEntity*>(e)->closed) {
                ctx.message("Serve una polilinea chiusa.");
                return CommandStatus::Continue;
            }
            const auto outline = closedOutline(*e);
            auto w = std::make_unique<WipeoutEntity>(outline);
            w->copyStyleFrom(*e);
            std::vector<std::unique_ptr<Entity>> added;
            added.push_back(std::move(w));
            ctx.document.commit(std::move(added), {e->id()});   // la polilinea diventa la copertura
            return CommandStatus::Finished;
        }
        if (asking_ == 1)
            return Command::onPoint(ctx, p);
        points_.push_back(p);
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (asking_ == 1) {
            ctx.document.wipeoutFrame = 1;
            ctx.document.touch();
            return CommandStatus::Finished;
        }
        if (asking_ == 0 && points_.empty()) {
            asking_ = 2;   // opzione predefinita: Polilinea
            return CommandStatus::Continue;
        }
        if (points_.size() < 3) {
            ctx.message("Servono almeno tre punti.");
            return points_.empty() ? CommandStatus::Finished : CommandStatus::Continue;
        }
        ctx.document.add(std::make_unique<WipeoutEntity>(points_));
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (points_.empty())
            return result;
        std::vector<PolyVertex> v;
        for (Vec2 p : points_)
            v.push_back({p, 0.0});
        v.push_back({cursor, 0.0});
        result.push_back(std::make_unique<PolylineEntity>(std::move(v), points_.size() >= 2));
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    int asking_ = 0;   // 1 = cornici, 2 = polilinea
    std::vector<Vec2> points_;
};

// --------------------------------------------------------------------- MLINE

int gMlineJust = MlineEntity::Top;
double gMlineScale = 20.0;

class MlineCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (asking_ == 1)
            return "Immettere tipo di giustificazione [Superiore/Zero/Inferiore] <" + justName() + ">:";
        if (asking_ == 2)
            return "Immettere scala multilinea <" + fmtNumber(gMlineScale) + ">:";
        if (points_.empty())
            return "Impostazioni correnti: giustificazione = " + justName() + ", scala = " + fmtNumber(gMlineScale)
                + ", stile = STANDARD. Specificare punto iniziale o [Giustificazione/Scala]:";
        return points_.size() < 3 ? "Specificare punto successivo o [Annulla]:"
                                  : "Specificare punto successivo o [Chiudi/Annulla]:";
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (asking_ == 1) {
            if (w == "S" || w == "SUPERIORE" || w == "T" || w == "TOP")
                gMlineJust = MlineEntity::Top;
            else if (w == "Z" || w == "ZERO")
                gMlineJust = MlineEntity::Zero;
            else if (w == "I" || w == "INFERIORE" || w == "B" || w == "BOTTOM")
                gMlineJust = MlineEntity::Bottom;
            else
                return Command::onKeyword(ctx, w);
            asking_ = 0;
            return CommandStatus::Continue;
        }
        if (points_.empty() && (w == "G" || w == "GIUSTIFICAZIONE" || w == "J" || w == "JUSTIFICATION")) {
            asking_ = 1;
            return CommandStatus::Continue;
        }
        if (points_.empty() && (w == "S" || w == "SCALA" || w == "SCALE")) {
            asking_ = 2;
            return CommandStatus::Continue;
        }
        if (!points_.empty() && (w == "A" || w == "ANNULLA" || w == "U" || w == "UNDO")) {
            points_.pop_back();
            return CommandStatus::Continue;
        }
        if (points_.size() >= 3 && (w == "C" || w == "CHIUDI" || w == "CLOSE"))
            return create(ctx, true);
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (asking_ == 2 && v != 0.0) {
            gMlineScale = v;
            asking_ = 0;
            return CommandStatus::Continue;
        }
        return Command::onValue(ctx, v);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (asking_ != 0)
            return Command::onPoint(ctx, p);
        points_.push_back(p);
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (asking_ != 0) {
            asking_ = 0;
            return CommandStatus::Continue;
        }
        if (points_.size() < 2)
            return CommandStatus::Finished;
        return create(ctx, false);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (points_.empty() || distance(points_.back(), cursor) <= 0.0)
            return result;
        auto m = make(false);
        m->vertices.push_back(cursor);
        result.push_back(std::move(m));
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    static std::string justName()
    {
        return gMlineJust == MlineEntity::Top ? "Superiore" : gMlineJust == MlineEntity::Zero ? "Zero" : "Inferiore";
    }

    // Stile STANDARD: due elementi a +0,5 e -0,5, spostati secondo la giustificazione.
    std::unique_ptr<MlineEntity> make(bool closed) const
    {
        auto m = std::make_unique<MlineEntity>();
        m->vertices = points_;
        m->closed = closed;
        m->scale = gMlineScale;
        m->justification = gMlineJust;
        const double shift = gMlineJust == MlineEntity::Top ? -0.5 : gMlineJust == MlineEntity::Bottom ? 0.5 : 0.0;
        m->offsets = {(0.5 + shift) * gMlineScale, (-0.5 + shift) * gMlineScale};
        return m;
    }

    CommandStatus create(CommandContext& ctx, bool closed)
    {
        ctx.document.add(make(closed));
        return CommandStatus::Finished;
    }

    int asking_ = 0;   // 1 = giustificazione, 2 = scala
    std::vector<Vec2> points_;
};

// --------------------------------------------------------------------- TABLE

int gTableColumns = 5;
int gTableRows = 3;
double gTableColumnWidth = 25.0;

class TableCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (step_) {
        case 0: return "Numero di colonne <" + std::to_string(gTableColumns) + ">:";
        case 1: return "Numero di righe <" + std::to_string(gTableRows) + ">:";
        case 2: return "Larghezza delle colonne <" + fmtNumber(gTableColumnWidth) + ">:";
        default: return "Specificare punto di inserimento (angolo in alto a sinistra):";
        }
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (step_ == 0 && v >= 1 && v <= 100)
            gTableColumns = static_cast<int>(v);
        else if (step_ == 1 && v >= 1 && v <= 1000)
            gTableRows = static_cast<int>(v);
        else if (step_ == 2 && v > 0.0)
            gTableColumnWidth = v;
        else
            return Command::onValue(ctx, v);
        ++step_;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (step_ < 3) {
            ++step_;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (step_ < 3)
            step_ = 3;   // un punto subito: valori predefiniti
        auto table = make(p, ctx.variable("TEXTSIZE"));
        ctx.document.add(std::move(table));
        ctx.message("Tabella inserita: TABLEDIT (o doppio clic) per scrivere nelle celle.");
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (step_ >= 3)
            result.push_back(make(cursor, {}));
        return result;
    }

private:
    static std::unique_ptr<TableEntity> make(Vec2 p, const std::string& textSize)
    {
        auto t = std::make_unique<TableEntity>();
        t->position = p;
        double h = 2.5;
        try {
            if (!textSize.empty())
                h = std::stod(textSize);
        } catch (...) {
        }
        t->textHeight = h;
        t->resize(gTableRows, gTableColumns, gTableColumnWidth, h * 3.0);
        return t;
    }

    int step_ = 0;
};

// TABLEDIT: clic in una cella e testo (Invio a vuoto cancella il contenuto).
class TableEditCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!table_)
            return "Selezionare una cella della tabella:";
        return "Testo della cella (riga " + std::to_string(row_ + 1) + ", colonna " + std::to_string(col_ + 1) + ") <"
            + current_ + ">:";
    }
    bool wantsSelection() const override { return !table_; }
    bool wantsText() const override { return table_.has_value(); }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        for (const Entity* e : ctx.document.entitiesNear(p, 0.0)) {
            if (e->type() != EntityType::Table)
                continue;
            const auto& t = static_cast<const TableEntity&>(*e);
            const auto [r, c] = t.cellAt(p);
            if (r < 0)
                continue;
            table_ = e->id();
            row_ = r;
            col_ = c;
            current_ = t.cell(r, c);
            return CommandStatus::Continue;
        }
        ctx.message("Nessuna cella in quel punto.");
        return CommandStatus::Continue;
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override { return set(ctx, text); }
    CommandStatus onEnter(CommandContext& ctx) override
    {
        return table_ ? set(ctx, current_) : CommandStatus::Finished;
    }

private:
    CommandStatus set(CommandContext& ctx, const std::string& text)
    {
        const Entity* e = ctx.document.find(*table_);
        if (!e)
            return CommandStatus::Finished;
        auto copy = e->clone();
        static_cast<TableEntity&>(*copy).cell(row_, col_) = text;
        std::vector<std::unique_ptr<Entity>> modified;
        modified.push_back(std::move(copy));
        ctx.document.commit(std::move(modified), {*table_});
        // Si passa alla cella successiva (come il tasto Tab).
        const auto& t = static_cast<const TableEntity&>(*ctx.document.find(*table_));
        if (++col_ >= t.columns) {
            col_ = 0;
            if (++row_ >= t.rows)
                return CommandStatus::Finished;
        }
        current_ = t.cell(row_, col_);
        return CommandStatus::Continue;
    }

    std::optional<EntityId> table_;
    int row_ = 0;
    int col_ = 0;
    std::string current_;
};
} // namespace

namespace {

// SPLINE: curva che passa per i punti indicati (B-spline cubica, come la vogliono i DXF).
class SplineCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (points_.empty())
            return "Specificare primo punto:";
        if (points_.size() < 3)
            return "Specificare punto successivo o [Annulla]:";
        return "Specificare punto successivo o [Chiudi/Annulla] <fine>:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!points_.empty() && distance(points_.back(), p) < 1e-9)
            return CommandStatus::Continue;
        points_.push_back(p);
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if ((w == "A" || w == "ANNULLA" || w == "U" || w == "UNDO") && !points_.empty()) {
            points_.pop_back();
            if (!points_.empty())
                ctx.lastPoint = points_.back();
            return CommandStatus::Continue;
        }
        if ((w == "C" || w == "CHIUDI" || w == "CLOSE") && points_.size() >= 3)
            return finish(ctx, true);
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (points_.size() < 2)
            return CommandStatus::Finished;
        return finish(ctx, false);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (points_.empty())
            return out;
        auto s = std::make_unique<SplineEntity>();
        s->fit = points_;
        if (distance(points_.back(), cursor) > 1e-9)
            s->fit.push_back(cursor);
        if (interpolateSpline(*s))
            out.push_back(std::move(s));
        return out;
    }

    std::optional<Vec2> basePoint() const override
    {
        return points_.empty() ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    CommandStatus finish(CommandContext& ctx, bool closed)
    {
        auto s = std::make_unique<SplineEntity>();
        s->fit = points_;
        s->closed = closed;
        if (!interpolateSpline(*s)) {
            ctx.message("Punti non validi per una spline.");
            return CommandStatus::Finished;
        }
        ctx.document.add(std::move(s));
        return CommandStatus::Finished;
    }

    std::vector<Vec2> points_;
};

// AUDIT: controlla il disegno e, se richiesto, corregge gli errori.
class AuditCommand final : public Command {
public:
    std::string prompt() const override { return "Correggere gli errori rilevati? [Sì/No] <N>:"; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (w == "S" || w == "SI" || w == "SÌ" || w == "Y" || w == "YES")
            return run(ctx, true);
        if (w == "N" || w == "NO")
            return run(ctx, false);
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onEnter(CommandContext& ctx) override { return run(ctx, false); }

private:
    CommandStatus run(CommandContext& ctx, bool fix)
    {
        const auto report = ctx.document.audit(fix);
        const std::size_t shown = std::min<std::size_t>(report.lines.size(), 200);
        for (std::size_t i = 0; i < shown; ++i)
            ctx.message(report.lines[i]);
        if (report.lines.size() > shown)
            ctx.message("... e altri " + std::to_string(report.lines.size() - shown) + ".");
        if (report.found == 0)
            ctx.message("Controllo completato: nessun errore trovato.");
        else if (fix)
            ctx.message("Controllo completato: " + std::to_string(report.found) + " errori trovati, "
                        + std::to_string(report.fixed) + " corretti.");
        else
            ctx.message("Controllo completato: " + std::to_string(report.found)
                        + " errori trovati (AUDIT e Sì per correggerli).");
        return CommandStatus::Finished;
    }
};

} // namespace

void registerExtraCommands(CommandManager& manager)
{
    manager.add("SPLINE", {"SPL"}, [] { return std::make_unique<SplineCommand>(); });
    manager.add("AUDIT", {"CONTROLLA", "VERIFICA"}, [] { return std::make_unique<AuditCommand>(); });
    manager.add("POLYGON", {"POL", "POLIGONO"}, [] { return std::make_unique<PolygonCommand>(); });
    manager.add("REVCLOUD", {"NUVOLA", "NUVOLAREV"}, [] { return std::make_unique<RevcloudCommand>(); });
    manager.add("XLINE", {"XL", "COSTRUZIONE"}, [] { return std::make_unique<XlineCommand>(false); });
    manager.add("RAY", {"SEMIRETTA"}, [] { return std::make_unique<XlineCommand>(true); });
    manager.add("DIVIDE", {"DIV", "DIVIDI"}, [] { return std::make_unique<DivideCommand>(false); });
    manager.add("MEASURE", {"ME", "MISURA"}, [] { return std::make_unique<DivideCommand>(true); });
    manager.add("LENGTHEN", {"LEN", "ALLUNGA"}, [] { return std::make_unique<LengthenCommand>(); });
    manager.add("ALIGN", {"AL", "ALLINEA"}, [] { return std::make_unique<AlignCommand>(); });
    manager.add("OVERKILL", {"DOPPIONI"}, [] { return std::make_unique<OverkillCommand>(); });
    manager.add("PURGE", {"PU", "-PURGE", "ELIMINAINUTILIZZATI"}, [] { return std::make_unique<PurgeCommand>(); });
    manager.add("GROUP", {"G", "GRUPPO"}, [] { return std::make_unique<GroupCommand>(); });
    manager.add("UNGROUP", {"SCIOGLIGRUPPO"}, [] { return std::make_unique<UngroupCommand>(); });
    manager.add("WIPEOUT", {"COPERTURA"}, [] { return std::make_unique<WipeoutCommand>(); });
    manager.add("MLINE", {"ML", "MULTILINEA"}, [] { return std::make_unique<MlineCommand>(); });
    manager.add("TABLE", {"TB", "TABELLA"}, [] { return std::make_unique<TableCommand>(); });
    manager.add("TABLEDIT", {"MODIFICATABELLA"}, [] { return std::make_unique<TableEditCommand>(); });
    manager.add("SELECTSIMILAR", {"SIMILI"}, [] { return std::make_unique<SelectSimilarCommand>(); });
}

} // namespace cartesio
