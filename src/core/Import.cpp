#include "core/Import.h"

#include <algorithm>
#include <cmath>

#include <cctype>

namespace cartesio {

namespace {
bool special(const std::string& lt)
{
    std::string u;
    for (char c : lt)
        u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u.empty() || u == "BYLAYER" || u == "BYBLOCK" || u == "CONTINUOUS";
}
} // namespace

ImportSource ImportSource::from(const Document& doc)
{
    ImportSource s;
    s.layers = doc.layers();
    s.linetypes = doc.linetypes();
    s.dimStyles = doc.dimStyles();
    s.blocks = doc.blocks();
    return s;
}

std::shared_ptr<const BlockDef> ImportSource::findBlock(const std::string& name) const
{
    for (const auto& b : blocks)
        if (b->name == name)
            return b;
    return nullptr;
}

std::string Importer::named(const std::string& name) const
{
    if (prefix_.empty() || name == "0")
        return name;
    return prefix_ + name;
}

int Importer::layer(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(src_.layers.size()))
        return sourceIndex < 0 ? sourceIndex : 0;
    if (const auto it = layers_.find(sourceIndex); it != layers_.end())
        return it->second;
    Layer l = src_.layers[static_cast<std::size_t>(sourceIndex)];
    l.name = named(l.name);
    int index = doc_.findLayer(l.name);
    if (index < 0) {
        index = doc_.addLayer(l);
        linetype(l.linetype);
    }
    layers_[sourceIndex] = index;
    return index;
}

void Importer::linetype(const std::string& name)
{
    if (special(name) || doc_.findLinetype(name))
        return;
    for (const Linetype& lt : src_.linetypes)
        if (lt.name == name) {
            doc_.addLinetype(lt);
            return;
        }
    doc_.loadStandardLinetype(name);
}

std::shared_ptr<const BlockDef> Importer::block(const std::string& name)
{
    const std::string target = named(name);
    if (auto existing = doc_.findBlock(target))
        return existing;
    const auto source = src_.findBlock(name);
    if (!source || importing_.count(name))
        return nullptr;
    importing_.insert(name);
    auto def = std::make_shared<BlockDef>();
    def->name = target;
    def->base = source->base;
    def->dynamicName = source->dynamicName.empty() ? std::string() : named(source->dynamicName);
    for (const auto& child : source->entities) {
        auto c = child->clone();
        fix(*c);
        def->entities.push_back(std::move(c));
    }
    importing_.erase(name);
    doc_.addBlock(def);
    return doc_.findBlock(target);
}

void Importer::fix(Entity& e)
{
    e.setLayer(layer(e.layer()));
    linetype(e.linetype());
    switch (e.type()) {
    case EntityType::Insert: {
        auto& in = static_cast<InsertEntity&>(e);
        in.block = block(in.blockName);
        in.blockName = named(in.blockName);
        for (Attribute& a : in.attributes)
            a.text.setLayer(layer(a.text.layer()));
        break;
    }
    case EntityType::Dimension: {
        auto& d = static_cast<DimensionEntity&>(e);
        if (!doc_.findDimStyle(d.style.name))
            doc_.setDimStyle(d.style);
        break;
    }
    case EntityType::Viewport:
        for (int& l : static_cast<ViewportEntity&>(e).frozenLayers)
            l = layer(l);
        break;
    default:
        break;
    }
}

std::unique_ptr<Entity> Importer::copy(const Entity& e)
{
    auto c = e.clone();
    c->setId(0);
    fix(*c);
    return c;
}

void importDrawingAsBlock(Document& target, const Document& source, const std::string& name, double scale)
{
    const Affine scaling = Affine::scaling({0.0, 0.0}, scale);
    const ImportSource src = ImportSource::from(source);
    Importer importer(target, src);
    auto def = std::make_shared<BlockDef>();
    def->name = name;
    for (const auto& e : source.entities())
        if (e->space() <= 0)   // solo lo spazio modello
        {
            auto copy = importer.copy(*e);
            if (scale != 1.0)
                copy->transform(scaling);
            def->entities.push_back(std::move(copy));
        }
    target.addBlock(def);   // ridefinisce un blocco con lo stesso nome e ricollega gli inserimenti
}

bool bindXref(Document& doc, const std::string& name, bool insert, std::string* error)
{
    const auto fail = [error](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    std::shared_ptr<BlockDef> xref = doc.findBlock(name);
    if (!xref || !xref->isXref())
        return fail("\"" + name + "\" non è un riferimento esterno.");
    if (!xref->xrefLoaded)
        return fail("Il riferimento \"" + name + "\" non è caricato: prima va trovato il file.");
    xref = doc.editableBlock(name);   // copie: Associa si annulla
    const std::string prefix = name + "|";
    const auto renamed = [&](const std::string& old) {
        const std::string rest = old.substr(prefix.size());
        if (insert)
            return rest;
        // Associa: primo nome libero NOME$n$x.
        for (int n = 0;; ++n) {
            const std::string candidate = name + "$" + std::to_string(n) + "$" + rest;
            if (doc.findLayer(candidate) < 0 && !doc.findBlock(candidate))
                return candidate;
        }
    };

    // Layer: rinominati, o uniti a quello del disegno con lo stesso nome.
    std::map<int, int> layerMap;
    for (int i = 0; i < static_cast<int>(doc.layers().size()); ++i) {
        const std::string old = doc.layers()[static_cast<std::size_t>(i)].name;
        if (old.rfind(prefix, 0) != 0)
            continue;
        const std::string target = renamed(old);
        const int existing = doc.findLayer(target);
        if (existing >= 0 && existing != i)
            layerMap[i] = existing;
        else
            doc.renameLayer(i, target);
    }

    // Blocchi del riferimento (lui e quelli annidati "NOME|x").
    std::vector<std::shared_ptr<BlockDef>> mine{xref};
    std::map<std::string, std::string> blockMap;
    std::vector<std::string> nested;
    for (const auto& b : doc.blocks())
        if (b->name.rfind(prefix, 0) == 0)
            nested.push_back(b->name);
    for (const std::string& n : nested)
        mine.push_back(doc.editableBlock(n));
    for (std::size_t k = 1; k < mine.size(); ++k) {
        const std::string target = renamed(mine[k]->name);
        blockMap[mine[k]->name] = target;
    }
    std::vector<std::string> dropped;
    for (std::size_t k = 1; k < mine.size(); ++k) {
        const std::string target = blockMap[mine[k]->name];
        if (doc.findBlock(target))
            dropped.push_back(mine[k]->name);   // Inserisci: vale il blocco del disegno con quel nome
        else
            mine[k]->name = target;
    }
    for (const auto& b : mine)
        for (auto& e : b->entities) {
            if (const auto it = layerMap.find(e->layer()); it != layerMap.end())
                e->setLayer(it->second);
            if (e->type() == EntityType::Insert) {
                auto& ins = static_cast<InsertEntity&>(*e);
                if (const auto it = blockMap.find(ins.blockName); it != blockMap.end())
                    ins.blockName = it->second;
            }
        }
    for (const std::string& n : dropped)
        doc.eraseBlock(n);
    // Vuoti ormai; dal più alto, perché eliminare un layer sposta gli indici dei successivi. Se non si può
    // (es. è nella cronologia) resta.
    for (auto it = layerMap.rbegin(); it != layerMap.rend(); ++it)
        doc.removeLayer(it->first);

    xref->xrefPath.clear();
    xref->xrefFound.clear();
    xref->xrefLoaded = false;
    xref->xrefOverlay = false;
    doc.touch();
    return true;
}

bool copyBlockForEditing(const Document& host, const std::string& name, Document& out, std::string* error)
{
    const std::shared_ptr<BlockDef> def = host.findBlock(name);
    if (!def) {
        if (error)
            *error = "Il blocco \"" + name + "\" non esiste.";
        return false;
    }
    if (def->isXref()) {
        if (error)
            *error = "\"" + name + "\" è un riferimento esterno: si modifica aprendo il suo file.";
        return false;
    }
    for (const Layer& l : host.layers())
        out.addLayer(l);
    out.setInsUnits(host.insUnits());
    out.setLtscale(host.ltscale());
    const ImportSource src = ImportSource::from(host);
    Importer importer(out, src);
    std::vector<std::unique_ptr<Entity>> added;
    for (const auto& e : def->entities)
        added.push_back(importer.copy(*e));
    out.commit(std::move(added));
    return true;
}

int saveEditedBlock(Document& host, const std::string& name, const Document& edited, std::string* error)
{
    const auto fail = [error](const std::string& message) {
        if (error)
            *error = message;
        return -1;
    };
    const std::shared_ptr<BlockDef> old = host.findBlock(name);
    if (!old)
        return fail("Il blocco \"" + name + "\" non c'è più nel disegno.");
    for (const auto& e : edited.entities())
        if (e->type() == EntityType::Insert && static_cast<const InsertEntity&>(*e).blockName == name)
            return fail("Un blocco non può contenere sé stesso: togliere gli inserimenti di \"" + name + "\".");
    auto def = std::make_shared<BlockDef>();
    def->name = old->name;
    def->base = old->base;
    def->dynamicName = old->dynamicName;
    const ImportSource src = ImportSource::from(edited);
    Importer importer(host, src);
    for (const auto& e : edited.entities())
        if (e->space() <= 0)
            def->entities.push_back(importer.copy(*e));
    host.addBlock(def);   // ridefinisce e ricollega gli inserimenti
    host.touch();
    int inserts = 0;
    for (const auto& e : host.entities())
        if (e->type() == EntityType::Insert && static_cast<const InsertEntity&>(*e).blockName == name)
            ++inserts;
    return inserts;
}

bool importBlock(Document& target, const Document& source, const std::string& name, double scale)
{
    if (!source.findBlock(name))
        return false;
    if (target.findBlock(name))
        return true;
    const ImportSource src = ImportSource::from(source);
    Importer importer(target, src);
    const InsertEntity probe(name, Vec2{});
    importer.copy(probe);   // copiando un inserimento il blocco (e gli annidati) passa nel disegno
    const auto def = target.findBlock(name);
    if (def && scale > 0.0 && std::abs(scale - 1.0) > 1e-12) {
        // Gli annidati seguono attraverso la scala dei loro inserimenti.
        const Affine scaling = Affine::scaling({0.0, 0.0}, scale);
        for (auto& e : def->entities)
            e->transform(scaling);
        def->base = def->base * scale;
        def->invalidateBounds();
        target.touch();
    }
    return def != nullptr;
}

std::vector<std::string> userBlockNames(const Document& doc)
{
    std::vector<std::string> names;
    for (const auto& b : doc.blocks()) {
        if (b->name.empty() || b->name[0] == '*' || b->isXref() || b->name.find('|') != std::string::npos)
            continue;
        names.push_back(b->name);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        std::string x = a, y = b;
        for (char& c : x)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : y)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x < y;
    });
    return names;
}

void loadXref(Document& target, const BlockDef& xref, const Document& source, double scale)
{
    const Affine scaling = Affine::scaling({0.0, 0.0}, scale);
    const ImportSource src = ImportSource::from(source);
    Importer importer(target, src);
    importer.setPrefix(xref.name + "|");
    auto def = std::make_shared<BlockDef>();
    def->name = xref.name;
    def->base = xref.base;
    def->xrefPath = xref.xrefPath;
    def->xrefOverlay = xref.xrefOverlay;
    def->xrefFound = xref.xrefFound;
    def->xrefLoaded = true;
    for (const auto& e : source.entities())
        if (e->space() <= 0) {
            auto copy = importer.copy(*e);
            if (scale != 1.0)
                copy->transform(scaling);
            def->entities.push_back(std::move(copy));
        }
    target.addBlock(def);   // sostituisce il blocco vuoto e ricollega gli inserimenti
}

} // namespace cartesio

namespace cartesio {

DwgMergeReport mergeAlternativeConversion(Document& base, const Document& alt)
{
    DwgMergeReport report;
    const ImportSource source = ImportSource::from(alt);
    Importer importer(base, source);

    // Blocchi: si tiene la versione con più oggetti.
    for (const auto& altBlock : alt.blocks()) {
        const auto mine = base.findBlock(altBlock->name);
        if (mine && mine->entities.size() >= altBlock->entities.size())
            continue;
        auto def = std::make_shared<BlockDef>();
        def->name = altBlock->name;
        def->base = altBlock->base;
        def->dynamicName = mine && !mine->dynamicName.empty() ? mine->dynamicName : altBlock->dynamicName;
        for (const auto& e : altBlock->entities)
            def->entities.push_back(importer.copy(*e));
        base.addBlock(def);   // sostituisce quello con lo stesso nome e ricollega gli inserimenti
        ++report.blocks;
    }

    // Layout vuoti in `base` ma con oggetti in `alt` (stesso nome): se ne copia il contenuto.
    const auto count = [](const Document& d, int space) {
        std::size_t n = 0;
        for (const auto& e : d.entities())
            if (e->space() == space)
                ++n;
        return n;
    };
    for (const Layout& altLayout : alt.layouts()) {
        const std::size_t altCount = count(alt, altLayout.id);
        if (altCount == 0)
            continue;
        const Layout* mine = nullptr;
        for (const Layout& l : base.layouts())
            if (l.name == altLayout.name)
                mine = &l;
        if (!mine || count(base, mine->id) > 0)
            continue;
        const int target = mine->id;
        for (const auto& e : alt.entities())
            if (e->space() == altLayout.id) {
                auto copy = importer.copy(*e);
                copy->setSpace(target);
                base.insertLoaded(std::move(copy));
                ++report.entities;
            }
        ++report.layouts;
    }
    if (report.blocks || report.layouts)
        base.linkBlocks();
    return report;
}

} // namespace cartesio
