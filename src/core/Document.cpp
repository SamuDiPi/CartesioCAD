#include "core/Document.h"
#include "core/QuickSelect.h"

#include "core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string_view>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cartesio {

namespace {

int spaceOf(const Entity& e)
{
    return e.space() < 0 ? 0 : e.space();
}

} // namespace

Document::Document()
{
    layers_.push_back(Layer{"0", 7, true});
    linetypes_.push_back(Linetype{"Continuous", "Solid line", {}});
    TextStyle standard;
    standard.name = "Standard";
    standard.font = "arial.ttf";
    standard.family = "Arial";
    textStyles_.push_back(standard);
    dimStyles_.push_back(DimStyle{});
    addLayout("Layout1");
    revision_ = 0;
}

void Document::rebuildIdIndex() const
{
    idIndex_.clear();
    idIndex_.reserve(entities_.size());
    for (std::size_t i = 0; i < entities_.size(); ++i)
        idIndex_.emplace(entities_[i]->id(), i);
    idRevision_ = revision_;
}

const Entity* Document::find(EntityId id) const
{
    if (idRevision_ != revision_ || idIndex_.size() != entities_.size())
        rebuildIdIndex();
    auto it = idIndex_.find(id);
    if (it != idIndex_.end() && it->second < entities_.size() && entities_[it->second]->id() == id)
        return entities_[it->second].get();
    if (it == idIndex_.end())
        return nullptr;
    // L'elenco è cambiato senza nuova revisione (es. riordino): si rifà l'indice una volta.
    rebuildIdIndex();
    it = idIndex_.find(id);
    return it != idIndex_.end() ? entities_[it->second].get() : nullptr;
}

// Ingombri: se sono cambiati solo degli oggetti si ricalcolano solo i loro (riconosciuti dal serial), altrimenti
// tutti (es. un blocco ridefinito cambia l'ingombro dei suoi inserimenti senza cambiare gli inserimenti).
void Document::ensureBounds() const
{
    if (boundsRevision_ == revision_ && boundsCache_.size() == entities_.size())
        return;
    const bool all = boundsTableRevision_ != tableRevision();
    std::vector<CachedBounds> next;
    next.reserve(entities_.size());
    std::unordered_map<std::uint64_t, std::size_t> bySerial;   // solo se gli oggetti non sono più allineati
    bool mapped = false;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const Entity& e = *entities_[i];
        const std::uint64_t serial = e.serial();
        if (!all) {
            if (i < boundsCache_.size() && boundsCache_[i].serial == serial) {
                next.push_back(boundsCache_[i]);
                continue;
            }
            if (!mapped) {
                bySerial.reserve(boundsCache_.size());
                for (std::size_t k = 0; k < boundsCache_.size(); ++k)
                    bySerial.emplace(boundsCache_[k].serial, k);
                mapped = true;
            }
            if (const auto it = bySerial.find(serial); it != bySerial.end()) {
                next.push_back(boundsCache_[it->second]);
                continue;
            }
        }
        next.push_back({serial, e.bounds()});
    }
    boundsCache_ = std::move(next);
    boundsRevision_ = revision_;
    boundsTableRevision_ = tableRevision();
}

std::vector<const Entity*> Document::entitiesNear(Vec2 p, double tolerance, int space) const
{
    ensureBounds();
    const int wanted = resolveSpace(space);
    std::vector<const Entity*> result;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const BBox& b = boundsCache_[i].box;
        if (!b.isValid() || p.x < b.min.x - tolerance || p.x > b.max.x + tolerance || p.y < b.min.y - tolerance
            || p.y > b.max.y + tolerance)
            continue;
        if (spaceOf(*entities_[i]) == wanted && isShown(*entities_[i]))
            result.push_back(entities_[i].get());
    }
    return result;
}

std::vector<const Entity*> Document::entitiesInRect(const BBox& rect, double minSize, int space) const
{
    ensureBounds();
    const int wanted = resolveSpace(space);
    std::vector<const Entity*> result;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const BBox& b = boundsCache_[i].box;
        if (!b.isValid() || b.max.x < rect.min.x || b.min.x > rect.max.x || b.max.y < rect.min.y || b.min.y > rect.max.y)
            continue;
        const double size = std::max(b.width(), b.height());
        if (size > 0.0 && size < minSize)
            continue;
        if (spaceOf(*entities_[i]) == wanted && isShown(*entities_[i]))
            result.push_back(entities_[i].get());
    }
    return result;
}

const Entity* Document::pick(Vec2 p, double tolerance) const
{
    // Prima i bordi e le linee (a parità vince l'ultimo disegnato, quello sopra); l'interno di tratteggi e
    // immagini solo se lì non c'è altro: un clic su una linea sopra un tratteggio seleziona la linea.
    const Entity* best = nullptr;
    double bestDistance = tolerance;
    const Entity* inside = nullptr;
    double insideArea = 0.0;
    for (const Entity* e : entitiesNear(p, tolerance)) {
        if (!isSelectable(*e))
            continue;
        if (e->type() == EntityType::Hatch || e->type() == EntityType::Image) {
            const double edge = e->Entity::distanceTo(p);
            if (edge <= bestDistance) {
                bestDistance = edge;
                best = e;
            } else if (e->distanceTo(p) <= 0.0) {
                const BBox b = e->bounds();
                const double area = b.isValid() ? b.width() * b.height() : 0.0;
                if (!inside || area < insideArea) {   // il più piccolo che contiene il punto
                    inside = e;
                    insideArea = area;
                }
            }
            continue;
        }
        const double d = e->distanceTo(p);
        if (d <= bestDistance) {
            bestDistance = d;
            best = e;
        }
    }
    return best ? best : inside;
}

std::vector<EntityId> Document::selectInRect(const BBox& rect, bool crossing) const
{
    // Con gli ingombri già pronti: si esaminano davvero solo gli oggetti che toccano il rettangolo.
    ensureBounds();
    std::vector<EntityId> ids;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const Entity& e = *entities_[i];
        const BBox& b = boundsCache_[i].box;
        if (!b.isValid() || b.max.x < rect.min.x || b.min.x > rect.max.x || b.max.y < rect.min.y || b.min.y > rect.max.y)
            continue;
        if (spaceOf(e) != activeSpace_ || !isSelectable(e))
            continue;
        if (containsBox(rect, b) || (crossing && touchesRect(e, rect)))
            ids.push_back(e.id());
    }
    return ids;
}

std::vector<const Entity*> Document::pickAll(Vec2 p, double tolerance) const
{
    // Stesso ordine di pick(): bordi e linee per distanza (a parità l'ultimo disegnato), poi gli interni di
    // tratteggi e immagini dal più piccolo.
    struct Candidate {
        const Entity* e;
        int group;      // 0 = bordo o linea, 1 = interno
        double key;     // distanza o area
        std::size_t order;
    };
    std::vector<Candidate> found;
    std::size_t order = 0;
    for (const Entity* e : entitiesNear(p, tolerance)) {
        ++order;
        if (!isSelectable(*e))
            continue;
        if (e->type() == EntityType::Hatch || e->type() == EntityType::Image) {
            const double edge = e->Entity::distanceTo(p);
            if (edge <= tolerance) {
                found.push_back({e, 0, edge, order});
            } else if (e->distanceTo(p) <= 0.0) {
                const BBox b = e->bounds();
                found.push_back({e, 1, b.isValid() ? b.width() * b.height() : 0.0, order});
            }
            continue;
        }
        const double d = e->distanceTo(p);
        if (d <= tolerance)
            found.push_back({e, 0, d, order});
    }
    std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        if (a.group != b.group)
            return a.group < b.group;
        if (a.key != b.key)
            return a.key < b.key;
        return a.order > b.order;   // a parità vince quello sopra
    });
    std::vector<const Entity*> result;
    result.reserve(found.size());
    for (const Candidate& c : found)
        result.push_back(c.e);
    return result;
}

namespace {

bool insidePolygon(const std::vector<Vec2>& poly, Vec2 p)
{
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

bool segmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const auto side = [](Vec2 o, Vec2 u, Vec2 v) { return (u.x - o.x) * (v.y - o.y) - (u.y - o.y) * (v.x - o.x); };
    const double d1 = side(c, d, a), d2 = side(c, d, b), d3 = side(a, b, c), d4 = side(a, b, d);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

// Spezzata che rappresenta l'oggetto per il lazo: le primitive (archi a tratti), oppure i vertici del riquadro.
std::vector<std::pair<Vec2, Vec2>> lassoSegments(const Entity& e)
{
    std::vector<std::pair<Vec2, Vec2>> segments;
    for (const Prim& p : e.prims()) {
        if (p.kind == Prim::Kind::Segment) {
            segments.emplace_back(p.a, p.b);
            continue;
        }
        const int n = std::clamp(static_cast<int>(p.sweep / (kPi / 16.0)) + 1, 2, 64);
        Vec2 last = p.pointAt(p.start);
        for (int k = 1; k <= n; ++k) {
            const Vec2 next = p.pointAt(p.start + p.sweep * k / n);
            segments.emplace_back(last, next);
            last = next;
        }
    }
    if (segments.empty()) {   // testi, punti: il riquadro
        const BBox b = e.bounds();
        if (b.isValid()) {
            const Vec2 c[4] = {b.min, {b.max.x, b.min.y}, b.max, {b.min.x, b.max.y}};
            for (int k = 0; k < 4; ++k)
                segments.emplace_back(c[k], c[(k + 1) % 4]);
        }
    }
    return segments;
}

} // namespace

std::vector<EntityId> Document::selectInPolygon(const std::vector<Vec2>& polygon, bool crossing) const
{
    std::vector<EntityId> ids;
    if (polygon.size() < 3)
        return ids;
    BBox area;
    for (Vec2 v : polygon)
        area.expand(v);
    for (const auto& e : entities_) {
        if (spaceOf(*e) != activeSpace_ || !isSelectable(*e))
            continue;
        const BBox b = e->bounds();
        if (!b.isValid() || b.max.x < area.min.x || b.min.x > area.max.x || b.max.y < area.min.y || b.min.y > area.max.y)
            continue;
        if (!crossing && !containsBox(area, b))
            continue;
        const auto segments = lassoSegments(*e);
        if (segments.empty())
            continue;
        bool anyInside = false, allInside = true, crosses = false;
        for (const auto& [a, z] : segments) {
            const bool ia = insidePolygon(polygon, a), iz = insidePolygon(polygon, z);
            anyInside = anyInside || ia || iz;
            allInside = allInside && ia && iz;
            if (!crosses)
                for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
                    if (segmentsCross(a, z, polygon[j], polygon[i])) {
                        crosses = true;
                        break;
                    }
            if (crossing && (anyInside || crosses))
                break;
            if (!crossing && (!allInside || crosses))
                break;
        }
        if (crossing ? (anyInside || crosses) : (allInside && !crosses))
            ids.push_back(e->id());
    }
    return ids;
}

void Document::hideObjects(const std::vector<EntityId>& ids)
{
    for (EntityId id : ids)
        hidden_.insert(id);
    ++visibilityRevision_;
}

void Document::isolateObjects(const std::vector<EntityId>& ids)
{
    const std::unordered_set<EntityId> keep(ids.begin(), ids.end());
    for (const auto& e : entities_)
        if (spaceOf(*e) == activeSpace_ && !keep.count(e->id()))
            hidden_.insert(e->id());
    for (EntityId id : ids)
        hidden_.erase(id);
    ++visibilityRevision_;
}

void Document::showObjects(const std::vector<EntityId>& ids)
{
    for (EntityId id : ids)
        hidden_.erase(id);
    ++visibilityRevision_;
}

std::size_t Document::unisolateObjects()
{
    const std::size_t n = hidden_.size();
    hidden_.clear();
    ++visibilityRevision_;
    return n;
}

BBox Document::extents(int space) const
{
    ensureBounds();
    const int wanted = resolveSpace(space);
    BBox box;
    for (std::size_t i = 0; i < entities_.size(); ++i)
        if (spaceOf(*entities_[i]) == wanted && isLayerVisible(entities_[i]->layer()))
            // Le linee di costruzione sono infinite: conta solo il loro punto base.
            box.expand(entities_[i]->type() == EntityType::XLine
                           ? [&] {
                                 BBox b;
                                 b.expand(static_cast<const XLineEntity&>(*entities_[i]).base);
                                 return b;
                             }()
                           : boundsCache_[i].box);
    return box;
}

// ----------------------------------------------------------------------- layout

Layout* Document::findLayout(int id)
{
    for (Layout& l : layouts_)
        if (l.id == id)
            return &l;
    return nullptr;
}

const Layout* Document::findLayout(int id) const
{
    for (const Layout& l : layouts_)
        if (l.id == id)
            return &l;
    return nullptr;
}

int Document::addLayout(const std::string& name)
{
    std::string n = name;
    for (int i = static_cast<int>(layouts_.size()) + 1; n.empty(); ++i) {
        const std::string candidate = "Layout" + std::to_string(i);
        if (std::none_of(layouts_.begin(), layouts_.end(), [&](const Layout& l) { return l.name == candidate; }))
            n = candidate;
    }
    Layout layout;
    layout.id = nextLayoutId_++;
    layout.name = n;
    layouts_.push_back(layout);
    ++revision_;
    return layout.id;
}

bool Document::renameLayout(int id, const std::string& name, std::string* error)
{
    const auto fail = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    Layout* layout = findLayout(id);
    if (!layout)
        return fail("Layout inesistente.");
    if (name.empty() || name == "Model" || name == "Modello")
        return fail("Nome del layout non valido.");
    for (const Layout& l : layouts_)
        if (l.id != id && l.name == name)
            return fail("Esiste già un layout con questo nome.");
    layout->name = name;
    ++revision_;
    return true;
}

bool Document::removeLayout(int id, std::string* error)
{
    if (layouts_.size() <= 1) {
        if (error)
            *error = "Il disegno deve avere almeno un layout.";
        return false;
    }
    const auto it = std::find_if(layouts_.begin(), layouts_.end(), [id](const Layout& l) { return l.id == id; });
    if (it == layouts_.end()) {
        if (error)
            *error = "Layout inesistente.";
        return false;
    }
    std::vector<EntityId> contents;
    for (const auto& e : entities_)
        if (spaceOf(*e) == id)
            contents.push_back(e->id());
    if (!contents.empty())
        erase(contents);
    layouts_.erase(it);
    if (activeSpace_ == id)
        activeSpace_ = 0;
    ++revision_;
    return true;
}

int Document::displayColor(const Entity& e) const
{
    const int c = e.color();
    if (c >= 1 && c <= 255)
        return c;
    if (c == kColorByBlock)
        return 7;
    const int l = e.layer();
    if (l >= 0 && l < static_cast<int>(layers_.size()))
        return layers_[static_cast<std::size_t>(l)].color;
    return 7;
}

// ----------------------------------------------------------------------- layer

bool Document::isLayerVisible(int index) const
{
    if (index < 0 || index >= static_cast<int>(layers_.size()))
        return true;
    const Layer& l = layers_[static_cast<std::size_t>(index)];
    return l.visible && !l.frozen;
}

bool Document::isLayerSelectable(int index) const
{
    if (!isLayerVisible(index))
        return false;
    if (index < 0 || index >= static_cast<int>(layers_.size()))
        return true;
    return !layers_[static_cast<std::size_t>(index)].locked;
}

int Document::addLayer(Layer layer)
{
    if (const int existing = findLayer(layer.name); existing >= 0)
        return existing;
    layers_.push_back(std::move(layer));
    ++revision_;
    return static_cast<int>(layers_.size()) - 1;
}

int Document::findLayer(std::string_view name) const
{
    for (std::size_t i = 0; i < layers_.size(); ++i)
        if (layers_[i].name == name)
            return static_cast<int>(i);
    return -1;
}

bool Document::renameLayer(int index, const std::string& name, std::string* error)
{
    const auto fail = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    if (index < 0 || index >= static_cast<int>(layers_.size()))
        return fail("Layer inesistente.");
    if (index == 0)
        return fail("Il layer 0 non può essere rinominato.");
    if (name.empty())
        return fail("Il nome del layer non può essere vuoto.");
    const int existing = findLayer(name);
    if (existing >= 0 && existing != index)
        return fail("Esiste già un layer con questo nome.");
    layers_[static_cast<std::size_t>(index)].name = name;
    ++revision_;
    return true;
}

bool Document::removeLayer(int index, std::string* error)
{
    const auto fail = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    if (index < 0 || index >= static_cast<int>(layers_.size()))
        return fail("Layer inesistente.");
    if (index == 0)
        return fail("Il layer 0 non può essere eliminato.");
    if (index == currentLayer_)
        return fail("Il layer corrente non può essere eliminato.");

    const auto uses = [index](const std::unique_ptr<Entity>& e) { return e->layer() == index; };
    if (std::any_of(entities_.begin(), entities_.end(), uses))
        return fail("Il layer contiene oggetti.");
    for (const auto& b : blocks_)
        if (std::any_of(b->entities.begin(), b->entities.end(), uses))
            return fail("Il layer è usato nella definizione di un blocco.");
    for (const auto* history : {&undo_, &redo_})
        for (const Step& step : *history)
            if (std::any_of(step.added.begin(), step.added.end(), uses)
                || std::any_of(step.removed.begin(), step.removed.end(), uses))
                return fail("Il layer è usato da oggetti nella cronologia di annullamento.");

    layers_.erase(layers_.begin() + index);
    // Gli indici dei layer successivi scalano di uno, ovunque compaiano.
    const auto shift = [index](const std::unique_ptr<Entity>& e) {
        if (e->layer() > index)
            e->setLayer(e->layer() - 1);
        // Layer congelati nelle finestre dei layout.
        if (e->type() == EntityType::Viewport) {
            auto& frozen = static_cast<ViewportEntity&>(*e).frozenLayers;
            std::erase(frozen, index);
            for (int& l : frozen)
                if (l > index)
                    --l;
        }
    };
    std::for_each(entities_.begin(), entities_.end(), shift);
    for (auto& b : blocks_)
        std::for_each(b->entities.begin(), b->entities.end(), shift);
    for (auto* history : {&undo_, &redo_})
        for (Step& step : *history) {
            std::for_each(step.added.begin(), step.added.end(), shift);
            std::for_each(step.removed.begin(), step.removed.end(), shift);
        }
    if (currentLayer_ > index)
        --currentLayer_;
    ++revision_;
    return true;
}

void Document::setCurrentLayer(int index)
{
    if (index >= 0 && index < static_cast<int>(layers_.size()))
        currentLayer_ = index;
}

// ---------------------------------------------------------------------- blocchi

std::shared_ptr<BlockDef> Document::findBlock(std::string_view name) const
{
    for (const auto& b : blocks_)
        if (b->name == name)
            return b;
    return nullptr;
}

void Document::addBlock(std::shared_ptr<BlockDef> block)
{
    for (auto& b : blocks_) {
        if (b->name == block->name) {
            b = std::move(block);
            linkBlocks();
            ++revision_;
            return;
        }
    }
    blocks_.push_back(std::move(block));
    linkBlocks();
    ++revision_;
}

void Document::linkBlocks()
{
    const auto link = [this](Entity& e) {
        if (e.type() == EntityType::Insert) {
            auto& insert = static_cast<InsertEntity&>(e);
            insert.block = findBlock(insert.blockName);
        } else if (e.type() == EntityType::Dimension) {
            auto& dim = static_cast<DimensionEntity&>(e);
            if (!dim.blockName.empty())
                dim.block = findBlock(dim.blockName);
        }
    };
    for (auto& e : entities_)
        link(*e);
    for (auto& b : blocks_)
        for (auto& e : b->entities)
            link(*e);
}

// -------------------------------------------------------------------- modifiche

void Document::prepareNew(Entity& e)
{
    if (e.id() == 0)
        e.setId(nextId_++);
    if (e.layer() < 0)
        e.setLayer(currentLayer_);
    if (e.color() < 0)
        e.setColor(currentColor_);
    if (e.linetype().empty())
        e.setLinetype(currentLinetype_);
    if (e.lineweight() == kLineweightUnset)
        e.setLineweight(currentLineweight_);
    if (e.space() < 0)
        e.setSpace(activeSpace_);
}

void Document::commit(std::vector<std::unique_ptr<Entity>> added, const std::vector<EntityId>& removed)
{
    Step step;
    if (!removed.empty()) {
        std::unordered_map<EntityId, std::size_t> at;
        at.reserve(removed.size());
        for (EntityId id : removed)
            at.emplace(id, 0);
        for (std::size_t i = 0; i < entities_.size(); ++i)
            if (const auto it = at.find(entities_[i]->id()); it != at.end())
                it->second = i;
        for (EntityId id : removed)
            if (const Entity* e = find(id)) {
                step.removed.push_back(e->clone());
                step.removedAt.push_back(at[id]);
            }
    }

    for (auto& e : added) {
        if (!e)
            continue;
        prepareNew(*e);
        step.added.push_back(std::move(e));
    }

    if (step.added.empty() && step.removed.empty())
        return;

    apply(step, true);
    undo_.push_back(std::move(step));
    redo_.clear();
}

bool Document::amendLastAdd(EntityId id, std::unique_ptr<Entity> replacement)
{
    if (undo_.empty())
        return false;
    Step& step = undo_.back();
    if (step.added.size() != 1 || !step.removed.empty() || step.added.front()->id() != id || step.tablesBefore
        || !step.orderAfter.empty())
        return false;
    if (!replacement) {
        removeById(id);
        undo_.pop_back();
    } else {
        replacement->setId(id);
        step.added.front() = replacement->clone();
        for (auto& e : entities_)
            if (e->id() == id)
                e = std::move(replacement);
    }
    ++revision_;
    ++entityRevision_;
    return true;
}

EntityId Document::add(std::unique_ptr<Entity> entity)
{
    if (!entity)
        return 0;
    prepareNew(*entity);
    const EntityId id = entity->id();
    std::vector<std::unique_ptr<Entity>> added;
    added.push_back(std::move(entity));
    commit(std::move(added));
    return id;
}

void Document::erase(const std::vector<EntityId>& ids)
{
    commit({}, ids);
}

bool Document::undo()
{
    checkpoint();   // eventuali modifiche ai layer non ancora registrate
    if (undo_.empty())
        return false;
    Step step = std::move(undo_.back());
    undo_.pop_back();
    apply(step, false);
    redo_.push_back(std::move(step));
    resetCheckpoint();
    return true;
}

bool Document::redo()
{
    checkpoint();
    if (redo_.empty())
        return false;
    Step step = std::move(redo_.back());
    redo_.pop_back();
    apply(step, true);
    undo_.push_back(std::move(step));
    resetCheckpoint();
    return true;
}

namespace {

bool sameLayer(const Layer& a, const Layer& b)
{
    return a.name == b.name && a.color == b.color && a.visible == b.visible && a.frozen == b.frozen && a.locked == b.locked
           && a.linetype == b.linetype && a.lineweight == b.lineweight && a.plot == b.plot && a.trueColor == b.trueColor;
}

std::shared_ptr<BlockDef> cloneBlock(const BlockDef& b)
{
    auto copy = std::make_shared<BlockDef>();
    copy->name = b.name;
    copy->base = b.base;
    copy->dynamicName = b.dynamicName;
    copy->xrefPath = b.xrefPath;
    copy->xrefOverlay = b.xrefOverlay;
    copy->xrefLoaded = b.xrefLoaded;
    copy->xrefFound = b.xrefFound;
    copy->entities.reserve(b.entities.size());
    for (const auto& e : b.entities)
        copy->entities.push_back(e->clone());
    return copy;
}

} // namespace

Document::TableState Document::currentTables() const
{
    return {layers_,         currentLayer_,       blocks_,      textStyles_,     currentTextStyle_, dimStyles_,
            currentDimStyle_, mleaderStyles_, currentMLeaderStyle_, annoScales_, annotationScale_, annoAllVisible_,
            canaloTrays_};
}

void Document::restoreTables(const TableState& state)
{
    layers_ = state.layers;
    if (layers_.empty())
        layers_.push_back(Layer{"0"});
    currentLayer_ = std::clamp(state.currentLayer, 0, static_cast<int>(layers_.size()) - 1);
    blocks_ = state.blocks;
    linkBlocks();
    textStyles_ = state.textStyles;
    currentTextStyle_ = state.currentTextStyle;
    dimStyles_ = state.dimStyles;
    currentDimStyle_ = state.currentDimStyle;
    mleaderStyles_ = state.mleaderStyles;
    currentMLeaderStyle_ = state.currentMLeaderStyle;
    annoScales_ = state.annoScales;
    annotationScale_ = state.annotationScale;
    annoAllVisible_ = state.annoAllVisible;
    canaloTrays_ = state.canaloTrays;
    ++revision_;
}

void Document::saveLayerState(const std::string& name, const std::string& description)
{
    LayerState state{name, description, layers_.at(static_cast<std::size_t>(currentLayer_)).name, layers_};
    for (auto& s : layerStates_)
        if (s.name == name) {
            s = std::move(state);
            ++revision_;
            return;
        }
    layerStates_.push_back(std::move(state));
    ++revision_;
}

int Document::restoreLayerState(const std::string& name)
{
    const auto it = std::find_if(layerStates_.begin(), layerStates_.end(), [&](const LayerState& s) { return s.name == name; });
    if (it == layerStates_.end())
        return -1;
    int updated = 0;
    for (const Layer& saved : it->layers) {
        const int index = findLayer(saved.name);
        if (index < 0)
            continue;
        Layer& l = layers_[static_cast<std::size_t>(index)];
        l.color = saved.color;
        l.trueColor = saved.trueColor;
        l.visible = saved.visible;
        l.frozen = saved.frozen;
        l.locked = saved.locked;
        l.linetype = saved.linetype;
        l.lineweight = saved.lineweight;
        l.plot = saved.plot;
        ++updated;
    }
    if (const int current = findLayer(it->currentLayer); current >= 0)
        currentLayer_ = current;
    ++revision_;
    return updated;
}

bool Document::removeLayerState(const std::string& name)
{
    const auto before = layerStates_.size();
    std::erase_if(layerStates_, [&](const LayerState& s) { return s.name == name; });
    if (layerStates_.size() != before)
        ++revision_;
    return layerStates_.size() != before;
}

void Document::resetCheckpoint()
{
    checkpoint_ = currentTables();
    hasCheckpoint_ = true;
    checkpointUndo_ = undo_.size();
}

void Document::checkpoint()
{
    if (!hasCheckpoint_) {
        resetCheckpoint();
        return;
    }
    bool same = checkpoint_.currentLayer == currentLayer_ && checkpoint_.layers.size() == layers_.size()
                && checkpoint_.blocks == blocks_ && checkpoint_.sameStyles(currentTables());
    for (std::size_t i = 0; same && i < layers_.size(); ++i)
        same = sameLayer(checkpoint_.layers[i], layers_[i]);
    if (same) {
        checkpointUndo_ = undo_.size();
        return;
    }
    if (layers_.size() < checkpoint_.layers.size()) {   // layer eliminati: non si annulla
        resetCheckpoint();
        return;
    }
    auto before = std::make_shared<const TableState>(checkpoint_);
    auto after = std::make_shared<const TableState>(currentTables());
    if (undo_.size() > checkpointUndo_ && !undo_.back().tablesBefore) {
        undo_.back().tablesBefore = std::move(before);   // stesso comando: un solo Annulla
        undo_.back().tablesAfter = std::move(after);
    } else {
        Step step;
        step.tablesBefore = std::move(before);
        step.tablesAfter = std::move(after);
        undo_.push_back(std::move(step));
    }
    redo_.clear();
    resetCheckpoint();
}

std::shared_ptr<BlockDef> Document::editableBlock(std::string_view name)
{
    for (auto& b : blocks_) {
        if (b->name != name)
            continue;
        if (hasCheckpoint_ && std::find(checkpoint_.blocks.begin(), checkpoint_.blocks.end(), b) != checkpoint_.blocks.end()) {
            b = cloneBlock(*b);
            linkBlocks();
            ++revision_;
        }
        return b;
    }
    return nullptr;
}

EntityId Document::insertLoaded(std::unique_ptr<Entity> entity)
{
    if (!entity)
        return 0;
    entity->setId(nextId_++);
    if (entity->layer() < 0)
        entity->setLayer(0);
    if (entity->color() < 0)
        entity->setColor(kColorByLayer);
    if (entity->space() < 0)
        entity->setSpace(0);
    const EntityId id = entity->id();
    entities_.push_back(std::move(entity));
    ++revision_;
    return id;
}

void Document::setOrder(const std::vector<EntityId>& order)
{
    std::unordered_map<EntityId, std::size_t> rank;
    rank.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        rank[order[i]] = i;
    std::stable_sort(entities_.begin(), entities_.end(), [&rank](const std::unique_ptr<Entity>& a, const std::unique_ptr<Entity>& b) {
        const auto ra = rank.find(a->id()), rb = rank.find(b->id());
        return (ra == rank.end() ? SIZE_MAX : ra->second) < (rb == rank.end() ? SIZE_MAX : rb->second);
    });
    ++revision_;
    ++entityRevision_;   // solo l'ordine degli oggetti
}

void Document::reorder(const std::vector<EntityId>& ids, DrawOrder where, EntityId reference)
{
    if (ids.empty())
        return;
    std::vector<EntityId> before;
    before.reserve(entities_.size());
    for (const auto& e : entities_)
        before.push_back(e->id());
    const std::unordered_set<EntityId> moving(ids.begin(), ids.end());
    std::vector<EntityId> rest, moved;
    for (EntityId id : before)
        (moving.count(id) ? moved : rest).push_back(id);
    if (moved.empty())
        return;
    std::vector<EntityId> after;
    after.reserve(before.size());
    if (where == DrawOrder::Back) {
        after = moved;
        after.insert(after.end(), rest.begin(), rest.end());
    } else if (where == DrawOrder::Front || reference == 0 || moving.count(reference)) {
        after = rest;
        after.insert(after.end(), moved.begin(), moved.end());
    } else {
        auto it = std::find(rest.begin(), rest.end(), reference);
        if (it == rest.end())
            return;
        if (where == DrawOrder::Above)
            ++it;
        after.assign(rest.begin(), it);
        after.insert(after.end(), moved.begin(), moved.end());
        after.insert(after.end(), it, rest.end());
    }
    if (after == before)
        return;
    Step step;
    step.orderBefore = std::move(before);
    step.orderAfter = std::move(after);
    setOrder(step.orderAfter);
    undo_.push_back(std::move(step));
    redo_.clear();
}

void Document::sortLoaded(const std::function<std::optional<std::uint64_t>(const Entity&)>& key)
{
    std::vector<std::pair<std::uint64_t, std::size_t>> keys;
    keys.reserve(entities_.size());
    std::uint64_t last = 0;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (const auto k = key(*entities_[i]))
            last = *k;
        keys.emplace_back(last, i);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::unique_ptr<Entity>> sorted;
    sorted.reserve(entities_.size());
    for (const auto& [k, i] : keys)
        sorted.push_back(std::move(entities_[i]));
    entities_ = std::move(sorted);
    ++revision_;
}

void Document::apply(const Step& step, bool forward)
{
    // Avanti: prima le tabelle (es. il layer nuovo), poi gli oggetti; indietro al contrario.
    if (forward && step.tablesAfter)
        restoreTables(*step.tablesAfter);
    applyEntities(step, forward);
    if (!forward && step.tablesBefore)
        restoreTables(*step.tablesBefore);
    // Gli oggetti rimessi possono puntare a definizioni di blocco vecchie: si ricollegano.
    if (!step.added.empty() || !step.removed.empty() || step.tablesAfter)
        linkBlocks();
}

void Document::applyEntities(const Step& step, bool forward)
{
    if (!step.orderAfter.empty()) {   // DRAWORDER
        setOrder(forward ? step.orderAfter : step.orderBefore);
        return;
    }
    const auto& toRemove = forward ? step.removed : step.added;
    const auto& toInsert = forward ? step.added : step.removed;

    // Le entità che vengono rimosse e reinserite con lo stesso id (modifiche) restano al loro posto
    // nell'ordine di disegno; le altre vengono rimosse o aggiunte in fondo.
    std::unordered_set<EntityId> removeIds;
    for (const auto& e : toRemove)
        removeIds.insert(e->id());

    // Annullando una cancellazione gli oggetti tornano al loro posto nell'ordine di disegno (prima finivano in fondo:
    // un oggetto coperto poteva finire sopra gli altri).
    std::vector<std::pair<std::size_t, const Entity*>> positioned;
    std::vector<const Entity*> appended;
    std::unordered_map<EntityId, std::size_t> position;
    for (std::size_t i = 0; i < entities_.size(); ++i)
        position.emplace(entities_[i]->id(), i);
    for (std::size_t i = 0; i < toInsert.size(); ++i) {
        const auto& e = toInsert[i];
        if (removeIds.erase(e->id())) {
            if (const auto it = position.find(e->id()); it != position.end()) {
                entities_[it->second] = e->clone();   // modifica: resta al suo posto
                continue;
            }
        }
        if (!forward && i < step.removedAt.size())
            positioned.emplace_back(step.removedAt[i], e.get());
        else
            appended.push_back(e.get());
    }
    if (!removeIds.empty())
        std::erase_if(entities_, [&](const std::unique_ptr<Entity>& x) { return removeIds.count(x->id()) > 0; });
    std::sort(positioned.begin(), positioned.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [at, e] : positioned)
        entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(std::min(at, entities_.size())), e->clone());
    for (const Entity* e : appended)
        entities_.push_back(e->clone());

    ++revision_;
    ++entityRevision_;   // solo oggetti
}

void Document::removeById(EntityId id)
{
    std::erase_if(entities_, [id](const std::unique_ptr<Entity>& e) { return e->id() == id; });
}

} // namespace cartesio

namespace cartesio {

const std::vector<Linetype>& standardLinetypes()
{
    // Tipi di linea di CartesioCAD, in millimetri tondi su un modulo di 3 mm (tratto, spazio, punto = 0). I nomi
    // sono quelli d'uso comune, così i disegni che li citano li ritrovano; i file portano le loro definizioni.
    static const std::vector<Linetype> types = {
        {"BORDER", "Bordo __ __ . __ __ .", {12, -3, 12, -3, 0, -3}},
        {"CENTER", "Asse ____ _ ____ _", {24, -3, 6, -3}},
        {"DASHDOT", "Tratto punto __ . __ .", {12, -3, 0, -3}},
        {"DASHED", "Tratteggiata __ __ __", {6, -3}},
        {"DIVIDE", "Tratto due punti __ . . __", {12, -3, 0, -3, 0, -3}},
        {"DOT", "Punteggiata . . . .", {0, -3}},
        {"HIDDEN", "Nascosta _ _ _ _", {3, -1.5}},
        {"PHANTOM", "Fantasma ____ _ _ ____", {24, -3, 6, -3, 6, -3}},
    };
    return types;
}

namespace {
bool sameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}
} // namespace

const TextStyle* Document::findTextStyle(std::string_view name) const
{
    for (const TextStyle& s : textStyles_)
        if (sameName(s.name, name))
            return &s;
    return nullptr;
}

const TextStyle& Document::textStyleOrStandard(std::string_view name) const
{
    if (const TextStyle* s = findTextStyle(name))
        return *s;
    if (const TextStyle* s = findTextStyle("Standard"))
        return *s;
    return textStyles_.front();
}

void Document::setTextStyle(TextStyle style)
{
    for (TextStyle& s : textStyles_)
        if (sameName(s.name, style.name)) {
            s = std::move(style);
            ++revision_;
            return;
        }
    textStyles_.push_back(std::move(style));
    ++revision_;
}

const Linetype* Document::findLinetype(std::string_view name) const
{
    for (const Linetype& lt : linetypes_)
        if (sameName(lt.name, name))
            return &lt;
    return nullptr;
}

void Document::addLinetype(Linetype linetype)
{
    for (Linetype& lt : linetypes_)
        if (sameName(lt.name, linetype.name)) {
            lt = std::move(linetype);
            ++revision_;
            return;
        }
    linetypes_.push_back(std::move(linetype));
    ++revision_;
}

bool Document::loadStandardLinetype(std::string_view name)
{
    if (findLinetype(name))
        return true;
    for (const Linetype& lt : standardLinetypes())
        if (sameName(lt.name, name)) {
            addLinetype(lt);
            return true;
        }
    return false;
}

const Linetype* Document::effectiveLinetype(const Entity& e, const std::string& blockLinetype) const
{
    std::string_view name = e.linetype();
    if (name.empty() || sameName(name, "ByLayer")) {
        const int l = e.layer();
        if (l < 0 || l >= static_cast<int>(layers_.size()))
            return nullptr;
        name = layers_[static_cast<std::size_t>(l)].linetype;
    } else if (sameName(name, "ByBlock")) {
        name = blockLinetype;
    }
    if (name.empty() || sameName(name, "Continuous") || sameName(name, "ByLayer") || sameName(name, "ByBlock"))
        return nullptr;
    const Linetype* lt = findLinetype(name);
    return lt && !lt->pattern.empty() ? lt : nullptr;
}

int Document::effectiveLineweight(const Entity& e, int blockLineweight) const
{
    int lw = e.lineweight();
    if (lw == kLineweightByLayer || lw == kLineweightUnset) {
        const int l = e.layer();
        lw = l >= 0 && l < static_cast<int>(layers_.size()) ? layers_[static_cast<std::size_t>(l)].lineweight
                                                             : kLineweightDefault;
    } else if (lw == kLineweightByBlock) {
        lw = blockLineweight;
    }
    return lw < 0 ? kDefaultLineweight : lw;
}

} // namespace cartesio

namespace cartesio {

const DimStyle* Document::findDimStyle(std::string_view name) const
{
    for (const DimStyle& s : dimStyles_)
        if (sameName(s.name, name))
            return &s;
    return nullptr;
}

void Document::setDimStyle(DimStyle style)
{
    for (DimStyle& s : dimStyles_)
        if (sameName(s.name, style.name)) {
            s = std::move(style);
            ++revision_;
            return;
        }
    dimStyles_.push_back(std::move(style));
    ++revision_;
}

const DimStyle& Document::currentDimStyle() const
{
    if (const DimStyle* s = findDimStyle(currentDimStyle_))
        return *s;
    return dimStyles_.front();
}

} // namespace cartesio

namespace cartesio {

const Document::MLeaderStyle* Document::findMLeaderStyle(std::string_view name) const
{
    for (const MLeaderStyle& s : mleaderStyles_)
        if (sameName(s.name, name))
            return &s;
    return nullptr;
}

void Document::setMLeaderStyle(MLeaderStyle style)
{
    for (MLeaderStyle& s : mleaderStyles_)
        if (sameName(s.name, style.name)) {
            s = std::move(style);
            ++revision_;
            return;
        }
    mleaderStyles_.push_back(std::move(style));
    ++revision_;
}

bool Document::removeMLeaderStyle(const std::string& name, std::string* error)
{
    const auto it = std::find_if(mleaderStyles_.begin(), mleaderStyles_.end(),
                                 [&](const MLeaderStyle& s) { return sameName(s.name, name); });
    if (it == mleaderStyles_.end())
        return false;
    if (sameName(name, currentMLeaderStyle_)) {
        if (error)
            *error = "Lo stile corrente non si può eliminare.";
        return false;
    }
    for (const auto& e : entities_)
        if (e->type() == EntityType::Leader && sameName(static_cast<const LeaderEntity&>(*e).style, name)) {
            if (error)
                *error = "Lo stile è usato da direttrici del disegno.";
            return false;
        }
    mleaderStyles_.erase(it);
    ++revision_;
    return true;
}

int Document::restyleLeaders(const std::string& name)
{
    const MLeaderStyle* style = findMLeaderStyle(name);
    if (!style)
        return 0;
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> ids;
    for (const auto& e : entities_) {
        if (e->type() != EntityType::Leader || !sameName(static_cast<const LeaderEntity&>(*e).style, name))
            continue;
        auto copy = std::unique_ptr<LeaderEntity>(static_cast<LeaderEntity*>(e->clone().release()));
        // Annotative: le misure dello stile sono sulla carta, nel modello valgono per il fattore dell'oggetto.
        const double k = copy->annotative() ? copy->annoFactor() : 1.0;
        copy->textHeight = style->textHeight * k;
        copy->arrowSize = style->arrowSize * k;
        copy->textStyle = style->textStyle;
        copy->landingGap = style->landingGap > 0.0 ? style->landingGap * k : style->landingGap;
        ids.push_back(e->id());
        modified.push_back(std::move(copy));
    }
    if (!ids.empty())
        commit(std::move(modified), ids);
    return static_cast<int>(ids.size());
}

namespace {

// Stile di testo usato dall'oggetto (nullptr se l'oggetto non ne ha).
std::string* textStyleOf(Entity& e)
{
    switch (e.type()) {
    case EntityType::Text: return &static_cast<TextEntity&>(e).style;
    case EntityType::MText: return &static_cast<MTextEntity&>(e).style;
    case EntityType::AttDef: return &static_cast<AttDefEntity&>(e).text.style;
    case EntityType::Leader: return &static_cast<LeaderEntity&>(e).textStyle;
    default: return nullptr;
    }
}

} // namespace

bool Document::textStyleUsed(const std::string& name) const
{
    const auto uses = [&](const Entity& e) {
        if (std::string* s = textStyleOf(const_cast<Entity&>(e)); s && sameName(*s, name))
            return true;
        if (e.type() == EntityType::Insert)
            for (const Attribute& a : static_cast<const InsertEntity&>(e).attributes)
                if (sameName(a.text.style, name))
                    return true;
        return false;
    };
    for (const auto& e : entities_)
        if (uses(*e))
            return true;
    for (const auto& b : blocks_)
        for (const auto& e : b->entities)
            if (uses(*e))
                return true;
    for (const MLeaderStyle& s : mleaderStyles_)
        if (sameName(s.textStyle, name))
            return true;
    return false;
}

bool Document::removeTextStyle(const std::string& name, std::string* error)
{
    const auto it = std::find_if(textStyles_.begin(), textStyles_.end(), [&](const TextStyle& s) { return sameName(s.name, name); });
    if (it == textStyles_.end())
        return false;
    const auto fail = [error](const char* why) {
        if (error)
            *error = why;
        return false;
    };
    if (sameName(name, "Standard"))
        return fail("Lo stile Standard non si può eliminare.");
    if (sameName(name, currentTextStyle_))
        return fail("Lo stile corrente non si può eliminare.");
    if (textStyleUsed(name))
        return fail("Lo stile è usato da testi del disegno.");
    textStyles_.erase(it);
    ++revision_;
    return true;
}

bool Document::renameTextStyle(const std::string& from, const std::string& to, std::string* error)
{
    TextStyle* style = nullptr;
    for (TextStyle& s : textStyles_)
        if (sameName(s.name, from))
            style = &s;
    const auto fail = [error](const char* why) {
        if (error)
            *error = why;
        return false;
    };
    if (!style)
        return fail("Stile di testo non trovato.");
    if (sameName(from, "Standard"))
        return fail("Lo stile Standard non si rinomina.");
    if (to.empty() || (findTextStyle(to) && !sameName(from, to)))
        return fail("Nome vuoto o già usato.");
    // Passo annullabile: le tabelle di prima (stili, blocchi) restano nel punto di controllo; gli oggetti cambiati
    // sono copie nel passo; i blocchi che usano lo stile si sostituiscono con copie (quelli di prima restano
    // nello stato salvato per Annulla).
    checkpoint();
    style->name = to;
    // Gli oggetti che lo usano passano al nome nuovo (nel disegno e nei blocchi).
    const auto uses = [&](const Entity& e) {
        if (std::string* s = textStyleOf(const_cast<Entity&>(e)); s && sameName(*s, from))
            return true;
        if (e.type() == EntityType::Insert)
            for (const Attribute& a : static_cast<const InsertEntity&>(e).attributes)
                if (sameName(a.text.style, from))
                    return true;
        return false;
    };
    const auto rename = [&](Entity& e) {
        if (std::string* s = textStyleOf(e); s && sameName(*s, from))
            *s = to;
        if (e.type() == EntityType::Insert)
            for (Attribute& a : static_cast<InsertEntity&>(e).attributes)
                if (sameName(a.text.style, from))
                    a.text.style = to;
    };
    for (auto& b : blocks_) {
        if (std::none_of(b->entities.begin(), b->entities.end(), [&](const auto& e) { return uses(*e); }))
            continue;
        auto copy = std::make_shared<BlockDef>();
        copy->name = b->name;
        copy->base = b->base;
        copy->dynamicName = b->dynamicName;
        copy->xrefPath = b->xrefPath;
        copy->xrefOverlay = b->xrefOverlay;
        copy->xrefLoaded = b->xrefLoaded;
        copy->xrefFound = b->xrefFound;
        for (const auto& e : b->entities) {
            auto c = e->clone();
            c->setId(e->id());
            rename(*c);
            copy->entities.push_back(std::move(c));
        }
        b = std::move(copy);
    }
    linkBlocks();
    for (MLeaderStyle& s : mleaderStyles_)
        if (sameName(s.textStyle, from))
            s.textStyle = to;
    if (sameName(currentTextStyle_, from))
        currentTextStyle_ = to;
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> ids;
    for (const auto& e : entities_)
        if (uses(*e)) {
            auto copy = e->clone();
            rename(*copy);
            modified.push_back(std::move(copy));
            ids.push_back(e->id());
        }
    if (!ids.empty())
        commit(std::move(modified), ids);
    ++revision_;
    checkpoint();   // stili e blocchi nello stesso passo degli oggetti
    return true;
}

Document::AuditReport Document::audit(bool fix)
{
    AuditReport report;
    const auto note = [&](const std::string& text) {
        ++report.found;
        if (fix)
            ++report.fixed;
        report.lines.push_back((fix ? "Corretto: " : "Errore: ") + text);
    };
    const auto finite = [](Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); };
    const auto label = [](const Entity& e) { return std::string(entityTypeName(e.type())) + " (id " + std::to_string(e.id()) + ")"; };

    std::vector<EntityId> erase;
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> modifiedIds;
    std::set<EntityId> seen;
    const int layerCount = static_cast<int>(layers_.size());
    for (const auto& ptr : entities_) {
        const Entity& e = *ptr;
        // Id ripetuti: l'oggetto doppio diventa un oggetto nuovo.
        if (!seen.insert(e.id()).second) {
            note(label(e) + ": identificativo ripetuto");
            erase.push_back(e.id());
            auto copy = e.clone();
            copy->setId(0);
            modified.push_back(std::move(copy));
            continue;
        }
        // Geometria non valida: si elimina.
        std::string bad;
        const BBox b = e.bounds();
        if (b.isValid() && (!finite(b.min) || !finite(b.max)))
            bad = "coordinate non valide";
        switch (e.type()) {
        case EntityType::Circle: {
            const double r = static_cast<const CircleEntity&>(e).radius;
            if (!(r > 0.0) || !std::isfinite(r))
                bad = "raggio non valido";
            break;
        }
        case EntityType::Arc: {
            const double r = static_cast<const ArcEntity&>(e).radius;
            if (!(r > 0.0) || !std::isfinite(r))
                bad = "raggio non valido";
            break;
        }
        case EntityType::Polyline:
            if (static_cast<const PolylineEntity&>(e).vertices.size() < 2)
                bad = "polilinea con meno di 2 vertici";
            break;
        case EntityType::Spline: {
            const auto& sp = static_cast<const SplineEntity&>(e);
            if (sp.control.size() < 2 && sp.fit.size() < 2)
                bad = "spline senza punti";
            break;
        }
        case EntityType::Hatch:
            if (static_cast<const HatchEntity&>(e).loops.empty())
                bad = "tratteggio senza contorno";
            break;
        case EntityType::Insert: {
            const auto& in = static_cast<const InsertEntity&>(e);
            if (!in.block && !findBlock(in.blockName))
                bad = "blocco \"" + in.blockName + "\" inesistente";
            break;
        }
        default:
            break;
        }
        if (!bad.empty()) {
            note(label(e) + ": " + bad + (fix ? ", eliminato" : ""));
            erase.push_back(e.id());
            continue;
        }
        // Proprietà da correggere: layer, tipo di linea, stile e altezza dei testi.
        std::unique_ptr<Entity> copy;
        const auto edit = [&]() -> Entity& {
            if (!copy)
                copy = e.clone();
            return *copy;
        };
        if (e.layer() < 0 || e.layer() >= layerCount) {
            note(label(e) + ": layer inesistente, passa al layer 0");
            edit().setLayer(0);
        }
        if (!e.linetype().empty() && !findLinetype(e.linetype()) && !sameName(e.linetype(), "ByLayer")
            && !sameName(e.linetype(), "ByBlock") && !sameName(e.linetype(), "Continuous")) {
            note(label(e) + ": tipo di linea \"" + e.linetype() + "\" inesistente, passa a DaLayer");
            edit().setLinetype("ByLayer");
        }
        if (e.type() == EntityType::Text || e.type() == EntityType::MText) {
            const bool text = e.type() == EntityType::Text;
            const std::string& style = text ? static_cast<const TextEntity&>(e).style : static_cast<const MTextEntity&>(e).style;
            const double height = text ? static_cast<const TextEntity&>(e).height : static_cast<const MTextEntity&>(e).height;
            if (!style.empty() && !findTextStyle(style)) {
                note(label(e) + ": stile di testo \"" + style + "\" inesistente, passa a Standard");
                *textStyleOf(edit()) = "Standard";
            }
            if (!(height > 0.0) || !std::isfinite(height)) {
                note(label(e) + ": altezza del testo non valida, diventa 2.5");
                if (text)
                    static_cast<TextEntity&>(edit()).height = 2.5;
                else
                    static_cast<MTextEntity&>(edit()).height = 2.5;
            }
        }
        if (copy) {
            modifiedIds.push_back(e.id());
            modified.push_back(std::move(copy));
        }
    }
    // Tabelle: layer con tipi di linea inesistenti, correnti non validi, gruppi con oggetti spariti.
    for (Layer& l : layers_)
        if (!l.linetype.empty() && !findLinetype(l.linetype) && !sameName(l.linetype, "Continuous")) {
            note("layer \"" + l.name + "\": tipo di linea \"" + l.linetype + "\" inesistente, passa a Continuous");
            if (fix)
                l.linetype = "Continuous";
        }
    if (currentLayer_ < 0 || currentLayer_ >= layerCount) {
        note("layer corrente inesistente, diventa 0");
        if (fix)
            currentLayer_ = 0;
    }
    if (!findTextStyle(currentTextStyle_)) {
        note("stile di testo corrente \"" + currentTextStyle_ + "\" inesistente, diventa Standard");
        if (fix)
            currentTextStyle_ = "Standard";
    }
    if (!currentMLeaderStyle_.empty() && !findMLeaderStyle(currentMLeaderStyle_)) {
        note("stile di direttrice corrente \"" + currentMLeaderStyle_ + "\" inesistente");
        if (fix)
            currentMLeaderStyle_.clear();
    }
    std::set<EntityId> existing;
    for (const auto& e : entities_)
        existing.insert(e->id());
    for (auto it = groups_.begin(); it != groups_.end();) {
        const std::size_t before = it->ids.size();
        std::vector<EntityId> kept;
        for (EntityId id : it->ids)
            if (existing.count(id) && std::find(erase.begin(), erase.end(), id) == erase.end())
                kept.push_back(id);
        if (kept.size() != before) {
            note("gruppo \"" + it->name + "\": " + std::to_string(before - kept.size()) + " oggetti inesistenti tolti");
            if (fix)
                it->ids = kept;
        }
        if (fix && it->ids.empty()) {
            it = groups_.erase(it);
            continue;
        }
        ++it;
    }
    if (fix) {
        std::vector<EntityId> removed = erase;
        removed.insert(removed.end(), modifiedIds.begin(), modifiedIds.end());
        if (!removed.empty() || !modified.empty())
            commit(std::move(modified), removed);
        else if (report.found > 0)
            ++revision_;   // corretti solo layer, correnti o gruppi
    }
    return report;
}

int Document::restyleDimensions(const std::string& name)
{
    const DimStyle* style = findDimStyle(name);
    if (!style)
        return 0;
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> ids;
    for (const auto& e : entities_) {
        if (e->type() != EntityType::Dimension)
            continue;
        const auto& dim = static_cast<const DimensionEntity&>(*e);
        if (!sameName(dim.style.name, name))
            continue;
        auto copy = std::unique_ptr<DimensionEntity>(static_cast<DimensionEntity*>(e->clone().release()));
        copy->style = *style;
        if (copy->annotative())
            copy->style.scale = copy->annoFactor();   // DIMSCALE = scala di annotazione dell'oggetto
        // La geometria si ricalcola dallo stile (il blocco anonimo letto dal file non vale più).
        copy->block.reset();
        copy->blockName.clear();
        ids.push_back(e->id());
        modified.push_back(std::move(copy));
    }
    const int count = static_cast<int>(modified.size());
    if (count > 0)
        commit(std::move(modified), ids);
    return count;
}

} // namespace cartesio

namespace cartesio {

namespace {
// Tutti gli oggetti del disegno: spazi, blocchi e cronologia (anche quelli annullati vanno rispettati).
template <typename F>
void forEveryEntity(const std::vector<std::unique_ptr<Entity>>& entities,
                    const std::vector<std::shared_ptr<BlockDef>>& blocks, F&& f)
{
    for (const auto& e : entities)
        f(*e);
    for (const auto& b : blocks)
        for (const auto& e : b->entities)
            f(*e);
}
} // namespace

std::vector<std::string> Document::purgeBlocks()
{
    std::vector<std::string> removed;
    // Si ripete finché si trova qualcosa: un blocco può essere usato solo da un altro blocco inutilizzato.
    for (bool again = true; again;) {
        again = false;
        std::set<std::string> used;
        const auto collect = [&used](const Entity& e) {
            if (e.type() == EntityType::Insert)
                used.insert(static_cast<const InsertEntity&>(e).blockName);
            else if (e.type() == EntityType::Dimension)
                used.insert(static_cast<const DimensionEntity&>(e).blockName);
        };
        forEveryEntity(entities_, blocks_, collect);
        for (const auto* history : {&undo_, &redo_})
            for (const Step& s : *history) {
                for (const auto& e : s.added)
                    collect(*e);
                for (const auto& e : s.removed)
                    collect(*e);
            }
        for (auto it = blocks_.begin(); it != blocks_.end();) {
            const std::string& name = (*it)->name;
            if (!name.empty() && name.front() != '*' && !used.count(name)) {
                removed.push_back(name);
                it = blocks_.erase(it);
                again = true;
            } else {
                ++it;
            }
        }
    }
    if (!removed.empty())
        ++revision_;
    return removed;
}

std::vector<std::string> Document::purgeLayers()
{
    std::vector<std::string> removed;
    for (int i = static_cast<int>(layers_.size()) - 1; i > 0; --i) {
        const std::string name = layers_[static_cast<std::size_t>(i)].name;
        if (name == "Defpoints")
            continue;
        if (removeLayer(i))   // rifiuta da sé i layer usati, il corrente e lo 0
            removed.push_back(name);
    }
    return removed;
}

std::vector<std::string> Document::purgeLinetypes()
{
    std::set<std::string> used;
    const auto upper = [](std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    for (const Layer& l : layers_)
        used.insert(upper(l.linetype));
    used.insert(upper(currentLinetype_));
    forEveryEntity(entities_, blocks_, [&](const Entity& e) { used.insert(upper(e.linetype())); });
    std::vector<std::string> removed;
    for (auto it = linetypes_.begin(); it != linetypes_.end();) {
        if (!sameName(it->name, "Continuous") && !used.count(upper(it->name))) {
            removed.push_back(it->name);
            it = linetypes_.erase(it);
        } else {
            ++it;
        }
    }
    if (!removed.empty())
        ++revision_;
    return removed;
}

std::vector<std::string> Document::purgeDimStyles()
{
    std::set<std::string> used{currentDimStyle_};
    forEveryEntity(entities_, blocks_, [&](const Entity& e) {
        if (e.type() == EntityType::Dimension)
            used.insert(static_cast<const DimensionEntity&>(e).style.name);
    });
    std::vector<std::string> removed;
    for (auto it = dimStyles_.begin(); it != dimStyles_.end();) {
        if (it->name != "Standard" && !used.count(it->name)) {
            removed.push_back(it->name);
            it = dimStyles_.erase(it);
        } else {
            ++it;
        }
    }
    if (!removed.empty())
        ++revision_;
    return removed;
}

} // namespace cartesio

namespace cartesio {

std::string Document::addGroup(std::vector<EntityId> ids, std::string name, std::string description)
{
    if (name.empty()) {
        int n = 1;
        const auto taken = [this](const std::string& s) {
            return std::any_of(groups_.begin(), groups_.end(), [&s](const Group& g) { return g.name == s; });
        };
        while (taken("*A" + std::to_string(n)))
            ++n;
        name = "*A" + std::to_string(n);
    }
    groups_.push_back({name, std::move(description), std::move(ids), true});
    ++revision_;
    return name;
}

int Document::removeGroupsOf(const std::vector<EntityId>& ids)
{
    const auto before = groups_.size();
    std::erase_if(groups_, [&ids](const Group& g) {
        return std::any_of(g.ids.begin(), g.ids.end(),
                           [&ids](EntityId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); });
    });
    const int removed = static_cast<int>(before - groups_.size());
    if (removed > 0)
        ++revision_;
    return removed;
}

void Document::expandGroups(std::vector<EntityId>& ids) const
{
    if (!groupSelection || groups_.empty())
        return;
    const std::vector<EntityId> start = ids;
    for (const Group& g : groups_) {
        if (!g.selectable)
            continue;
        const bool hit = std::any_of(g.ids.begin(), g.ids.end(),
                                     [&start](EntityId id) { return std::find(start.begin(), start.end(), id) != start.end(); });
        if (!hit)
            continue;
        for (EntityId id : g.ids)
            if (find(id) && std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
    }
}

// ---------------------------------------------------------------------- stampa

namespace {

bool sameNameNoCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size()
           && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                  return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
              });
}

} // namespace

PlotSettings layoutPlotSettings(const Layout& layout)
{
    PlotSettings s = layout.plot;
    s.setFlag(kPlotModelType, false);
    // Il supporto ha le misure del foglio del layout, nell'orientamento della rotazione.
    if (s.rotation % 2 != 0) {
        s.paperWidth = layout.paperHeight;
        s.paperHeight = layout.paperWidth;
    } else {
        s.paperWidth = layout.paperWidth;
        s.paperHeight = layout.paperHeight;
    }
    const double plotMargin = std::max({s.marginLeft, s.marginBottom, s.marginRight, s.marginTop});
    if (std::abs(plotMargin - layout.margin) > 1e-9)
        s.marginLeft = s.marginBottom = s.marginRight = s.marginTop = layout.margin;
    return s;
}

void applyPlotSettings(Layout& layout, const PlotSettings& settings)
{
    layout.plot = settings;
    layout.plot.setFlag(kPlotModelType, false);
    const SheetSize sheet = sheetSize(settings);
    layout.paperWidth = sheet.width;
    layout.paperHeight = sheet.height;
    layout.margin = std::max({settings.marginLeft, settings.marginBottom, settings.marginRight, settings.marginTop});
    const MediaFormat* media = findMediaBySize(sheet.width, sheet.height);
    if (media && std::string_view(media->name).starts_with("ISO "))
        layout.paperName = std::string(media->name).substr(4);   // "A3", come nei formati del cartiglio
    else
        layout.paperName = mediaDisplayName(sheet.width, sheet.height);
}

const PlotSettings* Document::findPageSetup(std::string_view name) const
{
    for (const auto& s : pageSetups_)
        if (sameNameNoCase(s.pageSetupName, name))
            return &s;
    return nullptr;
}

void Document::setPageSetup(const PlotSettings& setup)
{
    ++revision_;
    for (auto& s : pageSetups_)
        if (sameNameNoCase(s.pageSetupName, setup.pageSetupName)) {
            s = setup;
            return;
        }
    pageSetups_.push_back(setup);
}

bool Document::removePageSetup(std::string_view name)
{
    const auto before = pageSetups_.size();
    std::erase_if(pageSetups_, [name](const PlotSettings& s) { return sameNameNoCase(s.pageSetupName, name); });
    if (pageSetups_.size() == before)
        return false;
    ++revision_;
    return true;
}

} // namespace cartesio

// ================================================================ scale di annotazione

namespace cartesio {

const AnnoScale* Document::findAnnoScale(std::string_view name) const
{
    for (const AnnoScale& s : annoScales_)
        if (sameName(s.name, name))
            return &s;
    return nullptr;
}

const AnnoScale* Document::annoScaleForFactor(double factor) const
{
    for (const AnnoScale& s : annoScales_)
        if (sameFactor(s.factor(), factor))
            return &s;
    return nullptr;
}

void Document::setAnnoScales(std::vector<AnnoScale> scales)
{
    if (scales.empty())
        scales.push_back({"1:1", 1.0, 1.0});
    annoScales_ = std::move(scales);
    if (!findAnnoScale(annotationScale_))
        annotationScale_ = annoScales_.front().name;
    ++revision_;
}

double Document::annotationFactor() const
{
    const AnnoScale* s = findAnnoScale(annotationScale_);
    return s ? s->factor() : 1.0;
}

bool Document::supportsFactor(const Entity& e, double factor) const
{
    for (const std::string& name : e.annoScales())
        if (const AnnoScale* s = findAnnoScale(name); s && sameFactor(s->factor(), factor))
            return true;
    return false;
}

void Document::setAnnoAllVisible(bool on)
{
    if (annoAllVisible_ == on)
        return;
    annoAllVisible_ = on;
    ++revision_;
}

bool Document::setAnnotationScale(const std::string& name)
{
    const AnnoScale* scale = findAnnoScale(name);
    if (!scale)
        return false;
    checkpoint();   // modifiche alle tabelle precedenti: passo a sé
    const double factor = scale->factor();
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> ids;
    for (const auto& e : entities_) {
        if (!e->annotative() || e->space() != 0 || sameFactor(e->annoFactor(), factor) || !supportsFactor(*e, factor))
            continue;
        modified.push_back(annotationAt(*e, factor));
        ids.push_back(e->id());
    }
    if (!ids.empty())
        commit(std::move(modified), ids);
    annotationScale_ = scale->name;
    ++revision_;
    checkpoint();   // la scala corrente nello stesso passo degli oggetti ridimensionati
    return true;
}

void Document::prepareAnnotation(Entity& e) const
{
    if (!canBeAnnotative(e.type()) || e.annotative())
        return;
    bool annotative = false;
    switch (e.type()) {
    case EntityType::Text:
        annotative = textStyleOrStandard(static_cast<const TextEntity&>(e).style).annotative;
        break;
    case EntityType::MText:
        annotative = textStyleOrStandard(static_cast<const MTextEntity&>(e).style).annotative;
        break;
    case EntityType::Dimension: {
        auto& d = static_cast<DimensionEntity&>(e);
        annotative = d.style.annotative;
        if (annotative)
            d.style.scale = 1.0;   // DIMSCALE non conta: la scala è quella di annotazione
        break;
    }
    case EntityType::Leader: {
        const auto& l = static_cast<const LeaderEntity&>(e);
        const MLeaderStyle* s = l.style.empty() ? nullptr : findMLeaderStyle(l.style);
        annotative = s && s->annotative;
        break;
    }
    default:
        break;
    }
    if (!annotative)
        return;
    const AnnoScale* scale = findAnnoScale(annotationScale_);
    const double factor = scale ? scale->factor() : 1.0;
    rescaleAnnotation(e, factor);
    e.setAnnoScales({scale ? scale->name : std::string("1:1")});
    e.setAnnoFactor(factor);
}

void Document::markAnnotative(Entity& e) const
{
    if (!canBeAnnotative(e.type()))
        return;
    const AnnoScale* scale = findAnnoScale(annotationScale_);
    e.setAnnoScales({scale ? scale->name : std::string("1:1")});
    e.setAnnoFactor(scale ? scale->factor() : 1.0);
}

DimStyle Document::dimStyleForNew() const
{
    DimStyle s = currentDimStyle();
    if (s.annotative)
        s.scale = annotationFactor();
    return s;
}

int Document::addObjectScale(const std::vector<EntityId>& ids, const std::string& name)
{
    const AnnoScale* scale = findAnnoScale(name);
    if (!scale)
        return 0;
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> changed;
    for (EntityId id : ids) {
        const Entity* e = find(id);
        if (!e || !e->annotative())
            continue;
        bool present = false;
        for (const std::string& s : e->annoScales())
            present = present || sameName(s, scale->name);
        if (present)
            continue;
        auto copy = e->clone();
        auto scales = copy->annoScales();
        scales.push_back(scale->name);
        copy->setAnnoScales(std::move(scales));
        // Nel modello, se è la scala corrente, l'oggetto passa subito a quella.
        if (copy->space() == 0 && sameName(scale->name, annotationScale_))
            copy = annotationAt(*copy, scale->factor());
        modified.push_back(std::move(copy));
        changed.push_back(id);
    }
    if (!changed.empty())
        commit(std::move(modified), changed);
    return static_cast<int>(changed.size());
}

int Document::removeObjectScale(const std::vector<EntityId>& ids, const std::string& name)
{
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> changed;
    for (EntityId id : ids) {
        const Entity* e = find(id);
        if (!e || !e->annotative())
            continue;
        std::vector<std::string> scales;
        for (const std::string& s : e->annoScales())
            if (!sameName(s, name))
                scales.push_back(s);
        if (scales.size() == e->annoScales().size() || scales.empty())
            continue;   // non c'era, o era l'ultima (un oggetto annotativo ne ha sempre almeno una)
        auto copy = e->clone();
        copy->setAnnoScales(std::move(scales));
        modified.push_back(std::move(copy));
        changed.push_back(id);
    }
    if (!changed.empty())
        commit(std::move(modified), changed);
    return static_cast<int>(changed.size());
}

int Document::setAnnotative(const std::vector<EntityId>& ids, bool on)
{
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> changed;
    const AnnoScale* scale = findAnnoScale(annotationScale_);
    for (EntityId id : ids) {
        const Entity* e = find(id);
        if (!e || !canBeAnnotative(e->type()) || e->annotative() == on)
            continue;
        auto copy = e->clone();
        if (on) {
            // L'oggetto resta com'è: vale per la scala corrente (le sue misure sulla carta sono quelle / scala).
            copy->setAnnoScales({scale ? scale->name : std::string("1:1")});
            copy->setAnnoFactor(scale ? scale->factor() : 1.0);
        } else {
            copy->setAnnoScales({});
            copy->setAnnoFactor(1.0);
        }
        modified.push_back(std::move(copy));
        changed.push_back(id);
    }
    if (!changed.empty())
        commit(std::move(modified), changed);
    return static_cast<int>(changed.size());
}

void Document::setLoadedAnnotationScale(const std::string& name, bool allVisible)
{
    if (const AnnoScale* s = findAnnoScale(name))
        annotationScale_ = s->name;
    annoAllVisible_ = allVisible;
    ++revision_;
}

void Document::normalizeAnnotations()
{
    const double factor = annotationFactor();
    for (auto& e : entities_) {
        if (!e->annotative() || e->space() != 0 || sameFactor(e->annoFactor(), factor) || !supportsFactor(*e, factor))
            continue;
        e = annotationAt(*e, factor);   // stesso id: l'indice id -> posizione resta valido
        ++revision_;
    }
}

} // namespace cartesio

// ================================================================ viste con nome

namespace cartesio {

const Document::NamedView* Document::findNamedView(std::string_view name) const
{
    for (const NamedView& v : namedViews_)
        if (sameName(v.name, name))
            return &v;
    return nullptr;
}

void Document::setNamedView(NamedView view)
{
    for (NamedView& v : namedViews_)
        if (sameName(v.name, view.name)) {
            v = std::move(view);
            ++revision_;
            return;
        }
    namedViews_.push_back(std::move(view));
    ++revision_;
}

bool Document::removeNamedView(const std::string& name)
{
    const auto before = namedViews_.size();
    std::erase_if(namedViews_, [&](const NamedView& v) { return sameName(v.name, name); });
    if (namedViews_.size() == before)
        return false;
    ++revision_;
    return true;
}

} // namespace cartesio
