#include "core/commands/Command.h"

#include <algorithm>

namespace cartesio {

// ----------------------------------------------------------- SelectionPicker

int SelectionPicker::apply(CommandContext& ctx, std::vector<EntityId> ids)
{
    auto& selection = ctx.selection;
    ctx.document.expandGroups(ids);   // tutto il gruppo
    for (EntityId id : ids) {
        const auto it = std::find(selection.begin(), selection.end(), id);
        if (ctx.shiftPressed) {
            if (it != selection.end())
                selection.erase(it);
        } else if (it == selection.end()) {
            selection.push_back(id);
        }
    }
    return static_cast<int>(ids.size());
}

int SelectionPicker::click(CommandContext& ctx, Vec2 p)
{
    if (corner_) {
        BBox box;
        box.expand(*corner_);
        box.expand(p);
        const bool crossing = p.x < corner_->x;   // da destra a sinistra
        corner_.reset();
        return apply(ctx, ctx.document.selectInRect(box, crossing));
    }

    // L'oggetto scelto col ciclo di selezione (se è ancora lì sotto), altrimenti quello che vince da solo.
    const Entity* e = nullptr;
    if (ctx.pickHint != 0)
        if (const Entity* hint = ctx.document.find(ctx.pickHint);
            hint && ctx.document.isSelectable(*hint) && hint->distanceTo(p) <= ctx.pickTolerance)
            e = hint;
    if (!e)
        e = ctx.document.pick(p, ctx.pickTolerance);
    if (e)
        return apply(ctx, {e->id()});

    corner_ = p;
    return -1;
}

int SelectionPicker::lasso(CommandContext& ctx, const std::vector<Vec2>& polygon, bool crossing)
{
    corner_.reset();
    return apply(ctx, ctx.document.selectInPolygon(polygon, crossing));
}

int selectionKeyword(CommandContext& ctx, const std::string& word, bool remove)
{
    const Document& doc = ctx.document;
    std::vector<EntityId> ids;
    const auto usable = [&doc](const Entity& e) {
        return doc.resolveSpace(e.space()) == doc.activeSpace() && doc.isSelectable(e);
    };
    if (word == "T" || word == "TUTTO" || word == "ALL") {
        for (const auto& e : doc.entities())
            if (usable(*e))
                ids.push_back(e->id());
    } else if (word == "U" || word == "ULTIMO" || word == "L" || word == "LAST") {
        const auto& all = doc.entities();
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            if (usable(**it)) {
                ids.push_back((*it)->id());
                break;
            }
    } else if (word == "P" || word == "PRECEDENTE" || word == "PREVIOUS") {
        for (EntityId id : ctx.previousSelection)
            if (const Entity* e = doc.find(id); e && usable(*e))
                ids.push_back(id);
    } else {
        return -1;
    }
    auto& selection = ctx.selection;
    for (EntityId id : ids) {
        const auto it = std::find(selection.begin(), selection.end(), id);
        if (remove) {
            if (it != selection.end())
                selection.erase(it);
        } else if (it == selection.end()) {
            selection.push_back(id);
        }
    }
    return static_cast<int>(ids.size());
}

// ---------------------------------------------------------- SelectingCommand

std::string SelectingCommand::prompt() const
{
    if (!selecting_)
        return actionPrompt();
    return removing_ ? "Oggetti da togliere o [AGgiungi/Tutto/Ultimo/Precedente] (Invio per finire):"
                     : "Oggetti da selezionare o [Tutto/Ultimo/Precedente/Rimuovi] (Invio per finire):";
}

CommandStatus SelectingCommand::start(CommandContext& ctx)
{
    if (ctx.selection.empty())
        return CommandStatus::Continue;
    selecting_ = false;
    ctx.previousSelection = ctx.selection;
    return onSelected(ctx);
}

void SelectingCommand::report(CommandContext& ctx, int found) const
{
    if (found == 0)
        ctx.message("Nessun oggetto trovato.");
    else if (found > 0)
        ctx.message(std::to_string(found) + (found == 1 ? " trovato" : " trovati") + (removing_ ? " (rimossi)" : "")
                    + ", " + std::to_string(ctx.selection.size()) + " in totale.");
}

bool SelectingCommand::lassoSelect(CommandContext& ctx, const std::vector<Vec2>& polygon, bool crossing)
{
    if (!selecting_)
        return false;
    const bool shift = ctx.shiftPressed;
    ctx.shiftPressed = shift || removing_;
    report(ctx, picker_.lasso(ctx, polygon, crossing));
    ctx.shiftPressed = shift;
    return true;
}

CommandStatus SelectingCommand::onPoint(CommandContext& ctx, Vec2 p)
{
    if (!selecting_)
        return actionPoint(ctx, p);
    const bool shift = ctx.shiftPressed;
    ctx.shiftPressed = shift || removing_;
    report(ctx, picker_.click(ctx, p));
    ctx.shiftPressed = shift;
    return CommandStatus::Continue;
}

CommandStatus SelectingCommand::onValue(CommandContext& ctx, double v)
{
    return selecting_ ? Command::onValue(ctx, v) : actionValue(ctx, v);
}

CommandStatus SelectingCommand::onKeyword(CommandContext& ctx, const std::string& word)
{
    if (!selecting_)
        return actionKeyword(ctx, word);
    if (word == "R" || word == "RIMUOVI" || word == "REMOVE") {
        removing_ = true;
        return CommandStatus::Continue;
    }
    if (word == "AG" || word == "AGGIUNGI" || word == "A" || word == "ADD") {
        removing_ = false;
        return CommandStatus::Continue;
    }
    const int found = selectionKeyword(ctx, word, removing_);
    if (found < 0)
        return Command::onKeyword(ctx, word);
    picker_.reset();
    report(ctx, found);
    return CommandStatus::Continue;
}

CommandStatus SelectingCommand::onEnter(CommandContext& ctx)
{
    if (!selecting_)
        return actionEnter(ctx);
    picker_.reset();
    if (ctx.selection.empty()) {
        ctx.message("Nessun oggetto selezionato.");
        return CommandStatus::Finished;
    }
    selecting_ = false;
    ctx.previousSelection = ctx.selection;
    return onSelected(ctx);
}

void SelectingCommand::cancel(CommandContext& ctx)
{
    picker_.reset();
    ctx.selection.clear();
}

std::vector<std::unique_ptr<Entity>> SelectingCommand::preview(Vec2 cursor) const
{
    if (selecting_)
        return {};
    return actionPreview(cursor);
}

std::optional<Vec2> SelectingCommand::basePoint() const
{
    if (selecting_)
        return std::nullopt;
    return actionBasePoint();
}

std::optional<Vec2> SelectingCommand::selectionCorner() const
{
    if (selecting_)
        return picker_.corner();
    return std::nullopt;
}

} // namespace cartesio
