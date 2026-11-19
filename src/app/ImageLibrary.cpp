#include "app/ImageLibrary.h"

#include "app/AppOptions.h"
#include "app/OleImage.h"
#include "core/Document.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

namespace cartesio {

ImageLibrary& ImageLibrary::instance()
{
    static ImageLibrary library;
    return library;
}

void ImageLibrary::reset()
{
    found_.clear();
    images_.clear();
    ole_.clear();
}

QString ImageLibrary::source(const Document& doc, const ImageEntity& image)
{
    if (image.isOle()) {
        const QString key = QStringLiteral("ole:%1").arg(reinterpret_cast<quintptr>(image.ole.get()), 0, 16);
        ole_.emplace(key, image.ole);
        return key;
    }
    return find(doc, image.path);
}

QString ImageLibrary::find(const Document& doc, const std::string& path)
{
    if (path.empty())
        return {};
    const QString drawing = QString::fromStdString(doc.filePath());
    const QString key = drawing + QLatin1Char('|') + QString::fromStdString(path);
    if (const auto it = found_.find(key); it != found_.end())
        return it->second;
    const QString result = locate(drawing, path);
    found_[key] = result;
    return result;
}

QString ImageLibrary::locate(const QString& drawingFile, const std::string& path)
{
    if (path.empty())
        return {};
    const QString folder = drawingFile.isEmpty() ? QString() : QFileInfo(drawingFile).absolutePath();
    const QString saved = QDir::fromNativeSeparators(QString::fromStdString(path));

    QStringList candidates;
    const QFileInfo info(saved);
    if (info.isAbsolute())
        candidates << saved;
    else if (!folder.isEmpty())
        candidates << QDir(folder).filePath(saved);
    const QString name = info.fileName();
    if (!folder.isEmpty()) {
        candidates << QDir(folder).filePath(name);
        const QDir dir(folder);
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            candidates << dir.filePath(sub + QLatin1Char('/') + name);
    }
    for (const QString& extra : appOptions().fontFolders.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        candidates << QDir(extra.trimmed()).filePath(name);

    QString result;
    for (const QString& c : candidates)
        if (QFileInfo(c).isFile()) {
            result = QFileInfo(c).absoluteFilePath();
            break;
        }
    return result;
}

QImage ImageLibrary::image(const QString& file)
{
    if (file.isEmpty())
        return {};
    if (const auto it = images_.find(file); it != images_.end())
        return it->second;
    if (file.startsWith(QLatin1String("ole:"))) {   // oggetto OLE incorporato
        const auto data = ole_.find(file);
        QImage image = data != ole_.end() && data->second ? decodeOleImage(*data->second) : QImage();
        if (!image.isNull())
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        images_[file] = image;
        return image;
    }
    QImageReader reader(file);
    reader.setAutoTransform(true);
    // Le immagini enormi (scansioni, ortofoto) si riducono: a schermo bastano 4096 pixel per lato.
    const QSize size = reader.size();
    if (size.isValid() && std::max(size.width(), size.height()) > 4096)
        reader.setScaledSize(size.scaled(4096, 4096, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (!image.isNull())
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    images_[file] = image;
    return image;
}

} // namespace cartesio
