#include "app/Thumbnails.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstring>

namespace cartesio {

namespace {

quint32 le32(const QByteArray& data, qsizetype at)
{
    if (at < 0 || at + 4 > data.size())
        return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint16 le16(const QByteArray& data, qsizetype at)
{
    if (at < 0 || at + 2 > data.size())
        return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

void appendLe32(QByteArray& out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xff));
}

// Bitmap "DIB" (senza l'intestazione del file .bmp), come la salvano DWG e DXF.
QImage imageFromDib(const QByteArray& dib)
{
    if (dib.size() < 40)
        return {};
    const quint32 headerSize = le32(dib, 0);
    const quint16 bitCount = le16(dib, 14);
    const quint32 compression = le32(dib, 16);
    const quint32 colorsUsed = le32(dib, 32);
    if (headerSize < 40 || headerSize > 256)
        return {};
    quint32 colors = colorsUsed != 0 ? colorsUsed : (bitCount <= 8 ? (1u << bitCount) : 0u);
    quint32 offset = 14 + headerSize + colors * 4;
    if (compression == 3 && headerSize == 40)
        offset += 12;   // maschere dei colori (BI_BITFIELDS)
    QByteArray bmp("BM");
    appendLe32(bmp, quint32(14 + dib.size()));
    appendLe32(bmp, 0);
    appendLe32(bmp, offset);
    bmp += dib;
    QImage image;
    image.loadFromData(bmp, "BMP");
    return image;
}

// DWG e DWT: all'indirizzo scritto a 0x0D c'è il blocco delle immagini (sentinella, poi l'elenco).
QImage dwgPreview(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray head = file.read(0x20);
    if (head.size() < 0x11 || !head.startsWith("AC10"))
        return {};
    const quint32 address = le32(head, 0x0D);
    if (address == 0 || address >= quint64(file.size()) || !file.seek(address))
        return {};
    static const unsigned char kSentinel[16] = {0x1F, 0x25, 0x6D, 0x07, 0xD4, 0x36, 0x28, 0x28,
                                                0x9D, 0x57, 0xCA, 0x3F, 0x9D, 0x44, 0x10, 0x2B};
    const QByteArray block = file.read(16 + 4 + 1 + 9 * 8);
    if (block.size() < 21 || std::memcmp(block.constData(), kSentinel, 16) != 0)
        return {};
    const int count = static_cast<unsigned char>(block.at(20));
    qint64 bmpStart = 0, bmpSize = 0, pngStart = 0, pngSize = 0;
    for (int i = 0; i < count && 21 + i * 9 + 9 <= block.size(); ++i) {
        const qsizetype at = 21 + i * 9;
        const int code = static_cast<unsigned char>(block.at(at));
        const quint32 start = le32(block, at + 1);
        const quint32 size = le32(block, at + 5);
        if (code == 2) {
            bmpStart = start;
            bmpSize = size;
        } else if (code == 6) {
            pngStart = start;
            pngSize = size;
        }
    }
    constexpr qint64 kMaxImage = 8 * 1024 * 1024;
    if (pngSize > 0 && pngSize < kMaxImage && file.seek(pngStart)) {
        QImage image;
        if (image.loadFromData(file.read(pngSize), "PNG"))
            return image;
    }
    if (bmpSize > 0 && bmpSize < kMaxImage && file.seek(bmpStart))
        return imageFromDib(file.read(bmpSize));
    return {};
}

// DXF: sezione THUMBNAILIMAGE (90 = byte, 310 = righe esadecimali), di solito in fondo al file.
QImage dxfPreview(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    constexpr qint64 kTail = 2 * 1024 * 1024;
    if (file.size() > kTail && !file.seek(file.size() - kTail))
        return {};
    const QByteArray tail = file.readAll();
    const qsizetype at = tail.lastIndexOf("THUMBNAILIMAGE");
    if (at < 0)
        return {};
    const QList<QByteArray> lines = tail.mid(at).split('\n');
    QByteArray hex;
    for (qsizetype i = 1; i + 1 < lines.size(); i += 2) {
        const QByteArray code = lines.at(i).trimmed();
        const QByteArray value = lines.at(i + 1).trimmed();
        if (code == "0")
            break;
        if (code == "310")
            hex += value;
    }
    return imageFromDib(QByteArray::fromHex(hex));
}

QString cacheFolder()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/CartesioCAD");
    return base + QStringLiteral("/Miniature");
}

QString cacheFile(const QString& path)
{
    const QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
    return cacheFolder() + QLatin1Char('/')
           + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex())
           + QStringLiteral(".png");
}

} // namespace

QImage embeddedPreview(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("dwg") || suffix == QLatin1String("dwt"))
        return dwgPreview(path);
    if (suffix == QLatin1String("dxf"))
        return dxfPreview(path);
    return {};
}

void storeThumbnail(const QString& path, const QImage& image)
{
    if (image.isNull())
        return;
    QDir().mkpath(cacheFolder());
    QImage scaled = image;
    if (image.width() > kThumbnailWidth || image.height() > kThumbnailHeight)
        scaled = image.scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QString target = cacheFile(path);
    const QString temp = target + QStringLiteral(".tmp");
    if (scaled.save(temp, "PNG")) {
        QFile::remove(target);
        QFile::rename(temp, target);
    }
}

QString thumbnailFile(const QString& path)
{
    const QString cache = cacheFile(path);
    const QFileInfo cached(cache);
    const QFileInfo info(path);
    if (!info.exists())
        return cached.exists() ? cache : QString();
    if (cached.exists() && cached.lastModified() >= info.lastModified())
        return cache;
    const QImage image = embeddedPreview(path);
    if (!image.isNull()) {
        storeThumbnail(path, image);
        if (QFileInfo::exists(cache))
            return cache;
    }
    return cached.exists() ? cache : QString();
}

} // namespace cartesio
