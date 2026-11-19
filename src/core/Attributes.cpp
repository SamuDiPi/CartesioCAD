#include "core/Attributes.h"

#include <map>

namespace cartesio {

std::vector<const AttDefEntity*> attributeDefinitions(const BlockDef& block)
{
    std::vector<const AttDefEntity*> out;
    for (const auto& child : block.entities)
        if (child->type() == EntityType::AttDef)
            out.push_back(static_cast<const AttDefEntity*>(child.get()));
    return out;
}

void syncAttributes(InsertEntity& insert)
{
    if (!insert.block)
        return;
    std::map<std::string, std::string> values;
    for (const Attribute& a : insert.attributes)
        values.emplace(a.tag, a.text.text);
    const Affine t = insert.affine();
    std::vector<Attribute> synced;
    for (const AttDefEntity* def : attributeDefinitions(*insert.block)) {
        if (def->flags & AttConstant)
            continue;
        TextEntity text = def->text;
        text.transform(t);
        const auto it = values.find(def->tag);
        text.text = it != values.end() ? it->second : def->text.text;
        text.setLayer(def->layer());
        text.setColor(def->color());
        synced.push_back({def->tag, text, def->flags});
    }
    insert.attributes = std::move(synced);
}

int syncBlockAttributes(Document& doc, const std::string& blockName)
{
    std::vector<std::unique_ptr<Entity>> changed;
    std::vector<EntityId> ids;
    for (const auto& e : doc.entities()) {
        if (e->type() != EntityType::Insert || static_cast<const InsertEntity&>(*e).blockName != blockName)
            continue;
        auto copy = e->clone();
        syncAttributes(static_cast<InsertEntity&>(*copy));
        ids.push_back(e->id());
        changed.push_back(std::move(copy));
    }
    const int count = static_cast<int>(ids.size());
    if (count > 0)
        doc.commit(std::move(changed), ids);
    return count;
}

bool setAttributeValues(Document& doc, EntityId id, const std::vector<std::pair<std::string, std::string>>& values)
{
    const Entity* e = doc.find(id);
    if (!e || e->type() != EntityType::Insert)
        return false;
    auto copy = e->clone();
    auto& insert = static_cast<InsertEntity&>(*copy);
    bool changed = false;
    for (const auto& [tag, value] : values)
        for (Attribute& a : insert.attributes)
            if (a.tag == tag && a.text.text != value) {
                a.text.text = value;
                changed = true;
            }
    if (!changed)
        return false;
    std::vector<std::unique_ptr<Entity>> added;
    added.push_back(std::move(copy));
    doc.commit(std::move(added), {id});
    return true;
}

} // namespace cartesio
