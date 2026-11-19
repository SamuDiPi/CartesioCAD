// Comandi di annotazione: TEXT, MTEXT, DIMLINEAR, DIMALIGNED, DIMRADIUS, DIMDIAMETER, HATCH.
#include "core/commands/CommandManager.h"

#include "core/Geometry.h"
#include "core/HatchBoundary.h"

#include <cmath>
#include <locale>
#include <sstream>

namespace cartesio {

namespace {

double& gTextHeight = textSizeSetting();   // si ricorda l'ultima altezza del testo
double gTextRotation = 0.0;    // gradi
std::string gHatchPattern = "SOLID";
double gHatchScale = 1.0;
double gHatchAngle = 0.0;   // gradi

} // namespace

HatchDefaults hatchDefaults()
{
    return {gHatchPattern, gHatchScale, gHatchAngle};
}

namespace {

std::string formatNumber(double v)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(10);
    out << v;
    return out.str();
}

// ------------------------------------------------------------------- TEXT

// Testo su una riga: punto iniziale, altezza, rotazione, poi una riga di testo per ogni Invio.
class TextCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Position: return "Punto iniziale del testo:";
        case Stage::Height: return "Altezza <" + formatNumber(gTextHeight) + ">:";
        case Stage::Rotation: return "Angolo di rotazione del testo <" + formatNumber(gTextRotation) + ">:";
        case Stage::Content: return "Testo (Invio a vuoto per finire):";
        }
        return {};
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        switch (stage_) {
        case Stage::Position:
            position_ = p;
            ctx.lastPoint = p;
            stage_ = Stage::Height;
            break;
        case Stage::Height:
            return onValue(ctx, distance(position_, p));
        case Stage::Rotation:
            return onValue(ctx, normalizeAngle(angleOf(p - position_)) * 180.0 / kPi);
        case Stage::Content:
            position_ = p;   // un clic sposta la riga successiva
            break;
        }
        return CommandStatus::Continue;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (stage_ == Stage::Height) {
            if (v <= 0.0) {
                ctx.message("L'altezza deve essere maggiore di zero.");
                return CommandStatus::Continue;
            }
            gTextHeight = v;
            stage_ = Stage::Rotation;
        } else if (stage_ == Stage::Rotation) {
            gTextRotation = v;
            stage_ = Stage::Content;
            return writeInPlace(ctx);
        } else {
            return Command::onValue(ctx, v);
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (stage_ == Stage::Height) {
            stage_ = Stage::Rotation;
        } else if (stage_ == Stage::Rotation) {
            stage_ = Stage::Content;
            return writeInPlace(ctx);
        } else {
            return CommandStatus::Finished;
        }
        return CommandStatus::Continue;
    }

    // Il testo si scrive direttamente nel disegno (Invio conferma, niente Invio a vuoto); negli script e senza
    // schermo resta la riga di comando.
    CommandStatus writeInPlace(CommandContext& ctx)
    {
        if (!ctx.addTextInPlace)
            return CommandStatus::Continue;
        std::unique_ptr<Entity> entity = std::make_unique<TextEntity>(position_, gTextHeight, std::string(), degToRad(gTextRotation));
        static_cast<TextEntity&>(*entity).style = ctx.document.currentTextStyle();
        ctx.document.prepareAnnotation(*entity);
        return ctx.addTextInPlace(entity) ? CommandStatus::Finished : CommandStatus::Continue;
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const double rot = degToRad(gTextRotation);
        auto entity = std::make_unique<TextEntity>(position_, gTextHeight, text, rot);
        entity->style = ctx.document.currentTextStyle();
        ctx.document.prepareAnnotation(*entity);   // stile annotativo: l'altezza data è quella sulla carta
        const double height = entity->height;
        ctx.document.add(std::move(entity));
        // La riga successiva va sotto.
        position_ = position_ + Vec2::polar(height * 1.667, rot - kPi / 2.0);
        return CommandStatus::Continue;
    }

    bool wantsText() const override { return stage_ == Stage::Content; }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (stage_ == Stage::Height || stage_ == Stage::Rotation)
            result.push_back(std::make_unique<LineEntity>(position_, cursor));
        if (stage_ == Stage::Content) {
            auto marker = std::make_unique<LineEntity>(position_, position_ + Vec2::polar(gTextHeight, degToRad(gTextRotation) + kPi / 2.0));
            result.push_back(std::move(marker));
        }
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        if (stage_ == Stage::Height || stage_ == Stage::Rotation)
            return position_;
        return std::nullopt;
    }

private:
    enum class Stage { Position, Height, Rotation, Content };
    Stage stage_ = Stage::Position;
    Vec2 position_;
};

// ------------------------------------------------------------------ MTEXT

// Testo multilinea: due angoli per la larghezza, poi un paragrafo per ogni Invio.
class MTextCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!first_)
            return "Primo angolo:";
        if (!width_)
            return "Angolo opposto:";
        return "Testo (Invio a vuoto per finire):";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            ctx.lastPoint = p;
        } else if (!width_) {
            width_ = std::abs(p.x - first_->x);
            corner_ = Vec2{std::min(p.x, first_->x), std::max(p.y, first_->y)};
            // Il testo si scrive nel riquadro, direttamente nel disegno (negli script: riga di comando).
            if (ctx.addTextInPlace) {
                auto mtext = std::make_unique<MTextEntity>(corner_, gTextHeight, std::string(), *width_);
                mtext->style = ctx.document.currentTextStyle();
                ctx.document.prepareAnnotation(*mtext);
                mtext->width = *width_;   // il riquadro è stato disegnato nel modello: resta com'è
                std::unique_ptr<Entity> entity = std::move(mtext);
                if (ctx.addTextInPlace(entity))
                    return CommandStatus::Finished;
            }
        }
        return CommandStatus::Continue;
    }

    CommandStatus onText(CommandContext&, const std::string& text) override
    {
        if (!content_.empty())
            content_ += "\\P";
        content_ += text;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (width_ && !content_.empty()) {
            auto entity = std::make_unique<MTextEntity>(corner_, gTextHeight, content_, *width_);
            entity->style = ctx.document.currentTextStyle();
            const double w = entity->width;
            ctx.document.prepareAnnotation(*entity);
            entity->width = w;   // il riquadro è stato disegnato nel modello: resta com'è
            ctx.document.add(std::move(entity));
        }
        return CommandStatus::Finished;
    }

    bool wantsText() const override { return width_.has_value(); }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (first_ && !width_) {
            const Vec2 a = *first_;
            result.push_back(std::make_unique<PolylineEntity>(
                std::vector<PolyVertex>{{a, 0}, {{cursor.x, a.y}, 0}, {cursor, 0}, {{a.x, cursor.y}, 0}}, true));
        } else if (width_) {
            auto text = std::make_unique<MTextEntity>(corner_, gTextHeight, content_.empty() ? std::string("_") : content_, *width_);
            result.push_back(std::move(text));
        }
        return result;
    }

private:
    std::optional<Vec2> first_;
    std::optional<double> width_;
    Vec2 corner_;
    std::string content_;
};

// ------------------------------------------------------------------ QUOTE

// Quota lineare (orizzontale/verticale secondo la posizione) o allineata.
class LinearDimCommand final : public Command {
public:
    // Stile di quota corrente, preso all'avvio (serve anche all'anteprima).
    CommandStatus start(CommandContext& ctx) override
    {
        style_ = ctx.document.dimStyleForNew();
        return CommandStatus::Continue;
    }

    explicit LinearDimCommand(bool aligned) : aligned_(aligned) {}

    std::string prompt() const override
    {
        if (!p1_)
            return "Primo punto da quotare:";
        if (!p2_)
            return "Secondo punto da quotare:";
        return "Posizione della quota:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.lastPoint = p;
        if (!p1_) {
            p1_ = p;
        } else if (!p2_) {
            if (distance(*p1_, p) < 1e-12) {
                ctx.message("I due punti coincidono.");
                return CommandStatus::Continue;
            }
            p2_ = p;
        } else {
            auto dim = make(p);
            ctx.message("Testo di quota = " + dim->displayText());
            ctx.document.prepareAnnotation(*dim);
            ctx.document.add(std::move(dim));
            return CommandStatus::Finished;
        }
        return CommandStatus::Continue;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (p1_ && p2_)
            result.push_back(make(cursor));
        else if (p1_)
            result.push_back(std::make_unique<LineEntity>(*p1_, cursor));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return p2_ ? std::nullopt : p1_; }

private:
    DimStyle style_;
    std::unique_ptr<DimensionEntity> make(Vec2 location) const
    {
        auto dim = std::make_unique<DimensionEntity>();
        dim->style = style_;
        dim->p1 = *p1_;
        dim->p2 = *p2_;
        dim->dimLine = location;
        dim->textPos = location;
        if (aligned_) {
            dim->kind = DimensionEntity::Aligned;
        } else {
            // Orizzontale se la posizione è sopra/sotto i punti, verticale se è a lato.
            const Vec2 mid = (*p1_ + *p2_) / 2.0;
            const double dx = std::abs(location.x - mid.x) - std::abs(p2_->x - p1_->x) / 2.0;
            const double dy = std::abs(location.y - mid.y) - std::abs(p2_->y - p1_->y) / 2.0;
            dim->kind = DimensionEntity::Linear;
            dim->rotation = dy >= dx ? 0.0 : kPi / 2.0;
        }
        return dim;
    }

    bool aligned_;
    std::optional<Vec2> p1_;
    std::optional<Vec2> p2_;
};


// ----------------------------------------------------------- DIMANGULAR

// Quota angolare: due linee (il lato si sceglie con il punto del clic), un arco, oppure Invio e tre punti (vertice,
// primo e secondo lato); poi la posizione dell'arco di quota.
class AngularDimCommand final : public Command {
public:
    CommandStatus start(CommandContext& ctx) override
    {
        style_ = ctx.document.dimStyleForNew();
        return CommandStatus::Continue;
    }

    std::string prompt() const override
    {
        if (points_) {
            if (!vertex_)
                return "Vertice dell'angolo:";
            if (!p1_)
                return "Primo punto finale dell'angolo:";
            if (!p2_)
                return "Secondo punto finale dell'angolo:";
        } else if (!p1_) {
            return "Arco, cerchio o linea <vertice>:";
        } else if (!p2_) {
            return "Seconda linea:";
        }
        return "Posizione dell'arco della quota:";
    }

    bool wantsSelection() const override { return !points_ && !p2_; }

    CommandStatus onEnter(CommandContext&) override
    {
        if (!points_ && !p1_) {
            points_ = true;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.lastPoint = p;
        if (points_) {
            if (!vertex_)
                vertex_ = p;
            else if (!p1_)
                p1_ = p;
            else if (!p2_)
                p2_ = p;
            else
                return place(ctx, p);
            return CommandStatus::Continue;
        }
        if (p1_ && p2_ && vertex_)
            return place(ctx, p);
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e) {
            ctx.message("Nessun oggetto qui.");
            return CommandStatus::Continue;
        }
        if (!p1_ && (e->type() == EntityType::Arc)) {
            const auto& a = static_cast<const ArcEntity&>(*e);
            vertex_ = a.center;
            p1_ = a.center + Vec2::polar(a.radius, a.startAngle);
            p2_ = a.center + Vec2::polar(a.radius, a.endAngle);
            return CommandStatus::Continue;
        }
        if (e->type() != EntityType::Line) {
            ctx.message("Scegli una linea o un arco (Invio per i tre punti).");
            return CommandStatus::Continue;
        }
        const auto& l = static_cast<const LineEntity&>(*e);
        // Il lato è quello dalla parte del clic: il punto sulla linea più vicino al clic.
        const Vec2 d = l.end - l.start;
        const double len2 = d.dot(d);
        const Vec2 onLine = len2 > 0.0 ? l.start + d * ((p - l.start).dot(d) / len2) : l.start;
        if (!p1_) {
            line1_ = {l.start, l.end};
            p1_ = onLine;
            return CommandStatus::Continue;
        }
        const Vec2 u = line1_->second - line1_->first, w = d;
        const double den = cross(u, w);
        if (std::abs(den) < 1e-12) {
            ctx.message("Le linee sono parallele.");
            return CommandStatus::Continue;
        }
        vertex_ = line1_->first + u * (cross(l.start - line1_->first, w) / den);
        p2_ = onLine;
        // Un clic proprio sul vertice non dice il lato: si usa l'estremo più lontano.
        if (distance(*p1_, *vertex_) < 1e-9)
            p1_ = distance(line1_->first, *vertex_) > distance(line1_->second, *vertex_) ? line1_->first : line1_->second;
        if (distance(*p2_, *vertex_) < 1e-9)
            p2_ = distance(l.start, *vertex_) > distance(l.end, *vertex_) ? l.start : l.end;
        return CommandStatus::Continue;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (vertex_ && p1_ && p2_)
            out.push_back(make(cursor));
        return out;
    }

private:
    std::unique_ptr<DimensionEntity> make(Vec2 location) const
    {
        auto dim = std::make_unique<DimensionEntity>();
        dim->style = style_;
        dim->kind = DimensionEntity::Angular3;
        dim->curvePoint = *vertex_;
        dim->p1 = *p1_;
        dim->p2 = *p2_;
        dim->dimLine = location;
        dim->textPos = location;
        return dim;
    }

    CommandStatus place(CommandContext& ctx, Vec2 p)
    {
        auto dim = make(p);
        ctx.message("Testo di quota = " + dim->displayText());
        ctx.document.prepareAnnotation(*dim);
        ctx.document.add(std::move(dim));
        return CommandStatus::Finished;
    }

    DimStyle style_;
    bool points_ = false;
    std::optional<std::pair<Vec2, Vec2>> line1_;
    std::optional<Vec2> vertex_, p1_, p2_;
};

// ----------------------------------------------------------- DIMORDINATE

// Quota a coordinata: punto, poi la fine della direttrice. X o Y si decide dalla direzione (verticale = X),
// oppure con le opzioni Xdato/Ydato. L'origine è quella del disegno.
class OrdinateDimCommand final : public Command {
public:
    CommandStatus start(CommandContext& ctx) override
    {
        style_ = ctx.document.dimStyleForNew();
        return CommandStatus::Continue;
    }
    std::string prompt() const override
    {
        return point_ ? "Punto finale della direttrice o [Xdato/Ydato]:" : "Posizione del punto:";
    }
    CommandStatus onKeyword(CommandContext& ctx, const std::string& word) override
    {
        std::string w;
        for (char c : word)
            w += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (point_ && (w == "X" || w == "XDATO"))
            forced_ = true, forcedX_ = true;
        else if (point_ && (w == "Y" || w == "YDATO"))
            forced_ = true, forcedX_ = false;
        else
            return Command::onKeyword(ctx, word);
        return CommandStatus::Continue;
    }
    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.lastPoint = p;
        if (!point_) {
            point_ = p;
            return CommandStatus::Continue;
        }
        auto dim = make(p);
        ctx.message("Testo di quota = " + dim->displayText());
        ctx.document.prepareAnnotation(*dim);
        ctx.document.add(std::move(dim));
        return CommandStatus::Finished;
    }
    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (point_ && distance(*point_, cursor) > 1e-9)
            out.push_back(make(cursor));
        return out;
    }
    std::optional<Vec2> basePoint() const override { return point_; }

private:
    std::unique_ptr<DimensionEntity> make(Vec2 end) const
    {
        auto dim = std::make_unique<DimensionEntity>();
        dim->style = style_;
        dim->kind = DimensionEntity::Ordinate;
        dim->p1 = *point_;
        dim->p2 = end;
        dim->dimLine = {0.0, 0.0};
        dim->textPos = end;
        const Vec2 d = end - *point_;
        dim->ordinateX = forced_ ? forcedX_ : std::abs(d.y) >= std::abs(d.x);
        return dim;
    }
    DimStyle style_;
    std::optional<Vec2> point_;
    bool forced_ = false, forcedX_ = false;
};

// ------------------------------------------------- DIMCONTINUE / DIMBASELINE

// Ultima quota lineare o allineata dello spazio corrente (da cui continuare).
const DimensionEntity* lastLinearDimension(const Document& doc)
{
    const int space = doc.activeSpace();
    for (auto it = doc.entities().rbegin(); it != doc.entities().rend(); ++it) {
        const Entity& e = **it;
        if (e.type() != EntityType::Dimension || (e.space() < 0 ? 0 : e.space()) != space)
            continue;
        const auto& d = static_cast<const DimensionEntity&>(e);
        if (d.kind == DimensionEntity::Linear || d.kind == DimensionEntity::Aligned)
            return &d;
    }
    return nullptr;
}

// Quote in serie (DIMCONTINUE: ognuna parte dalla fine della precedente, sulla stessa linea) o dalla stessa
// linea di base (DIMBASELINE: ognuna parte dalla prima linea di estensione, spostata della distanza fra le linee).
class SeriesDimCommand final : public Command {
public:
    explicit SeriesDimCommand(bool baseline) : baseline_(baseline) {}

    CommandStatus start(CommandContext& ctx) override
    {
        if (const DimensionEntity* d = lastLinearDimension(ctx.document)) {
            prev_ = std::make_unique<DimensionEntity>(*d);
            return CommandStatus::Continue;
        }
        selecting_ = true;
        return CommandStatus::Continue;
    }

    std::string prompt() const override
    {
        if (selecting_)
            return baseline_ ? "Quota di base:" : "Quota continua:";
        return "Punto successivo da quotare o [Seleziona] <Fine>:";
    }
    bool wantsSelection() const override { return selecting_; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& word) override
    {
        std::string w;
        for (char c : word)
            w += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (w == "S" || w == "SELEZIONA" || w == "SELECT") {
            selecting_ = true;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, word);
    }

    CommandStatus onEnter(CommandContext&) override { return CommandStatus::Finished; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.lastPoint = p;
        if (selecting_) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (!e || e->type() != EntityType::Dimension
                || (static_cast<const DimensionEntity*>(e)->kind != DimensionEntity::Linear
                    && static_cast<const DimensionEntity*>(e)->kind != DimensionEntity::Aligned)) {
                ctx.message("Scegli una quota lineare o allineata.");
                return CommandStatus::Continue;
            }
            prev_ = std::make_unique<DimensionEntity>(*static_cast<const DimensionEntity*>(e));
            // Si continua dall'estremo più vicino al clic.
            if (!baseline_ && distance(p, prev_->p1) < distance(p, prev_->p2))
                std::swap(prev_->p1, prev_->p2);
            selecting_ = false;
            return CommandStatus::Continue;
        }
        if (!prev_)
            return CommandStatus::Finished;
        auto dim = make(p);
        ctx.message("Testo di quota = " + dim->displayText());
        auto copy = std::make_unique<DimensionEntity>(*dim);
        ctx.document.prepareAnnotation(*dim);
        ctx.document.add(std::move(dim));
        prev_ = std::move(copy);
        if (baseline_)   // la prossima parte ancora dalla stessa base
            prev_->p2 = p;
        return CommandStatus::Continue;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (prev_ && !selecting_)
            out.push_back(make(cursor));
        return out;
    }

private:
    std::unique_ptr<DimensionEntity> make(Vec2 point) const
    {
        auto dim = std::make_unique<DimensionEntity>(*prev_);
        dim->setId(0);
        dim->blockName.clear();
        dim->block.reset();
        dim->textOverride.clear();
        const Vec2 dir = prev_->kind == DimensionEntity::Aligned && distance(prev_->p1, prev_->p2) > 0.0
                             ? (prev_->p2 - prev_->p1) / distance(prev_->p1, prev_->p2)
                             : Vec2::polar(1.0, prev_->rotation);
        const Vec2 n{-dir.y, dir.x};
        if (baseline_) {
            dim->p2 = point;
            // Linea di quota spostata (DIMDLI 3,75 con la scala dello stile), dalla parte opposta ai punti.
            const double spacing = 3.75 * (prev_->style.scale > 0.0 ? prev_->style.scale : 1.0);
            const double side = (prev_->dimLine - prev_->p1).dot(n) >= 0.0 ? 1.0 : -1.0;
            dim->dimLine = prev_->dimLine + n * (side * spacing);
        } else {
            dim->p1 = prev_->p2;
            dim->p2 = point;
            dim->dimLine = prev_->dimLine;
        }
        dim->textPos = dim->dimLine;
        return dim;
    }

    bool baseline_;
    bool selecting_ = false;
    std::unique_ptr<DimensionEntity> prev_;
};

// ----------------------------------------------------------------- QDIM

// Quotatura rapida: gli estremi degli oggetti selezionati, quotati in serie (Continua) o dalla base (Linea di
// base); orizzontale o verticale secondo dove si mette la linea di quota.
class QDimCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        style_ = ctx.document.dimStyleForNew();
        points_.clear();
        for (EntityId id : ctx.selection) {
            const Entity* e = ctx.document.find(id);
            if (!e)
                continue;
            switch (e->type()) {
            case EntityType::Line:
                points_.push_back(static_cast<const LineEntity&>(*e).start);
                points_.push_back(static_cast<const LineEntity&>(*e).end);
                break;
            case EntityType::Polyline:
                for (const PolyVertex& v : static_cast<const PolylineEntity&>(*e).vertices)
                    points_.push_back(v.point);
                break;
            case EntityType::Arc: {
                const auto& a = static_cast<const ArcEntity&>(*e);
                points_.push_back(a.center + Vec2::polar(a.radius, a.startAngle));
                points_.push_back(a.center + Vec2::polar(a.radius, a.endAngle));
                break;
            }
            case EntityType::Circle:
                points_.push_back(static_cast<const CircleEntity&>(*e).center);
                break;
            case EntityType::Insert:
                points_.push_back(static_cast<const InsertEntity&>(*e).position);
                break;
            case EntityType::Point:
                for (const SnapPoint& sp : e->snapPoints())
                    points_.push_back(sp.position);
                break;
            default:
                break;
            }
        }
        ctx.selection.clear();
        if (points_.size() < 2) {
            ctx.message("Servono almeno due punti (linee, polilinee, archi, cerchi, blocchi).");
            return CommandStatus::Finished;
        }
        return CommandStatus::Continue;
    }

    std::string actionPrompt() const override
    {
        return std::string("Posizione della quota o [Continua/Linea di base] <") +
               (baseline_ ? "Linea di base" : "Continua") + ">:";
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& word) override
    {
        std::string w;
        for (char c : word)
            w += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (w == "C" || w == "CONTINUA")
            baseline_ = false;
        else if (w == "L" || w == "B" || w == "BASE" || w == "LINEA")
            baseline_ = true;
        else
            return SelectingCommand::actionKeyword(ctx, word);
        return CommandStatus::Continue;
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        auto dims = make(p);
        const auto n = dims.size();
        std::vector<std::unique_ptr<Entity>> added;
        for (auto& d : dims) {
            ctx.document.prepareAnnotation(*d);
            added.push_back(std::move(d));
        }
        ctx.document.commit(std::move(added));
        ctx.message(std::to_string(n) + " quote create.");
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> actionPreview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        for (auto& d : make(cursor))
            out.push_back(std::move(d));
        return out;
    }

private:
    std::vector<std::unique_ptr<DimensionEntity>> make(Vec2 location) const
    {
        std::vector<std::unique_ptr<DimensionEntity>> out;
        BBox box;
        for (Vec2 p : points_)
            box.expand(p);
        // Sopra o sotto i punti: quote orizzontali (si misura x); a lato: verticali (si misura y).
        const double dx = std::max(box.min.x - location.x, location.x - box.max.x);
        const double dy = std::max(box.min.y - location.y, location.y - box.max.y);
        const bool horizontal = dy >= dx;
        // Un punto per coordinata (quello più vicino alla linea di quota), in ordine.
        std::vector<Vec2> pts;
        std::vector<Vec2> sorted = points_;
        std::sort(sorted.begin(), sorted.end(), [&](Vec2 a, Vec2 b) { return horizontal ? a.x < b.x : a.y < b.y; });
        const double tol = std::max(box.width(), box.height()) * 1e-9 + 1e-9;
        for (Vec2 p : sorted) {
            const double key = horizontal ? p.x : p.y;
            if (!pts.empty() && std::abs((horizontal ? pts.back().x : pts.back().y) - key) <= tol) {
                const double dNew = horizontal ? std::abs(p.y - location.y) : std::abs(p.x - location.x);
                const double dOld = horizontal ? std::abs(pts.back().y - location.y) : std::abs(pts.back().x - location.x);
                if (dNew < dOld)
                    pts.back() = p;
                continue;
            }
            pts.push_back(p);
        }
        const double spacing = 3.75 * (style_.scale > 0.0 ? style_.scale : 1.0);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            auto dim = std::make_unique<DimensionEntity>();
            dim->style = style_;
            dim->kind = DimensionEntity::Linear;
            dim->rotation = horizontal ? 0.0 : kPi / 2.0;
            dim->p1 = baseline_ ? pts.front() : pts[i - 1];
            dim->p2 = pts[i];
            Vec2 line = location;
            if (baseline_) {   // una linea di quota per livello, allontanandosi dai punti
                const double away = horizontal ? (location.y >= box.max.y ? 1.0 : -1.0) : (location.x >= box.max.x ? 1.0 : -1.0);
                const double shift = static_cast<double>(i - 1) * spacing * away;
                line = horizontal ? Vec2{location.x, location.y + shift} : Vec2{location.x + shift, location.y};
            }
            dim->dimLine = line;
            dim->textPos = line;
            out.push_back(std::move(dim));
        }
        return out;
    }

    DimStyle style_;
    std::vector<Vec2> points_;
    bool baseline_ = false;
};

// Quota di raggio o diametro su un cerchio o un arco.
class RadialDimCommand final : public Command {
public:
    // Stile di quota corrente, preso all'avvio (serve anche all'anteprima).
    CommandStatus start(CommandContext& ctx) override
    {
        style_ = ctx.document.dimStyleForNew();
        return CommandStatus::Continue;
    }

    explicit RadialDimCommand(bool diameter) : diameter_(diameter) {}

    std::string prompt() const override
    {
        return center_ ? "Posizione della quota:" : "Arco o cerchio:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!center_) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (e && e->type() == EntityType::Circle) {
                const auto& c = static_cast<const CircleEntity&>(*e);
                center_ = c.center;
                radius_ = c.radius;
            } else if (e && e->type() == EntityType::Arc) {
                const auto& a = static_cast<const ArcEntity&>(*e);
                center_ = a.center;
                radius_ = a.radius;
            } else {
                ctx.message("Scegli un cerchio o un arco.");
            }
            return CommandStatus::Continue;
        }
        auto dim = make(p);
        ctx.message("Testo di quota = " + dim->displayText());
        ctx.document.prepareAnnotation(*dim);
        ctx.document.add(std::move(dim));
        return CommandStatus::Finished;
    }

    bool wantsSelection() const override { return !center_; }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (center_)
            result.push_back(make(cursor));
        return result;
    }

private:
    DimStyle style_;
    std::unique_ptr<DimensionEntity> make(Vec2 location) const
    {
        Vec2 u = location - *center_;
        u = u.length() > 1e-12 ? u / u.length() : Vec2{1.0, 0.0};
        auto dim = std::make_unique<DimensionEntity>();
        dim->style = style_;
        dim->kind = diameter_ ? DimensionEntity::Diameter : DimensionEntity::Radius;
        dim->dimLine = diameter_ ? *center_ - u * radius_ : *center_;
        dim->curvePoint = *center_ + u * radius_;
        dim->textPos = location;
        return dim;
    }

    bool diameter_;
    std::optional<Vec2> center_;
    double radius_ = 0.0;
};


// ----------------------------------------------------------------- LEADER

// Direttrice come MLEADER/QLEADER: punta della freccia, uno o più punti, poi il testo
// (una riga per volta, Invio a vuoto per finire). Dimensioni dallo stile di quota corrente.
class LeaderCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (writing_)
            return lines_.empty() ? "Testo della direttrice:" : "Riga successiva (Invio per finire):";
        if (points_.empty())
            return "Punta della freccia:";
        if (points_.size() == 1)
            return "Fine della direttrice, dove si attacca il testo:";
        return "Punto successivo (Invio per il testo):";
    }

    bool wantsText() const override { return writing_; }

    CommandStatus start(CommandContext& ctx) override
    {
        // Stile della direttrice corrente (MLEADERSTYLE); senza: le misure dello stile di quota corrente.
        if (const auto* m = ctx.document.findMLeaderStyle(ctx.document.currentMLeaderStyle())) {
            // Stile annotativo: misure sulla carta, nel modello per la scala di annotazione corrente.
            annotative_ = m->annotative;
            const double k = annotative_ ? ctx.document.annotationFactor() : 1.0;
            textHeight_ = m->textHeight * k;
            arrowSize_ = m->arrowSize * k;
            style_ = m->name;
            textStyle_ = m->textStyle;
            landingGap_ = m->landingGap > 0.0 ? m->landingGap * k : m->landingGap;
            return CommandStatus::Continue;
        }
        const DimStyle& s = ctx.document.currentDimStyle();
        const double k = s.scale > 0.0 ? s.scale : 1.0;
        textHeight_ = s.textHeight * k;
        arrowSize_ = s.arrowSize * k;
        textStyle_ = ctx.document.currentTextStyle();
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (writing_)
            return Command::onPoint(ctx, p);
        points_.push_back(p);
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    CommandStatus onText(CommandContext&, const std::string& text) override
    {
        lines_.push_back(text);
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (!writing_) {
            if (points_.size() < 2) {
                ctx.message("Servono almeno la punta della freccia e un secondo punto.");
                return points_.empty() ? CommandStatus::Finished : CommandStatus::Continue;
            }
            writing_ = true;
            return CommandStatus::Continue;
        }
        auto leader = std::make_unique<LeaderEntity>();
        leader->points = points_;
        // Approdo orizzontale sotto il testo, come l'aggancio delle direttrici.
        const Vec2 last = points_.back();
        const Vec2 prev = points_[points_.size() - 2];
        if (!lines_.empty() && std::abs(last.y - prev.y) > 1e-9) {
            const double dir = last.x >= prev.x ? 1.0 : -1.0;
            leader->points.push_back(last + Vec2{dir * arrowSize_, 0.0});
        }
        std::string text;
        for (const std::string& l : lines_)
            text += (text.empty() ? "" : "\\P") + l;
        leader->text = text;
        leader->textHeight = textHeight_;
        leader->arrowSize = arrowSize_;
        leader->style = style_;
        leader->textStyle = textStyle_;
        leader->landingGap = landingGap_;
        if (annotative_)
            ctx.document.markAnnotative(*leader);
        ctx.document.add(std::move(leader));
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (points_.empty() || writing_)
            return result;
        LeaderEntity l;
        l.points = points_;
        l.points.push_back(cursor);
        l.arrowSize = arrowSize_;
        for (auto& part : l.geometry())
            result.push_back(std::move(part));
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        return points_.empty() || writing_ ? std::nullopt : std::optional<Vec2>(points_.back());
    }

private:
    std::vector<Vec2> points_;
    std::vector<std::string> lines_;
    bool writing_ = false;
    double textHeight_ = 2.5;
    double arrowSize_ = 2.5;
    std::string style_;
    std::string textStyle_ = "Standard";
    double landingGap_ = -1.0;
    bool annotative_ = false;   // stile della direttrice annotativo
};

// ------------------------------------------------------------- DIMTEDIT, DIMEDIT

// Copia modificata di una quota (la geometria si ricalcola: il blocco letto dal file non vale più).
void changeDimension(CommandContext& ctx, EntityId id, const std::function<void(DimensionEntity&)>& change)
{
    const Entity* e = ctx.document.find(id);
    if (!e || e->type() != EntityType::Dimension)
        return;
    auto copy = e->clone();
    auto& d = static_cast<DimensionEntity&>(*copy);
    change(d);
    d.block.reset();
    d.blockName.clear();
    std::vector<std::unique_ptr<Entity>> added;
    added.push_back(std::move(copy));
    ctx.document.commit(std::move(added), {id});
    ctx.selection.clear();   // finito: la quota non resta selezionata
}

// DIMTEDIT: sposta o ruota il testo di una quota (Sinistra/Destra lungo la linea, Centro, Origine, Angolo).
class DimTextEditCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!dim_)
            return "Quota:";
        if (angle_)
            return "Angolo del testo della quota:";
        return "Nuova posizione del testo o [Sinistra/Destra/Centro/Origine/ANgolo]:";
    }
    bool wantsSelection() const override { return !dim_; }

    CommandStatus start(CommandContext& ctx) override
    {
        if (ctx.selection.size() == 1)
            if (const Entity* e = ctx.document.find(ctx.selection.front()); e && e->type() == EntityType::Dimension)
                dim_ = e->id();
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!dim_) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (!e || e->type() != EntityType::Dimension) {
                ctx.message("Scegli una quota.");
                return CommandStatus::Continue;
            }
            dim_ = e->id();
            ctx.selection = {e->id()};
            return CommandStatus::Continue;
        }
        if (angle_)
            return Command::onPoint(ctx, p);
        changeDimension(ctx, dim_, [p](DimensionEntity& d) {
            d.textMoved = true;
            d.textPos = p;
        });
        return CommandStatus::Finished;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!dim_ || !angle_)
            return Command::onValue(ctx, v);
        changeDimension(ctx, dim_, [v](DimensionEntity& d) { d.textRotation = degToRad(v); });
        return CommandStatus::Finished;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!dim_ || angle_)
            return Command::onKeyword(ctx, w);
        if (w == "AN" || w == "ANGOLO" || w == "A" || w == "ANGLE") {
            angle_ = true;
            return CommandStatus::Continue;
        }
        if (w == "C" || w == "CENTRO" || w == "CENTER" || w == "O" || w == "ORIGINE" || w == "H" || w == "HOME") {
            const bool home = w != "C" && w != "CENTRO" && w != "CENTER";
            changeDimension(ctx, dim_, [home](DimensionEntity& d) {
                d.textMoved = false;
                if (home)
                    d.textRotation.reset();
            });
            return CommandStatus::Finished;
        }
        const bool left = w == "S" || w == "SINISTRA" || w == "L" || w == "LEFT";
        const bool right = w == "D" || w == "DESTRA" || w == "R" || w == "RIGHT";
        if (!left && !right)
            return Command::onKeyword(ctx, w);
        const auto* dim = static_cast<const DimensionEntity*>(ctx.document.find(dim_));
        if (!dim || (dim->kind != DimensionEntity::Linear && dim->kind != DimensionEntity::Aligned)) {
            ctx.message("Sinistra e Destra valgono per le quote lineari e allineate.");
            return CommandStatus::Continue;
        }
        changeDimension(ctx, dim_, [left](DimensionEntity& d) {
            auto [a, b] = d.dimensionLineEnds();
            double rot = angleOf(b - a);
            if (rot > kPi / 2.0 + 1e-9 || rot <= -kPi / 2.0 + 1e-9) {   // da sinistra a destra come si legge
                std::swap(a, b);
                rot = angleOf(b - a);
            }
            const double s = d.style.scale > 0.0 ? d.style.scale : 1.0;
            const Vec2 up = Vec2::polar(1.0, rot + kPi / 2.0);
            d.textMoved = true;
            d.textPos = a + (b - a) * (left ? 0.2 : 0.8) + up * (d.style.textGap * s + d.style.textHeight * s / 2.0);
        });
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        (void)cursor;
        return out;
    }

private:
    EntityId dim_ = 0;
    bool angle_ = false;
};

// DIMEDIT: Origine (testo al suo posto), Nuovo (testo sostitutivo), Ruota (testo), Obliqua (linee di estensione).
class DimEditCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Option: return "Modifica della quota [Origine/Nuovo/Ruota/Obliqua] <Origine>:";
        case Stage::Text: return "Nuovo testo della quota (<> = misura):";
        case Stage::Angle:
            return option_ == Option::Rotate ? "Angolo del testo della quota:"
                                             : "Inclinazione (Invio: nessuna):";
        case Stage::Select: break;
        }
        return "Quote (Invio per finire):";
    }
    bool wantsText() const override { return stage_ == Stage::Text; }
    bool wantsSelection() const override { return stage_ == Stage::Select; }
    std::optional<Vec2> selectionCorner() const override { return stage_ == Stage::Select ? picker_.corner() : std::nullopt; }

    CommandStatus start(CommandContext& ctx) override
    {
        preselected_ = ctx.selection;
        return CommandStatus::Continue;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (stage_ != Stage::Option)
            return Command::onKeyword(ctx, w);
        if (w == "O" || w == "ORIGINE" || w == "H" || w == "HOME")
            option_ = Option::Home;
        else if (w == "N" || w == "NUOVO" || w == "NEW")
            option_ = Option::New;
        else if (w == "R" || w == "RUOTA" || w == "ROTATE")
            option_ = Option::Rotate;
        else if (w == "OB" || w == "OBLIQUA" || w == "OBLIQUE")
            option_ = Option::Oblique;
        else
            return Command::onKeyword(ctx, w);
        return next(ctx);
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        text_ = text;
        stage_ = Stage::Select;
        return selectOrApply(ctx);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (stage_ != Stage::Angle)
            return Command::onValue(ctx, v);
        angle_ = degToRad(v);
        stage_ = Stage::Select;
        return selectOrApply(ctx);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (stage_ != Stage::Select)
            return Command::onPoint(ctx, p);
        const int found = picker_.click(ctx, p);
        if (found >= 0)
            ctx.message(std::to_string(found) + " trovati, " + std::to_string(ctx.selection.size()) + " in totale.");
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        switch (stage_) {
        case Stage::Option:
            option_ = Option::Home;
            return next(ctx);
        case Stage::Text:
            text_.clear();
            stage_ = Stage::Select;
            return selectOrApply(ctx);
        case Stage::Angle:
            angle_ = 0.0;
            stage_ = Stage::Select;
            return selectOrApply(ctx);
        case Stage::Select:
            break;
        }
        apply(ctx, ctx.selection);
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override
    {
        picker_.reset();
        ctx.selection.clear();
    }

private:
    enum class Stage { Option, Text, Angle, Select };
    enum class Option { Home, New, Rotate, Oblique };

    CommandStatus next(CommandContext& ctx)
    {
        if (option_ == Option::New)
            stage_ = Stage::Text;
        else if (option_ == Option::Rotate || option_ == Option::Oblique)
            stage_ = Stage::Angle;
        else
            stage_ = Stage::Select;
        return stage_ == Stage::Select ? selectOrApply(ctx) : CommandStatus::Continue;
    }

    // Quote già selezionate prima del comando: si modificano subito.
    CommandStatus selectOrApply(CommandContext& ctx)
    {
        if (preselected_.empty()) {
            ctx.selection.clear();
            return CommandStatus::Continue;
        }
        apply(ctx, preselected_);
        return CommandStatus::Finished;
    }

    void apply(CommandContext& ctx, const std::vector<EntityId>& ids)
    {
        std::vector<std::unique_ptr<Entity>> added;
        std::vector<EntityId> removed;
        for (EntityId id : ids) {
            const Entity* e = ctx.document.find(id);
            if (!e || e->type() != EntityType::Dimension)
                continue;
            auto copy = e->clone();
            auto& d = static_cast<DimensionEntity&>(*copy);
            switch (option_) {
            case Option::Home:
                d.textMoved = false;
                d.textRotation.reset();
                break;
            case Option::New:
                d.textOverride = text_;
                break;
            case Option::Rotate:
                d.textRotation = angle_;
                break;
            case Option::Oblique:
                if (d.kind != DimensionEntity::Linear && d.kind != DimensionEntity::Aligned)
                    continue;
                d.oblique = angle_;
                break;
            }
            d.block.reset();
            d.blockName.clear();
            removed.push_back(id);
            added.push_back(std::move(copy));
        }
        ctx.message(std::to_string(removed.size()) + " quote modificate.");
        if (!removed.empty())
            ctx.document.commit(std::move(added), removed);
        ctx.selection.clear();
    }

    Stage stage_ = Stage::Option;
    Option option_ = Option::Home;
    std::string text_;
    double angle_ = 0.0;
    std::vector<EntityId> preselected_;
    SelectionPicker picker_;
};

// ------------------------------------------------------------------ HATCH

// Tratteggio: si indicano punti interni alle aree da tratteggiare (il contorno e le isole
// vengono trovati in automatico), oppure si selezionano oggetti chiusi. Invio crea il tratteggio.
class HatchCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (mode_) {
        case Mode::Objects: return "Oggetti chiusi (Invio per finire):";
        case Mode::Pattern: return "Motivo [Solido/ANSI31/ANSI32/ANSI37/ANSI38/NET/LINE/DOTS/BRICK/SQUARE/EARTH...] <" + gHatchPattern + ">:";
        case Mode::Scale: return "Scala del motivo <" + number(gHatchScale) + ">:";
        case Mode::Angle: return "Angolo del motivo <" + number(gHatchAngle) + ">:";
        case Mode::Points: break;
        }
        return std::string(loops_.empty() ? "Punto interno" : "Altro punto interno")
            + " o [Oggetti/Motivo/Scala/ANgolo/Annulla] <" + gHatchPattern + ">:";
    }

    bool wantsSelection() const override { return mode_ == Mode::Objects; }
    std::optional<Vec2> selectionCorner() const override
    {
        return mode_ == Mode::Objects ? picker_.corner() : std::nullopt;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (mode_ == Mode::Objects) {
            const int found = picker_.click(ctx, p);
            if (found > 0)
                ctx.message(std::to_string(ctx.selection.size()) + " selezionati.");
            return CommandStatus::Continue;
        }
        if (mode_ != Mode::Points)
            return Command::onPoint(ctx, p);
        const auto boundary = findHatchBoundary(ctx.document.entitiesNear(p, 1e300), p);
        if (boundary.empty()) {
            ctx.message("Contorno non trovato: il punto deve stare in un'area chiusa.");
            return CommandStatus::Continue;
        }
        std::vector<std::vector<PolyVertex>> area;
        for (const auto& polygon : boundary) {
            std::vector<PolyVertex> loop;
            for (Vec2 v : polygon)
                loop.push_back({v, 0.0});
            area.push_back(std::move(loop));
        }
        const std::size_t islands = boundary.size() - 1;
        ctx.message(islands == 0   ? std::string("Contorno trovato.")
                    : islands == 1 ? std::string("Contorno trovato con 1 isola.")
                                   : "Contorno trovato con " + std::to_string(islands) + " isole.");
        loops_.push_back(std::move(area));
        return CommandStatus::Continue;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (mode_ == Mode::Pattern) {
            if (w == "S" || w == "SOLIDO" || w == "SOLID")
                gHatchPattern = "SOLID";
            else if (std::find(standardHatchPatternNames().begin(), standardHatchPatternNames().end(), w)
                     != standardHatchPatternNames().end())
                gHatchPattern = w;
            else
                return Command::onKeyword(ctx, w);
            mode_ = Mode::Points;
            return CommandStatus::Continue;
        }
        if (mode_ == Mode::Points) {
            if (w == "O" || w == "OGGETTI" || w == "S" || w == "SELECT") {
                mode_ = Mode::Objects;
                return CommandStatus::Continue;
            }
            if (w == "M" || w == "MOTIVO" || w == "P" || w == "PATTERN") {
                // Con l'interfaccia: finestra con le anteprime dei motivi (anche scala e angolo).
                if (ctx.chooseHatchPattern && ctx.chooseHatchPattern())
                    return CommandStatus::Continue;
                mode_ = Mode::Pattern;
                return CommandStatus::Continue;
            }
            if (w == "SC" || w == "SCALA" || w == "SCALE") {
                mode_ = Mode::Scale;
                return CommandStatus::Continue;
            }
            if (w == "AN" || w == "ANGOLO" || w == "ANGLE") {
                mode_ = Mode::Angle;
                return CommandStatus::Continue;
            }
            if (w == "A" || w == "ANNULLA" || w == "U" || w == "UNDO") {
                if (!loops_.empty()) {
                    loops_.pop_back();
                    ctx.message("Ultima area tolta.");
                }
                return CommandStatus::Continue;
            }
            // Il nome di un motivo vale anche direttamente.
            if (w == "SOLIDO" || std::find(standardHatchPatternNames().begin(), standardHatchPatternNames().end(), w)
                                     != standardHatchPatternNames().end()) {
                gHatchPattern = w == "SOLIDO" ? "SOLID" : w;
                return CommandStatus::Continue;
            }
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (mode_ == Mode::Scale && v > 0.0) {
            gHatchScale = v;
            mode_ = Mode::Points;
            return CommandStatus::Continue;
        }
        if (mode_ == Mode::Angle) {
            gHatchAngle = v;
            mode_ = Mode::Points;
            return CommandStatus::Continue;
        }
        return Command::onValue(ctx, v);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (mode_ == Mode::Objects) {
            picker_.reset();
            for (EntityId id : ctx.selection)
                if (const Entity* e = ctx.document.find(id)) {
                    auto loop = loopOf(*e);
                    if (loop.empty())
                        ++skipped_;
                    else
                        loops_.push_back({std::move(loop)});
                }
            ctx.selection.clear();
            if (skipped_ > 0)
                ctx.message(std::to_string(skipped_) + " oggetti ignorati: servono cerchi, ellissi o polilinee chiuse.");
            return create(ctx);
        }
        if (mode_ != Mode::Points) {
            mode_ = Mode::Points;   // Invio = valore attuale
            return CommandStatus::Continue;
        }
        return create(ctx);
    }

    void cancel(CommandContext& ctx) override
    {
        picker_.reset();
        if (mode_ == Mode::Objects)
            ctx.selection.clear();
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2) const override
    {
        // Contorni già trovati, evidenziati mentre si continua a indicare punti.
        std::vector<std::unique_ptr<Entity>> result;
        for (const auto& area : loops_)
            for (const auto& loop : area)
                result.push_back(std::make_unique<PolylineEntity>(loop, true));
        return result;
    }

private:
    enum class Mode { Points, Objects, Pattern, Scale, Angle };

    static std::string number(double v)
    {
        std::ostringstream out;
        out << v;
        return out.str();
    }

    static std::vector<PolyVertex> loopOf(const Entity& e)
    {
        switch (e.type()) {
        case EntityType::Circle: {
            const auto& c = static_cast<const CircleEntity&>(e);
            return {{c.center + Vec2{c.radius, 0.0}, 1.0}, {c.center - Vec2{c.radius, 0.0}, 1.0}};
        }
        case EntityType::Polyline: {
            const auto& p = static_cast<const PolylineEntity&>(e);
            if (p.closed && p.vertices.size() >= 3)
                return p.vertices;
            break;
        }
        default:
            break;
        }
        // Ellissi e spline chiuse: contorno approssimato.
        std::vector<PolyVertex> loop;
        for (Vec2 v : closedOutline(e))
            loop.push_back({v, 0.0});
        return loop;
    }

    // Un tratteggio con tutte le aree indicate ("Crea tratteggi separati" spento).
    CommandStatus create(CommandContext& ctx)
    {
        if (loops_.empty()) {
            ctx.message("Nessuna area da tratteggiare.");
            return CommandStatus::Finished;
        }
        auto hatch = std::make_unique<HatchEntity>();
        for (auto& area : loops_)
            for (auto& loop : area)
                hatch->loops.push_back(std::move(loop));
        hatch->solid = gHatchPattern == "SOLID";
        hatch->pattern = gHatchPattern;
        hatch->patternScale = gHatchScale;
        hatch->patternAngle = degToRad(gHatchAngle);
        if (!hatch->solid)
            hatch->patternLines = standardHatchPattern(gHatchPattern, gHatchScale, hatch->patternAngle);
        ctx.document.add(std::move(hatch));
        return CommandStatus::Finished;
    }

    Mode mode_ = Mode::Points;
    std::vector<std::vector<std::vector<PolyVertex>>> loops_;   // aree (contorno + isole)
    SelectionPicker picker_;
    int skipped_ = 0;
};

// ---------------------------------------------------------------- BOUNDARY

// BOUNDARY (CONTORNO): un clic dentro un'area chiusa crea polilinee chiuse sul suo contorno e sulle isole,
// con la stessa ricerca del tratteggio per punto interno.
class BoundaryCommand final : public Command {
public:
    std::string prompt() const override { return "Punto interno:"; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const auto boundary = findHatchBoundary(ctx.document.entitiesNear(p, 1e300), p);
        if (boundary.empty()) {
            ctx.message("Contorno non trovato: il punto deve stare in un'area chiusa.");
            return CommandStatus::Continue;
        }
        std::vector<std::unique_ptr<Entity>> added;
        for (const auto& polygon : boundary) {
            std::vector<PolyVertex> loop;
            for (Vec2 v : polygon)
                loop.push_back({v, 0.0});
            auto pl = std::make_unique<PolylineEntity>(std::move(loop), true);
            pl->setLayer(ctx.document.currentLayer());
            added.push_back(std::move(pl));
        }
        created_ += static_cast<int>(added.size());
        ctx.document.commit(std::move(added));
        ctx.message(boundary.size() == 1 ? std::string("Contorno trovato.")
                                         : "Contorno trovato con " + std::to_string(boundary.size() - 1)
                                               + (boundary.size() == 2 ? " isola." : " isole."));
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        ctx.message("BOUNDARY ha creato " + std::to_string(created_) + (created_ == 1 ? " polilinea." : " polilinee."));
        return CommandStatus::Finished;
    }

private:
    int created_ = 0;
};

// --------------------------------------------------------------- HATCHEDIT

// HATCHEDIT (MODTRATT): motivo, scala e angolo di un tratteggio esistente.
class HatchEditCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!target_)
            return "Oggetto tratteggio:";
        const HatchEntity* h = hatch();
        switch (mode_) {
        case Mode::Pattern:
            return "Motivo [Solido/" + joinedNames() + "] <" + (h ? h->pattern : std::string()) + ">:";
        case Mode::Scale: return "Scala del motivo <" + number(h ? h->patternScale : 1.0) + ">:";
        case Mode::Angle: return "Angolo del motivo <" + number(h ? radToDeg(h->patternAngle) : 0.0) + ">:";
        case Mode::Options: break;
        }
        return "Opzione [Motivo/Scala/ANgolo] <Fine>:";
    }

    bool wantsSelection() const override { return !target_; }

    CommandStatus start(CommandContext& ctx) override
    {
        doc_ = &ctx.document;
        // Un tratteggio già selezionato si modifica subito.
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id); e && e->type() == EntityType::Hatch) {
                target_ = id;
                break;
            }
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        doc_ = &ctx.document;
        if (target_)
            return Command::onPoint(ctx, p);
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e || e->type() != EntityType::Hatch) {
            ctx.message(e ? "L'oggetto selezionato non è un tratteggio." : "Nessun oggetto qui.");
            return CommandStatus::Continue;
        }
        target_ = e->id();
        return CommandStatus::Continue;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        const HatchEntity* h = hatch();
        if (!h)
            return Command::onKeyword(ctx, w);
        if (mode_ == Mode::Pattern) {
            const std::string name = w == "S" || w == "SOLIDO" ? "SOLID" : w;
            if (!apply(ctx, name, h->patternScale, h->patternAngle))
                ctx.message("Motivo sconosciuto: " + w + ".");
            mode_ = Mode::Options;
            return CommandStatus::Continue;
        }
        if (mode_ == Mode::Options) {
            if (w == "M" || w == "MOTIVO" || w == "P" || w == "PATTERN")
                mode_ = Mode::Pattern;
            else if (w == "S" || w == "SC" || w == "SCALA" || w == "SCALE")
                mode_ = Mode::Scale;
            else if (w == "AN" || w == "ANGOLO" || w == "ANGLE")
                mode_ = Mode::Angle;
            else
                return Command::onKeyword(ctx, w);
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        const HatchEntity* h = hatch();
        if (!h || (mode_ != Mode::Scale && mode_ != Mode::Angle))
            return Command::onValue(ctx, v);
        if (mode_ == Mode::Scale && v <= 0.0) {
            ctx.message("La scala deve essere maggiore di zero.");
            return CommandStatus::Continue;
        }
        if (mode_ == Mode::Scale)
            apply(ctx, h->pattern, v, h->patternAngle);
        else
            apply(ctx, h->pattern, h->patternScale, degToRad(v));
        mode_ = Mode::Options;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext&) override
    {
        if (target_ && mode_ != Mode::Options) {   // Invio: valore attuale
            mode_ = Mode::Options;
            return CommandStatus::Continue;
        }
        return CommandStatus::Finished;
    }

private:
    enum class Mode { Options, Pattern, Scale, Angle };

    static std::string number(double v)
    {
        std::ostringstream out;
        out << v;
        return out.str();
    }

    static std::string joinedNames()
    {
        std::string s;
        for (const std::string& n : standardHatchPatternNames()) {
            if (n == "SOLID")
                continue;
            if (s.size() > 60) {
                s += "...";
                break;
            }
            s += (s.empty() ? "" : "/") + n;
        }
        return s;
    }

    const HatchEntity* hatch() const
    {
        if (!target_ || !doc_)
            return nullptr;
        const Entity* e = doc_->find(*target_);
        return e && e->type() == EntityType::Hatch ? static_cast<const HatchEntity*>(e) : nullptr;
    }

    bool apply(CommandContext& ctx, const std::string& name, double scale, double angle)
    {
        const HatchEntity* h = hatch();
        if (!h)
            return false;
        auto copy = std::make_unique<HatchEntity>(*h);
        if (!copy->setPattern(name, scale, angle))
            return false;
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(copy));
        ctx.document.commit(std::move(added), {*target_});
        return true;
    }

    const Document* doc_ = nullptr;
    std::optional<EntityId> target_;
    Mode mode_ = Mode::Options;
};

} // namespace

double& textSizeSetting()
{
    static double size = 2.5;
    return size;
}

// ------------------------------------------------------------------ MULTIDIRETTRICI

// Direttrice sotto il punto (linee, frecce o testo), oppure nullptr.
const LeaderEntity* pickLeader(CommandContext& ctx, Vec2 p)
{
    const LeaderEntity* best = nullptr;
    double bestDistance = ctx.pickTolerance;
    for (const Entity* e : ctx.document.entitiesNear(p, ctx.pickTolerance * 4.0))
        if (e->type() == EntityType::Leader && e->distanceTo(p) <= bestDistance) {
            bestDistance = e->distanceTo(p);
            best = static_cast<const LeaderEntity*>(e);
        }
    return best;
}

// Indice del punto d'attacco nella direttrice principale.
std::size_t junctionIndex(const LeaderEntity& l)
{
    const Vec2 j = l.junction();
    for (std::size_t i = 0; i < l.points.size(); ++i)
        if (distance(l.points[i], j) < 1e-12)
            return i;
    return l.points.empty() ? 0 : l.points.size() - 1;
}

double segmentDistance(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double len2 = ab.dot(ab);
    const double t = len2 > 0.0 ? std::clamp((p - a).dot(ab) / len2, 0.0, 1.0) : 0.0;
    return distance(p, a + ab * t);
}

// MLEADERADD: altre frecce sulla stessa direttrice (stesso testo).
class LeaderAddCommand final : public Command {
public:
    std::string prompt() const override
    {
        return leader_ ? "Punta della freccia (Invio per finire):" : "Direttrice:";
    }
    bool wantsSelection() const override { return !leader_; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!leader_) {
            const LeaderEntity* l = pickLeader(ctx, p);
            if (!l) {
                ctx.message("Nessuna direttrice qui.");
                return CommandStatus::Continue;
            }
            leader_ = *l;
            id_ = l->id();
            return CommandStatus::Continue;
        }
        leader_->extraTips.push_back(p);
        ++added_;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (leader_ && added_ > 0) {
            std::vector<std::unique_ptr<Entity>> changed;
            changed.push_back(leader_->clone());
            ctx.document.commit(std::move(changed), {id_});
            ctx.message(std::to_string(added_) + " frecce aggiunte.");
        }
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (leader_) {
            auto l = std::make_unique<LeaderEntity>(*leader_);
            l->extraTips.push_back(cursor);
            out.push_back(std::move(l));
        }
        return out;
    }

private:
    std::optional<LeaderEntity> leader_;
    EntityId id_ = 0;
    int added_ = 0;
};

// MLEADERREMOVE: toglie frecce da una direttrice (ne resta sempre almeno una).
class LeaderRemoveCommand final : public Command {
public:
    std::string prompt() const override
    {
        return leader_ ? "Freccia da rimuovere (Invio per finire):" : "Direttrice:";
    }
    bool wantsSelection() const override { return true; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!leader_) {
            const LeaderEntity* l = pickLeader(ctx, p);
            if (!l) {
                ctx.message("Nessuna direttrice qui.");
                return CommandStatus::Continue;
            }
            leader_ = *l;
            id_ = l->id();
            return CommandStatus::Continue;
        }
        LeaderEntity& l = *leader_;
        if (l.extraTips.empty()) {
            ctx.message("La direttrice ha una sola freccia.");
            return CommandStatus::Continue;
        }
        // La freccia più vicina: una delle aggiunte, o la principale (allora una delle altre ne prende il posto).
        const Vec2 join = l.junction();
        std::size_t best = 0;
        double bestDistance = 1e300;
        for (std::size_t k = 0; k < l.extraTips.size(); ++k)
            if (const double d = segmentDistance(p, l.extraTips[k], join); d < bestDistance) {
                bestDistance = d;
                best = k + 1;
            }
        const std::size_t j = junctionIndex(l);
        for (std::size_t i = 0; i < j; ++i)
            if (const double d = segmentDistance(p, l.points[i], l.points[i + 1]); d < bestDistance) {
                bestDistance = d;
                best = 0;
            }
        if (best > 0) {
            l.extraTips.erase(l.extraTips.begin() + static_cast<std::ptrdiff_t>(best - 1));
        } else {
            std::vector<Vec2> points{l.extraTips.front()};
            points.insert(points.end(), l.points.begin() + static_cast<std::ptrdiff_t>(j), l.points.end());
            l.points = std::move(points);
            l.extraTips.erase(l.extraTips.begin());
        }
        ++removed_;
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (leader_ && removed_ > 0) {
            std::vector<std::unique_ptr<Entity>> changed;
            changed.push_back(leader_->clone());
            ctx.document.commit(std::move(changed), {id_});
            ctx.message(std::to_string(removed_) + " frecce rimosse.");
        }
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (leader_ && removed_ > 0)
            out.push_back(std::make_unique<LeaderEntity>(*leader_));
        return out;
    }

private:
    std::optional<LeaderEntity> leader_;
    EntityId id_ = 0;
    int removed_ = 0;
};

// Sposta testo e tratto d'approdo di una direttrice (dal punto d'attacco in poi): le frecce restano dove sono.
void moveLeaderText(LeaderEntity& l, Vec2 d)
{
    const std::size_t j = junctionIndex(l);
    for (std::size_t i = j; i < l.points.size(); ++i)
        l.points[i] = l.points[i] + d;
}

// MLEADERALIGN: allinea i testi delle direttrici selezionate a quello di una direttrice di riferimento.
class LeaderAlignCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id); e && e->type() == EntityType::Leader)
                ids_.push_back(id);
        ctx.selection.clear();
        if (ids_.size() < 2) {
            ctx.message("Scegli almeno due direttrici.");
            return CommandStatus::Finished;
        }
        return CommandStatus::Continue;
    }
    std::string actionPrompt() const override
    {
        return reference_ ? "Direzione [Verticale/Orizzontale] <Verticale>:" : "Direttrice a cui allinearsi:";
    }
    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (reference_)
            return apply(ctx, true);
        const LeaderEntity* l = pickLeader(ctx, p);
        if (!l) {
            ctx.message("Nessuna direttrice qui.");
            return CommandStatus::Continue;
        }
        reference_ = l->junction();
        return CommandStatus::Continue;
    }
    CommandStatus actionKeyword(CommandContext& ctx, const std::string& word) override
    {
        std::string w = word;
        for (char& c : w)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (!reference_)
            return Command::onKeyword(ctx, word);
        return apply(ctx, !(w == "O" || w == "ORIZZONTALE" || w == "H" || w == "HORIZONTAL"));
    }
    CommandStatus actionEnter(CommandContext& ctx) override
    {
        return reference_ ? apply(ctx, true) : CommandStatus::Finished;
    }

private:
    // Verticale: i punti d'attacco sulla stessa verticale (testi in colonna); orizzontale: sulla stessa riga.
    CommandStatus apply(CommandContext& ctx, bool vertical)
    {
        std::vector<std::unique_ptr<Entity>> changed;
        std::vector<EntityId> ids;
        for (EntityId id : ids_) {
            const Entity* e = ctx.document.find(id);
            if (!e)
                continue;
            auto copy = std::make_unique<LeaderEntity>(static_cast<const LeaderEntity&>(*e));
            const Vec2 j = copy->junction();
            const Vec2 d = vertical ? Vec2{reference_->x - j.x, 0.0} : Vec2{0.0, reference_->y - j.y};
            if (d.length() < 1e-12)
                continue;
            moveLeaderText(*copy, d);
            changed.push_back(std::move(copy));
            ids.push_back(id);
        }
        if (!ids.empty())
            ctx.document.commit(std::move(changed), ids);
        ctx.message(std::to_string(ids.size()) + " direttrici allineate.");
        return CommandStatus::Finished;
    }
    std::vector<EntityId> ids_;
    std::optional<Vec2> reference_;
};

// MLEADERCOLLECT: più direttrici in una sola, con tutte le frecce; il testo è quello della prima (più gli altri
// testi diversi, sotto).
class LeaderCollectCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        std::vector<const LeaderEntity*> leaders;
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id); e && e->type() == EntityType::Leader)
                leaders.push_back(static_cast<const LeaderEntity*>(e));
        ctx.selection.clear();
        if (leaders.size() < 2) {
            ctx.message("Scegli almeno due direttrici.");
            return CommandStatus::Finished;
        }
        auto main = std::make_unique<LeaderEntity>(*leaders.front());
        std::vector<EntityId> removed{leaders.front()->id()};
        for (std::size_t k = 1; k < leaders.size(); ++k) {
            const LeaderEntity& l = *leaders[k];
            if (!l.points.empty())
                main->extraTips.push_back(l.points.front());
            main->extraTips.insert(main->extraTips.end(), l.extraTips.begin(), l.extraTips.end());
            if (!l.text.empty() && main->text.find(l.text) == std::string::npos)
                main->text += (main->text.empty() ? "" : "\\P") + l.text;
            removed.push_back(l.id());
        }
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(main));
        ctx.document.commit(std::move(added), removed);
        ctx.message(std::to_string(leaders.size()) + " direttrici raccolte in una.");
        return CommandStatus::Finished;
    }
};

// ------------------------------------------------------------------ LINEE D'ASSE

// Tipo di linea d'asse (CENTER) caricato, oppure continua.
std::string centerLinetype(Document& doc)
{
    if (doc.findLinetype("CENTER") || doc.loadStandardLinetype("CENTER"))
        return doc.findLinetype("CENTER")->name;
    return "ByLayer";
}

// CENTERMARK: segno di centro (croce che sporge oltre il bordo) sui cerchi e sugli archi selezionati.
class CenterMarkCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        Document& doc = ctx.document;
        std::vector<std::unique_ptr<Entity>> added;
        const std::string lt = centerLinetype(doc);
        for (EntityId id : ctx.selection) {
            const Entity* e = doc.find(id);
            Vec2 c;
            double r = 0.0;
            if (e && e->type() == EntityType::Circle) {
                c = static_cast<const CircleEntity*>(e)->center;
                r = static_cast<const CircleEntity*>(e)->radius;
            } else if (e && e->type() == EntityType::Arc) {
                c = static_cast<const ArcEntity*>(e)->center;
                r = static_cast<const ArcEntity*>(e)->radius;
            } else {
                continue;
            }
            const double ext = r + std::max(r * 0.15, 1.0);   // oltre il bordo (CENTEREXE)
            for (const Vec2 d : {Vec2{1.0, 0.0}, Vec2{0.0, 1.0}}) {
                auto line = std::make_unique<LineEntity>(c - d * ext, c + d * ext);
                line->setLinetype(lt);
                added.push_back(std::move(line));
            }
        }
        ctx.selection.clear();
        if (added.empty()) {
            ctx.message("Scegli cerchi o archi.");
            return CommandStatus::Finished;
        }
        const std::size_t n = added.size() / 2;
        doc.commit(std::move(added));
        ctx.message(std::to_string(n) + " segni di centro creati.");
        return CommandStatus::Finished;
    }
};

// CENTERLINE: linea d'asse fra due linee (a metà fra le due, lunga quanto le loro proiezioni, un po' di più).
class CenterLineCommand final : public Command {
public:
    std::string prompt() const override
    {
        return first_ ? "Seconda linea:" : "Prima linea:";
    }
    bool wantsSelection() const override { return true; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const LineEntity* hit = nullptr;
        double best = ctx.pickTolerance;
        for (const Entity* e : ctx.document.entitiesNear(p, ctx.pickTolerance))
            if (e->type() == EntityType::Line && e->distanceTo(p) <= best) {
                best = e->distanceTo(p);
                hit = static_cast<const LineEntity*>(e);
            }
        if (!hit) {
            ctx.message("Nessuna linea qui.");
            return CommandStatus::Continue;
        }
        if (!first_) {
            first_ = *hit;
            return CommandStatus::Continue;
        }
        const LineEntity& a = *first_;
        const LineEntity& b = *hit;
        Vec2 da = a.end - a.start, db = b.end - b.start;
        if (da.length() < 1e-12 || db.length() < 1e-12)
            return CommandStatus::Finished;
        da = da / da.length();
        db = db / db.length();
        if (da.dot(db) < 0.0)
            db = db * -1.0;
        Vec2 dir = da + db;
        dir = dir / dir.length();
        const Vec2 mid = (a.start + a.end + b.start + b.end) / 4.0;
        double lo = 1e300, hi = -1e300;
        for (Vec2 q : {a.start, a.end, b.start, b.end}) {
            const double t = (q - mid).dot(dir);
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
        const double ext = std::max((hi - lo) * 0.05, 1.0);
        auto line = std::make_unique<LineEntity>(mid + dir * (lo - ext), mid + dir * (hi + ext));
        line->setLinetype(centerLinetype(ctx.document));
        ctx.document.add(std::move(line));
        ctx.message("Linea d'asse creata.");
        return CommandStatus::Finished;
    }

private:
    std::optional<LineEntity> first_;
};

// -VIEW: viste con nome dalla riga di comando: ? elenco, Salva, Ripristina, Elimina.
class ViewCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Save: return "Nome della vista da salvare:";
        case Stage::Restore: return "Nome della vista da ripristinare:";
        case Stage::Remove: return "Nome della vista da eliminare:";
        default: return "Opzione [?/Salva/Ripristina/Elimina] <Fine>:";
        }
    }
    bool wantsText() const override { return true; }
    bool textIsLine() const override { return stage_ != Stage::Option; }

    CommandStatus onEnter(CommandContext&) override
    {
        if (stage_ == Stage::Option)
            return CommandStatus::Finished;
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        std::string v = text;
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
            v.pop_back();
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
            v.erase(v.begin());
        Document& doc = ctx.document;
        if (stage_ == Stage::Option) {
            std::string o = v;
            for (char& c : o)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (o == "?") {
                std::string list;
                for (const auto& nv : doc.namedViews())
                    list += (list.empty() ? "" : ", ") + nv.name;
                ctx.message(list.empty() ? "Nessuna vista con nome." : "Viste: " + list + ".");
            } else if (o == "S" || o == "SALVA" || o == "SAVE") {
                stage_ = Stage::Save;
            } else if (o == "R" || o == "RIPRISTINA" || o == "RESTORE") {
                stage_ = Stage::Restore;
            } else if (o == "E" || o == "ELIMINA" || o == "D" || o == "DELETE") {
                stage_ = Stage::Remove;
            } else {
                ctx.message("Opzione non riconosciuta.");
            }
            return CommandStatus::Continue;
        }
        if (v.empty()) {
            stage_ = Stage::Option;
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Save) {
            if (!ctx.visibleArea)
                return CommandStatus::Finished;
            const BBox area = ctx.visibleArea();
            Document::NamedView nv{v, area.center(), area.width(), area.height(), ctx.shownSpace ? ctx.shownSpace() : 0};
            doc.setNamedView(nv);
            ctx.message("Vista \"" + v + "\" salvata.");
        } else if (stage_ == Stage::Restore) {
            const Document::NamedView* nv = doc.findNamedView(v);
            if (!nv) {
                ctx.message("Vista \"" + v + "\" inesistente.");
            } else if (ctx.zoomWindow) {
                BBox box;
                box.expand(nv->center - Vec2{nv->width / 2.0, nv->height / 2.0});
                box.expand(nv->center + Vec2{nv->width / 2.0, nv->height / 2.0});
                ctx.zoomWindow(box);
            }
        } else {
            ctx.message(doc.removeNamedView(v) ? "Vista \"" + v + "\" eliminata." : "Vista \"" + v + "\" inesistente.");
        }
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

private:
    enum class Stage { Option, Save, Restore, Remove };
    Stage stage_ = Stage::Option;
};

void registerAnnotateCommands(CommandManager& manager)
{
    manager.add("CENTERMARK", {"CM", "SEGNOCENTRO"}, [] { return std::make_unique<CenterMarkCommand>(); });
    manager.add("MLEADERADD", {"AGGIUNGIDIRETTRICE"}, [] { return std::make_unique<LeaderAddCommand>(); });
    manager.add("MLEADERREMOVE", {"RIMUOVIDIRETTRICE"}, [] { return std::make_unique<LeaderRemoveCommand>(); });
    manager.add("MLEADERALIGN", {"MLA", "ALLINEADIRETTRICI"}, [] { return std::make_unique<LeaderAlignCommand>(); });
    manager.add("MLEADERCOLLECT", {"MLC", "RACCOGLIDIRETTRICI"}, [] { return std::make_unique<LeaderCollectCommand>(); });
    manager.add("CENTERLINE", {"CL", "LINEAASSE"}, [] { return std::make_unique<CenterLineCommand>(); });
    manager.add("-VIEW", {"-V", "-VISTA"}, [] { return std::make_unique<ViewCommand>(); });
    manager.add("TEXT", {"DT", "DTEXT", "TESTO"}, [] { return std::make_unique<TextCommand>(); });
    manager.add("MTEXT", {"T", "MT", "TESTOM"}, [] { return std::make_unique<MTextCommand>(); });
    manager.add("DIMLINEAR", {"DLI", "QUOTALIN"}, [] { return std::make_unique<LinearDimCommand>(false); });
    manager.add("DIMALIGNED", {"DAL", "QUOTALL"}, [] { return std::make_unique<LinearDimCommand>(true); });
    manager.add("DIMRADIUS", {"DRA", "QUOTARAG"}, [] { return std::make_unique<RadialDimCommand>(false); });
    manager.add("DIMANGULAR", {"DAN", "QUOTANG"}, [] { return std::make_unique<AngularDimCommand>(); });
    manager.add("DIMORDINATE", {"DOR", "QUOTACOORD"}, [] { return std::make_unique<OrdinateDimCommand>(); });
    manager.add("DIMCONTINUE", {"DCO", "QUOTACONT"}, [] { return std::make_unique<SeriesDimCommand>(false); });
    manager.add("DIMBASELINE", {"DBA", "QUOTABASE"}, [] { return std::make_unique<SeriesDimCommand>(true); });
    manager.add("QDIM", {"QUOTARAPIDA"}, [] { return std::make_unique<QDimCommand>(); });
    manager.add("DIMDIAMETER", {"DDI", "QUOTADIA"}, [] { return std::make_unique<RadialDimCommand>(true); });
    manager.add("LEADER", {"LE", "MLEADER", "MLD", "QLEADER", "DIRETTRICE"}, [] { return std::make_unique<LeaderCommand>(); });
    manager.add("DIMTEDIT", {"DIMTED", "MODTESTOQUOTA"}, [] { return std::make_unique<DimTextEditCommand>(); });
    manager.add("DIMEDIT", {"DED", "MODQUOTA"}, [] { return std::make_unique<DimEditCommand>(); });
    manager.add("HATCH", {"H", "TRATTEGGIO", "TRATT"}, [] { return std::make_unique<HatchCommand>(); });
    manager.add("BOUNDARY", {"BO", "BPOLY", "CONTORNO"}, [] { return std::make_unique<BoundaryCommand>(); });
    manager.add("HATCHEDIT", {"HE", "MODTRATT"}, [] { return std::make_unique<HatchEditCommand>(); });
}

} // namespace cartesio
