#include "app/ExternalRefs.h"

#include "app/ImageLibrary.h"
#include "core/Document.h"

#include <QDir>
#include <QFileInfo>

#include <map>
#include <optional>

namespace cartesio {

namespace {

// Immagini nel disegno e nei blocchi (non negli XREF: quelle sono del disegno esterno).
void collectImages(const std::vector<std::unique_ptr<Entity>>& entities, std::map<std::string, int>& uses)
{
    for (const auto& e : entities)
        if (e->type() == EntityType::Image && !static_cast<const ImageEntity&>(*e).isOle())
            ++uses[static_cast<const ImageEntity&>(*e).path];
}

QString fileName(const std::string& path)
{
    return QFileInfo(QDir::fromNativeSeparators(QString::fromStdString(path))).fileName();
}

} // namespace

std::vector<ExternalRef> externalReferences(const Document& doc)
{
    std::vector<ExternalRef> out;
    std::map<std::string, int> inserts;
    for (const auto& e : doc.entities())
        if (e->type() == EntityType::Insert)
            ++inserts[static_cast<const InsertEntity&>(*e).blockName];
    for (const auto& b : doc.blocks())
        for (const auto& e : b->entities)
            if (e->type() == EntityType::Insert)
                ++inserts[static_cast<const InsertEntity&>(*e).blockName];

    std::map<std::string, int> images;
    collectImages(doc.entities(), images);
    for (const auto& b : doc.blocks()) {
        if (b->isXref())
            continue;
        collectImages(b->entities, images);
    }

    for (const auto& b : doc.blocks()) {
        if (!b->isXref())
            continue;
        ExternalRef r;
        r.kind = ExternalRef::Drawing;
        r.name = QString::fromStdString(b->name);
        r.saved = b->xrefPath;
        r.found = b->xrefLoaded ? QString::fromStdString(b->xrefFound) : QString();
        r.loaded = b->xrefLoaded;
        r.overlay = b->xrefOverlay;
        r.uses = inserts[b->name];
        out.push_back(std::move(r));
    }
    for (const auto& [path, count] : images) {
        ExternalRef r;
        r.kind = ExternalRef::Image;
        r.name = fileName(path);
        if (r.name.isEmpty())
            r.name = QObject::tr("(senza nome)");
        r.saved = path;
        r.found = ImageLibrary::instance().find(doc, path);
        r.loaded = !r.found.isEmpty() && !ImageLibrary::instance().image(r.found).isNull();
        r.uses = count;
        out.push_back(std::move(r));
    }
    return out;
}

QString externalRefStatus(const ExternalRef& r)
{
    if (r.saved.empty())
        return QObject::tr("Percorso mancante nel disegno");
    if (r.found.isEmpty())
        return QObject::tr("Non trovato");
    if (!r.loaded)
        return r.kind == ExternalRef::Image ? QObject::tr("Formato non leggibile") : QObject::tr("Non caricato");
    if (r.uses == 0)
        return QObject::tr("Caricato, non usato");
    return QObject::tr("Caricato");
}

void setExternalRefPath(Document& doc, const ExternalRef& r, const std::string& path)
{
    if (r.kind == ExternalRef::Drawing) {
        if (auto b = doc.editableBlock(r.name.toStdString())) {
            b->xrefPath = path;
            b->xrefLoaded = false;
            b->xrefFound.clear();
            b->entities.clear();
            doc.touch();
        }
        return;
    }
    // Immagini nel disegno: un passo annullabile; immagini nei blocchi: direttamente.
    std::vector<std::unique_ptr<Entity>> changed;
    std::vector<EntityId> ids;
    for (const auto& e : doc.entities())
        if (e->type() == EntityType::Image && static_cast<const ImageEntity&>(*e).path == r.saved) {
            auto copy = e->clone();
            static_cast<ImageEntity&>(*copy).path = path;
            ids.push_back(e->id());
            changed.push_back(std::move(copy));
        }
    for (const auto& b : doc.blocks())
        for (auto& e : b->entities)
            if (e->type() == EntityType::Image && static_cast<ImageEntity&>(*e).path == r.saved)
                static_cast<ImageEntity&>(*e).path = path;
    if (!changed.empty())
        doc.commit(std::move(changed), ids);
    else
        doc.touch();
    ImageLibrary::instance().reset();
}

std::string refPath(const QString& file, const QString& drawingFile, int type)
{
    const QFileInfo info(file);
    if (type == RefPathNone)
        return info.fileName().toStdString();
    const QString absolute = QDir::toNativeSeparators(info.absoluteFilePath());
    if (type == RefPathFull || drawingFile.isEmpty())
        return absolute.toStdString();
    const QDir folder = QFileInfo(drawingFile).absoluteDir();
    QString relative = folder.relativeFilePath(info.absoluteFilePath());
    if (QFileInfo(relative).isAbsolute() || relative.contains(QLatin1Char(':')))   // altra unità
        return absolute.toStdString();
    if (!relative.startsWith(QLatin1String("..")))
        relative = QStringLiteral("./") + relative;
    return QDir::toNativeSeparators(relative).toStdString();
}

int refPathTypeOf(const std::string& saved)
{
    const QString s = QDir::fromNativeSeparators(QString::fromStdString(saved));
    if (s.isEmpty() || !s.contains(QLatin1Char('/')))
        return RefPathNone;
    if (QFileInfo(s).isAbsolute() || s.startsWith(QLatin1String("//")))
        return RefPathFull;
    return RefPathRelative;
}

QString refPathTypeName(int type)
{
    switch (type) {
    case RefPathRelative: return QObject::tr("Relativo al disegno");
    case RefPathFull: return QObject::tr("Completo (assoluto)");
    default: return QObject::tr("Nessun percorso (solo il nome)");
    }
}

bool setExternalRefPathType(Document& doc, const ExternalRef& r, int type)
{
    if (r.found.isEmpty())
        return false;
    const std::string path = refPath(r.found, QString::fromStdString(doc.filePath()), type);
    if (path == r.saved)
        return true;
    if (r.kind == ExternalRef::Drawing) {
        // Lo stesso file: il contenuto caricato resta valido, cambia solo il percorso salvato.
        if (auto b = doc.editableBlock(r.name.toStdString())) {
            b->xrefPath = path;
            doc.touch();
        }
        return true;
    }
    setExternalRefPath(doc, r, path);
    return true;
}

int rebaseRelativeRefs(Document& doc, const QString& oldDrawingFile, const QString& newDrawingFile)
{
    if (oldDrawingFile.isEmpty() || newDrawingFile.isEmpty()
        || QFileInfo(oldDrawingFile).absolutePath() == QFileInfo(newDrawingFile).absolutePath())
        return 0;
    const QDir oldFolder = QFileInfo(oldDrawingFile).absoluteDir();
    int changed = 0;
    const auto rebased = [&](const std::string& saved) -> std::optional<std::string> {
        if (refPathTypeOf(saved) != RefPathRelative)
            return std::nullopt;
        const QString absolute =
            QDir::cleanPath(oldFolder.filePath(QDir::fromNativeSeparators(QString::fromStdString(saved))));
        const std::string path = refPath(absolute, newDrawingFile, RefPathRelative);
        return path == saved ? std::nullopt : std::optional<std::string>(path);
    };
    for (const auto& b : doc.blocks())
        if (b->isXref())
            if (const auto p = rebased(b->xrefPath)) {
                b->xrefPath = *p;
                ++changed;
            }
    for (const auto& e : doc.entities())
        if (e->type() == EntityType::Image) {
            // Modifica diretta: il salvataggio non è un passo da annullare.
            auto& im = const_cast<ImageEntity&>(static_cast<const ImageEntity&>(*e));
            if (const auto p = rebased(im.path)) {
                im.path = *p;
                ++changed;
            }
        }
    for (const auto& b : doc.blocks())
        if (!b->isXref())
            for (auto& e : b->entities)
                if (e->type() == EntityType::Image) {
                    auto& im = static_cast<ImageEntity&>(*e);
                    if (const auto p = rebased(im.path)) {
                        im.path = *p;
                        ++changed;
                    }
                }
    if (changed)
        ImageLibrary::instance().reset();
    return changed;
}

int detachExternalRef(Document& doc, const ExternalRef& r)
{
    std::vector<EntityId> ids;
    const std::string block = r.name.toStdString();
    for (const auto& e : doc.entities()) {
        if (r.kind == ExternalRef::Drawing && e->type() == EntityType::Insert
            && static_cast<const InsertEntity&>(*e).blockName == block)
            ids.push_back(e->id());
        if (r.kind == ExternalRef::Image && e->type() == EntityType::Image
            && static_cast<const ImageEntity&>(*e).path == r.saved)
            ids.push_back(e->id());
    }
    if (!ids.empty())
        doc.erase(ids);
    if (r.kind == ExternalRef::Drawing)
        doc.eraseBlock(block);
    doc.touch();
    return static_cast<int>(ids.size());
}

} // namespace cartesio
