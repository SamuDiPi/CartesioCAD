// Altri comandi di modifica e di interrogazione: ROTATE, SCALE, MIRROR, ARRAY, EXTEND, FILLET,
// MATCHPROP, DIST, AREA, ID, LIST, COPYCLIP, PASTECLIP e la modifica con le maniglie.
#include "core/commands/CommandManager.h"

#include "core/Geometry.h"
#include "core/Import.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace cartesio {

namespace {

std::string fmt(double v, int decimals = 4)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(decimals) << v;
    return out.str();
}

std::string pointText(Vec2 p)
{
    return "X = " + fmt(p.x) + "  Y = " + fmt(p.y) + "  Z = " + fmt(0.0);
}

// ------------------------------------------------- comandi con trasformazione

// Base comune per ROTATE, SCALE, MIRROR: selezione, poi punti, con anteprima delle copie trasformate.
class TransformCommand : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        ids_ = ctx.selection;
        for (EntityId id : ids_)
            if (const Entity* e = ctx.document.find(id))
                originals_.push_back(e->clone());
        return CommandStatus::Continue;
    }

    void cancel(CommandContext& ctx) override
    {
        SelectingCommand::cancel(ctx);
    }

    std::vector<std::unique_ptr<Entity>> transformed(const Affine& t, bool readable = false) const
    {
        std::vector<std::unique_ptr<Entity>> result;
        for (const auto& e : originals_) {
            auto copy = e->clone();
            copy->transform(t);
            if (readable)
                keepTextReadable(*copy);
            result.push_back(std::move(copy));
        }
        return result;
    }

    // Sostituisce gli originali con le copie trasformate (o le aggiunge, se keepOriginals).
    CommandStatus apply(CommandContext& ctx, const Affine& t, bool keepOriginals = false, bool readable = false)
    {
        auto result = transformed(t, readable);
        if (keepOriginals)
            for (auto& e : result)
                e->setId(0);
        ctx.document.commit(std::move(result), keepOriginals ? std::vector<EntityId>{} : ids_);
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

    std::vector<EntityId> ids_;
    std::vector<std::unique_ptr<Entity>> originals_;
};

class RotateCommand final : public TransformCommand {
protected:
    std::string actionPrompt() const override
    {
        return base_ ? "Specificare angolo di rotazione:" : "Specificare punto base:";
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!base_) {
            base_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        return apply(ctx, Affine::rotation(*base_, angleOf(p - *base_)));
    }

    CommandStatus actionValue(CommandContext& ctx, double v) override
    {
        if (!base_)
            return Command::onValue(ctx, v);
        return apply(ctx, Affine::rotation(*base_, degToRad(v)));
    }

    std::vector<std::unique_ptr<Entity>> actionPreview(Vec2 cursor) const override
    {
        if (!base_)
            return {};
        auto result = transformed(Affine::rotation(*base_, angleOf(cursor - *base_)));
        result.push_back(std::make_unique<LineEntity>(*base_, cursor));
        return result;
    }

    std::optional<Vec2> actionBasePoint() const override { return base_; }

private:
    std::optional<Vec2> base_;
};

class ScaleCommand final : public TransformCommand {
protected:
    std::string actionPrompt() const override
    {
        return base_ ? "Specificare fattore di scala:" : "Specificare punto base:";
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!base_) {
            base_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        return actionValue(ctx, distance(*base_, p));
    }

    CommandStatus actionValue(CommandContext& ctx, double v) override
    {
        if (!base_)
            return Command::onValue(ctx, v);
        if (v <= 0.0) {
            ctx.message("Il fattore di scala deve essere maggiore di zero.");
            return CommandStatus::Continue;
        }
        return apply(ctx, Affine::scaling(*base_, v));
    }

    std::vector<std::unique_ptr<Entity>> actionPreview(Vec2 cursor) const override
    {
        if (!base_ || distance(*base_, cursor) <= 0.0)
            return {};
        auto result = transformed(Affine::scaling(*base_, distance(*base_, cursor)));
        result.push_back(std::make_unique<LineEntity>(*base_, cursor));
        return result;
    }

    std::optional<Vec2> actionBasePoint() const override { return base_; }

private:
    std::optional<Vec2> base_;
};

class MirrorCommand final : public TransformCommand {
protected:
    std::string actionPrompt() const override
    {
        if (!first_)
            return "Specificare primo punto della linea di specchiatura:";
        if (!second_)
            return "Specificare secondo punto della linea di specchiatura:";
        return "Cancellare gli oggetti di origine? [Sì/No] <No>:";
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            ctx.lastPoint = p;
        } else if (!second_ && distance(*first_, p) > 0.0) {
            second_ = p;
        }
        return CommandStatus::Continue;
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!second_)
            return Command::onKeyword(ctx, w);
        const bool erase = w == "S" || w == "SI" || w == "SÌ" || w == "Y" || w == "YES";
        return apply(ctx, Affine::mirror(*first_, *second_), !erase, true);
    }

    CommandStatus actionEnter(CommandContext& ctx) override
    {
        if (!second_)
            return CommandStatus::Finished;
        return apply(ctx, Affine::mirror(*first_, *second_), true, true);
    }

    std::vector<std::unique_ptr<Entity>> actionPreview(Vec2 cursor) const override
    {
        const Vec2 b = second_.value_or(cursor);
        if (!first_ || distance(*first_, b) <= 0.0)
            return {};
        auto result = transformed(Affine::mirror(*first_, b), true);
        result.push_back(std::make_unique<LineEntity>(*first_, b));
        return result;
    }

    std::optional<Vec2> actionBasePoint() const override { return second_ ? std::nullopt : first_; }

private:
    std::optional<Vec2> first_;
    std::optional<Vec2> second_;
};

// ------------------------------------------------------------------ ARRAY

class ArrayCommand final : public TransformCommand {
protected:
    std::string actionPrompt() const override
    {
        switch (stage_) {
        case Stage::Type: return "Tipo di serie [Rettangolare/Polare] <Rettangolare>:";
        case Stage::Rows: return "Numero di righe <" + std::to_string(rows_) + ">:";
        case Stage::Columns: return "Numero di colonne <" + std::to_string(columns_) + ">:";
        case Stage::RowGap: return "Distanza tra le righe (anche negativa):";
        case Stage::ColumnGap: return "Distanza tra le colonne (anche negativa):";
        case Stage::Center: return "Specificare centro della serie:";
        case Stage::Count: return "Numero di elementi <" + std::to_string(count_) + ">:";
        case Stage::Angle: return "Angolo da riempire (+ antiorario, - orario) <360>:";
        }
        return {};
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (stage_ != Stage::Type)
            return Command::onKeyword(ctx, w);
        if (w == "R" || w == "RETTANGOLARE" || w == "RECTANGULAR")
            stage_ = Stage::Rows;
        else if (w == "P" || w == "PO" || w == "POLARE" || w == "POLAR")
            stage_ = Stage::Center;
        else
            return Command::onKeyword(ctx, w);
        return CommandStatus::Continue;
    }

    CommandStatus actionEnter(CommandContext& ctx) override
    {
        switch (stage_) {
        case Stage::Type: stage_ = Stage::Rows; break;
        case Stage::Rows: stage_ = Stage::Columns; break;
        case Stage::Columns: stage_ = rows_ > 1 ? Stage::RowGap : Stage::ColumnGap; break;
        case Stage::Count: stage_ = Stage::Angle; break;
        case Stage::Angle: return polar(ctx, 360.0);
        default: return CommandStatus::Finished;
        }
        if (stage_ == Stage::ColumnGap && columns_ <= 1)
            return rectangular(ctx);
        return CommandStatus::Continue;
    }

    CommandStatus actionValue(CommandContext& ctx, double v) override
    {
        switch (stage_) {
        case Stage::Rows:
            rows_ = std::max(1, static_cast<int>(v));
            stage_ = Stage::Columns;
            return CommandStatus::Continue;
        case Stage::Columns:
            columns_ = std::max(1, static_cast<int>(v));
            if (rows_ > 1) {
                stage_ = Stage::RowGap;
                return CommandStatus::Continue;
            }
            if (columns_ > 1) {
                stage_ = Stage::ColumnGap;
                return CommandStatus::Continue;
            }
            return rectangular(ctx);
        case Stage::RowGap:
            rowGap_ = v;
            if (columns_ > 1) {
                stage_ = Stage::ColumnGap;
                return CommandStatus::Continue;
            }
            return rectangular(ctx);
        case Stage::ColumnGap:
            columnGap_ = v;
            return rectangular(ctx);
        case Stage::Count:
            count_ = std::max(2, static_cast<int>(v));
            stage_ = Stage::Angle;
            return CommandStatus::Continue;
        case Stage::Angle:
            return polar(ctx, v);
        default:
            return Command::onValue(ctx, v);
        }
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (stage_ == Stage::Center) {
            center_ = p;
            ctx.lastPoint = p;
            stage_ = Stage::Count;
            return CommandStatus::Continue;
        }
        return Command::onPoint(ctx, p);
    }

private:
    enum class Stage { Type, Rows, Columns, RowGap, ColumnGap, Center, Count, Angle };

    CommandStatus add(CommandContext& ctx, const std::vector<Affine>& placements)
    {
        std::vector<std::unique_ptr<Entity>> copies;
        for (const Affine& t : placements)
            for (auto& e : transformed(t)) {
                e->setId(0);
                copies.push_back(std::move(e));
            }
        ctx.message(std::to_string(placements.size()) + " copie create.");
        ctx.document.commit(std::move(copies));
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

    CommandStatus rectangular(CommandContext& ctx)
    {
        std::vector<Affine> placements;
        for (int r = 0; r < rows_; ++r)
            for (int c = 0; c < columns_; ++c)
                if (r != 0 || c != 0)
                    placements.push_back(Affine::translation({c * columnGap_, r * rowGap_}));
        return add(ctx, placements);
    }

    CommandStatus polar(CommandContext& ctx, double fillDeg)
    {
        std::vector<Affine> placements;
        // Con 360° gli elementi si distribuiscono su tutto il giro (l'ultimo non coincide col primo).
        const bool full = std::abs(std::abs(fillDeg) - 360.0) < 1e-9;
        const double step = degToRad(fillDeg) / (full ? count_ : count_ - 1);
        for (int i = 1; i < count_; ++i)
            placements.push_back(Affine::rotation(center_, step * i));
        return add(ctx, placements);
    }

    Stage stage_ = Stage::Type;
    int rows_ = 3;
    int columns_ = 4;
    double rowGap_ = 10.0;
    double columnGap_ = 10.0;
    Vec2 center_;
    int count_ = 6;
};

// ------------------------------------------------------- EXTEND e FILLET

class ExtendCommand final : public Command {
public:
    std::string prompt() const override { return "Selezionare oggetto da estendere (Invio per terminare):"; }
    bool wantsSelection() const override { return true; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* target = ctx.document.pick(p, ctx.pickTolerance);
        if (!target) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }
        if (target->type() != EntityType::Line && target->type() != EntityType::Arc) {
            ctx.message("EXTEND funziona con linee e archi.");
            return CommandStatus::Continue;
        }
        std::vector<const Entity*> boundaries;
        for (const auto& e : ctx.document.entities())
            if (e->id() != target->id() && e->space() == target->space() && ctx.document.isShown(*e))
                boundaries.push_back(e.get());
        auto result = extendEntity(*target, boundaries, p);
        if (!result) {
            ctx.message("L'oggetto non incontra nessun contorno.");
            return CommandStatus::Continue;
        }
        const EntityId id = target->id();
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(result));
        ctx.document.commit(std::move(added), {id});
        return CommandStatus::Continue;
    }
};

double gFilletRadius = 0.0;

class FilletCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (askingRadius_)
            return "Specificare raggio di raccordo <" + fmt(gFilletRadius) + ">:";
        if (!first_)
            return "Selezionare prima linea o [Raggio] (raggio = " + fmt(gFilletRadius) + "):";
        return "Selezionare seconda linea:";
    }
    bool wantsSelection() const override { return !askingRadius_; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!first_ && (w == "R" || w == "RAGGIO" || w == "RADIUS")) {
            askingRadius_ = true;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!askingRadius_)
            return Command::onValue(ctx, v);
        if (v < 0.0) {
            ctx.message("Il raggio non può essere negativo.");
            return CommandStatus::Continue;
        }
        gFilletRadius = v;
        askingRadius_ = false;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (askingRadius_) {
            askingRadius_ = false;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e || e->type() != EntityType::Line) {
            ctx.message("Selezionare una linea.");
            return CommandStatus::Continue;
        }
        if (!first_) {
            first_ = e->id();
            firstPick_ = p;
            ctx.selection = {e->id()};
            return CommandStatus::Continue;
        }
        if (e->id() == *first_) {
            ctx.message("Selezionare un'altra linea.");
            return CommandStatus::Continue;
        }
        const auto* a = static_cast<const LineEntity*>(ctx.document.find(*first_));
        const auto& b = static_cast<const LineEntity&>(*e);
        auto result = a ? filletLines(*a, firstPick_, b, p, gFilletRadius) : std::nullopt;
        ctx.selection.clear();
        if (!result) {
            ctx.message("Impossibile raccordare: linee parallele o raggio troppo grande.");
            return CommandStatus::Finished;
        }
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(result->first));
        added.push_back(std::move(result->second));
        if (result->arc)
            added.push_back(std::move(result->arc));
        ctx.document.commit(std::move(added), {*first_, b.id()});
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

private:
    bool askingRadius_ = false;
    std::optional<EntityId> first_;
    Vec2 firstPick_;
};

// --------------------------------------------------------------- MATCHPROP

class MatchPropCommand final : public Command {
public:
    std::string prompt() const override
    {
        return source_ ? "Selezionare oggetti di destinazione (Invio per terminare):" : "Selezionare oggetto di origine:";
    }
    bool wantsSelection() const override { return true; }
    std::optional<Vec2> selectionCorner() const override { return source_ ? picker_.corner() : std::nullopt; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!source_) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (!e) {
                ctx.message("Nessun oggetto trovato.");
                return CommandStatus::Continue;
            }
            source_ = e->id();
            layer_ = e->layer();
            color_ = e->color();
            linetype_ = e->linetype();
            lineweight_ = e->lineweight();
            ltscale_ = e->linetypeScale();
            trueColor_ = e->trueColor();
            transparency_ = e->transparency();
            return CommandStatus::Continue;
        }
        // Gli oggetti trovati prendono subito layer e colore dell'origine.
        std::vector<EntityId> before = ctx.selection;
        ctx.selection.clear();
        picker_.click(ctx, p);
        std::vector<EntityId> targets = ctx.selection;
        ctx.selection = before;
        std::erase(targets, *source_);
        if (!targets.empty()) {
            std::vector<std::unique_ptr<Entity>> changed;
            for (EntityId id : targets)
                if (const Entity* e = ctx.document.find(id)) {
                    auto copy = e->clone();
                    copy->setLayer(layer_);
                    copy->setColor(color_);
                    copy->setLinetype(linetype_);
                    copy->setLineweight(lineweight_);
                    copy->setLinetypeScale(ltscale_);
                    copy->setTrueColor(trueColor_);
                    copy->setTransparency(transparency_);
                    changed.push_back(std::move(copy));
                }
            ctx.document.commit(std::move(changed), targets);
        }
        return CommandStatus::Continue;
    }

private:
    std::optional<EntityId> source_;
    int layer_ = 0;
    int color_ = kColorByLayer;
    std::string linetype_ = "ByLayer";
    int lineweight_ = kLineweightByLayer;
    double ltscale_ = 1.0;
    int trueColor_ = -1;
    int transparency_ = -1;
    SelectionPicker picker_;
};

// --------------------------------------------------------- interrogazione

class DistCommand final : public Command {
public:
    std::string prompt() const override { return first_ ? "Specificare secondo punto:" : "Specificare primo punto:"; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        const Vec2 d = p - *first_;
        ctx.message("Distanza = " + fmt(d.length()) + ",  Angolo nel piano XY = " +
                    fmt(normalizeAngle(angleOf(d)) * 180.0 / kPi, 2) + "°");
        ctx.message("Delta X = " + fmt(d.x) + ",  Delta Y = " + fmt(d.y));
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (first_)
            result.push_back(std::make_unique<LineEntity>(*first_, cursor));
        return result;
    }
    std::optional<Vec2> basePoint() const override { return first_; }

private:
    std::optional<Vec2> first_;
};

class IdCommand final : public Command {
public:
    std::string prompt() const override { return "Specificare punto:"; }
    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.message(pointText(p));
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }
};

class AreaCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (objectMode_)
            return "Selezionare oggetto:";
        return points_.empty() ? "Specificare primo punto d'angolo o [Oggetto]:"
                               : "Specificare punto successivo o Invio per il totale:";
    }
    bool wantsSelection() const override { return objectMode_; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (points_.empty() && (w == "O" || w == "OGGETTO" || w == "OBJECT")) {
            objectMode_ = true;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!objectMode_) {
            points_.push_back(p);
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }
        double area = 0.0;
        double perimeter = 0.0;
        switch (e->type()) {
        case EntityType::Circle: {
            const double r = static_cast<const CircleEntity&>(*e).radius;
            area = kPi * r * r;
            perimeter = 2.0 * kPi * r;
            break;
        }
        case EntityType::Polyline: {
            const auto& pl = static_cast<const PolylineEntity&>(*e);
            area = pl.area();
            perimeter = pl.length();
            break;
        }
        case EntityType::Ellipse: {
            const auto& el = static_cast<const EllipseEntity&>(*e);
            const double a = el.majorAxis.length();
            const double b = a * el.ratio;
            area = kPi * a * b;
            perimeter = kPi * (3.0 * (a + b) - std::sqrt((3.0 * a + b) * (a + 3.0 * b)));   // Ramanujan
            break;
        }
        default:
            ctx.message("L'oggetto deve essere un cerchio, un'ellisse o una polilinea.");
            return CommandStatus::Continue;
        }
        ctx.message("Area = " + fmt(area) + ",  " + (e->type() == EntityType::Circle ? "Circonferenza" : "Perimetro") +
                    " = " + fmt(perimeter));
        return CommandStatus::Finished;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (points_.size() >= 3) {
            double twice = 0.0;
            double perimeter = 0.0;
            for (std::size_t i = 0; i < points_.size(); ++i) {
                const Vec2 a = points_[i];
                const Vec2 b = points_[(i + 1) % points_.size()];
                twice += cross(a, b);
                perimeter += distance(a, b);
            }
            ctx.message("Area = " + fmt(std::abs(twice) / 2.0) + ",  Perimetro = " + fmt(perimeter));
        } else if (!points_.empty()) {
            ctx.message("Servono almeno tre punti.");
        }
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
        if (points_.empty())
            return std::nullopt;
        return points_.back();
    }

private:
    std::vector<Vec2> points_;
    bool objectMode_ = false;
};

std::string typeName(EntityType t)
{
    switch (t) {
    case EntityType::Line: return "LINEA";
    case EntityType::Circle: return "CERCHIO";
    case EntityType::Arc: return "ARCO";
    case EntityType::Point: return "PUNTO";
    case EntityType::Polyline: return "POLILINEA";
    case EntityType::Ellipse: return "ELLISSE";
    case EntityType::Spline: return "SPLINE";
    case EntityType::Text: return "TESTO";
    case EntityType::MText: return "TESTO MULTILINEA";
    case EntityType::Solid: return "SOLIDO";
    case EntityType::Hatch: return "TRATTEGGIO";
    case EntityType::Insert: return "RIFERIMENTO DI BLOCCO";
    case EntityType::Dimension: return "QUOTA";
    case EntityType::AttDef: return "DEFINIZIONE DI ATTRIBUTO";
    case EntityType::Viewport: return "FINESTRA";
    case EntityType::Leader: return "DIRETTRICE";
    case EntityType::XLine: return "LINEA DI COSTRUZIONE";
    case EntityType::Wipeout: return "COPERTURA";
    case EntityType::Image: return "IMMAGINE RASTER";
    case EntityType::Mline: return "MULTILINEA";
    case EntityType::Table: return "TABELLA";
    }
    return {};
}

class ListCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        const auto& layers = ctx.document.layers();
        for (EntityId id : ctx.selection) {
            const Entity* e = ctx.document.find(id);
            if (!e)
                continue;
            const int li = e->layer();
            const std::string layer = li >= 0 && li < static_cast<int>(layers.size()) ? layers[static_cast<std::size_t>(li)].name : "0";
            const int c = e->color();
            const std::string color = c == kColorByLayer ? "DALAYER" : c == kColorByBlock ? "DABLOCCO" : std::to_string(c);
            ctx.message(typeName(e->type()) + "   Layer: \"" + layer + "\"   Colore: " + color +
                        "   Spazio: " + (e->space() > 0 ? "carta" : "modello"));
            switch (e->type()) {
            case EntityType::Line: {
                const auto& l = static_cast<const LineEntity&>(*e);
                ctx.message("   da " + pointText(l.start));
                ctx.message("   a  " + pointText(l.end));
                ctx.message("   Lunghezza = " + fmt(distance(l.start, l.end)) + ",  Angolo = " +
                            fmt(normalizeAngle(angleOf(l.end - l.start)) * 180.0 / kPi, 2) + "°");
                break;
            }
            case EntityType::Circle: {
                const auto& ci = static_cast<const CircleEntity&>(*e);
                ctx.message("   centro " + pointText(ci.center));
                ctx.message("   Raggio = " + fmt(ci.radius) + ",  Area = " + fmt(kPi * ci.radius * ci.radius));
                break;
            }
            case EntityType::Arc: {
                const auto& a = static_cast<const ArcEntity&>(*e);
                ctx.message("   centro " + pointText(a.center) + ",  Raggio = " + fmt(a.radius));
                ctx.message("   Angolo iniziale = " + fmt(radToDeg(normalizeAngle(a.startAngle)), 2) +
                            "°,  finale = " + fmt(radToDeg(normalizeAngle(a.endAngle)), 2) + "°");
                break;
            }
            case EntityType::Polyline: {
                const auto& p = static_cast<const PolylineEntity&>(*e);
                ctx.message("   Vertici = " + std::to_string(p.vertices.size()) + (p.closed ? ", chiusa" : ", aperta") +
                            ",  Lunghezza = " + fmt(p.length()) + (p.closed ? ",  Area = " + fmt(p.area()) : ""));
                break;
            }
            case EntityType::Insert: {
                const auto& in = static_cast<const InsertEntity&>(*e);
                ctx.message("   Blocco \"" + in.blockName + "\" in " + pointText(in.position));
                ctx.message("   Scala X = " + fmt(in.scaleX) + ",  Y = " + fmt(in.scaleY) + ",  Rotazione = " +
                            fmt(radToDeg(in.rotation), 2) + "°");
                for (const Attribute& a : in.attributes)
                    ctx.message("   Attributo " + a.tag + " = " + a.text.text);
                break;
            }
            case EntityType::Text:
                ctx.message("   \"" + static_cast<const TextEntity&>(*e).text + "\"");
                break;
            default: {
                const BBox b = e->bounds();
                if (b.isValid())
                    ctx.message("   Ingombro da " + pointText(b.min) + " a " + pointText(b.max));
                break;
            }
            }
        }
        return CommandStatus::Finished;
    }
};

// ------------------------------------------------------------ appunti

struct ClipboardData {
    std::vector<std::unique_ptr<Entity>> entities;
    ImportSource source;   // layer, tipi di linea, stili e blocchi del disegno di origine
    Vec2 base;
};

ClipboardData& clipboard()
{
    static ClipboardData data;
    return data;
}

// Copia la selezione negli appunti; senza punto base si usa l'angolo in basso a sinistra dell'ingombro.
void copyToClipboard(CommandContext& ctx, std::optional<Vec2> base)
{
    ClipboardData& clip = clipboard();
    clip = ClipboardData{};
    clip.source = ImportSource::from(ctx.document);
    BBox box;
    for (EntityId id : ctx.selection)
        if (const Entity* e = ctx.document.find(id)) {
            clip.entities.push_back(e->clone());
            box.expand(e->bounds());
        }
    clip.base = base ? *base : box.isValid() ? box.min : Vec2{};
    ctx.message(std::to_string(clip.entities.size()) + " oggetti copiati negli appunti.");
}

class CopyClipCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        copyToClipboard(ctx, std::nullopt);
        return CommandStatus::Finished;
    }
};

// CUTCLIP (Ctrl+X): negli appunti e poi cancellati.
class CutClipCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        copyToClipboard(ctx, std::nullopt);
        ctx.document.erase(ctx.selection);
        ctx.selection.clear();
        return CommandStatus::Finished;
    }
};

// Incolla gli appunti con il punto base in `at` (layer, tipi di linea, stili e blocchi si ritrovano per nome).
void pasteClipboard(CommandContext& ctx, Vec2 at)
{
    ClipboardData& clip = clipboard();
    Document& doc = ctx.document;
    Importer importer(doc, clip.source);
    std::vector<std::unique_ptr<Entity>> added;
    for (const auto& e : clip.entities) {
        auto c = importer.copy(*e);
        c->setSpace(-1);
        c->translate(at - clip.base);
        added.push_back(std::move(c));
    }
    doc.commit(std::move(added));
    doc.linkBlocks();
    ctx.lastPoint = at;
}

bool clipboardEmpty(CommandContext& ctx)
{
    if (!clipboard().entities.empty())
        return false;
    ctx.message("Gli appunti sono vuoti: copiare prima degli oggetti (Ctrl+C).");
    return true;
}

// PASTEORIG: incolla nella stessa posizione del disegno da cui si è copiato.
class PasteOrigCommand final : public Command {
public:
    std::string prompt() const override { return {}; }
    CommandStatus start(CommandContext& ctx) override
    {
        if (!clipboardEmpty(ctx))
            pasteClipboard(ctx, clipboard().base);
        return CommandStatus::Finished;
    }
};

// PASTEBLOCK (Ctrl+Maiusc+V): gli appunti diventano un blocco (nome A$C...), inserito nel punto indicato.
class PasteBlockCommand final : public Command {
public:
    std::string prompt() const override { return "Specificare punto di inserimento:"; }
    CommandStatus start(CommandContext& ctx) override
    {
        return clipboardEmpty(ctx) ? CommandStatus::Finished : CommandStatus::Continue;
    }
    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ClipboardData& clip = clipboard();
        Document& doc = ctx.document;
        std::string name;
        for (unsigned n = static_cast<unsigned>(doc.blocks().size()) + 1;; ++n) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "A$C%08X", n * 2654435761u);
            if (!doc.findBlock(buf)) {
                name = buf;
                break;
            }
        }
        Importer importer(doc, clip.source);
        auto block = std::make_shared<BlockDef>();
        block->name = name;
        block->base = clip.base;
        for (const auto& e : clip.entities) {
            auto c = importer.copy(*e);
            c->setId(0);
            block->entities.push_back(std::move(c));
        }
        doc.addBlock(block);
        auto insert = std::make_unique<InsertEntity>(name, p);
        insert->block = block;
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(insert));
        doc.commit(std::move(added));
        doc.linkBlocks();
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }
    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        for (const auto& e : clipboard().entities) {
            auto c = e->clone();
            c->translate(cursor - clipboard().base);
            result.push_back(std::move(c));
        }
        return result;
    }
};

// COPYBASE: come COPYCLIP ma con il punto base scelto (incollando, il punto base va sul cursore).
class CopyBaseCommand final : public SelectingCommand {
protected:
    std::string actionPrompt() const override { return "Specificare punto base:"; }
    CommandStatus onSelected(CommandContext&) override { return CommandStatus::Continue; }
    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        copyToClipboard(ctx, p);
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }
};

class PasteClipCommand final : public Command {
public:
    std::string prompt() const override { return "Specificare punto di inserimento:"; }

    CommandStatus start(CommandContext& ctx) override
    {
        if (clipboard().entities.empty()) {
            ctx.message("Gli appunti sono vuoti: copiare prima degli oggetti (Ctrl+C).");
            return CommandStatus::Finished;
        }
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        pasteClipboard(ctx, p);
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        for (const auto& e : clipboard().entities) {
            auto c = e->clone();
            c->translate(cursor - clipboard().base);
            result.push_back(std::move(c));
        }
        return result;
    }
};

// -------------------------------------------------- modifica con le maniglie

class GripEditCommand final : public Command {
public:
    GripEditCommand(EntityId id, std::size_t index, Vec2 from) : id_(id), index_(index), from_(from) {}

    std::string prompt() const override { return "** STIRA ** Specificare punto di stiramento:"; }

    CommandStatus start(CommandContext& ctx) override
    {
        if (const Entity* e = ctx.document.find(id_))
            original_ = e->clone();
        return original_ ? CommandStatus::Continue : CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        auto result = gripEdit(*original_, index_, p);
        if (!result) {
            ctx.message("Posizione non valida per questa maniglia.");
            return CommandStatus::Continue;
        }
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(result));
        ctx.document.commit(std::move(added), {id_});
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        const Vec2 d = ctx.cursor - from_;
        if (d.length() < 1e-12)
            return CommandStatus::Continue;
        return onPoint(ctx, from_ + d * (v / d.length()));
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (original_)
            if (auto e = gripEdit(*original_, index_, cursor))
                result.push_back(std::move(e));
        result.push_back(std::make_unique<LineEntity>(from_, cursor));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return from_; }

private:
    EntityId id_;
    std::size_t index_;
    Vec2 from_;
    std::unique_ptr<Entity> original_;
};


// ------------------------------------------------------------------- CHAMFER

double gChamferDist1 = 0.0;
double gChamferDist2 = 0.0;

class ChamferCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (asking_ == 1)
            return "Specificare prima distanza di cimatura <" + fmt(gChamferDist1) + ">:";
        if (asking_ == 2)
            return "Specificare seconda distanza di cimatura <" + fmt(gChamferDist2) + ">:";
        if (!first_)
            return "Selezionare prima linea o [Distanza] (distanze = " + fmt(gChamferDist1) + ", " + fmt(gChamferDist2)
                + "):";
        return "Selezionare seconda linea:";
    }
    bool wantsSelection() const override { return asking_ == 0; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!first_ && asking_ == 0 && (w == "D" || w == "DISTANZA" || w == "DISTANCE")) {
            asking_ = 1;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (asking_ == 0)
            return Command::onValue(ctx, v);
        if (v < 0.0) {
            ctx.message("La distanza non può essere negativa.");
            return CommandStatus::Continue;
        }
        if (asking_ == 1) {
            gChamferDist1 = v;
            gChamferDist2 = v;   // la seconda propone la prima
            asking_ = 2;
        } else {
            gChamferDist2 = v;
            asking_ = 0;
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (asking_ != 0) {
            asking_ = asking_ == 1 ? 2 : 0;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e || e->type() != EntityType::Line) {
            ctx.message("Selezionare una linea.");
            return CommandStatus::Continue;
        }
        if (!first_) {
            first_ = e->id();
            firstPick_ = p;
            ctx.selection = {e->id()};
            return CommandStatus::Continue;
        }
        if (e->id() == *first_) {
            ctx.message("Selezionare un'altra linea.");
            return CommandStatus::Continue;
        }
        const auto* a = static_cast<const LineEntity*>(ctx.document.find(*first_));
        const auto& b = static_cast<const LineEntity&>(*e);
        auto result = a ? chamferLines(*a, firstPick_, b, p, gChamferDist1, gChamferDist2) : std::nullopt;
        ctx.selection.clear();
        if (!result) {
            ctx.message("Impossibile cimare: linee parallele o distanze troppo grandi.");
            return CommandStatus::Finished;
        }
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(result->first));
        added.push_back(std::move(result->second));
        if (result->chamfer)
            added.push_back(std::move(result->chamfer));
        ctx.document.commit(std::move(added), {*first_, b.id()});
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

private:
    int asking_ = 0;   // 1 = prima distanza, 2 = seconda
    std::optional<EntityId> first_;
    Vec2 firstPick_;
};

// --------------------------------------------------------------------- BREAK

// BREAK: il punto con cui si seleziona l'oggetto è il primo punto di interruzione;
// "@" come secondo punto spezza l'oggetto in quel punto. BREAKATPOINT spezza direttamente in un punto.
class BreakCommand final : public Command {
public:
    explicit BreakCommand(bool atPoint) : atPoint_(atPoint) {}

    std::string prompt() const override
    {
        if (!target_)
            return "Selezionare oggetto:";
        if (atPoint_)
            return "Specificare punto di interruzione:";
        if (choosingFirst_)
            return "Specificare primo punto di interruzione:";
        return "Specificare secondo punto di interruzione o [Primo punto]:";
    }
    bool wantsSelection() const override { return !target_; }
    std::optional<Vec2> basePoint() const override { return target_ && !atPoint_ ? std::optional<Vec2>(first_) : std::nullopt; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!target_) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (!e) {
                ctx.message("Nessun oggetto trovato.");
                return CommandStatus::Continue;
            }
            target_ = e->id();
            first_ = p;
            ctx.lastPoint = p;
            ctx.selection = {e->id()};
            return CommandStatus::Continue;
        }
        if (atPoint_)
            return apply(ctx, p, p);
        if (choosingFirst_) {
            first_ = p;
            ctx.lastPoint = p;
            choosingFirst_ = false;
            return CommandStatus::Continue;
        }
        return apply(ctx, first_, p);
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (target_ && !atPoint_ && (w == "P" || w == "PRIMO" || w == "F" || w == "FIRST")) {
            choosingFirst_ = true;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

private:
    CommandStatus apply(CommandContext& ctx, Vec2 p1, Vec2 p2)
    {
        ctx.selection.clear();
        const Entity* e = ctx.document.find(*target_);
        auto pieces = e ? breakEntity(*e, p1, p2) : std::nullopt;
        if (!pieces) {
            ctx.message(e && e->type() == EntityType::Circle
                            ? "Un cerchio va interrotto fra due punti diversi."
                            : "Si possono interrompere linee, archi, cerchi e polilinee.");
            return CommandStatus::Finished;
        }
        ctx.document.commit(std::move(*pieces), {*target_});
        return CommandStatus::Finished;
    }

    bool atPoint_;
    std::optional<EntityId> target_;
    Vec2 first_;
    bool choosingFirst_ = false;
};

// ---------------------------------------------------------------------- JOIN

class JoinCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        std::vector<const Entity*> entities;
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id))
                entities.push_back(e);
        ctx.selection.clear();
        std::vector<EntityId> consumed;
        auto joined = joinEntities(entities, std::max(1e-9, ctx.pickTolerance * 1e-3), consumed);
        if (joined.empty()) {
            ctx.message("Nessun oggetto da unire: servono linee allineate, archi dello stesso cerchio o tratti che "
                        "si toccano agli estremi.");
            return CommandStatus::Finished;
        }
        ctx.message(std::to_string(consumed.size()) + " oggetti uniti in " + std::to_string(joined.size()) + ".");
        ctx.document.commit(std::move(joined), consumed);
        return CommandStatus::Finished;
    }
};


// ------------------------------------------------------------------- PEDIT

static std::string widthText(double v)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(10);
    out << v;
    return out.str();
}

// PEDIT: modifica di una polilinea (una linea o un arco si convertono). Opzioni: Chiudi/Apri,
// Unisci, Larghezza (uguale per tutti i tratti), Inverti, Spline (curva morbida per i vertici), Rettifica
// (tratti diritti), Annulla.
class PeditCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!target_)
            return "Selezionare polilinea:";
        if (joining_)
            return "Selezionare oggetti da unire (Invio per terminare):";
        const auto* pl = polyline();
        if (askingWidth_)
            return "Specificare la nuova larghezza per tutti i segmenti <" + widthText(pl ? pl->maxWidth() : 0.0) + ">:";
        const std::string close = pl && pl->closed ? "Apri" : "Chiudi";
        return "Immettere un'opzione [" + close + "/Unisci/Larghezza/Inverti/Spline/Rettifica/Annulla] <Fine>:";
    }

    bool wantsSelection() const override { return !target_ || joining_; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        doc_ = &ctx.document;
        if (joining_) {
            if (const Entity* e = ctx.document.pick(p, ctx.pickTolerance)) {
                if (e->id() != target_ && std::find(toJoin_.begin(), toJoin_.end(), e->id()) == toJoin_.end()) {
                    toJoin_.push_back(e->id());
                    ctx.message("1 trovato, " + std::to_string(toJoin_.size()) + " in totale.");
                }
            }
            return CommandStatus::Continue;
        }
        if (target_)
            return Command::onPoint(ctx, p);
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e) {
            ctx.message("Nessun oggetto qui.");
            return CommandStatus::Continue;
        }
        if (e->type() == EntityType::Polyline) {
            target_ = e->id();
            return CommandStatus::Continue;
        }
        // Linea o arco: diventa una polilinea (stesso id).
        std::vector<PolyVertex> v;
        if (e->type() == EntityType::Line) {
            const auto& l = static_cast<const LineEntity&>(*e);
            v = {{l.start, 0.0}, {l.end, 0.0}};
        } else if (e->type() == EntityType::Arc) {
            const auto& a = static_cast<const ArcEntity&>(*e);
            const double sweep = normalizeAngle(a.endAngle - a.startAngle);
            v = {{a.center + Vec2::polar(a.radius, a.startAngle), std::tan(sweep / 4.0)},
                 {a.center + Vec2::polar(a.radius, a.endAngle), 0.0}};
        } else {
            ctx.message("L'oggetto selezionato non è una polilinea, una linea o un arco.");
            return CommandStatus::Continue;
        }
        auto pl = std::make_unique<PolylineEntity>(std::move(v), false);
        copyProps(*e, *pl);
        target_ = e->id();
        replace(ctx, std::move(pl));
        ctx.message("Oggetto convertito in polilinea.");
        return CommandStatus::Continue;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& word) override
    {
        if (!target_ || joining_)
            return Command::onKeyword(ctx, word);
        std::string w;
        for (char c : word)
            w += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto* pl = polyline();
        if (!pl)
            return CommandStatus::Finished;
        if (w == "C" || w == "CHIUDI" || w == "A" || w == "APRI" || w == "O" || w == "OPEN" || w == "CLOSE") {
            auto copy = std::make_unique<PolylineEntity>(*pl);
            copy->closed = !pl->closed;
            replace(ctx, std::move(copy));
            return CommandStatus::Continue;
        }
        if (w == "U" || w == "UNISCI" || w == "J" || w == "JOIN") {
            joining_ = true;
            toJoin_.clear();
            return CommandStatus::Continue;
        }
        if (w == "L" || w == "LARGHEZZA" || w == "W" || w == "WIDTH") {
            askingWidth_ = true;
            return CommandStatus::Continue;
        }
        if (w == "I" || w == "INVERTI" || w == "REVERSE") {
            replace(ctx, reversed(*pl));
            ctx.message("La direzione della polilinea è stata invertita.");
            return CommandStatus::Continue;
        }
        if (w == "S" || w == "SPLINE") {
            replace(ctx, splined(*pl));
            return CommandStatus::Continue;
        }
        if (w == "R" || w == "RETTIFICA" || w == "D" || w == "DECURVE") {
            auto copy = std::make_unique<PolylineEntity>(*pl);
            for (PolyVertex& v : copy->vertices)
                v.bulge = 0.0;
            replace(ctx, std::move(copy));
            return CommandStatus::Continue;
        }
        if (w == "AN" || w == "ANNULLA" || w == "UNDO") {
            if (steps_ > 0 && ctx.document.undo())
                --steps_;
            else
                ctx.message("Niente da annullare.");
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, word);
    }

    CommandStatus onValue(CommandContext& ctx, double value) override
    {
        if (!askingWidth_)
            return Command::onValue(ctx, value);
        const auto* pl = polyline();
        if (value < 0.0) {
            ctx.message("La larghezza non può essere negativa.");
            return CommandStatus::Continue;
        }
        askingWidth_ = false;
        if (pl) {
            auto copy = std::make_unique<PolylineEntity>(*pl);
            copy->setWidth(value);
            replace(ctx, std::move(copy));
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (askingWidth_) {   // Invio: si tiene la larghezza attuale
            askingWidth_ = false;
            return CommandStatus::Continue;
        }
        if (!joining_)
            return CommandStatus::Finished;
        joining_ = false;
        const auto* pl = polyline();
        if (!pl || toJoin_.empty())
            return CommandStatus::Continue;
        std::vector<const Entity*> entities{pl};
        for (EntityId id : toJoin_)
            if (const Entity* e = ctx.document.find(id))
                entities.push_back(e);
        std::vector<EntityId> consumed;
        auto joined = joinEntities(entities, std::max(1e-9, ctx.pickTolerance * 1e-3), consumed);
        if (joined.empty() || std::find(consumed.begin(), consumed.end(), *target_) == consumed.end()) {
            ctx.message("Nessun segmento unito: gli oggetti devono toccare gli estremi della polilinea.");
            return CommandStatus::Continue;
        }
        // Il risultato che contiene la polilinea prende il suo id (si continua a modificarla).
        joined.front()->setId(0);
        ctx.document.commit(std::move(joined), consumed);
        ++steps_;
        target_.reset();
        // La polilinea nuova è l'ultimo oggetto aggiunto.
        if (!ctx.document.entities().empty())
            target_ = ctx.document.entities().back()->id();
        ctx.message(std::to_string(consumed.size() - 1) + " segmenti aggiunti alla polilinea.");
        return CommandStatus::Continue;
    }

private:
    bool askingWidth_ = false;

    const PolylineEntity* polyline() const
    {
        if (!target_ || !doc_)
            return nullptr;
        const Entity* e = doc_->find(*target_);
        return e && e->type() == EntityType::Polyline ? static_cast<const PolylineEntity*>(e) : nullptr;
    }

    static void copyProps(const Entity& from, Entity& to)
    {
        to.setLayer(from.layer());
        to.setColor(from.color());
        to.setLinetype(from.linetype());
        to.setLineweight(from.lineweight());
        to.setId(from.id());
    }

    void replace(CommandContext& ctx, std::unique_ptr<Entity> e)
    {
        doc_ = &ctx.document;
        e->setId(*target_);
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(e));
        ctx.document.commit(std::move(added), {*target_});
        ++steps_;
    }

    // Stessi tratti percorsi al contrario (il bulge cambia segno).
    static std::unique_ptr<PolylineEntity> reversed(const PolylineEntity& pl)
    {
        auto out = std::make_unique<PolylineEntity>(pl);
        const auto& v = pl.vertices;
        const std::size_t n = v.size();
        if (n < 2)
            return out;
        std::vector<PolyVertex> r(n);
        if (!pl.closed) {
            for (std::size_t i = 0; i < n; ++i) {
                r[i].point = v[n - 1 - i].point;
                r[i].bulge = i + 1 < n ? -v[n - 2 - i].bulge : 0.0;
            }
        } else {
            // Chiusa: il tratto da v[k] a v[k+1] diventa quello da v[k+1] a v[k].
            r[0] = {v[0].point, -v[n - 1].bulge};
            for (std::size_t i = 1; i < n; ++i)
                r[i] = {v[n - i].point, -v[n - i - 1].bulge};
        }
        out->vertices = std::move(r);
        return out;
    }

    // Curva morbida (B-spline cubica uniforme) con i vertici come punti di controllo, in tratti diritti.
    static std::unique_ptr<PolylineEntity> splined(const PolylineEntity& pl)
    {
        auto out = std::make_unique<PolylineEntity>(pl);
        const auto& v = pl.vertices;
        if (v.size() < 3)
            return out;
        std::vector<Vec2> c;
        for (const PolyVertex& p : v)
            c.push_back(p.point);
        std::vector<PolyVertex> pts;
        const int per = 8;
        const std::size_t n = c.size();
        const auto at = [&](long i) {
            if (pl.closed)
                return c[static_cast<std::size_t>((i % static_cast<long>(n) + static_cast<long>(n)) % static_cast<long>(n))];
            return c[static_cast<std::size_t>(std::clamp<long>(i, 0, static_cast<long>(n) - 1))];
        };
        const long first = pl.closed ? 0 : -1;
        const long last = pl.closed ? static_cast<long>(n) : static_cast<long>(n);
        for (long i = first; i < last; ++i)
            for (int k = 0; k < per; ++k) {
                const double t = static_cast<double>(k) / per;
                const double b0 = (1 - t) * (1 - t) * (1 - t) / 6.0;
                const double b1 = (3 * t * t * t - 6 * t * t + 4) / 6.0;
                const double b2 = (-3 * t * t * t + 3 * t * t + 3 * t + 1) / 6.0;
                const double b3 = t * t * t / 6.0;
                pts.push_back({at(i - 1) * b0 + at(i) * b1 + at(i + 1) * b2 + at(i + 2) * b3, 0.0});
            }
        if (!pl.closed) {   // gli estremi restano dove sono
            pts.front().point = c.front();
            pts.push_back({c.back(), 0.0});
        }
        out->vertices = std::move(pts);
        return out;
    }

    std::optional<EntityId> target_;
    Document* doc_ = nullptr;
    bool joining_ = false;
    std::vector<EntityId> toJoin_;
    int steps_ = 0;
};

// ------------------------------------------------------------------- STRETCH

// STRETCH: finestra di intersezione (due angoli), punto base e secondo punto. I punti degli oggetti dentro
// la finestra si spostano, il resto rimane: le linee si allungano, gli oggetti interi si spostano.
class StretchCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!corner1_)
            return "Selezionare oggetti da stirare con una finestra di intersezione: primo angolo:";
        if (!window_)
            return "Specificare angolo opposto:";
        if (!base_)
            return "Specificare punto base:";
        return "Specificare secondo punto:";
    }
    bool wantsSelection() const override { return !window_; }
    std::optional<Vec2> selectionCorner() const override { return window_ ? std::nullopt : corner1_; }
    std::optional<Vec2> basePoint() const override { return base_; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!corner1_) {
            corner1_ = p;
            return CommandStatus::Continue;
        }
        if (!window_) {
            BBox w;
            w.expand(*corner1_);
            w.expand(p);
            window_ = w;
            ids_ = ctx.document.selectInRect(w, true);
            ctx.selection = ids_;
            if (ids_.empty()) {
                ctx.message("Nessun oggetto nella finestra.");
                return CommandStatus::Finished;
            }
            ctx.message(std::to_string(ids_.size()) + " trovati.");
            return CommandStatus::Continue;
        }
        if (!base_) {
            base_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        const Vec2 delta = p - *base_;
        std::vector<std::unique_ptr<Entity>> modified;
        std::vector<EntityId> ids;
        for (EntityId id : ids_)
            if (const Entity* e = ctx.document.find(id))
                if (auto s = stretchEntity(*e, *window_, delta)) {
                    modified.push_back(std::move(s));
                    ids.push_back(id);
                }
        ctx.selection.clear();
        if (!modified.empty())
            ctx.document.commit(std::move(modified), ids);
        return CommandStatus::Finished;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        // Distanza nella direzione del cursore, come per MOVE.
        if (base_ && window_) {
            const Vec2 dir = ctx.cursor - *base_;
            if (dir.length() > 0.0)
                return onPoint(ctx, *base_ + dir / dir.length() * v);
        }
        return Command::onValue(ctx, v);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (!base_ || !window_ || !doc_)
            return result;
        for (EntityId id : ids_)
            if (const Entity* e = doc_->find(id))
                if (auto s = stretchEntity(*e, *window_, cursor - *base_))
                    result.push_back(std::move(s));
        return result;
    }

    CommandStatus start(CommandContext& ctx) override
    {
        doc_ = &ctx.document;
        ctx.selection.clear();
        return CommandStatus::Continue;
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

private:
    const Document* doc_ = nullptr;
    std::optional<Vec2> corner1_;
    std::optional<BBox> window_;
    std::optional<Vec2> base_;
    std::vector<EntityId> ids_;
};

// ------------------------------------------------ maniglie multifunzione

class GripActionCommand final : public Command {
public:
    GripActionCommand(EntityId id, std::size_t index, GripAction action, Vec2 from)
        : id_(id), index_(index), action_(action), from_(from)
    {
    }

    std::string prompt() const override
    {
        switch (action_) {
        case GripAction::AddVertex: return "** AGGIUNGI VERTICE ** Specificare nuovo punto del vertice:";
        case GripAction::ToArc: return "** CONVERTI IN ARCO ** Specificare punto medio dell'arco:";
        case GripAction::Lengthen: return "** ALLUNGA ** Specificare punto finale:";
        default: return "** STIRA ** Specificare punto di stiramento:";
        }
    }

    CommandStatus start(CommandContext& ctx) override
    {
        if (const Entity* e = ctx.document.find(id_))
            original_ = e->clone();
        if (!original_)
            return CommandStatus::Finished;
        if (!gripActionNeedsPoint(action_))
            return onPoint(ctx, from_);
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        auto result = gripApply(*original_, index_, action_, p);
        if (!result) {
            ctx.message("Operazione non possibile in questo punto.");
            return gripActionNeedsPoint(action_) ? CommandStatus::Continue : CommandStatus::Finished;
        }
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(result));
        ctx.document.commit(std::move(added), {id_});
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (original_)
            if (auto e = gripApply(*original_, index_, action_, cursor))
                result.push_back(std::move(e));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return from_; }

private:
    EntityId id_;
    std::size_t index_;
    GripAction action_;
    Vec2 from_;
    std::unique_ptr<Entity> original_;
};

// ------------------------------------------------------------- MEASUREGEOM

class MeasureGeomCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (inner_)
            return inner_->prompt();
        switch (mode_) {
        case Mode::Radius: return "Selezionare arco o cerchio (Invio per tornare alla misura rapida):";
        case Mode::Angle:
            return firstLine_ ? "Selezionare seconda linea:"
                              : "Selezionare arco o linea (Invio per tornare alla misura rapida):";
        default: return "Spostare il cursore o [Distanza/Raggio/ANgolo/Area/Rapido/eSci] <eSci>:";
        }
    }

    bool quickMeasure() const override { return !inner_ && mode_ == Mode::Quick; }
    bool wantsSelection() const override
    {
        return inner_ ? inner_->wantsSelection() : mode_ == Mode::Radius || mode_ == Mode::Angle;
    }
    std::optional<Vec2> basePoint() const override { return inner_ ? inner_->basePoint() : std::nullopt; }
    std::optional<Vec2> selectionCorner() const override { return inner_ ? inner_->selectionCorner() : std::nullopt; }
    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        return inner_ ? inner_->preview(cursor) : std::vector<std::unique_ptr<Entity>>{};
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (inner_)
            return wrap(inner_->onKeyword(ctx, w));
        firstLine_.reset();
        if (w == "D" || w == "DISTANZA" || w == "DISTANCE") {
            inner_ = std::make_unique<DistCommand>();
        } else if (w == "A" || w == "AREA") {
            inner_ = std::make_unique<AreaCommand>();
        } else if (w == "R" || w == "RAGGIO" || w == "RADIUS") {
            mode_ = Mode::Radius;
        } else if (w == "AN" || w == "ANGOLO" || w == "ANGLE") {
            mode_ = Mode::Angle;
        } else if (w == "RA" || w == "RAPIDO" || w == "Q" || w == "QUICK") {
            mode_ = Mode::Quick;
        } else if (w == "S" || w == "ESCI" || w == "X" || w == "EXIT") {
            return CommandStatus::Finished;
        } else {
            return Command::onKeyword(ctx, w);
        }
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (inner_)
            return wrap(inner_->onPoint(ctx, p));
        if (mode_ == Mode::Quick)
            return CommandStatus::Continue;
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }
        if (mode_ == Mode::Radius) {
            double r = 0.0;
            if (e->type() == EntityType::Circle)
                r = static_cast<const CircleEntity&>(*e).radius;
            else if (e->type() == EntityType::Arc)
                r = static_cast<const ArcEntity&>(*e).radius;
            if (r <= 0.0) {
                ctx.message("Selezionare un arco o un cerchio.");
                return CommandStatus::Continue;
            }
            ctx.message("Raggio = " + fmt(r) + ",  Diametro = " + fmt(2.0 * r));
            return CommandStatus::Continue;
        }
        // Angolo: di un arco, oppure fra due linee.
        if (e->type() == EntityType::Arc && !firstLine_) {
            const auto& a = static_cast<const ArcEntity&>(*e);
            ctx.message("Angolo = " + fmt(normalizeAngle(a.endAngle - a.startAngle) * 180.0 / kPi, 2) + "°");
            return CommandStatus::Continue;
        }
        if (e->type() != EntityType::Line) {
            ctx.message("Selezionare un arco o una linea.");
            return CommandStatus::Continue;
        }
        const auto& l = static_cast<const LineEntity&>(*e);
        if (!firstLine_) {
            firstLine_ = l.end - l.start;
            return CommandStatus::Continue;
        }
        double angle = std::abs(normalizeAngle(angleOf(l.end - l.start) - angleOf(*firstLine_)));
        if (angle > kPi)
            angle = 2.0 * kPi - angle;
        if (angle > kPi / 2.0)
            angle = kPi - angle;   // l'angolo acuto fra le due direzioni
        ctx.message("Angolo = " + fmt(angle * 180.0 / kPi, 2) + "°");
        firstLine_.reset();
        return CommandStatus::Continue;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        return inner_ ? wrap(inner_->onValue(ctx, v)) : Command::onValue(ctx, v);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (inner_)
            return wrap(inner_->onEnter(ctx));
        if (mode_ != Mode::Quick) {
            mode_ = Mode::Quick;
            firstLine_.reset();
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override
    {
        if (inner_)
            inner_->cancel(ctx);
    }

private:
    enum class Mode { Quick, Radius, Angle };

    // Il comando interno (DIST, AREA) finito: si torna alla misura rapida.
    CommandStatus wrap(CommandStatus status)
    {
        if (status == CommandStatus::Finished) {
            inner_.reset();
            mode_ = Mode::Quick;
        }
        return CommandStatus::Continue;
    }

    std::unique_ptr<Command> inner_;
    Mode mode_ = Mode::Quick;
    std::optional<Vec2> firstLine_;
};

// ----------------------------------------- SELECTPREVIOUS, isolamento oggetti

class SelectPreviousCommand final : public Command {
public:
    std::string prompt() const override { return {}; }
    CommandStatus start(CommandContext& ctx) override
    {
        ctx.selection.clear();
        const int found = selectionKeyword(ctx, "P", false);
        ctx.message(found > 0 ? std::to_string(found) + " oggetti selezionati." : "Nessuna selezione precedente.");
        return CommandStatus::Finished;
    }
};

class HideObjectsCommand final : public SelectingCommand {
public:
    explicit HideObjectsCommand(bool isolate) : isolate_(isolate) {}

protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        if (isolate_) {
            ctx.document.isolateObjects(ctx.selection);
            ctx.message(std::to_string(ctx.selection.size()) + " oggetti isolati (UNISOLATEOBJECTS per mostrare tutto).");
        } else {
            ctx.document.hideObjects(ctx.selection);
            ctx.message(std::to_string(ctx.selection.size()) + " oggetti nascosti (UNISOLATEOBJECTS per mostrarli).");
        }
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

private:
    bool isolate_;
};

class UnisolateObjectsCommand final : public Command {
public:
    std::string prompt() const override { return {}; }
    CommandStatus start(CommandContext& ctx) override
    {
        const std::size_t n = ctx.document.unisolateObjects();
        ctx.message(n > 0 ? std::to_string(n) + " oggetti di nuovo visibili." : "Nessun oggetto isolato o nascosto.");
        return CommandStatus::Finished;
    }
};

// ------------------------------------------------------------- ADDSELECTED

// Avvia il comando che crea un oggetto dello stesso tipo, con le proprietà dell'oggetto scelto (layer,
// colore, tipo e spessore di linea, stile di testo e di quota, blocco); finito quel comando si torna alle
// proprietà correnti di prima.
class AddSelectedCommand final : public Command {
public:
    std::string prompt() const override { return "Selezionare oggetto:"; }
    bool wantsSelection() const override { return true; }

    CommandStatus start(CommandContext& ctx) override
    {
        if (ctx.selection.size() == 1)
            if (const Entity* e = ctx.document.find(ctx.selection.front()))
                return launch(ctx, *e);
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }
        return launch(ctx, *e);
    }

private:
    static std::vector<std::string> commandFor(const Entity& e)
    {
        switch (e.type()) {
        case EntityType::Line: return {"LINE"};
        case EntityType::Circle: return {"CIRCLE"};
        case EntityType::Arc: return {"ARC"};
        case EntityType::Point: return {"POINT"};
        case EntityType::Polyline: return {"PLINE"};
        case EntityType::Ellipse: return {"ELLIPSE"};
        case EntityType::Text: return {"TEXT"};
        case EntityType::MText: return {"MTEXT"};
        case EntityType::Hatch: return {"HATCH"};
        case EntityType::Leader: return {"LEADER"};
        case EntityType::Wipeout: return {"WIPEOUT"};
        case EntityType::Mline: return {"MLINE"};
        case EntityType::Table: return {"TABLE"};
        case EntityType::XLine: return {static_cast<const XLineEntity&>(e).ray ? "RAY" : "XLINE"};
        case EntityType::Insert: {
            const std::string& name = static_cast<const InsertEntity&>(e).blockName;
            if (name.empty() || name[0] == '*')
                return {};
            return {"INSERT", name};
        }
        case EntityType::Dimension:
            switch (static_cast<const DimensionEntity&>(e).kind) {
            case DimensionEntity::Linear: return {"DIMLINEAR"};
            case DimensionEntity::Aligned: return {"DIMALIGNED"};
            case DimensionEntity::Angular:
            case DimensionEntity::Angular3: return {"DIMANGULAR"};
            case DimensionEntity::Diameter: return {"DIMDIAMETER"};
            case DimensionEntity::Radius: return {"DIMRADIUS"};
            case DimensionEntity::Ordinate: return {"DIMORDINATE"};
            default: return {};
            }
        default: return {};
        }
    }

    CommandStatus launch(CommandContext& ctx, const Entity& e)
    {
        std::vector<std::string> words = commandFor(e);
        if (words.empty()) {
            ctx.message("Per questo tipo di oggetto non c'è un comando di creazione.");
            return CommandStatus::Finished;
        }
        Document& doc = ctx.document;
        struct Saved {
            int layer, color, lineweight;
            std::string linetype, textStyle, dimStyle;
            double textSize;
        };
        const Saved saved{doc.currentLayer(), doc.currentColor(), doc.currentLineweight(), doc.currentLinetype(),
                          doc.currentTextStyle(), doc.currentDimStyle().name, textSizeSetting()};
        if (e.layer() >= 0)
            doc.setCurrentLayer(e.layer());
        doc.setCurrentColor(e.color() < 0 ? kColorByLayer : e.color());
        doc.setCurrentLinetype(e.linetype().empty() ? std::string("ByLayer") : e.linetype());
        doc.setCurrentLineweight(e.lineweight() == kLineweightUnset ? kLineweightByLayer : e.lineweight());
        if (e.type() == EntityType::Text) {
            const auto& t = static_cast<const TextEntity&>(e);
            doc.setCurrentTextStyle(t.style);
            textSizeSetting() = t.height;
        } else if (e.type() == EntityType::MText) {
            doc.setCurrentTextStyle(static_cast<const MTextEntity&>(e).style);
        } else if (e.type() == EntityType::Dimension) {
            doc.setCurrentDimStyle(static_cast<const DimensionEntity&>(e).style.name);
        }
        ctx.followUp = std::move(words);
        ctx.afterFollowUp = [&doc, saved] {
            doc.setCurrentLayer(saved.layer);
            doc.setCurrentColor(saved.color);
            doc.setCurrentLineweight(saved.lineweight);
            doc.setCurrentLinetype(saved.linetype);
            doc.setCurrentTextStyle(saved.textStyle);
            doc.setCurrentDimStyle(saved.dimStyle);
            textSizeSetting() = saved.textSize;
        };
        ctx.selection.clear();
        return CommandStatus::Finished;
    }
};

} // namespace

std::unique_ptr<Command> makeGripEditCommand(EntityId id, std::size_t gripIndex, Vec2 from)
{
    return std::make_unique<GripEditCommand>(id, gripIndex, from);
}

std::unique_ptr<Command> makeGripActionCommand(EntityId id, std::size_t gripIndex, int action, Vec2 from)
{
    return std::make_unique<GripActionCommand>(id, gripIndex, static_cast<GripAction>(action), from);
}

void registerEditCommands(CommandManager& manager)
{
    manager.add("ROTATE", {"RO", "RUOTA"}, [] { return std::make_unique<RotateCommand>(); });
    manager.add("SCALE", {"SC", "SCALA"}, [] { return std::make_unique<ScaleCommand>(); });
    manager.add("MIRROR", {"MI", "SPECCHIO"}, [] { return std::make_unique<MirrorCommand>(); });
    manager.add("ARRAY", {"AR", "SERIE"}, [] { return std::make_unique<ArrayCommand>(); });
    manager.add("EXTEND", {"EX", "ESTENDI"}, [] { return std::make_unique<ExtendCommand>(); });
    manager.add("FILLET", {"F", "RACCORDO"}, [] { return std::make_unique<FilletCommand>(); });
    manager.add("CHAMFER", {"CHA", "CIMA", "SMUSSO"}, [] { return std::make_unique<ChamferCommand>(); });
    manager.add("BREAK", {"BR", "INTERROMPI", "SPEZZA"}, [] { return std::make_unique<BreakCommand>(false); });
    manager.add("BREAKATPOINT", {"SPEZZAINPUNTO"}, [] { return std::make_unique<BreakCommand>(true); });
    manager.add("JOIN", {"J", "UNISCI"}, [] { return std::make_unique<JoinCommand>(); });
    manager.add("PEDIT", {"PE", "EDITPL", "MODPL"}, [] { return std::make_unique<PeditCommand>(); });
    manager.add("STRETCH", {"S", "STIRA"}, [] { return std::make_unique<StretchCommand>(); });
    manager.add("MATCHPROP", {"MA", "CORRISPROP"}, [] { return std::make_unique<MatchPropCommand>(); });
    manager.add("DIST", {"DI", "DISTANZA"}, [] { return std::make_unique<DistCommand>(); });
    manager.add("ID", {}, [] { return std::make_unique<IdCommand>(); });
    manager.add("AREA", {"AA"}, [] { return std::make_unique<AreaCommand>(); });
    manager.add("LIST", {"LI", "LS", "LISTA"}, [] { return std::make_unique<ListCommand>(); });
    manager.add("COPYCLIP", {}, [] { return std::make_unique<CopyClipCommand>(); });
    manager.add("PASTECLIP", {}, [] { return std::make_unique<PasteClipCommand>(); });
    manager.add("COPYBASE", {"COPIABASE"}, [] { return std::make_unique<CopyBaseCommand>(); });
    manager.add("CUTCLIP", {"TAGLIA"}, [] { return std::make_unique<CutClipCommand>(); });
    manager.add("PASTEORIG", {"INCOLLAORIG"}, [] { return std::make_unique<PasteOrigCommand>(); });
    manager.add("PASTEBLOCK", {"INCOLLABLOCCO"}, [] { return std::make_unique<PasteBlockCommand>(); });
    manager.add("MEASUREGEOM", {"MEA", "MISURAGEOM"}, [] { return std::make_unique<MeasureGeomCommand>(); });
    manager.add("SELECTPREVIOUS", {"SELEZIONEPRECEDENTE"}, [] { return std::make_unique<SelectPreviousCommand>(); });
    manager.add("HIDEOBJECTS", {"NASCONDIOGGETTI"}, [] { return std::make_unique<HideObjectsCommand>(false); });
    manager.add("ISOLATEOBJECTS", {"ISOLAOGGETTI"}, [] { return std::make_unique<HideObjectsCommand>(true); });
    manager.add("UNISOLATEOBJECTS", {"UNHIDEOBJECTS", "TERMINAISOLAMENTO"},
                [] { return std::make_unique<UnisolateObjectsCommand>(); });
    manager.add("ADDSELECTED", {"AGGIUNGISELEZIONATO"}, [] { return std::make_unique<AddSelectedCommand>(); });
}

} // namespace cartesio
