#include "app/OleImage.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cartesio {

namespace {

std::uint32_t u32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        return 0;
    std::uint32_t v;
    std::memcpy(&v, d.constData() + at, 4);
    return v;
}

std::uint16_t u16(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size())
        return 0;
    std::uint16_t v;
    std::memcpy(&v, d.constData() + at, 2);
    return v;
}

// File compound OLE (MS-CFB): flussi per nome.
class CompoundFile {
public:
    explicit CompoundFile(QByteArray data) : d_(std::move(data))
    {
        if (d_.size() < 512 || u32(d_, 0) != 0xE011CFD0u || u32(d_, 4) != 0xE11AB1A1u)
            return;
        const int shift = u16(d_, 0x1E);
        const int miniShift = u16(d_, 0x20);
        if (shift < 7 || shift > 16 || miniShift < 1 || miniShift > shift)
            return;
        sector_ = 1 << shift;
        mini_ = 1 << miniShift;
        cutoff_ = u32(d_, 0x38);

        // Settori della FAT: 109 nell'intestazione, gli altri nella catena DIFAT.
        std::vector<std::uint32_t> fatSectors;
        for (int i = 0; i < 109; ++i) {
            const std::uint32_t s = u32(d_, 0x4C + i * 4);
            if (s < 0xFFFFFFFA)
                fatSectors.push_back(s);
        }
        std::uint32_t difat = u32(d_, 0x44);
        for (std::uint32_t n = u32(d_, 0x48); n > 0 && difat < 0xFFFFFFFA && fatSectors.size() < 1000000; --n) {
            const qsizetype base = offset(difat);
            const int per = sector_ / 4 - 1;
            for (int i = 0; i < per; ++i) {
                const std::uint32_t s = u32(d_, base + i * 4);
                if (s < 0xFFFFFFFA)
                    fatSectors.push_back(s);
            }
            difat = u32(d_, base + per * 4);
        }
        for (std::uint32_t s : fatSectors) {
            const qsizetype base = offset(s);
            for (int i = 0; i < sector_ / 4; ++i)
                fat_.push_back(u32(d_, base + i * 4));
        }
        // MiniFAT.
        const QByteArray miniFat = chain(u32(d_, 0x3C), -1, fat_, sector_, false);
        for (qsizetype i = 0; i + 4 <= miniFat.size(); i += 4)
            miniFat_.push_back(u32(miniFat, i));
        // Directory.
        const QByteArray dir = chain(u32(d_, 0x30), -1, fat_, sector_, false);
        for (qsizetype at = 0; at + 128 <= dir.size(); at += 128) {
            const int len = u16(dir, at + 64);
            const int type = static_cast<unsigned char>(dir[at + 66]);
            QString name;
            for (int i = 0; i + 2 <= std::min(len, 64) - 2; i += 2)
                name += QChar(u16(dir, at + i));
            Entry e{type, u32(dir, at + 116), u32(dir, at + 120)};
            if (type == 5)
                root_ = e;
            else if (type == 2)
                entries_.emplace(name, e);
        }
        if (root_)
            ministream_ = chain(root_->start, root_->size, fat_, sector_, false);
        ok_ = true;
    }

    bool ok() const { return ok_; }

    std::optional<QByteArray> stream(const QString& name) const
    {
        const auto it = entries_.find(name);
        if (it == entries_.end())
            return std::nullopt;
        const Entry& e = it->second;
        if (e.size < cutoff_)
            return chain(e.start, e.size, miniFat_, mini_, true);
        return chain(e.start, e.size, fat_, sector_, false);
    }

    QStringList names() const
    {
        QStringList out;
        for (const auto& [name, e] : entries_)
            out << name;
        return out;
    }

private:
    struct Entry {
        int type;
        std::uint32_t start;
        std::uint32_t size;
    };

    qsizetype offset(std::uint32_t s) const { return (static_cast<qsizetype>(s) + 1) * sector_; }

    QByteArray chain(std::uint32_t start, qint64 size, const std::vector<std::uint32_t>& table, int unit, bool mini) const
    {
        QByteArray out;
        std::uint32_t s = start;
        std::size_t guard = 0;
        while (s < 0xFFFFFFFA && s < table.size() && guard++ < table.size() + 1) {
            const qsizetype at = mini ? static_cast<qsizetype>(s) * unit : offset(s);
            const QByteArray& source = mini ? ministream_ : d_;
            if (at < 0 || at + unit > source.size())
                break;
            out.append(source.constData() + at, unit);
            if (size >= 0 && out.size() >= size)
                break;
            s = table[s];
        }
        if (size >= 0 && out.size() > size)
            out.truncate(size);
        return out;
    }

    QByteArray d_;
    bool ok_ = false;
    int sector_ = 512;
    int mini_ = 64;
    std::uint32_t cutoff_ = 4096;
    std::vector<std::uint32_t> fat_;
    std::vector<std::uint32_t> miniFat_;
    std::map<QString, Entry> entries_;
    std::optional<Entry> root_;
    QByteArray ministream_;
};

// Un file immagine dentro dei dati (anche preceduto da altro, come nei pacchetti OLE).
QImage findImageFile(const QByteArray& data)
{
    QImage image = QImage::fromData(data);
    if (!image.isNull())
        return image;
    static const QByteArray signatures[] = {QByteArray("\x89PNG\r\n\x1a\n", 8), QByteArray("\xFF\xD8\xFF", 3),
                                            QByteArray("GIF8"), QByteArray("BM")};
    for (const QByteArray& sig : signatures) {
        qsizetype at = data.indexOf(sig);
        int tries = 0;
        while (at >= 0 && tries++ < 8) {
            image = QImage::fromData(data.mid(at));
            if (!image.isNull())
                return image;
            at = data.indexOf(sig, at + 1);
        }
    }
    return {};
}

// DIB (intestazione BITMAPINFOHEADER e pixel) -> file BMP.
QImage fromDib(const QByteArray& dib)
{
    const std::uint32_t header = u32(dib, 0);
    if (header != 40 && header != 108 && header != 124 && header != 12)
        return {};
    const int bits = u16(dib, 14);
    const std::uint32_t colorsUsed = header >= 40 ? u32(dib, 32) : 0;
    const std::uint32_t compression = header >= 40 ? u32(dib, 16) : 0;
    std::uint32_t palette = 0;
    if (bits <= 8)
        palette = (colorsUsed ? colorsUsed : (1u << bits)) * (header == 12 ? 3 : 4);
    else if (compression == 3 && header == 40)   // BI_BITFIELDS: tre maschere
        palette = 12;
    QByteArray bmp("BM");
    const std::uint32_t total = 14 + static_cast<std::uint32_t>(dib.size());
    const std::uint32_t pixels = 14 + header + palette;
    bmp.append(reinterpret_cast<const char*>(&total), 4);
    bmp.append(QByteArray(4, '\0'));
    bmp.append(reinterpret_cast<const char*>(&pixels), 4);
    bmp.append(dib);
    return QImage::fromData(bmp, "BMP");
}

#ifdef Q_OS_WIN
// Metafile di Windows (WMF o EMF) disegnato in un'immagine bianca di circa `longSide` pixel.
QImage renderMetafile(const QByteArray& data, bool enhanced, int himetricW, int himetricH)
{
    HENHMETAFILE meta = nullptr;
    if (enhanced) {
        meta = SetEnhMetaFileBits(static_cast<UINT>(data.size()), reinterpret_cast<const BYTE*>(data.constData()));
    } else {
        METAFILEPICT pict{};
        pict.mm = MM_ANISOTROPIC;
        pict.xExt = himetricW;
        pict.yExt = himetricH;
        meta = SetWinMetaFileBits(static_cast<UINT>(data.size()), reinterpret_cast<const BYTE*>(data.constData()), nullptr,
                                  &pict);
    }
    if (!meta)
        return {};
    // Proporzioni: dall'intestazione del metafile, altrimenti da quelle salvate nell'oggetto.
    ENHMETAHEADER header{};
    GetEnhMetaFileHeader(meta, sizeof(header), &header);
    double w = header.rclFrame.right - header.rclFrame.left;
    double h = header.rclFrame.bottom - header.rclFrame.top;
    if (w <= 0 || h <= 0) {
        w = himetricW > 0 ? himetricW : 1000;
        h = himetricH > 0 ? himetricH : 1000;
    }
    const int longSide = 2048;
    const int pw = std::max(1, static_cast<int>(w >= h ? longSide : longSide * w / h));
    const int ph = std::max(1, static_cast<int>(h >= w ? longSide : longSide * h / w));

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pw;
    info.bmiHeader.biHeight = -ph;   // dall'alto in basso
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage image;
    if (bitmap && bits) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        RECT rect{0, 0, pw, ph};
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        PlayEnhMetaFile(dc, meta, &rect);
        GdiFlush();
        image = QImage(static_cast<const uchar*>(bits), pw, ph, pw * 4, QImage::Format_RGB32).copy();
        SelectObject(dc, old);
    }
    if (bitmap)
        DeleteObject(bitmap);
    DeleteDC(dc);
    DeleteEnhMetaFile(meta);
    return image;
}
#endif

// Rappresentazione salvata (\2OlePresNNN): formato, dimensioni in HIMETRIC e dati.
QImage fromPresentation(const QByteArray& s)
{
    qsizetype at = 0;
    const std::uint32_t marker = u32(s, at);
    at += 4;
    if (marker == 0xFFFFFFFF || marker == 0xFFFFFFFE)
        at += 4;              // formato standard
    else if (marker > 0)
        at += marker;         // nome del formato
    const std::uint32_t deviceSize = u32(s, at);
    at += deviceSize >= 4 ? deviceSize : 4;
    at += 16;                 // aspetto, lindex, advf, riservato
    const int width = static_cast<int>(u32(s, at));
    const int height = static_cast<int>(u32(s, at + 4));
    const std::uint32_t size = u32(s, at + 8);
    at += 12;
    if (at >= s.size())
        return {};
    const QByteArray data = s.mid(at, std::min<qsizetype>(size, s.size() - at));
    // Si riconosce il contenuto dai primi byte.
    if (QImage dib = fromDib(data); !dib.isNull())
        return dib;
#ifdef Q_OS_WIN
    const bool emf = u32(data, 0) == 1 && data.mid(40, 4) == QByteArray(" EMF");
    const bool wmf = (u16(data, 0) == 1 || u16(data, 0) == 2) && u16(data, 2) == 9;
    const bool placeable = u32(data, 0) == 0x9AC6CDD7u;
    if (emf)
        return renderMetafile(data, true, width, height);
    if (placeable)
        return renderMetafile(data.mid(22), false, width, height);
    if (wmf)
        return renderMetafile(data, false, width, height);
#else
    Q_UNUSED(width);
    Q_UNUSED(height);
#endif
    return findImageFile(data);
}

} // namespace

QImage decodeOleImage(const std::string& raw)
{
    const QByteArray all = QByteArray::fromRawData(raw.data(), static_cast<qsizetype>(raw.size()));
    const qsizetype start = all.indexOf(QByteArray("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8));
    if (start < 0)
        return findImageFile(all);
    const CompoundFile cfb(QByteArray(all.constData() + start, all.size() - start));
    if (!cfb.ok())
        return {};
    // 1. Immagine nativa (Paint: un BMP preceduto dalla sua lunghezza; pacchetti: un file).
    if (const auto native = cfb.stream(QStringLiteral("\x01Ole10Native")); native && native->size() > 4) {
        const QImage image = findImageFile(native->mid(4));
        if (!image.isNull())
            return image;
    }
    // 2. Contenuto di alcuni oggetti immagine.
    for (const QString& name : {QStringLiteral("CONTENTS"), QStringLiteral("Contents"), QStringLiteral("Package")})
        if (const auto contents = cfb.stream(name)) {
            const QImage image = findImageFile(*contents);
            if (!image.isNull())
                return image;
        }
    // 3. Rappresentazione salvata dall'applicazione (Word, Excel...).
    for (int i = 0; i < 10; ++i)
        if (const auto pres = cfb.stream(QStringLiteral("\x02OlePres%1").arg(i, 3, 10, QLatin1Char('0')))) {
            const QImage image = fromPresentation(*pres);
            if (!image.isNull())
                return image;
        }
    return {};
}

} // namespace cartesio
