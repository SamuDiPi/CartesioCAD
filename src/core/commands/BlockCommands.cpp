#include "core/Attributes.h"
// Comandi dei blocchi: BLOCK (crea), INSERT (inserisce), EXPLODE (scompone).
#include "core/commands/CommandManager.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>

namespace cartesio {

namespace {

std::string trimmed(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

// ------------------------------------------------------------------ BLOCK

// Crea un blocco dagli oggetti selezionati e li sostituisce con un suo inserimento.
class BlockCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (name_.empty())
            return "Nome del blocco:";
        if (!base_)
            return "Punto base:";
        return "Oggetti da selezionare (Invio per finire):";
    }

    bool wantsText() const override { return name_.empty(); }
    bool textIsLine() const override { return false; }
    bool wantsSelection() const override { return base_.has_value(); }
    std::optional<Vec2> selectionCorner() const override { return base_ ? picker_.corner() : std::nullopt; }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const std::string name = trimmed(text);
        if (name.empty() || name.front() == '*') {
            ctx.message("Nome del blocco non valido.");
        } else if (ctx.document.findBlock(name)) {
            ctx.message("Esiste già un blocco \"" + name + "\".");
        } else {
            name_ = name;
        }
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (name_.empty())
            return Command::onPoint(ctx, p);
        if (!base_) {
            base_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        const int found = picker_.click(ctx, p);
        if (found >= 0)
            ctx.message(std::to_string(found) + " trovati, " + std::to_string(ctx.selection.size()) + " in totale.");
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (name_.empty() || !base_)
            return CommandStatus::Finished;
        if (ctx.selection.empty()) {
            ctx.message("Nessun oggetto selezionato.");
            return CommandStatus::Finished;
        }

        auto block = std::make_shared<BlockDef>();
        block->name = name_;
        block->base = *base_;
        for (EntityId id : ctx.selection) {
            if (const Entity* e = ctx.document.find(id)) {
                auto copy = e->clone();
                copy->setId(0);
                block->entities.push_back(std::move(copy));
            }
        }
        ctx.document.addBlock(block);

        auto insert = std::make_unique<InsertEntity>(name_, *base_);
        insert->block = block;
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(std::move(insert));
        ctx.document.commit(std::move(added), ctx.selection);
        ctx.message("Blocco \"" + name_ + "\" creato con " + std::to_string(block->entities.size()) + " oggetti.");
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

    void cancel(CommandContext& ctx) override { ctx.selection.clear(); }

private:
    std::string name_;
    std::optional<Vec2> base_;
    SelectionPicker picker_;
};

// ----------------------------------------------------------------- INSERT

class InsertCommand final : public Command {
public:
    std::string prompt() const override
    {
        if (!block_)
            return "Nome del blocco o [?]:";
        if (!position_)
            return "Punto di inserimento:";
        if (!scale_)
            return "Fattore di scala <1>:";
        if (!rotation_)
            return "Angolo di rotazione <0>:";
        // Valori degli attributi, uno alla volta.
        const AttDefEntity& def = *pending_[values_.size()];
        const std::string question = def.prompt.empty() ? def.tag : def.prompt;
        return question + " <" + def.text.text + ">:";
    }

    bool wantsText() const override { return !block_ || (rotation_.has_value() && values_.size() < pending_.size()); }
    bool textIsLine() const override { return block_ != nullptr; }   // nome: una parola; valori: la riga

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        if (block_) {   // valore di un attributo
            values_.push_back(text);
            return values_.size() == pending_.size() ? create(ctx) : CommandStatus::Continue;
        }
        const std::string name = trimmed(text);
        if (name == "?") {
            std::string list;
            for (const auto& b : ctx.document.blocks())
                if (!b->name.empty() && b->name.front() != '*')
                    list += (list.empty() ? "" : ", ") + b->name;
            ctx.message(list.empty() ? "Nessun blocco definito." : "Blocchi: " + list);
            return CommandStatus::Continue;
        }
        // Ricerca senza distinguere maiuscole e minuscole.
        for (const auto& b : ctx.document.blocks()) {
            if (std::equal(b->name.begin(), b->name.end(), name.begin(), name.end(), [](char x, char y) {
                    return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
                })) {
                block_ = b;
                pending_.clear();
                for (const auto& child : b->entities)
                    if (child->type() == EntityType::AttDef
                        && (static_cast<const AttDefEntity&>(*child).flags & AttConstant) == 0)
                        pending_.push_back(static_cast<const AttDefEntity*>(child.get()));
                return CommandStatus::Continue;
            }
        }
        ctx.message("Blocco \"" + name + "\" non trovato. Digitare ? per l'elenco.");
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!block_ || rotation_)
            return Command::onPoint(ctx, p);
        if (!position_) {
            position_ = p;
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        }
        if (!scale_)
            return onValue(ctx, distance(*position_, p));
        return onValue(ctx, normalizeAngle(angleOf(p - *position_)) * 180.0 / kPi);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!position_ || rotation_)
            return Command::onValue(ctx, v);
        if (!scale_) {
            if (v == 0.0) {
                ctx.message("Il fattore di scala non può essere zero.");
                return CommandStatus::Continue;
            }
            scale_ = v;
            return CommandStatus::Continue;
        }
        rotation_ = degToRad(v);
        return startAttributes(ctx);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (!block_ || !position_)
            return CommandStatus::Finished;
        if (!scale_) {
            scale_ = 1.0;
            return CommandStatus::Continue;
        }
        if (!rotation_) {
            rotation_ = 0.0;
            return startAttributes(ctx);
        }
        // Invio a vuoto su un attributo: valore predefinito.
        values_.push_back(pending_[values_.size()]->text.text);
        return values_.size() == pending_.size() ? create(ctx) : CommandStatus::Continue;
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> result;
        if (block_ && !rotation_)
            result.push_back(make(position_.value_or(cursor), scale_.value_or(1.0),
                                  position_ && scale_ ? angleOf(cursor - *position_) : 0.0));
        return result;
    }

    std::optional<Vec2> basePoint() const override { return rotation_ ? std::nullopt : position_; }

private:
    std::unique_ptr<InsertEntity> make(Vec2 p, double scale, double rotation) const
    {
        auto insert = std::make_unique<InsertEntity>(block_->name, p);
        insert->block = block_;
        insert->scaleX = scale;
        insert->scaleY = scale;
        insert->rotation = rotation;
        return insert;
    }

    // Con ATTREQ = 0 (tipico degli script) gli attributi prendono il valore predefinito.
    CommandStatus startAttributes(CommandContext& ctx)
    {
        if (pending_.empty())
            return create(ctx);
        if (ctx.variable("ATTREQ") == "0") {
            for (const AttDefEntity* def : pending_)
                values_.push_back(def->text.text);
            return create(ctx);
        }
        return CommandStatus::Continue;
    }

    CommandStatus create(CommandContext& ctx)
    {
        auto insert = make(*position_, *scale_, *rotation_);
        // Ogni attributo parte dalla sua definizione, portata nella posizione dell'inserimento.
        const Affine t = insert->affine();
        for (std::size_t i = 0; i < pending_.size(); ++i) {
            TextEntity text = pending_[i]->text;
            text.transform(t);
            text.text = values_[i];
            text.setLayer(pending_[i]->layer());
            text.setColor(pending_[i]->color());
            insert->attributes.push_back({pending_[i]->tag, text, pending_[i]->flags});
        }
        ctx.document.add(std::move(insert));
        return CommandStatus::Finished;
    }

    std::shared_ptr<const BlockDef> block_;
    std::vector<const AttDefEntity*> pending_;   // definizioni di attributo da compilare
    std::vector<std::string> values_;
    std::optional<Vec2> position_;
    std::optional<double> scale_;
    std::optional<double> rotation_;
};

// ----------------------------------------------------------------- ATTDEF

// Definizione di attributo: etichetta, richiesta, valore predefinito, posizione e altezza.
class AttDefCommand final : public Command {
public:
    std::string prompt() const override
    {
        switch (stage_) {
        case Stage::Tag: return "Nome dell'attributo (etichetta):";
        case Stage::Prompt: return "Domanda dell'attributo <" + tag_ + ">:";
        case Stage::Default: return "Valore iniziale dell'attributo:";
        case Stage::Position: return "Punto iniziale del testo:";
        case Stage::Height: return "Altezza <2.5>:";
        }
        return {};
    }

    bool wantsText() const override
    {
        return stage_ == Stage::Tag || stage_ == Stage::Prompt || stage_ == Stage::Default;
    }
    bool textIsLine() const override { return stage_ != Stage::Tag; }

    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        const std::string value = trimmed(text);
        if (stage_ == Stage::Tag) {
            if (value.empty() || value.find(' ') != std::string::npos) {
                ctx.message("L'etichetta non può essere vuota né contenere spazi.");
                return CommandStatus::Continue;
            }
            tag_ = value;
            for (char& c : tag_)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            stage_ = Stage::Prompt;
        } else if (stage_ == Stage::Prompt) {
            prompt_ = text;
            stage_ = Stage::Default;
        } else {
            default_ = text;
            stage_ = Stage::Position;
        }
        return CommandStatus::Continue;
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        switch (stage_) {
        case Stage::Tag:
            return CommandStatus::Finished;
        case Stage::Prompt:
            prompt_ = tag_;
            stage_ = Stage::Default;
            break;
        case Stage::Default:
            stage_ = Stage::Position;
            break;
        case Stage::Height:
            return create(ctx, 2.5);
        case Stage::Position:
            return CommandStatus::Finished;
        }
        return CommandStatus::Continue;
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (stage_ == Stage::Position) {
            position_ = p;
            ctx.lastPoint = p;
            stage_ = Stage::Height;
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Height)
            return create(ctx, distance(position_, p));
        return Command::onPoint(ctx, p);
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (stage_ != Stage::Height)
            return Command::onValue(ctx, v);
        return create(ctx, v);
    }

    std::optional<Vec2> basePoint() const override
    {
        return stage_ == Stage::Height ? std::optional<Vec2>(position_) : std::nullopt;
    }

private:
    enum class Stage { Tag, Prompt, Default, Position, Height };

    CommandStatus create(CommandContext& ctx, double height)
    {
        if (height <= 0.0) {
            ctx.message("L'altezza deve essere maggiore di zero.");
            return CommandStatus::Continue;
        }
        ctx.document.add(std::make_unique<AttDefEntity>(tag_, prompt_, TextEntity(position_, height, default_)));
        return CommandStatus::Finished;
    }

    Stage stage_ = Stage::Tag;
    std::string tag_;
    std::string prompt_;
    std::string default_;
    Vec2 position_;
};

// ---------------------------------------------------------------- EXPLODE

class ExplodeCommand final : public SelectingCommand {
protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        std::vector<std::unique_ptr<Entity>> added;
        std::vector<EntityId> removed;
        int skipped = 0;

        for (EntityId id : ctx.selection) {
            const Entity* e = ctx.document.find(id);
            if (!e)
                continue;
            std::vector<std::unique_ptr<Entity>> parts = explode(*e);
            if (parts.empty()) {
                ++skipped;
                continue;
            }
            for (auto& part : parts) {
                part->setId(0);
                // Gli oggetti sul layer "0" e con colore DaBlocco ereditano dal blocco.
                if (part->layer() <= 0)
                    part->setLayer(e->layer());
                if (part->color() < 0 || part->color() == kColorByBlock)
                    part->setColor(e->color());
                // Tipo e spessore DaBlocco (o non assegnati) diventano quelli dell'oggetto esploso.
                const std::string& lt = part->linetype();
                if (lt.empty() || lt == "ByBlock" || lt == "BYBLOCK")
                    part->setLinetype(e->linetype().empty() ? std::string("ByLayer") : e->linetype());
                if (part->lineweight() == kLineweightUnset || part->lineweight() == kLineweightByBlock)
                    part->setLineweight(e->lineweight() == kLineweightUnset ? kLineweightByLayer : e->lineweight());
                added.push_back(std::move(part));
            }
            removed.push_back(id);
        }

        if (!removed.empty())
            ctx.document.commit(std::move(added), removed);
        ctx.message(std::to_string(removed.size()) + " oggetti esplosi"
                    + (skipped ? ", " + std::to_string(skipped) + " non scomponibili." : "."));
        ctx.selection.clear();
        return CommandStatus::Finished;
    }

private:
    static std::vector<std::unique_ptr<Entity>> explode(const Entity& e)
    {
        std::vector<std::unique_ptr<Entity>> parts;
        switch (e.type()) {
        case EntityType::Polyline:
            for (const Prim& p : e.prims()) {
                if (p.kind == Prim::Kind::Segment)
                    parts.push_back(std::make_unique<LineEntity>(p.a, p.b));
                else
                    parts.push_back(std::make_unique<ArcEntity>(p.center, p.radius, p.start, p.start + p.sweep));
            }
            for (auto& part : parts)
                part->copyStyleFrom(e);
            break;
        case EntityType::Insert: {
            const auto& insert = static_cast<const InsertEntity&>(e);
            if (!insert.block)
                break;
            const Affine t = insert.affine();
            for (const auto& child : insert.block->entities) {
                auto copy = child->clone();
                copy->transform(t);
                parts.push_back(std::move(copy));
            }
            break;
        }
        case EntityType::Dimension:
            parts = static_cast<const DimensionEntity&>(e).geometry();
            for (auto& part : parts)
                part->setLayer(e.layer());
            break;
        case EntityType::Mline:
        case EntityType::Wipeout:
            // Multilinee e coperture diventano linee (la copertura il suo contorno).
            for (const Prim& p : e.prims())
                parts.push_back(std::make_unique<LineEntity>(p.a, p.b));
            for (auto& part : parts)
                part->copyStyleFrom(e);
            break;
        case EntityType::Table:
        case EntityType::Leader:
            parts = e.type() == EntityType::Table ? static_cast<const TableEntity&>(e).geometry()
                                                  : static_cast<const LeaderEntity&>(e).geometry();
            break;
        case EntityType::MText: {
            const auto& m = static_cast<const MTextEntity&>(e);
            const auto lines = m.lines();
            const auto box = m.box();
            const Vec2 down = Vec2::polar(1.0, m.rotation - kPi / 2.0);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const Vec2 baseline = box[3] + down * (m.height + static_cast<double>(i) * m.lineHeight());
                parts.push_back(std::make_unique<TextEntity>(baseline, m.height, lines[i], m.rotation));
            }
            for (auto& part : parts) {
                part->setLayer(e.layer());
                part->setColor(e.color());
            }
            break;
        }
        default:
            break;
        }
        return parts;
    }
};

// Inserimento sotto il punto (per i comandi sugli attributi).
const InsertEntity* pickInsert(CommandContext& ctx, Vec2 p)
{
    const InsertEntity* best = nullptr;
    double bestDistance = ctx.pickTolerance;
    for (const Entity* e : ctx.document.entitiesNear(p, ctx.pickTolerance)) {
        if (e->type() != EntityType::Insert)
            continue;
        const auto& insert = static_cast<const InsertEntity&>(*e);
        double d = e->distanceTo(p);
        for (const Attribute& a : insert.attributes)   // anche cliccando su un attributo
            d = std::min(d, a.text.distanceTo(p));
        if (d <= bestDistance) {
            bestDistance = d;
            best = &insert;
        }
    }
    return best;
}

// ATTSYNC: gli inserimenti di un blocco seguono le sue definizioni di attributo.
class AttSyncCommand final : public Command {
public:
    std::string prompt() const override
    {
        return selecting_ ? "Blocco:" : "Nome del blocco o [Seleziona] <Seleziona>:";
    }
    bool wantsText() const override { return !selecting_; }
    bool textIsLine() const override { return false; }
    CommandStatus onText(CommandContext& ctx, const std::string& text) override
    {
        std::string upper;
        for (char c : text)
            upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == "S" || upper == "SELEZIONA" || upper == "SELECT") {
            selecting_ = true;
            return CommandStatus::Continue;
        }
        return sync(ctx, text);
    }
    CommandStatus onEnter(CommandContext&) override
    {
        if (selecting_)
            return CommandStatus::Finished;
        selecting_ = true;
        return CommandStatus::Continue;
    }
    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (const InsertEntity* insert = pickInsert(ctx, p))
            return sync(ctx, insert->blockName);
        ctx.message("Nessun blocco qui.");
        return CommandStatus::Continue;
    }
    bool wantsSelection() const override { return selecting_; }

private:
    CommandStatus sync(CommandContext& ctx, const std::string& name)
    {
        const auto block = ctx.document.findBlock(name);
        if (!block) {
            ctx.message("Blocco \"" + name + "\" non trovato.");
            return CommandStatus::Continue;
        }
        const int n = syncBlockAttributes(ctx.document, block->name);
        ctx.message("ATTSYNC " + block->name + ": " + std::to_string(n) + (n == 1 ? " inserimento aggiornato." : " inserimenti aggiornati."));
        return CommandStatus::Finished;
    }
    bool selecting_ = false;
};

// EATTEDIT: si sceglie un blocco con attributi, la modifica la apre l'interfaccia.
class EAttEditCommand final : public Command {
public:
    std::string prompt() const override { return "Blocco:"; }
    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        const InsertEntity* insert = pickInsert(ctx, p);
        if (!insert || insert->attributes.empty()) {
            ctx.message(insert ? "Il blocco non ha attributi." : "Nessun blocco qui.");
            return CommandStatus::Continue;
        }
        if (ctx.editAttributes)
            ctx.editAttributes(insert->id());
        return CommandStatus::Finished;
    }
    bool wantsSelection() const override { return true; }
};


// ------------------------------------------------------------------- XCLIP

// XCLIP: ritaglio di blocchi e riferimenti esterni con un contorno rettangolare o poligonale; ON/OFF/Elimina.
class XClipCommand final : public SelectingCommand {
protected:
    std::string actionPrompt() const override
    {
        switch (stage_) {
        case Stage::Select: return {};
        case Stage::Option: return "Ritaglio [ON/OFF/Elimina/Nuovo contorno] <Nuovo>:";
        case Stage::Kind: return "Contorno [Poligonale/Rettangolare] <Rettangolare>:";
        case Stage::Corner1: return "Primo angolo:";
        case Stage::Corner2: return "Angolo opposto:";
        case Stage::Polygon:
            return points_.empty() ? "Primo punto:" : "Punto successivo (Invio per chiudere):";
        }
        return {};
    }

protected:
    CommandStatus onSelected(CommandContext& ctx) override
    {
        inserts_.clear();
        for (EntityId id : ctx.selection)
            if (const Entity* e = ctx.document.find(id); e && e->type() == EntityType::Insert)
                inserts_.push_back(id);
        ctx.selection.clear();
        if (inserts_.empty()) {
            ctx.message("Nessun blocco o riferimento esterno selezionato.");
            return CommandStatus::Finished;
        }
        stage_ = Stage::Option;
        return CommandStatus::Continue;
    }

    CommandStatus actionKeyword(CommandContext& ctx, const std::string& w) override
    {
        if (stage_ == Stage::Option) {
            if (w == "ON" || w == "OFF") {
                const bool on = w == "ON";
                modify(ctx, [on](InsertEntity& in) { in.clipOn = on; });
                return CommandStatus::Finished;
            }
            if (w == "E" || w == "ELIMINA" || w == "D" || w == "DELETE") {
                modify(ctx, [](InsertEntity& in) { in.clip.clear(); in.clipOn = true; });
                ctx.message("Ritaglio eliminato.");
                return CommandStatus::Finished;
            }
            if (w == "N" || w == "NUOVO" || w == "NEW") {
                stage_ = Stage::Kind;
                return CommandStatus::Continue;
            }
        } else if (stage_ == Stage::Kind) {
            if (w == "P" || w == "POLIGONALE" || w == "POLYGONAL") {
                stage_ = Stage::Polygon;
                return CommandStatus::Continue;
            }
            if (w == "R" || w == "RETTANGOLARE" || w == "RECTANGULAR") {
                stage_ = Stage::Corner1;
                return CommandStatus::Continue;
            }
        }
        return Command::onKeyword(ctx, w);
    }

    CommandStatus actionPoint(CommandContext& ctx, Vec2 p) override
    {
        switch (stage_) {
        case Stage::Corner1:
            points_ = {p};
            ctx.lastPoint = p;
            stage_ = Stage::Corner2;
            return CommandStatus::Continue;
        case Stage::Corner2:
            points_.push_back(p);
            apply(ctx);
            return CommandStatus::Finished;
        case Stage::Polygon:
            points_.push_back(p);
            ctx.lastPoint = p;
            return CommandStatus::Continue;
        case Stage::Kind:   // un punto subito: rettangolo
            points_ = {p};
            stage_ = Stage::Corner2;
            return CommandStatus::Continue;
        default:
            return Command::onPoint(ctx, p);
        }
    }

    CommandStatus actionEnter(CommandContext& ctx) override
    {
        if (stage_ == Stage::Option) {
            stage_ = Stage::Kind;
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Kind) {
            stage_ = Stage::Corner1;
            return CommandStatus::Continue;
        }
        if (stage_ == Stage::Polygon) {
            if (points_.size() < 3) {
                ctx.message("Servono almeno tre punti.");
                return CommandStatus::Continue;
            }
            apply(ctx);
        }
        return CommandStatus::Finished;
    }

    std::vector<std::unique_ptr<Entity>> actionPreview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (stage_ == Stage::Corner2 && !points_.empty()) {
            const Vec2 a = points_[0];
            out.push_back(std::make_unique<PolylineEntity>(
                std::vector<PolyVertex>{{a, 0.0}, {{cursor.x, a.y}, 0.0}, {cursor, 0.0}, {{a.x, cursor.y}, 0.0}}, true));
        } else if (stage_ == Stage::Polygon && !points_.empty()) {
            std::vector<PolyVertex> v;
            for (Vec2 p : points_)
                v.push_back({p, 0.0});
            v.push_back({cursor, 0.0});
            out.push_back(std::make_unique<PolylineEntity>(std::move(v), true));
        }
        return out;
    }

private:
    enum class Stage { Select, Option, Kind, Corner1, Corner2, Polygon };

    void modify(CommandContext& ctx, const std::function<void(InsertEntity&)>& change)
    {
        std::vector<std::unique_ptr<Entity>> added;
        for (EntityId id : inserts_)
            if (const Entity* e = ctx.document.find(id)) {
                auto copy = e->clone();
                change(static_cast<InsertEntity&>(*copy));
                added.push_back(std::move(copy));
            }
        ctx.document.commit(std::move(added), inserts_);
    }

    // Il contorno indicato (nel disegno) passa nelle coordinate di ogni blocco.
    void apply(CommandContext& ctx)
    {
        const std::vector<Vec2> world = points_;
        modify(ctx, [&world](InsertEntity& in) {
            const Affine a = in.affine();
            const double det = a.m11 * a.m22 - a.m12 * a.m21;
            if (std::abs(det) < 1e-300)
                return;
            in.clip.clear();
            for (Vec2 w : world) {
                const double x = w.x - a.dx, y = w.y - a.dy;
                in.clip.push_back({(a.m22 * x - a.m21 * y) / det, (-a.m12 * x + a.m11 * y) / det});
            }
            in.clipOn = true;
        });
        ctx.message(std::to_string(inserts_.size()) + (inserts_.size() == 1 ? " blocco ritagliato." : " blocchi ritagliati."));
    }

    Stage stage_ = Stage::Select;
    std::vector<EntityId> inserts_;
    std::vector<Vec2> points_;
};
} // namespace

void registerBlockCommands(CommandManager& manager)
{
    manager.add("BLOCK", {"B", "BLOCCO"}, [] { return std::make_unique<BlockCommand>(); });
    manager.add("INSERT", {"I", "INSERISCI"}, [] { return std::make_unique<InsertCommand>(); });
    manager.add("XCLIP", {"XC", "RITAGLIO"}, [] { return std::make_unique<XClipCommand>(); });
    manager.add("ATTSYNC", {"SINCATT"}, [] { return std::make_unique<AttSyncCommand>(); });
    manager.add("EATTEDIT", {"ATTEDIT", "ATE", "MODATT"}, [] { return std::make_unique<EAttEditCommand>(); });
    manager.add("ATTDEF", {"ATT", "-ATTDEF", "DEFATT"}, [] { return std::make_unique<AttDefCommand>(); });
    manager.add("EXPLODE", {"X", "ESPLODI"}, [] { return std::make_unique<ExplodeCommand>(); });
}

} // namespace cartesio
