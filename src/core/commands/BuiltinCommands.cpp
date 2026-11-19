// Comandi di disegno (LINE, CIRCLE, ARC) e di visualizzazione (UNDO, REDO, ZOOM).
// I comandi di modifica sono in ModifyCommands.cpp.
#include "core/commands/CommandManager.h"

#include "core/Geometry.h"
#include <sstream>
#include <locale>

namespace cartesio {

namespace {

// ------------------------------------------------------------------- LINE

class LineCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!current_)
            return "Primo punto:";
        if (segments_ >= 2)
            return "Punto successivo o [Chiudi]:";
        return "Punto successivo:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (current_) {
            if (p == *current_) {
                ctx.message("Il punto coincide con il precedente.");
                return CommandStatus::Continue;
            }
            ctx.document.add(std::make_unique<LineEntity>(*current_, p));
            ++segments_;
        } else {
            first_ = p;
        }
        current_ = p;
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    // Immissione diretta della distanza: lunghezza lungo la direzione del cursore.
    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!current_) {
            ctx.message("Indica un punto.");
            return CommandStatus::Continue;
        }
        const Vec2 dir = ctx.cursor - *current_;
        const double len = dir.length();
        if (len < 1e-12) {
            ctx.message("Muovi il cursore verso la direzione voluta.");
            return CommandStatus::Continue;
        }
        return onPoint(ctx, *current_ + dir * (v / len));
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& word) override
    {
        if ((word == "C" || word == "CHIUDI" || word == "CLOSE") && segments_ >= 2 && first_) {
            onPoint(ctx, *first_);
            return CommandStatus::Finished;
        }
        return Command::onKeyword(ctx, word);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (current_)
            result.push_back(std::make_unique<LineEntity>(*current_, cursor));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return current_; }

private:
    std::optional<Vec2> first_;
    std::optional<Vec2> current_;
    int segments_ = 0;
};

// ----------------------------------------------------------------- CIRCLE

class CircleCommand final : public Command {
public:
    std::string prompt() const override
    {
        return center_ ? "Raggio del cerchio:" : "Centro del cerchio:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!center_) {
            center_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        return create(ctx, distance(*center_, p));
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!center_) {
            ctx.message("Indica un punto.");
            return CommandStatus::Continue;
        }
        return create(ctx, v);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (center_) {
            const double r = distance(*center_, cursor);
            if (r > 0.0)
                result.push_back(std::make_unique<CircleEntity>(*center_, r));
            result.push_back(std::make_unique<LineEntity>(*center_, cursor));
        }
        return result;
    }

    std::optional<Vec2> basePoint() const override { return center_; }

private:
    CommandStatus create(CommandContext& ctx, double radius)
    {
        if (radius <= 0.0) {
            ctx.message("Il raggio deve essere maggiore di zero.");
            return CommandStatus::Continue;
        }
        ctx.document.add(std::make_unique<CircleEntity>(*center_, radius));
        return CommandStatus::Finished;
    }

    std::optional<Vec2> center_;
};

// -------------------------------------------------------------------- ARC

// Arco per tre punti: iniziale, un punto intermedio, finale.
class ArcCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (points_.size()) {
        case 0: return "Punto iniziale dell'arco:";
        case 1: return "Secondo punto dell'arco:";
        default: return "Punto finale dell'arco:";
        }
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!points_.empty() && p == points_.back()) {
            ctx.message("Il punto coincide con il precedente.");
            return CommandStatus::Continue;
        }
        if (points_.size() < 2) {
            points_.push_back(p);
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        auto arc = arcThroughPoints(points_[0], points_[1], p);
        if (!arc) {
            ctx.message("I tre punti sono allineati: impossibile creare l'arco.");
            return CommandStatus::Continue;
        }
        ctx.document.add(std::move(arc));
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (points_.size() == 2) {
            if (auto arc = arcThroughPoints(points_[0], points_[1], cursor)) {
                result.push_back(std::move(arc));
                return result;
            }
        }
        if (!points_.empty())
            result.push_back(std::make_unique<LineEntity>(points_.back(), cursor));
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
};

// ------------------------------------------------------------------ PLINE

// Larghezze correnti di PLINE (come PLINEWID: si ricordano tra un comando e l'altro).
double gPlineStartWidth = 0.0;
double gPlineEndWidth = 0.0;

std::string numberText(double v)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(10);
    out << v;
    return out.str();
}

// Polilinea con tratti rettilinei e ad arco (tangenti al tratto precedente), con larghezza iniziale e finale
// (opzioni Larghezza e Mezza larghezza: dopo un tratto rastremato si prosegue con la finale).
class PolylineCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (widthStep_ != 0) {
            const bool start = widthStep_ == 1;
            const double shown = start ? gPlineStartWidth : (startTyped_ ? newStart_ : gPlineEndWidth);
            return std::string(half_ ? "Mezza larghezza" : "Larghezza") + (start ? " iniziale" : " finale")
                   + " <" + numberText(half_ ? shown / 2.0 : shown) + ">:";
        }
        if (vertices_.empty())
            return "Punto iniziale:";
        const std::string options = vertices_.size() >= 3 ? "/Chiudi/Mezza larghezza/LArghezza/Annulla]:"
                                                           : "/Mezza larghezza/LArghezza/Annulla]:";
        if (arcMode_)
            return "Punto finale dell'arco o [Linea" + options;
        return "Punto successivo o [Arco" + options;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (widthStep_ != 0) {
            ctx.message("Scrivi un valore.");
            return CommandStatus::Continue;
        }
        if (!vertices_.empty() && p == vertices_.back().point) {
            ctx.message("Il punto coincide con il precedente.");
            return CommandStatus::Continue;
        }
        if (!vertices_.empty()) {
            vertices_.back().bulge = arcMode_ ? arcBulge(p) : 0.0;
            closeSegmentWidths();
        }
        vertices_.push_back({p, 0.0});
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (widthStep_ != 0) {
            if (v < 0.0) {
                ctx.message("La larghezza non può essere negativa.");
                return CommandStatus::Continue;
            }
            setWidthValue(half_ ? 2.0 * v : v);
            return CommandStatus::Continue;
        }
        if (vertices_.empty() || arcMode_) {
            ctx.message("Indica un punto.");
            return CommandStatus::Continue;
        }
        const Vec2 last = vertices_.back().point;
        const Vec2 dir = ctx.cursor - last;
        if (dir.length() < 1e-12) {
            ctx.message("Muovi il cursore verso la direzione voluta.");
            return CommandStatus::Continue;
        }
        return onPoint(ctx, last + dir * (v / dir.length()));
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (widthStep_ != 0)
            return Command::onKeyword(ctx, w);
        if (w == "LA" || w == "LARGHEZZA" || w == "W" || w == "WIDTH" || w == "M" || w == "MEZZA" || w == "MEZZA LARGHEZZA"
            || w == "H" || w == "HALFWIDTH") {
            half_ = w == "M" || w == "MEZZA" || w == "MEZZA LARGHEZZA" || w == "H" || w == "HALFWIDTH";
            widthStep_ = 1;
            startTyped_ = false;
            return CommandStatus::Continue;
        }
        if (w == "A" || w == "ARCO" || w == "ARC") {
            arcMode_ = true;
        } else if (w == "L" || w == "LINEA" || w == "LINE") {
            arcMode_ = false;
        } else if ((w == "U" || w == "ANNULLA" || w == "UNDO") && !vertices_.empty()) {
            vertices_.pop_back();
            if (!vertices_.empty())
                vertices_.back().bulge = 0.0;
        } else if ((w == "C" || w == "CHIUDI" || w == "CLOSE") && vertices_.size() >= 3) {
            vertices_.back().bulge = arcMode_ ? arcBulge(vertices_.front().point) : 0.0;
            closeSegmentWidths();
            ctx.document.add(std::make_unique<PolylineEntity>(vertices_, true));
            return CommandStatus::Finished;
        } else {
            return Command::onKeyword(ctx, w);
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (widthStep_ != 0) {   // Invio: il valore proposto
            setWidthValue(widthStep_ == 1 ? gPlineStartWidth : (startTyped_ ? newStart_ : gPlineEndWidth));
            return CommandStatus::Continue;
        }
        if (vertices_.size() >= 2)
            ctx.document.add(std::make_unique<PolylineEntity>(vertices_, false));
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (vertices_.empty())
            return result;
        auto pts = vertices_;
        if (cursor != pts.back().point && widthStep_ == 0) {
            pts.back().bulge = arcMode_ ? arcBulge(cursor) : 0.0;
            pts.back().startWidth = gPlineStartWidth;
            pts.back().endWidth = gPlineEndWidth;
            pts.push_back({cursor, 0.0});
        }
        result.push_back(std::make_unique<PolylineEntity>(std::move(pts), false));
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        if (vertices_.empty())
            return std::nullopt;
        return vertices_.back().point;
    }

private:
    // Direzione di arrivo nell'ultimo vertice (tangente per il tratto ad arco successivo).
    Vec2 lastTangent() const
    {
        if (vertices_.size() < 2)
            return {1.0, 0.0};
        const PolyVertex& a = vertices_[vertices_.size() - 2];
        const Vec2 chord = vertices_.back().point - a.point;
        const double half = 2.0 * std::atan(a.bulge);   // metà dell'angolo dell'arco, con segno
        return Vec2::polar(1.0, angleOf(chord) + half);
    }

    // Curvatura del tratto ad arco tangente che arriva in p.
    double arcBulge(Vec2 p) const
    {
        const Vec2 chord = p - vertices_.back().point;
        const Vec2 t = lastTangent();
        const double phi = std::atan2(cross(t, chord), t.dot(chord));
        return std::tan(phi / 2.0);
    }

    // Il tratto appena chiuso prende le larghezze correnti; i successivi proseguono con la finale.
    void closeSegmentWidths()
    {
        if (vertices_.empty())
            return;
        vertices_.back().startWidth = gPlineStartWidth;
        vertices_.back().endWidth = gPlineEndWidth;
        gPlineStartWidth = gPlineEndWidth;
    }

    void setWidthValue(double w)
    {
        if (widthStep_ == 1) {
            newStart_ = w;
            startTyped_ = true;
            widthStep_ = 2;
        } else {
            gPlineStartWidth = startTyped_ ? newStart_ : gPlineStartWidth;
            gPlineEndWidth = w;
            widthStep_ = 0;
        }
    }

    std::vector<PolyVertex> vertices_;
    bool arcMode_ = false;
    int widthStep_ = 0;        // 1 = si chiede la larghezza iniziale, 2 = la finale
    bool half_ = false;        // Mezza larghezza
    bool startTyped_ = false;
    double newStart_ = 0.0;
};

// ---------------------------------------------------------------- RECTANG

class RectangleCommand final : public Command {
public:
    std::string prompt() const override
    {
        return first_ ? "Altro angolo:" : "Primo angolo:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        if (std::abs(p.x - first_->x) < 1e-12 || std::abs(p.y - first_->y) < 1e-12) {
            ctx.message("Il rettangolo deve avere larghezza e altezza.");
            return CommandStatus::Continue;
        }
        ctx.document.add(rectangle(*first_, p));
        ctx.lastPoint = p;
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (first_)
            result.push_back(rectangle(*first_, cursor));
        return result;
    }

private:
    static std::unique_ptr<PolylineEntity> rectangle(Vec2 a, Vec2 b)
    {
        return std::make_unique<PolylineEntity>(
            std::vector<PolyVertex>{{a, 0.0}, {{b.x, a.y}, 0.0}, {b, 0.0}, {{a.x, b.y}, 0.0}}, true);
    }

    std::optional<Vec2> first_;
};

// ---------------------------------------------------------------- ELLIPSE

class EllipseCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (centerMode_) {
            if (!center_)
                return "Centro dell'ellisse:";
            if (!axisEnd_)
                return "Punto finale dell'asse:";
            return "Distanza dall'altro asse:";
        }
        if (!p1_)
            return "Punto finale dell'asse dell'ellisse o [Centro]:";
        if (!axisEnd_)
            return "Altro punto finale dell'asse:";
        return "Distanza dall'altro asse:";
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (!p1_ && !center_ && (w == "C" || w == "CENTRO" || w == "CENTER")) {
            centerMode_ = true;
            return CommandStatus::Continue;
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.lastPoint = p;
        if (centerMode_ && !center_) {
            center_ = p;
        } else if (!centerMode_ && !p1_) {
            p1_ = p;
        } else if (!axisEnd_) {
            if (centerMode_) {
                axisEnd_ = p;
            } else {
                center_ = (*p1_ + p) / 2.0;
                axisEnd_ = p;
            }
            if (distance(*center_, *axisEnd_) < 1e-12) {
                ctx.message("L'asse deve avere lunghezza.");
                axisEnd_.reset();
            }
        } else {
            return create(ctx, distance(*center_, p));
        }
        return CommandStatus::Continue;
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!axisEnd_)
            return Command::onValue(ctx, v);
        return create(ctx, v);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (axisEnd_) {
            if (auto e = make(distance(*center_, cursor)))
                result.push_back(std::move(e));
            result.push_back(std::make_unique<LineEntity>(*center_, cursor));
        } else if (p1_) {
            result.push_back(std::make_unique<LineEntity>(*p1_, cursor));
        } else if (center_) {
            result.push_back(std::make_unique<LineEntity>(*center_, cursor));
        }
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        if (axisEnd_)
            return center_;
        return p1_ ? p1_ : center_;
    }

private:
    std::unique_ptr<EllipseEntity> make(double otherHalf) const
    {
        const Vec2 major = *axisEnd_ - *center_;
        if (otherHalf <= 1e-12 || major.length() <= 1e-12)
            return nullptr;
        double ratio = otherHalf / major.length();
        Vec2 axis = major;
        if (ratio > 1.0) {   // l'asse maggiore è l'altro
            axis = Vec2{-major.y, major.x} * ratio;
            ratio = 1.0 / ratio;
        }
        return std::make_unique<EllipseEntity>(*center_, axis, ratio);
    }

    CommandStatus create(CommandContext& ctx, double otherHalf)
    {
        auto e = make(otherHalf);
        if (!e) {
            ctx.message("Distanza non valida.");
            return CommandStatus::Continue;
        }
        ctx.document.add(std::move(e));
        return CommandStatus::Finished;
    }

    bool centerMode_ = false;
    std::optional<Vec2> p1_;
    std::optional<Vec2> center_;
    std::optional<Vec2> axisEnd_;
};

// ------------------------------------------------------------------ POINT

class PointCommand final : public Command {
public:
    std::string prompt() const override { return "Punto (Invio per finire):"; }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        ctx.document.add(std::make_unique<PointEntity>(p));
        ctx.lastPoint = p;
        return CommandStatus::Continue;
    }
};

// ------------------------------------------------------------- UNDO / REDO

class UndoCommand final : public Command {
public:
    std::string prompt() const override { return {}; }
    CommandStatus start(CommandContext& ctx) override
    {
        if (!ctx.document.undo())
            ctx.message("Non c'è nulla da annullare.");
        return CommandStatus::Finished;
    }
};

class RedoCommand final : public Command {
public:
    std::string prompt() const override { return {}; }
    CommandStatus start(CommandContext& ctx) override
    {
        if (!ctx.document.redo())
            ctx.message("Non c'è nulla da ripetere.");
        return CommandStatus::Finished;
    }
};

// ------------------------------------------------------------------- ZOOM

class ZoomCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (corner_)
            return "Angolo opposto:";
        return "Primo angolo della finestra o [Estensioni/Finestra/Precedente] <Estensioni>:";
    }
    bool wantsSelection() const override { return true; }   // cursore a quadratino
    std::optional<Vec2> selectionCorner() const override { return corner_; }

    CommandStatus onEnter(CommandContext& ctx) override { return corner_ ? CommandStatus::Finished : extents(ctx); }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& word) override
    {
        if (word == "E" || word == "ESTENSIONI" || word == "EXTENTS")
            return extents(ctx);
        if (word == "F" || word == "FINESTRA" || word == "W" || word == "WINDOW")
            return CommandStatus::Continue;   // si indicano i due angoli
        if (word == "P" || word == "PRECEDENTE" || word == "PREVIOUS") {
            if (!ctx.zoomPrevious || !ctx.zoomPrevious())
                ctx.message("Non ci sono viste precedenti.");
            return CommandStatus::Finished;
        }
        return Command::onKeyword(ctx, word);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!corner_) {
            corner_ = p;
            return CommandStatus::Continue;
        }
        BBox box;
        box.expand(*corner_);
        box.expand(p);
        if (ctx.zoomWindow)
            ctx.zoomWindow(box);
        return CommandStatus::Finished;
    }

private:
    static CommandStatus extents(CommandContext& ctx)
    {
        if (ctx.zoomExtents)
            ctx.zoomExtents();
        return CommandStatus::Finished;
    }

    std::optional<Vec2> corner_;
};

template <typename T>
CommandManager::Factory factory()
{
    return [] { return std::make_unique<T>(); };
}

} // namespace

void registerBuiltinCommands(CommandManager& manager)
{
    manager.add("LINE",   {"L", "LINEA"},   factory<LineCommand>());
    manager.add("CIRCLE", {"C", "CERCHIO"}, factory<CircleCommand>());
    manager.add("ARC",    {"A", "ARCO"},    factory<ArcCommand>());
    manager.add("PLINE",  {"PL", "PLINEA", "POLILINEA"}, factory<PolylineCommand>());
    manager.add("RECTANG", {"REC", "RECTANGLE", "RETTANGOLO"}, factory<RectangleCommand>());
    manager.add("ELLIPSE", {"EL", "ELLISSE"}, factory<EllipseCommand>());
    manager.add("POINT",  {"PO", "PUNTO"},  factory<PointCommand>());
    manager.add("UNDO",   {"U", "ANNULLA"}, factory<UndoCommand>());
    manager.add("REDO",   {"RIPETI"},       factory<RedoCommand>());
    manager.add("ZOOM",   {"Z"},            factory<ZoomCommand>());
    registerModifyCommands(manager);
    registerAnnotateCommands(manager);
    registerBlockCommands(manager);
    registerLayoutCommands(manager);
    registerSystemCommands(manager);
    registerEditCommands(manager);
    registerExtraCommands(manager);
}

} // namespace cartesio
