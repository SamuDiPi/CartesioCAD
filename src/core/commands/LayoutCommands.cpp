// Comandi dei layout: MVIEW (crea finestre sullo spazio modello), VPCLIP (ritaglia una finestra),
// VPLAYER (congela i layer nelle singole finestre).
#include "core/commands/CommandManager.h"

#include "core/Geometry.h"
#include "core/TitleBlock.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace cartesio {

namespace {

bool inPaper(CommandContext& ctx, const char* name)
{
    if (ctx.document.activeSpace() != 0)
        return true;
    ctx.message(std::string(name)
                + " funziona nello spazio carta di un layout: scegliere una scheda di layout in basso"
                  " (doppio clic fuori dalle finestre per uscire dal modello).");
    return false;
}

// Finestra sotto il punto (sul bordo o all'interno).
const ViewportEntity* viewportAt(CommandContext& ctx, Vec2 p)
{
    if (const Entity* e = ctx.document.pick(p, ctx.pickTolerance); e && e->type() == EntityType::Viewport)
        return static_cast<const ViewportEntity*>(e);
    for (const Entity* e : ctx.document.entitiesNear(p, 0.0))
        if (e->type() == EntityType::Viewport && static_cast<const ViewportEntity*>(e)->containsPaperPoint(p))
            return static_cast<const ViewportEntity*>(e);
    return nullptr;
}

// Anteprima di un contorno poligonale in costruzione.
std::vector<std::unique_ptr<Entity>> polygonPreview(const std::vector<Vec2>& points, Vec2 cursor)
{
    std::vector<std::unique_ptr<Entity>> result;
    if (points.empty())
        return result;
    std::vector<PolyVertex> v;
    for (Vec2 p : points)
        v.push_back({p, 0.0});
    v.push_back({cursor, 0.0});
    result.push_back(std::make_unique<PolylineEntity>(std::move(v), points.size() >= 2));
    return result;
}

class MviewCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (mode_) {
        case Mode::Polygon:
            return points_.empty() ? "Specificare punto iniziale:"
                                   : points_.size() < 3 ? "Specificare punto successivo:"
                                                        : "Specificare punto successivo o [Chiudi]:";
        case Mode::Object:
            return "Selezionare l'oggetto per il ritaglio della finestra (polilinea chiusa, cerchio, ellisse):";
        case Mode::Rect:
            break;
        }
        return first_ ? "Specificare angolo opposto:"
                      : "Specificare angolo della finestra o [Adatta/Poligonale/Oggetto]:";
    }

    CommandStatus start(CommandContext& ctx) override
    {
        return inPaper(ctx, "MVIEW") ? CommandStatus::Continue : CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (mode_ == Mode::Polygon) {
            points_.push_back(p);
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        if (mode_ == Mode::Object) {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            const std::vector<Vec2> outline = e ? closedOutline(*e) : std::vector<Vec2>{};
            if (outline.empty()) {
                ctx.message("Serve un oggetto chiuso: polilinea chiusa, cerchio, ellisse o spline chiusa.");
                return CommandStatus::Continue;
            }
            // L'oggetto diventa il contorno della finestra (legato alla finestra).
            return create(ctx, outline, e->id());
        }
        if (!first_) {
            first_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        BBox r;
        r.expand(*first_);
        r.expand(p);
        if (r.width() <= 0.0 || r.height() <= 0.0) {
            ctx.message("La finestra deve avere larghezza e altezza.");
            return CommandStatus::Continue;
        }
        return create(ctx, {r.min, {r.max.x, r.min.y}, r.max, {r.min.x, r.max.y}}, 0, false);
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (mode_ == Mode::Rect && !first_) {
            if (w == "A" || w == "ADATTA" || w == "F" || w == "FIT") {
                const Layout* layout = ctx.document.findLayout(ctx.document.activeSpace());
                if (!layout)
                    return CommandStatus::Finished;
                const double m = layout->margin;
                const Vec2 o = layout->origin;
                const double W = o.x + layout->paperWidth - m;
                const double H = o.y + layout->paperHeight - m;
                return create(ctx, {{o.x + m, o.y + m}, {W, o.y + m}, {W, H}, {o.x + m, H}}, 0, false);
            }
            if (w == "P" || w == "POLIGONALE" || w == "POLYGONAL") {
                mode_ = Mode::Polygon;
                return CommandStatus::Continue;
            }
            if (w == "O" || w == "OGGETTO" || w == "OBJECT") {
                mode_ = Mode::Object;
                return CommandStatus::Continue;
            }
        }
        if (mode_ == Mode::Polygon && (w == "C" || w == "CHIUDI" || w == "CLOSE"))
            return onEnter(ctx);
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (mode_ == Mode::Polygon) {
            if (points_.size() < 3) {
                ctx.message("Servono almeno tre punti.");
                return points_.empty() ? CommandStatus::Finished : CommandStatus::Continue;
            }
            return create(ctx, points_, 0);
        }
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        if (mode_ == Mode::Polygon)
            return polygonPreview(points_, cursor);
        std::vector<std::unique_ptr<Entity>> result;
        if (first_) {
            const Vec2 a = *first_;
            result.push_back(std::make_unique<PolylineEntity>(
                std::vector<PolyVertex>{{a, 0}, {{cursor.x, a.y}, 0}, {cursor, 0}, {{a.x, cursor.y}, 0}}, true));
        }
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        if (mode_ == Mode::Polygon && !points_.empty())
            return points_.back();
        return first_;
    }

private:
    enum class Mode { Rect, Polygon, Object };

    // Nuova finestra che inquadra tutto il disegno del modello. `source` è l'oggetto usato come contorno.
    static CommandStatus create(CommandContext& ctx, const std::vector<Vec2>& outline, EntityId source,
                                bool polygonal = true)
    {
        BBox r;
        for (Vec2 p : outline)
            r.expand(p);
        if (r.width() <= 0.0 || r.height() <= 0.0) {
            ctx.message("La finestra deve avere larghezza e altezza.");
            return CommandStatus::Continue;
        }
        auto vp = std::make_unique<ViewportEntity>(r.center(), r.width(), r.height());
        const BBox model = ctx.document.extents(0);
        if (model.isValid() && (model.width() > 0.0 || model.height() > 0.0)) {
            vp->viewCenter = model.center();
            const double scale = std::min(model.width() > 0.0 ? r.width() / model.width() : 1e300,
                                          model.height() > 0.0 ? r.height() / model.height() : 1e300);
            vp->setScale(scale * 0.95);
        } else {
            vp->viewCenter = {};
            vp->viewHeight = r.height();
        }
        if (polygonal)
            vp->clip = outline;
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(vp));
        ctx.document.commit(std::move(added), source ? std::vector<EntityId>{source} : std::vector<EntityId>{});
        return CommandStatus::Finished;
    }

    Mode mode_ = Mode::Rect;
    std::optional<Vec2> first_;
    std::vector<Vec2> points_;
};

// VPCLIP: ritaglia una finestra esistente su un poligono o un oggetto chiuso, o toglie il ritaglio.
class VpclipCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!viewport_)
            return "Selezionare la finestra da ritagliare:";
        if (polygon_)
            return points_.empty() ? "Specificare punto iniziale:"
                                   : points_.size() < 3 ? "Specificare punto successivo:"
                                                        : "Specificare punto successivo o [Chiudi]:";
        return "Selezionare oggetto di ritaglio o [Poligonale/Elimina] <Poligonale>:";
    }

    CommandStatus start(CommandContext& ctx) override
    {
        return inPaper(ctx, "VPCLIP") ? CommandStatus::Continue : CommandStatus::Finished;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!viewport_) {
            const ViewportEntity* vp = viewportAt(ctx, p);
            if (!vp) {
                ctx.message("Nessuna finestra in quel punto.");
                return CommandStatus::Continue;
            }
            viewport_ = vp->id();
            return CommandStatus::Continue;
        }
        if (polygon_) {
            points_.push_back(p);
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        const std::vector<Vec2> outline = e && e->id() != viewport_ ? closedOutline(*e) : std::vector<Vec2>{};
        if (outline.empty()) {
            ctx.message("Serve un oggetto chiuso: polilinea chiusa, cerchio, ellisse o spline chiusa.");
            return CommandStatus::Continue;
        }
        return apply(ctx, outline, e->id());
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (viewport_ && !polygon_) {
            if (w == "P" || w == "POLIGONALE" || w == "POLYGONAL") {
                polygon_ = true;
                return CommandStatus::Continue;
            }
            if (w == "E" || w == "ELIMINA" || w == "DELETE" || w == "D")
                return apply(ctx, {}, 0);
        }
        if (polygon_ && (w == "C" || w == "CHIUDI" || w == "CLOSE"))
            return onEnter(ctx);
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (!viewport_)
            return CommandStatus::Finished;
        if (!polygon_) {
            polygon_ = true;   // opzione predefinita
            return CommandStatus::Continue;
        }
        if (points_.size() < 3) {
            ctx.message("Servono almeno tre punti.");
            return points_.empty() ? CommandStatus::Finished : CommandStatus::Continue;
        }
        return apply(ctx, points_, 0);
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        return polygon_ ? polygonPreview(points_, cursor) : std::vector<std::unique_ptr<Entity>>{};
    }

    std::optional<Vec2> basePoint() const override
    {
        if (polygon_ && !points_.empty())
            return points_.back();
        return std::nullopt;
    }

private:
    CommandStatus apply(CommandContext& ctx, const std::vector<Vec2>& outline, EntityId source)
    {
        const Entity* e = ctx.document.find(viewport_);
        if (!e || e->type() != EntityType::Viewport)
            return CommandStatus::Finished;
        auto copy = std::unique_ptr<ViewportEntity>(static_cast<ViewportEntity*>(e->clone().release()));
        if (outline.empty()) {
            if (copy->clip.empty()) {
                ctx.message("La finestra non è ritagliata.");
                return CommandStatus::Finished;
            }
            copy->clip.clear();
        } else {
            copy->setClip(outline);
        }
        std::vector<EntityId> removed{viewport_};
        if (source)
            removed.push_back(source);
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(copy));
        ctx.document.commit(std::move(added), removed);
        return CommandStatus::Finished;
    }

    EntityId viewport_ = 0;
    bool polygon_ = false;
    std::vector<Vec2> points_;
};

std::string upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// VPLAYER: congela o scongela layer solo in alcune finestre.
class VplayerCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (step_) {
        case Step::Option: return "Opzione [Congela/Scongela]:";
        case Step::Names: return "Nomi dei layer (separati da virgola, * = tutti):";
        case Step::Scope: return "Applica a [Tutte/Seleziona/Corrente] <Corrente>:";
        case Step::Select: return "Selezionare le finestre (Invio per terminare):";
        }
        return {};
    }

    bool wantsText() const override { return step_ == Step::Names; }
    bool textIsLine() const override { return false; }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (step_ == Step::Option) {
            if (w == "C" || w == "CONGELA" || w == "F" || w == "FREEZE")
                freeze_ = true;
            else if (w == "S" || w == "SCONGELA" || w == "T" || w == "THAW")
                freeze_ = false;
            else
                return Command::onKeyword(ctx, w);
            step_ = Step::Names;
            return CommandStatus::Continue;
        }
        if (step_ == Step::Scope) {
            if (w == "T" || w == "TUTTE" || w == "A" || w == "ALL") {
                std::vector<EntityId> all;
                for (const auto& e : ctx.document.entities())
                    if (e->type() == EntityType::Viewport)
                        all.push_back(e->id());
                return apply(ctx, all);
            }
            if (w == "S" || w == "SELEZIONA" || w == "SELECT") {
                if (ctx.document.activeSpace() == 0) {
                    ctx.message("Per selezionare le finestre passare allo spazio carta (PSPACE).");
                    return CommandStatus::Continue;
                }
                step_ = Step::Select;
                return CommandStatus::Continue;
            }
            if (w == "C" || w == "CORRENTE" || w == "CURRENT")
                return onEnter(ctx);
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        layers_.clear();
        std::stringstream in(text);
        std::string name;
        while (std::getline(in, name, ',')) {
            name.erase(0, name.find_first_not_of(' '));
            name.erase(name.find_last_not_of(' ') + 1);
            if (name == "*") {
                for (int i = 0; i < static_cast<int>(ctx.document.layers().size()); ++i)
                    layers_.push_back(i);
                continue;
            }
            int found = -1;
            for (int i = 0; i < static_cast<int>(ctx.document.layers().size()); ++i)
                if (upper(ctx.document.layers()[static_cast<std::size_t>(i)].name) == upper(name))
                    found = i;
            if (found < 0)
                ctx.message("Layer \"" + name + "\" inesistente.");
            else
                layers_.push_back(found);
        }
        if (layers_.empty())
            return CommandStatus::Continue;
        step_ = Step::Scope;
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (step_ != Step::Select)
            return Command::onPoint(ctx, p);
        if (const ViewportEntity* vp = viewportAt(ctx, p)) {
            if (std::find(selected_.begin(), selected_.end(), vp->id()) == selected_.end())
                selected_.push_back(vp->id());
            ctx.message("Finestre selezionate: " + std::to_string(selected_.size()));
        } else {
            ctx.message("Nessuna finestra in quel punto.");
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (step_ == Step::Select)
            return apply(ctx, selected_);
        if (step_ != Step::Scope)
            return CommandStatus::Finished;
        // Corrente: la finestra in cui si sta lavorando (CVPORT).
        const std::string current = ctx.variable("CVPORT");
        const EntityId id = current.empty() ? 0 : static_cast<EntityId>(std::stoull(current));
        const Entity* e = id ? ctx.document.find(id) : nullptr;
        if (!e || e->type() != EntityType::Viewport) {
            ctx.message("Nessuna finestra corrente: scegliere Tutte o Seleziona, oppure fare doppio clic in una finestra.");
            return CommandStatus::Continue;
        }
        return apply(ctx, {id});
    }

private:
    enum class Step { Option, Names, Scope, Select };

    CommandStatus apply(CommandContext& ctx, const std::vector<EntityId>& ids)
    {
        std::vector<std::unique_ptr<Entity>> added;
        std::vector<EntityId> removed;
        for (EntityId id : ids) {
            const Entity* e = ctx.document.find(id);
            if (!e || e->type() != EntityType::Viewport)
                continue;
            auto copy = std::unique_ptr<ViewportEntity>(static_cast<ViewportEntity*>(e->clone().release()));
            for (int layer : layers_) {
                std::erase(copy->frozenLayers, layer);
                if (freeze_)
                    copy->frozenLayers.push_back(layer);
            }
            std::sort(copy->frozenLayers.begin(), copy->frozenLayers.end());
            added.push_back(std::move(copy));
            removed.push_back(id);
        }
        if (added.empty()) {
            ctx.message("Nessuna finestra.");
            return CommandStatus::Finished;
        }
        ctx.message(std::string(freeze_ ? "Layer congelati" : "Layer scongelati") + " in "
                    + std::to_string(added.size()) + (added.size() == 1 ? " finestra." : " finestre."));
        ctx.document.commit(std::move(added), removed);
        return CommandStatus::Finished;
    }

    Step step_ = Step::Option;
    bool freeze_ = true;
    std::vector<int> layers_;
    std::vector<EntityId> selected_;
};


// CARTIGLIO: formato del foglio (A4…A0), cornice e cartiglio con attributi compilati in automatico.
class TitleBlockCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (step_) {
        case Step::Format: return "Formato del foglio [A4/A3/A2/A1/A0] <" + currentFormat_ + ">:";
        case Step::Title: return "Titolo del disegno:";
        case Step::Author: return "Autore <" + author_ + ">:";
        }
        return {};
    }

    bool wantsText() const override { return step_ != Step::Format; }

    CommandStatus start(CommandContext& ctx) override
    {
        if (!inPaper(ctx, "CARTIGLIO"))
            return CommandStatus::Finished;
        const Layout* layout = ctx.document.findLayout(ctx.document.activeSpace());
        if (!layout)
            return CommandStatus::Finished;
        currentFormat_ = paperFormatName(layout->paperWidth, layout->paperHeight);
        // Si riprendono i valori di un cartiglio già presente nel layout.
        for (const auto& e : ctx.document.entities()) {
            if (e->space() != ctx.document.activeSpace() || e->type() != EntityType::Insert)
                continue;
            const auto& insert = static_cast<const InsertEntity&>(*e);
            if (insert.blockName != kTitleBlockName)
                continue;
            for (const Attribute& a : insert.attributes) {
                previous_[a.tag] = a.text.text;
                if (a.tag == "AUTORE")
                    author_ = a.text.text;
            }
        }
        return CommandStatus::Continue;
    }

    CommandStatus onKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (step_ == Step::Format)
            for (const PaperFormat& f : isoPaperFormats())
                if (w == f.name) {
                    format_ = &f;
                    step_ = Step::Title;
                    return CommandStatus::Continue;
                }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        if (step_ == Step::Title) {
            title_ = text;
            step_ = Step::Author;
            return CommandStatus::Continue;
        }
        if (step_ == Step::Author) {
            author_ = text;
            return create(ctx);
        }
        return Command::onText(ctx, text);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        switch (step_) {
        case Step::Format:
            step_ = Step::Title;
            return CommandStatus::Continue;
        case Step::Title:
            title_ = previous_.count("TITOLO") ? previous_["TITOLO"] : std::string();
            step_ = Step::Author;
            return CommandStatus::Continue;
        case Step::Author:
            return create(ctx);
        }
        return CommandStatus::Finished;
    }

private:
    enum class Step { Format, Title, Author };

    CommandStatus create(CommandContext& ctx)
    {
        Document& doc = ctx.document;
        const int space = doc.activeSpace();
        Layout* layout = doc.findLayout(space);
        if (!layout)
            return CommandStatus::Finished;
        if (format_) {
            layout->paperWidth = format_->width;
            layout->paperHeight = format_->height;
            layout->paperName = format_->name;
            doc.touch();
        }
        if (!doc.findBlock(kTitleBlockName))
            doc.addBlock(makeTitleBlockDef());
        const auto block = doc.findBlock(kTitleBlockName);
        int layer = doc.findLayer(kTitleBlockLayer);
        if (layer < 0)
            layer = doc.addLayer(Layer{kTitleBlockLayer, 7, true});

        // Un cartiglio per layout: quello vecchio e la sua cornice vengono sostituiti.
        std::vector<EntityId> removed;
        for (const auto& e : doc.entities()) {
            if (e->space() != space)
                continue;
            if (e->type() == EntityType::Insert && static_cast<const InsertEntity&>(*e).blockName == kTitleBlockName)
                removed.push_back(e->id());
            if (e->type() == EntityType::Polyline && e->layer() == layer
                && static_cast<const PolylineEntity&>(*e).closed
                && static_cast<const PolylineEntity&>(*e).vertices.size() == 4)
                removed.push_back(e->id());
        }

        const double m = layout->margin;
        const Vec2 o = layout->origin;
        const double W = layout->paperWidth;
        const double H = layout->paperHeight;
        std::vector<std::unique_ptr<Entity>> added;
        auto frame = std::make_unique<PolylineEntity>(
            std::vector<PolyVertex>{{o + Vec2{m, m}, 0}, {o + Vec2{W - m, m}, 0}, {o + Vec2{W - m, H - m}, 0}, {o + Vec2{m, H - m}, 0}}, true);
        frame->setLayer(layer);
        added.push_back(std::move(frame));

        auto insert = std::make_unique<InsertEntity>(kTitleBlockName, o + Vec2{W - m, m});
        insert->block = block;
        insert->setLayer(layer);
        std::map<std::string, std::string> values = previous_;
        values["TITOLO"] = title_;
        values["AUTORE"] = author_;
        values["DATA"] = todayText();
        values["SCALA"] = titleBlockScale(doc, space);
        values["FOGLIO"] = paperFormatName(W, H);
        const Affine t = insert->affine();
        for (const auto& child : block->entities) {
            if (child->type() != EntityType::AttDef)
                continue;
            const auto& def = static_cast<const AttDefEntity&>(*child);
            TextEntity text = def.text;
            text.transform(t);
            if (const auto it = values.find(def.tag); it != values.end())
                text.text = it->second;
            text.setLayer(layer);
            insert->attributes.push_back({def.tag, text, def.flags});
        }
        added.push_back(std::move(insert));
        doc.commit(std::move(added), removed);
        ctx.message("Cartiglio " + paperFormatName(W, H) + " inserito. AGGCARTIGLIO aggiorna scala, data e formato;"
                    " gli altri campi si modificano dalla tavolozza Proprietà.");
        return CommandStatus::Finished;
    }

    Step step_ = Step::Format;
    const PaperFormat* format_ = nullptr;
    std::string currentFormat_;
    std::string title_;
    std::string author_;
    std::map<std::string, std::string> previous_;
};

// AGGCARTIGLIO: aggiorna i campi automatici dei cartigli (come UPDATEFIELD).
class UpdateTitleBlocksCommand final : public Command {
public:
    std::string prompt() const override { return {}; }
    CommandStatus start(CommandContext& ctx) override
    {
        const int count = updateTitleBlocks(ctx.document);
        ctx.message(count == 0 ? "Cartigli già aggiornati." : "Cartigli aggiornati: " + std::to_string(count) + ".");
        return CommandStatus::Finished;
    }
};

} // namespace

void registerLayoutCommands(CommandManager& manager)
{
    manager.add("MVIEW", {"MV", "FINESTRA"}, [] { return std::make_unique<MviewCommand>(); });
    manager.add("VPCLIP", {"RITAGLIAFINESTRA"}, [] { return std::make_unique<VpclipCommand>(); });
    manager.add("VPLAYER", {"LAYERFINESTRA"}, [] { return std::make_unique<VplayerCommand>(); });
    manager.add("CARTIGLIO", {"TITLEBLOCK", "CART"}, [] { return std::make_unique<TitleBlockCommand>(); });
    manager.add("AGGCARTIGLIO", {"AGGIORNACARTIGLIO", "UPDATEFIELD"}, [] { return std::make_unique<UpdateTitleBlocksCommand>(); });
}

} // namespace cartesio

namespace cartesio {

namespace {

// Due angoli di un rettangolo (finestra di stampa); il risultato va a chi ha avviato il comando.
class WindowPickCommand final : public Command {
public:
    explicit WindowPickCommand(std::function<void(std::optional<BBox>)> done) : done_(std::move(done)) {}

    std::string prompt() const override
    {
        return first_ ? "Specificare l'angolo opposto:" : "Specificare il primo angolo:";
    }
    CommandStatus onPoint(CommandContext&, Vec2 p) override
    {
        if (!first_) {
            first_ = p;
            return CommandStatus::Continue;
        }
        BBox box;
        box.expand(*first_);
        box.expand(p);
        finish(box);
        return CommandStatus::Finished;
    }
    CommandStatus onEnter(CommandContext&) override
    {
        finish(std::nullopt);
        return CommandStatus::Finished;
    }
    void cancel(CommandContext&) override { finish(std::nullopt); }
    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (first_) {
            const Vec2 a = *first_;
            std::vector<PolyVertex> v = {{{a.x, a.y}, 0.0}, {{cursor.x, a.y}, 0.0}, {{cursor.x, cursor.y}, 0.0},
                                         {{a.x, cursor.y}, 0.0}};
            out.push_back(std::make_unique<PolylineEntity>(std::move(v), true));
        }
        return out;
    }
    std::optional<Vec2> basePoint() const override { return first_; }

private:
    void finish(std::optional<BBox> box)
    {
        if (done_) {
            auto done = std::move(done_);
            done_ = nullptr;
            done(box);
        }
    }

    std::function<void(std::optional<BBox>)> done_;
    std::optional<Vec2> first_;
};

} // namespace

std::unique_ptr<Command> makeWindowPickCommand(std::function<void(std::optional<BBox>)> done)
{
    return std::make_unique<WindowPickCommand>(std::move(done));
}

} // namespace cartesio
