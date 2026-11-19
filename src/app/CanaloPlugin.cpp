#include "app/CanaloPlugin.h"

#include "app/AppOptions.h"
#include "core/Document.h"
#include "core/Import.h"
#include "core/io/Dxf.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <filesystem>

namespace cartesio {

QString canaloHtmlPath()
{
    // 1. il percorso scelto nelle Opzioni; 2. la copia distribuita con CartesioCAD;
    // 3. il repository di CanaloCAD in Documenti (anche se Documenti è reindirizzata su OneDrive).
    const QString configured = appOptions().canaloPath;
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/CanaloCAD/Canalo.html"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Canalo/Canalo.html"),
        QDir::homePath() + QStringLiteral("/Documents/Canalo/Canalo.html"),
    };
    for (const QString& path : candidates)
        if (QFileInfo::exists(path))
            return path;
    return {};
}

QString canaloExchangeFolder()
{
    const QString configured = appOptions().canaloFolder;
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

bool isCanaloDrawing(const QString& path)
{
    if (!path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    // I layer stanno nella sezione TABLES, all'inizio del file.
    QByteArray head = file.read(400000);
    head.replace("\r\n", "\n");
    return head.contains("\nCanalina\n") && head.contains("\nCavi\n");
}

QString latestCanaloDrawing(const QString& folder, const QDateTime& after)
{
    const QDir dir(folder);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.dxf")}, QDir::Files, QDir::Time);
    for (const QFileInfo& f : files) {   // dal più recente
        if (after.isValid() && f.lastModified() <= after)
            break;
        if (isCanaloDrawing(f.absoluteFilePath()))
            return f.absoluteFilePath();
    }
    return {};
}

canalo::Drawn drawnForCanalo(const Document& doc, const std::vector<EntityId>& ids)
{
    canalo::Drawn out;
    const int units = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
    const double k = unitsFactor(units, 4);
    auto mm = [k](Vec2 p) { return Vec2{p.x * k, p.y * k}; };
    for (EntityId id : ids) {
        const Entity* e = doc.find(id);
        if (!e)
            continue;
        const int li = e->layer();
        const std::string layer = li >= 0 && li < static_cast<int>(doc.layers().size()) ? doc.layers()[static_cast<std::size_t>(li)].name : "0";
        if (const auto* l = dynamic_cast<const LineEntity*>(e))
            out.segments.push_back({mm(l->start), mm(l->end), layer});
        else if (const auto* pl = dynamic_cast<const PolylineEntity*>(e)) {
            // Come la pagina: i tratti tra vertici consecutivi (senza quello di chiusura).
            for (std::size_t i = 0; i + 1 < pl->vertices.size(); ++i)
                out.segments.push_back({mm(pl->vertices[i].point), mm(pl->vertices[i + 1].point), layer});
        } else if (const auto* c = dynamic_cast<const CircleEntity*>(e))
            out.circles.push_back({mm(c->center), c->radius * k, layer});
        else if (const auto* t = dynamic_cast<const TextEntity*>(e))
            out.texts.push_back({t->displayText(), mm(t->position), layer});
        else if (const auto* m = dynamic_cast<const MTextEntity*>(e))
            out.texts.push_back({m->text, mm(m->position), layer});
    }
    return out;
}

bool exportForCanalo(const Document& doc, const std::vector<EntityId>& ids, const QString& path, QString* error)
{
    // CanaloCAD lavora in millimetri: la sezione si converte (es. da un disegno in cm).
    Document out;
    out.setInsUnits(4);
    const int units = doc.insUnits() > 0 ? doc.insUnits() : appOptions().insertTargetUnits;
    const Affine toMm = Affine::scaling({0.0, 0.0}, unitsFactor(units, 4));
    const ImportSource source = ImportSource::from(doc);
    Importer importer(out, source);
    int count = 0;
    for (EntityId id : ids)
        if (const Entity* e = doc.find(id)) {
            auto copy = importer.copy(*e);
            copy->setSpace(0);
            copy->transform(toMm);
            out.insertLoaded(std::move(copy));
            ++count;
        }
    if (count == 0) {
        if (error)
            *error = QObject::tr("Nessun oggetto selezionato: selezionare la sezione (cavi, canalina, testi).");
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    std::string failure;
    if (!writeDxf(out, std::filesystem::path(path.toStdWString()), &failure, nullptr, DxfVersion::R2000)) {
        if (error)
            *error = QString::fromStdString(failure);
        return false;
    }
    return true;
}

} // namespace cartesio
