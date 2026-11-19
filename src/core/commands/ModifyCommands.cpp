#include <cctype>
#include <optional>
// Comandi di modifica: ERASE, MOVE, COPY, OFFSET, TRIM.
#include "core/commands/CommandManager.h"

#include "core/Geometry.h"

#include <locale>
#include <sstream>

namespace cartesio {

namespace {

std::string formatNumber(double v)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(10);
    out << v;
    return out.str();
}

// ------------------------------------------------------------------ ERASE

class EraseCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        const auto count = ctx.selection.size();
        ctx.document.erase(ctx.selection);
        ctx.selection.clear();
        ctx.message(std::to_string(count) + (count == 1 ? " oggetto cancellato." : " oggetti cancellati."));
        return CommandStatus::Finished;
    }
};

// ----------------------------------------------------------- MOVE / COPY

// Sposta o copia la selezione da un punto base a un secondo punto.
// COPY continua a chiedere punti finché non si preme Invio (copie multiple).
class DisplaceCommand final : public SelectingCommand {
public:
    explicit DisplaceCommand(bool copy) : copy_(copy) {}

protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        ids_ = ctx.selection;
        return CommandStatus::Continue;
    }

    std::string actionPrompt() const override
    {
        if (!base_)
            return "Specificare punto base:";
        return copy_ ? "Specificare secondo punto (Invio per terminare):" : "Specificare secondo punto:";
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!base_) {
            base_ = p;
            ctx.lastPoint = p;
            snapshot(ctx);
            return CommandStatus::Continue;
        }
        return displace(ctx, p);
    }

    // Distanza diretta nella direzione del cursore.
    CommandStatus actionValue(CommandContext& ctx, double v) override
    {
        if (!base_) {
            ctx.message("Specificare un punto.");
            return CommandStatus::Continue;
        }
        const Vec2 dir = ctx.cursor - *base_;
        const double len = dir.length();
        if (len < 1e-12) {
            ctx.message("Spostare il cursore per indicare la direzione.");
            return CommandStatus::Continue;
        }
        return displace(ctx, *base_ + dir * (v / len));
    }

    CommandStatus actionEnter(CommandContext& ctx) override
    {
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> actionPreview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (!base_)
            return result;
        const Vec2 delta = cursor - *base_;
        for (const auto& e : originals_) {
            auto moved = e->clone();
            moved->translate(delta);
            result.push_back(std::move(moved));
        }
        result.push_back(std::make_unique<LineEntity>(*base_, cursor));
        return result;
    }

    std::optional<Vec2> actionBasePoint() const override { return base_; }

private:
    // Copia degli oggetti selezionati, usata per le anteprime.
    void snapshot(CommandContext& ctx)
    {
        originals_.clear();
        for (EntityId id : ids_)
            if (const Entity* e = ctx.document.find(id))
                originals_.push_back(e->clone());
    }

    CommandStatus displace(CommandContext& ctx, Vec2 target)
    {
        const Vec2 delta = target - *base_;
        std::vector<std::unique_ptr<Entity>> added;
        for (const auto& e : originals_) {
            auto moved = e->clone();
            moved->translate(delta);
            if (copy_)
                moved->setId(0);   // nuovo oggetto, stesso layer
            added.push_back(std::move(moved));
        }
        ctx.lastPoint = target;

        if (copy_) {
            ctx.document.commit(std::move(added));
            return CommandStatus::Continue;
        }
        ctx.document.commit(std::move(added), ids_);
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

    bool copy_;
    std::vector<EntityId> ids_;
    std::vector<std::unique_ptr<Entity>> originals_;
    std::optional<Vec2> base_;
};

// ----------------------------------------------------------------- OFFSET

std::optional<double> gLastOffset;   // si ricorda l'ultima distanza usata

class OffsetCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Distance:
            if (firstPoint_)
                return "Specificare secondo punto:";
            return "Specificare distanza di offset"
                + (gLastOffset ? " <" + formatNumber(*gLastOffset) + ">" : std::string{}) + ":";
        case Stage::Object:
            return "Selezionare oggetto di cui eseguire l'offset (Invio per terminare):";
        case Stage::Side:
            return "Specificare punto sul lato in cui eseguire l'offset:";
        }
        return {};
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (stage_ != Stage::Distance)
            return Command::onValue(ctx, v);
        return setDistance(ctx, v);
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        switch (stage_) {
        case Stage::Distance:
            // La distanza si può dare anche indicando due punti.
            if (!firstPoint_) {
                firstPoint_ = p;
                ctx.lastPoint = p;
                return CommandStatus::Continue;
            }
            return setDistance(ctx, distance(*firstPoint_, p));

        case Stage::Object: {
            const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
            if (!e) {
                ctx.message("Nessun oggetto trovato.");
                return CommandStatus::Continue;
            }
            target_ = e->clone();
            ctx.selection = {e->id()};
            stage_ = Stage::Side;
            return CommandStatus::Continue;
        }

        case Stage::Side: {
            auto result = offsetEntity(*target_, distance_, p);
            if (!result)
                ctx.message("Impossibile eseguire l'offset da questo lato a questa distanza.");
            else
                ctx.document.add(std::move(result));
            ctx.selection.clear();
            stage_ = Stage::Object;
            return CommandStatus::Continue;
        }
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        switch (stage_) {
        case Stage::Distance:
            if (gLastOffset && !firstPoint_)
                return setDistance(ctx, *gLastOffset);
            return CommandStatus::Finished;
        case Stage::Side:
            ctx.selection.clear();
            stage_ = Stage::Object;
            return CommandStatus::Continue;
        case Stage::Object:
            break;
        }
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (stage_ == Stage::Side) {
            if (auto e = offsetEntity(*target_, distance_, cursor))
                result.push_back(std::move(e));
        } else if (stage_ == Stage::Distance && firstPoint_) {
            result.push_back(std::make_unique<LineEntity>(*firstPoint_, cursor));
        }
        return result;
    }

    std::optional<Vec2> basePoint() const override
    {
        return stage_ == Stage::Distance ? firstPoint_ : std::nullopt;
    }

    bool wantsSelection() const override { return stage_ == Stage::Object; }

private:
    enum class Stage { Distance, Object, Side };

    CommandStatus setDistance(CommandContext& ctx, double d)
    {
        if (d <= 0.0) {
            ctx.message("La distanza deve essere maggiore di zero.");
            firstPoint_.reset();
            return CommandStatus::Continue;
        }
        distance_ = d;
        gLastOffset = d;
        stage_ = Stage::Object;
        return CommandStatus::Continue;
    }

    Stage stage_ = Stage::Distance;
    std::optional<Vec2> firstPoint_;
    double distance_ = 0.0;
    std::unique_ptr<Entity> target_;
};

// ------------------------------------------------------------------- TRIM

// Taglio rapido: tutti gli oggetti fanno da bordo di taglio. Si clicca il tratto da eliminare;
// se l'oggetto non interseca nulla viene cancellato per intero.
class TrimCommand final : public Command {
public:
    std::string prompt() const override
    {
        return "Selezionare oggetto da tagliare (Invio per terminare):";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const Entity* target = ctx.document.pick(p, ctx.pickTolerance);
        if (!target) {
            ctx.message("Nessun oggetto trovato.");
            return CommandStatus::Continue;
        }

        std::vector<Vec2> cuts;
        for (const auto& other : ctx.document.entities()) {
            if (other->id() == target->id() || other->space() != target->space()
                || !ctx.document.isShown(*other))
                continue;
            const auto points = intersections(*target, *other);
            cuts.insert(cuts.end(), points.begin(), points.end());
        }

        auto pieces = trimEntity(*target, cuts, p);
        if (!pieces) {
            ctx.message("Impossibile tagliare questo oggetto qui: serve un oggetto (linea, arco, cerchio o polilinea) con almeno due intersezioni.");
            return CommandStatus::Continue;
        }
        const EntityId id = target->id();
        ctx.document.commit(std::move(*pieces), {id});
        return CommandStatus::Continue;
    }

    bool wantsSelection() const override { return true; }
};

// ----------------------------------------------------------- DRAWORDER

// Ordine di visualizzazione degli oggetti selezionati: in primo piano, dietro, sopra o sotto un oggetto di
// riferimento. L'ordine si salva nel file.
class DrawOrderCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        ids_ = ctx.selection;
        return CommandStatus::Continue;
    }

    std::string actionPrompt() const override
    {
        if (reference_)
            return "Selezionare l'oggetto di riferimento:";
        return "Immettere opzione di ordinamento oggetti [Sopra oggetti/sOtto oggetti/Primo piano/Dietro] <Dietro>:";
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& word) override
    {
        std::string w;
        for (char c : word)
            w += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (w == "S" || w == "SOPRA" || w == "A" || w == "ABOVE") {
            reference_ = Document::DrawOrder::Above;
            return CommandStatus::Continue;
        }
        if (w == "O" || w == "SOTTO" || w == "U" || w == "UNDER") {
            reference_ = Document::DrawOrder::Under;
            return CommandStatus::Continue;
        }
        if (w == "P" || w == "PRIMO" || w == "F" || w == "FRONT")
            return apply(ctx, Document::DrawOrder::Front, 0);
        if (w == "D" || w == "DIETRO" || w == "B" || w == "BACK")
            return apply(ctx, Document::DrawOrder::Back, 0);
        return SelectingCommand::actionKeyword(ctx, word);
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!reference_)
            return SelectingCommand::actionPoint(ctx, p);
        const Entity* e = ctx.document.pick(p, ctx.pickTolerance);
        if (!e) {
            ctx.message("Nessun oggetto qui.");
            return CommandStatus::Continue;
        }
        return apply(ctx, *reference_, e->id());
    }

    CommandStatus actionEnter(CommandContext& ctx) override
    {
        if (reference_)
            return CommandStatus::Finished;
        return apply(ctx, Document::DrawOrder::Back, 0);
    }

private:
    CommandStatus apply(CommandContext& ctx, Document::DrawOrder where, EntityId reference)
    {
        ctx.document.reorder(ids_, where, reference);
        ctx.message(std::to_string(ids_.size()) + (ids_.size() == 1 ? " oggetto riordinato." : " oggetti riordinati."));
        return CommandStatus::Finished;
    }

    std::vector<EntityId> ids_;
    std::optional<Document::DrawOrder> reference_;
};

// TEXTTOFRONT: testi, quote e direttrici in primo piano; HATCHTOBACK: tratteggi dietro (tutto lo spazio corrente).
class OrderByTypeCommand final : public Command {
public:
    explicit OrderByTypeCommand(bool texts) : texts_(texts) {}
    std::string prompt() const override { return ""; }
    CommandStatus start(CommandContext& ctx) override
    {
        std::vector<EntityId> ids;
        const int space = ctx.document.activeSpace();
        for (const auto& e : ctx.document.entities()) {
            if ((e->space() < 0 ? 0 : e->space()) != space)
                continue;
            const EntityType t = e->type();
            const bool match = texts_ ? (t == EntityType::Text || t == EntityType::MText || t == EntityType::Dimension
                                         || t == EntityType::Leader)
                                      : t == EntityType::Hatch;
            if (match)
                ids.push_back(e->id());
        }
        ctx.document.reorder(ids, texts_ ? Document::DrawOrder::Front : Document::DrawOrder::Back);
        ctx.message(std::to_string(ids.size()) + (texts_ ? " testi, quote e direttrici portati in primo piano."
                                                           : " tratteggi portati dietro."));
        return CommandStatus::Finished;
    }

private:
    bool texts_;
};

} // namespace

void registerModifyCommands(CommandManager& manager)
{
    manager.add("DRAWORDER", {"DR", "ORDINE"}, [] { return std::make_unique<DrawOrderCommand>(); });
    manager.add("TEXTTOFRONT", {"TESTIDAVANTI"}, [] { return std::make_unique<OrderByTypeCommand>(true); });
    manager.add("HATCHTOBACK", {"TRATTEGGIDIETRO"}, [] { return std::make_unique<OrderByTypeCommand>(false); });
    manager.add("ERASE",  {"E", "CANCELLA"},        [] { return std::make_unique<EraseCommand>(); });
    manager.add("MOVE",   {"M", "SPOSTA"},          [] { return std::make_unique<DisplaceCommand>(false); });
    manager.add("COPY",   {"CO", "CP", "COPIA"},    [] { return std::make_unique<DisplaceCommand>(true); });
    manager.add("OFFSET", {"O"},                    [] { return std::make_unique<OffsetCommand>(); });
    manager.add("TRIM",   {"TR", "TAGLIA"},         [] { return std::make_unique<TrimCommand>(); });
}

} // namespace cartesio
