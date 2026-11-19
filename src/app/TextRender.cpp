#include "app/TextRender.h"

#include <QRegularExpression>

#include "app/AppOptions.h"
#include "core/Document.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace cartesio {

namespace {

constexpr double kShxPi = 3.14159265358979323846;

unsigned u16le(const QByteArray& d, int pos)
{
    return static_cast<unsigned char>(d[pos]) | (static_cast<unsigned>(static_cast<unsigned char>(d[pos + 1])) << 8);
}

unsigned u32le(const QByteArray& d, int pos)
{
    return u16le(d, pos) | (u16le(d, pos + 2) << 16);
}

} // namespace

// Stato della "penna" mentre si esegue la definizione di un carattere.
struct ShxPen {
    double x = 0.0, y = 0.0;
    bool down = true;
    double scale = 1.0;
    std::vector<QPointF> stack;
    std::vector<std::vector<QPointF>> strokes;
    std::vector<QPointF> current;

    void flush()
    {
        if (current.size() >= 2)
            strokes.push_back(std::move(current));
        current.clear();
    }
    void moveTo(double nx, double ny)
    {
        if (down) {
            if (current.empty())
                current.push_back({x, y});
            current.push_back({nx, ny});
        } else {
            flush();
        }
        x = nx;
        y = ny;
    }
    void setDown(bool d)
    {
        if (!d)
            flush();
        down = d;
    }
    // Arco di centro c, raggio r, dall'angolo a0 per `sweep` (radianti, positivo antiorario).
    void arc(QPointF c, double r, double a0, double sweep)
    {
        const int n = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / (kShxPi / 16.0))));
        for (int i = 1; i <= n; ++i) {
            const double a = a0 + sweep * i / n;
            moveTo(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
        }
    }
    // Arco per "bulge" (come le polilinee: tan dell'angolo/4, positivo antiorario) fino a (x+dx, y+dy).
    void bulge(double dx, double dy, double b)
    {
        if (std::abs(b) < 1e-9) {
            moveTo(x + dx, y + dy);
            return;
        }
        const double chord = std::hypot(dx, dy);
        const double sweep = 4.0 * std::atan(b);
        const double r = chord / (2.0 * std::sin(std::abs(sweep) / 2.0));
        // Centro: a metà corda, spostato perpendicolarmente.
        const double mx = x + dx / 2.0, my = y + dy / 2.0;
        const double d = r * std::cos(std::abs(sweep) / 2.0);
        const double nx = -dy / chord, ny = dx / chord;   // normale a sinistra
        const double side = (b > 0) == (std::abs(sweep) < kShxPi) ? 1.0 : -1.0;
        const QPointF c(mx + nx * d * side, my + ny * d * side);
        const double a0 = std::atan2(y - c.y(), x - c.x());
        const double ex = x + dx, ey = y + dy;
        arc(c, r, a0, sweep);
        x = ex;
        y = ey;
    }
};

std::shared_ptr<ShxFont> ShxFont::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("impossibile leggere %1").arg(path);
        return nullptr;
    }
    auto font = std::shared_ptr<ShxFont>(new ShxFont);
    font->name_ = QFileInfo(path).fileName();
    if (!font->parse(file.readAll())) {
        if (error)
            *error = QStringLiteral("%1 non è un font SHX leggibile").arg(path);
        return nullptr;
    }
    return font;
}

bool ShxFont::parse(const QByteArray& data)
{
    const int headerEnd = data.indexOf('\x1A');
    if (headerEnd < 0 || headerEnd > 64)
        return false;
    const QByteArray header = data.left(headerEnd).toLower();
    int pos = headerEnd + 1;
    const auto stripName = [](const QByteArray& def) {
        const int zero = def.indexOf('\0');
        return zero < 0 ? def : def.mid(zero + 1);
    };
    if (header.contains("unifont")) {
        unifont_ = true;
        if (pos + 6 > data.size())
            return false;
        const unsigned count = u32le(data, pos);
        const unsigned infoLength = u16le(data, pos + 4);
        pos += 6;
        if (pos + static_cast<int>(infoLength) > data.size())
            return false;
        const QByteArray info = stripName(data.mid(pos, static_cast<int>(infoLength)));
        if (info.size() >= 2) {
            above_ = static_cast<unsigned char>(info[0]);
            below_ = static_cast<unsigned char>(info[1]);
        }
        pos += static_cast<int>(infoLength);
        for (unsigned i = 0; i < count && pos + 4 <= data.size(); ++i) {
            const unsigned number = u16le(data, pos);
            const unsigned length = u16le(data, pos + 2);
            pos += 4;
            if (pos + static_cast<int>(length) > data.size())
                break;
            shapes_[number] = stripName(data.mid(pos, static_cast<int>(length)));
            pos += static_cast<int>(length);
        }
    } else if (header.contains("bigfont")) {
        bigfont_ = true;
        if (pos + 6 > data.size())
            return false;
        const unsigned count = u16le(data, pos + 2);
        const unsigned ranges = u16le(data, pos + 4);
        pos += 6 + static_cast<int>(ranges) * 4;
        for (unsigned i = 0; i < count && pos + 8 <= data.size(); ++i) {
            const unsigned number = u16le(data, pos);
            const unsigned length = u16le(data, pos + 2);
            const unsigned offset = u32le(data, pos + 4);
            pos += 8;
            if (length == 0 || offset + length > static_cast<unsigned>(data.size()))
                continue;
            shapes_[number] = stripName(data.mid(static_cast<int>(offset), static_cast<int>(length)));
        }
        if (const auto it = shapes_.find(0); it != shapes_.end() && it->second.size() >= 2) {
            above_ = static_cast<unsigned char>(it->second[0]);
            below_ = static_cast<unsigned char>(it->second[1]);
        }
    } else if (header.contains("shapes")) {
        if (pos + 6 > data.size())
            return false;
        const unsigned count = u16le(data, pos + 4);
        pos += 6;
        std::vector<std::pair<unsigned, unsigned>> index;
        for (unsigned i = 0; i < count && pos + 4 <= data.size(); ++i) {
            index.emplace_back(u16le(data, pos), u16le(data, pos + 2));
            pos += 4;
        }
        for (const auto& [number, length] : index) {
            if (pos + static_cast<int>(length) > data.size())
                break;
            shapes_[number] = stripName(data.mid(pos, static_cast<int>(length)));
            pos += static_cast<int>(length);
        }
        if (const auto it = shapes_.find(0); it != shapes_.end() && it->second.size() >= 2) {
            above_ = static_cast<unsigned char>(it->second[0]);
            below_ = static_cast<unsigned char>(it->second[1]);
        }
    } else {
        return false;
    }
    if (above_ <= 0.0)
        above_ = 1.0;
    return !shapes_.empty();
}

// Esegue la definizione di un carattere (anche per i sotto-caratteri, codice 7).
void ShxFont::run(const QByteArray& def, std::size_t start, ShxPen& pen, int depth) const
{
    static const double dirs[16][2] = {{1, 0},   {1, 0.5},   {1, 1},   {0.5, 1},  {0, 1},   {-0.5, 1},
                                       {-1, 1},  {-1, 0.5},  {-1, 0},  {-1, -0.5}, {-1, -1}, {-0.5, -1},
                                       {0, -1},  {0.5, -1},  {1, -1},  {1, -0.5}};
    const auto n = static_cast<std::size_t>(def.size());
    const auto byte = [&def, n](std::size_t& i) -> int {
        return i < n ? static_cast<unsigned char>(def[static_cast<int>(i++)]) : 0;
    };
    const auto sbyte = [&def, n](std::size_t& i) -> int {
        return i < n ? static_cast<signed char>(def[static_cast<int>(i++)]) : 0;
    };
    // Lunghezza dei parametri di un comando (per il codice 14, che salta il comando seguente nel testo orizzontale).
    const auto skip = [&](std::size_t& i) {
        const int b = byte(i);
        if (b >= 16)
            return;
        switch (b) {
        case 3: case 4: i += 1; break;
        case 7: i += (unifont_ || bigfont_) ? 2 : 1; break;
        case 8: i += 2; break;
        case 9: while (i + 1 < n) { const int dx = sbyte(i), dy = sbyte(i); if (dx == 0 && dy == 0) break; } break;
        case 10: i += 2; break;
        case 11: i += 5; break;
        case 12: i += 3; break;
        case 13: while (i + 1 < n) { const int dx = sbyte(i), dy = sbyte(i); if (dx == 0 && dy == 0) break; i += 1; } break;
        default: break;
        }
    };

    std::size_t i = start;
    while (i < n) {
        const int b = byte(i);
        if (b >= 16) {   // vettore: lunghezza (4 bit alti) e direzione (4 bit bassi)
            const double len = (b >> 4) * pen.scale;
            const auto& d = dirs[b & 15];
            pen.moveTo(pen.x + d[0] * len, pen.y + d[1] * len);
            continue;
        }
        switch (b) {
        case 0:
            return;
        case 1:
            pen.setDown(true);
            break;
        case 2:
            pen.setDown(false);
            break;
        case 3: {
            const int k = byte(i);
            if (k)
                pen.scale /= k;
            break;
        }
        case 4: {
            const int k = byte(i);
            if (k)
                pen.scale *= k;
            break;
        }
        case 5:
            if (pen.stack.size() < 16)
                pen.stack.push_back({pen.x, pen.y});
            break;
        case 6:
            if (!pen.stack.empty()) {
                const QPointF p = pen.stack.back();
                pen.stack.pop_back();
                const bool down = pen.down;
                pen.setDown(false);
                pen.moveTo(p.x(), p.y());
                pen.down = down;
            }
            break;
        case 7: {
            unsigned number = byte(i);
            if (unifont_ || bigfont_)
                number = (number << 8) | static_cast<unsigned>(byte(i));
            if (bigfont_ && number == 0) {   // forma estesa: numero, origine e dimensioni
                i += 6;
                break;
            }
            if (depth < 8)
                if (const auto it = shapes_.find(number); it != shapes_.end())
                    run(it->second, 0, pen, depth + 1);
            break;
        }
        case 8: {
            const int dx = sbyte(i), dy = sbyte(i);
            pen.moveTo(pen.x + dx * pen.scale, pen.y + dy * pen.scale);
            break;
        }
        case 9:
            while (i + 1 < n) {
                const int dx = sbyte(i), dy = sbyte(i);
                if (dx == 0 && dy == 0)
                    break;
                pen.moveTo(pen.x + dx * pen.scale, pen.y + dy * pen.scale);
            }
            break;
        case 10: {   // arco per ottanti
            const double r = byte(i) * pen.scale;
            const int oct = sbyte(i);
            const bool cw = oct < 0;
            const int first = (oct >> 4) & 7;
            int count = oct & 7;
            if (count == 0)
                count = 8;
            const double a0 = first * kShxPi / 4.0;
            const double sweep = count * kShxPi / 4.0 * (cw ? -1.0 : 1.0);
            const QPointF c(pen.x - r * std::cos(a0), pen.y - r * std::sin(a0));
            pen.arc(c, r, a0, sweep);
            break;
        }
        case 11: {   // arco frazionario
            const int startOffset = byte(i), endOffset = byte(i);
            const int hi = byte(i), lo = byte(i);
            const int oct = sbyte(i);
            const double r = (hi * 256 + lo) * pen.scale;
            const bool cw = oct < 0;
            const int first = (oct >> 4) & 7;
            int count = oct & 7;
            if (count == 0)
                count = 8;
            const double unit = kShxPi / 4.0;
            const double sign = cw ? -1.0 : 1.0;
            const double a0 = first * unit + sign * startOffset * unit / 256.0;
            const double a1 = (first + sign * (count - 1)) * unit + sign * (endOffset ? endOffset * unit / 256.0 : unit);
            const QPointF c(pen.x - r * std::cos(a0), pen.y - r * std::sin(a0));
            pen.arc(c, r, a0, a1 - a0);
            break;
        }
        case 12: {   // arco per bulge
            const int dx = sbyte(i), dy = sbyte(i), bulge = sbyte(i);
            pen.bulge(dx * pen.scale, dy * pen.scale, bulge / 127.0);
            break;
        }
        case 13:
            while (i + 1 < n) {
                const int dx = sbyte(i), dy = sbyte(i);
                if (dx == 0 && dy == 0)
                    break;
                const int bulge = sbyte(i);
                pen.bulge(dx * pen.scale, dy * pen.scale, bulge / 127.0);
            }
            break;
        case 14:   // solo per il testo verticale: si salta il comando seguente
            skip(i);
            break;
        default:
            break;
        }
    }
}

const ShxGlyph* ShxFont::glyph(char32_t code) const
{
    if (const auto it = cache_.find(code); it != cache_.end())
        return it->second.get();
    const auto shape = shapes_.find(static_cast<unsigned>(code));
    if (shape == shapes_.end() || code == 0) {
        cache_[code] = nullptr;
        return nullptr;
    }
    ShxPen pen;
    run(shape->second, 0, pen, 0);
    pen.flush();
    auto glyph = std::make_unique<ShxGlyph>();
    glyph->strokes = std::move(pen.strokes);
    glyph->advance = pen.x;
    const ShxGlyph* out = glyph.get();
    cache_[code] = std::move(glyph);
    return out;
}

// ================================================================ font a tratti incorporato

namespace {
#include "app/BuiltinStrokeFont.inc"

// Maiuscole da y = -12 a y = 9 nei dati (Y verso il basso): linea di base a 9, altezza 21, discesa 7.
constexpr double kStrokeBase = 9.0;

ShxGlyph strokeGlyph(const char* def)
{
    ShxGlyph g;
    const int left = def[0] - 'R';
    g.advance = (def[1] - 'R') - left;
    std::vector<QPointF> current;
    const auto flush = [&g, &current] {
        if (current.size() > 1)
            g.strokes.push_back(current);
        current.clear();
    };
    for (const char* p = def + 2; p[0] && p[1]; p += 2) {
        if (p[0] == ' ' && p[1] == 'R') {
            flush();
            continue;
        }
        current.emplace_back(p[0] - 'R' - left, kStrokeBase - (p[1] - 'R'));
    }
    flush();
    return g;
}
} // namespace

std::shared_ptr<ShxFont> ShxFont::builtin()
{
    static const std::shared_ptr<ShxFont> font = [] {
        auto f = std::shared_ptr<ShxFont>(new ShxFont);
        f->name_ = QStringLiteral("CartesioCAD (a tratti)");
        f->above_ = 21.0;
        f->below_ = 7.0;
        f->unifont_ = true;
        const auto put = [&f](char32_t code, ShxGlyph g) { f->cache_[code] = std::make_unique<ShxGlyph>(std::move(g)); };
        const auto get = [&f](char32_t code) { return *f->cache_.at(code); };
        for (int i = 0; i < 95; ++i)
            put(static_cast<char32_t>(32 + i), strokeGlyph(kStrokeGlyphs[i]));

        // Lettere accentate: la lettera più un tratto sopra (grave, acuto) o due punti (dieresi).
        const auto accented = [&](char32_t code, char32_t base, int kind) {
            ShxGlyph g = get(base);
            const bool upper = base < U'a';
            if (base == U'i' && !g.strokes.empty())
                g.strokes.erase(g.strokes.begin());   // senza il puntino
            const double cx = g.advance / 2.0, y = upper ? 23.0 : 16.5;
            if (kind == 0)
                g.strokes.push_back({{cx - 3.0, y + 4.0}, {cx + 1.0, y}});
            else if (kind == 1)
                g.strokes.push_back({{cx - 1.0, y}, {cx + 3.0, y + 4.0}});
            else {
                g.strokes.push_back({{cx - 3.0, y + 2.0}, {cx - 3.0, y + 3.0}});
                g.strokes.push_back({{cx + 3.0, y + 2.0}, {cx + 3.0, y + 3.0}});
            }
            put(code, std::move(g));
        };
        const char32_t grave[][2] = {{0xE0, U'a'}, {0xE8, U'e'}, {0xEC, U'i'}, {0xF2, U'o'}, {0xF9, U'u'},
                                     {0xC0, U'A'}, {0xC8, U'E'}, {0xCC, U'I'}, {0xD2, U'O'}, {0xD9, U'U'}};
        const char32_t acute[][2] = {{0xE1, U'a'}, {0xE9, U'e'}, {0xED, U'i'}, {0xF3, U'o'}, {0xFA, U'u'},
                                     {0xC1, U'A'}, {0xC9, U'E'}, {0xCD, U'I'}, {0xD3, U'O'}, {0xDA, U'U'}};
        const char32_t dots[][2] = {{0xE4, U'a'}, {0xF6, U'o'}, {0xFC, U'u'}, {0xC4, U'A'}, {0xD6, U'O'}, {0xDC, U'U'}};
        for (const auto& a : grave)
            accented(a[0], a[1], 0);
        for (const auto& a : acute)
            accented(a[0], a[1], 1);
        for (const auto& a : dots)
            accented(a[0], a[1], 2);

        // Gradi: cerchietto in alto.
        {
            ShxGlyph g;
            g.advance = 10.0;
            std::vector<QPointF> ring;
            for (int k = 0; k <= 10; ++k) {
                const double a = 2.0 * 3.14159265358979323846 * k / 10.0;
                ring.emplace_back(5.0 + 3.0 * std::cos(a), 18.0 + 3.0 * std::sin(a));
            }
            g.strokes.push_back(std::move(ring));
            put(0x00B0, std::move(g));
        }
        // Più o meno: il più con una linea sotto.
        {
            ShxGlyph g = get(U'+');
            g.strokes.push_back({{4.0, 1.0}, {g.advance - 4.0, 1.0}});
            put(0x00B1, std::move(g));
        }
        // Diametro: la O (maiuscola o minuscola) con una barra.
        {
            ShxGlyph g = get(U'O');
            g.strokes.push_back({{3.0, -2.0}, {g.advance - 3.0, 23.0}});
            put(0x00D8, g);
            put(0x2300, std::move(g));
            ShxGlyph s = get(U'o');
            s.strokes.push_back({{3.0, -2.0}, {s.advance - 3.0, 16.0}});
            put(0x00F8, std::move(s));
        }
        // Quadrato e cubo: la cifra piccola in alto. Per: la x.
        for (const auto& [code, base] : {std::pair<char32_t, char32_t>{0x00B2, U'2'}, {0x00B3, U'3'}}) {
            ShxGlyph g = get(base);
            for (auto& stroke : g.strokes)
                for (QPointF& p : stroke)
                    p = QPointF(p.x() * 0.55, 10.0 + p.y() * 0.55);
            g.advance *= 0.55;
            put(code, std::move(g));
        }
        put(0x00D7, get(U'x'));
        return f;
    }();
    return font;
}

// ================================================================ ricerca dei font

FontLibrary& FontLibrary::instance()
{
    static FontLibrary library;
    return library;
}

FontLibrary::FontLibrary() = default;

void FontLibrary::reset()
{
    cache_.clear();
}

std::vector<QString> FontLibrary::folders() const
{
    std::vector<QString> out;
    for (const QString& f : appOptions().fontFolders.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        out.push_back(f.trimmed());
    // Cartella "fonts" accanto al programma: per i font che l'utente ci mette.
    out.push_back(QCoreApplication::applicationDirPath() + QStringLiteral("/fonts"));
    // Solo se chiesto nelle Opzioni: cartelle Fonts degli altri programmi installati, Programmi\<produttore>\
    // <prodotto>\Fonts con dentro dei font SHX (le versioni più recenti prima). Si cercano una volta sola.
    if (appOptions().fontsOfInstalledPrograms) {
        static const std::vector<QString> installed = [] {
            std::vector<QString> found;
            for (const QString& root : {QStringLiteral("C:/Program Files"), QStringLiteral("C:/Program Files (x86)")}) {
                const QDir programs(root);
                for (const QString& vendor : programs.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    const QDir dir(root + QLatin1Char('/') + vendor);
                    for (const QString& product : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
                        const QString fonts = dir.filePath(product) + QStringLiteral("/Fonts");
                        if (QFileInfo(fonts).isDir() && !QDir(fonts).entryList({QStringLiteral("*.shx")}, QDir::Files).isEmpty())
                            found.push_back(fonts);
                    }
                }
            }
            return found;
        }();
        out.insert(out.end(), installed.begin(), installed.end());
    }
    out.push_back(QStringLiteral("C:/Windows/Fonts"));
    out.push_back(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                  + QStringLiteral("/Microsoft/Windows/Fonts"));
    return out;
}

QString FontLibrary::findFile(const QString& name) const
{
    if (name.isEmpty())
        return {};
    const QString file = QFileInfo(name).fileName();
    for (const QString& folder : folders()) {
        const QDir dir(folder);
        // Su Windows i nomi non distinguono maiuscole e minuscole.
        const QString path = dir.filePath(file);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

ResolvedFont FontLibrary::resolve(const TextStyle& style)
{
    const QString key = QString::fromStdString(style.font + '|' + style.family) + (style.bold ? QStringLiteral("|b") : QString())
                        + (style.italic ? QStringLiteral("|i") : QString());
    if (const auto it = cache_.find(key); it != cache_.end())
        return it->second;
    ResolvedFont out;
    QString family = QString::fromStdString(style.family);
    if (style.isShx()) {
        QString file = QString::fromStdString(style.font);
        if (!file.contains(QLatin1Char('.')))
            file += QStringLiteral(".shx");
        // Font mancante (FONTALT): si usa simplex.shx, poi romans.shx, poi quello incorporato.
        for (const QString& candidate : {file, QStringLiteral("simplex.shx"), QStringLiteral("romans.shx")}) {
            const QString lower = candidate.toLower();
            if (const auto it = shx_.find(lower); it != shx_.end() && it->second) {
                out.shx = it->second;
                break;
            }
            const QString path = findFile(candidate);
            if (path.isEmpty())
                continue;
            out.shx = ShxFont::load(path);
            shx_[lower] = out.shx;
            if (out.shx)
                break;
        }
        // Nessun file trovato: il font a tratti di CartesioCAD (non un TrueType: migliaia di testi TrueType
        // sono molto più pesanti da disegnare dei tratti).
        if (!out.shx)
            out.shx = ShxFont::builtin();
    } else if (family.isEmpty() && !style.font.empty()) {
        // TrueType indicato solo dal file: la famiglia si legge dal file.
        const QString file = QString::fromStdString(style.font).toLower();
        if (const auto it = ttfFamilies_.find(file); it != ttfFamilies_.end()) {
            family = it->second;
        } else {
            const QString path = findFile(QString::fromStdString(style.font));
            if (!path.isEmpty()) {
                const int id = QFontDatabase::addApplicationFont(path);
                const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
                if (!families.isEmpty())
                    family = families.front();
            }
            ttfFamilies_[file] = family;
        }
    }
    if (!out.shx) {
        // Famiglia non installata: si prova a caricarla dalla cartella dei font.
        if (!family.isEmpty() && !QFontDatabase::hasFamily(family) && !style.font.empty()) {
            const QString path = findFile(QString::fromStdString(style.font));
            if (!path.isEmpty())
                QFontDatabase::addApplicationFont(path);
        }
        QFont font(family.isEmpty() ? QStringLiteral("Arial") : family);
        font.setPixelSize(100);
        font.setBold(style.bold);
        font.setItalic(style.italic);
        out.font = font;
    }
    cache_[key] = out;
    return out;
}

// ================================================================ impaginazione

namespace {


// Codice del carattere nel font SHX: i font "shapes" usano la tabella di Windows (1252) e 127/128/129 per
// gradi, più/meno e diametro; gli unifont il codice Unicode.
char32_t shxCode(const ShxFont& font, char32_t c)
{
    if (font.unicode())
        return c;
    if (c == 0x00B0 && font.glyph(127))
        return 127;
    if (c == 0x00B1 && font.glyph(128))
        return 128;
    if ((c == 0x00D8 || c == 0x2300 || c == 0x00F8) && font.glyph(129))
        return 129;
    return c < 256 ? c : U'?';
}

struct Shaped {
    double advance = 0.0;   // unità del testo
    double descent = 0.0;
};

// Larghezza e discesa di una riga, in unità del testo (maiuscole alte `height`).
Shaped measure(const ResolvedFont& f, const QString& text, double height)
{
    Shaped out;
    if (f.shx) {
        const double k = height / f.shx->above();
        const auto codes = text.toUcs4();
        for (char32_t c : codes) {
            const ShxGlyph* g = f.shx->glyph(shxCode(*f.shx, c));
            out.advance += (g ? g->advance : f.shx->above() * 0.6) * k;
        }
        out.descent = f.shx->below() * k;
        return out;
    }
    const QFontMetricsF fm(f.font);
    const double cap = fm.capHeight() > 0 ? fm.capHeight() : fm.ascent() * 0.7;
    const double k = height / cap;
    out.advance = fm.horizontalAdvance(text) * k;
    out.descent = fm.descent() * k;
    return out;
}

PlacedText makeLine(const ResolvedFont& f, const QString& text, double height)
{
    PlacedText p;
    p.text = text;
    p.height = height;
    p.shx = f.shx;
    p.font = f.font;
    if (!f.shx) {
        const QFontMetricsF fm(f.font);
        const double cap = fm.capHeight() > 0 ? fm.capHeight() : fm.ascent() * 0.7;
        p.fontScale = height / cap;
    }
    p.advance = measure(f, text, height).advance;
    return p;
}


} // namespace

std::vector<std::vector<QPointF>> PlacedText::strokes() const
{
    std::vector<std::vector<QPointF>> out;
    if (!shx)
        return out;
    const double k = height / shx->above();
    double x = 0.0;
    const auto codes = text.toUcs4();
    for (char32_t c : codes) {
        const ShxGlyph* g = shx->glyph(shxCode(*shx, c));
        if (!g) {
            x += shx->above() * 0.6 * k;
            continue;
        }
        for (const auto& stroke : g->strokes) {
            std::vector<QPointF> s;
            s.reserve(stroke.size());
            for (const QPointF& p : stroke)
                s.push_back(toEntity.map(QPointF(x + p.x() * k, p.y() * k)));
            out.push_back(std::move(s));
        }
        x += g->advance * k;
    }
    return out;
}

std::pair<QPointF, QPointF> PlacedText::dash() const
{
    return {toEntity.map(QPointF(0.0, height * 0.4)), toEntity.map(QPointF(advance, height * 0.4))};
}

QTransform PlacedText::fontToEntity() const
{
    return QTransform::fromScale(fontScale, -fontScale) * toEntity;
}

std::vector<PlacedText> layoutText(const Document& doc, const TextEntity& t)
{
    const QString s = QString::fromStdString(t.displayText());
    if (s.isEmpty() || t.height <= 0.0)
        return {};
    const TextStyle& style = doc.textStyleOrStandard(t.style);
    const ResolvedFont font = FontLibrary::instance().resolve(style);
    PlacedText line = makeLine(font, s, t.height);
    const Shaped m = measure(font, s, t.height);

    const bool simple = t.hAlign == TextEntity::Left && t.vAlign == TextEntity::Baseline;
    const bool aligned = t.hAlign == TextEntity::Aligned;
    const bool fitted = aligned || t.hAlign == TextEntity::Fit;
    const Vec2 ref = simple || fitted ? t.position : t.alignPoint;
    double widthFactor = t.widthFactor > 0.0 ? t.widthFactor : 1.0;
    double heightFactor = 1.0;
    if (fitted && m.advance > 0.0) {
        const double target = distance(t.position, t.alignPoint);
        if (aligned) {   // ALLINEATO: altezza e larghezza in proporzione
            heightFactor = target / (m.advance * widthFactor);
            widthFactor *= heightFactor;
        } else {         // ADATTA: solo la larghezza
            widthFactor = target / m.advance;
        }
    }
    double dx = 0.0;
    if (t.hAlign == TextEntity::Center || t.hAlign == TextEntity::Middle)
        dx = -m.advance / 2.0;
    else if (t.hAlign == TextEntity::Right)
        dx = -m.advance;
    double dy = 0.0;
    if (t.hAlign == TextEntity::Middle || t.vAlign == TextEntity::VMiddle)
        dy = -t.height / 2.0;
    else if (t.vAlign == TextEntity::Top)
        dy = -t.height;
    else if (t.vAlign == TextEntity::Bottom)
        dy = m.descent;

    // Dalle coordinate della riga all'oggetto: spostamento d'allineamento, larghezza, obliquità, rotazione.
    const double oblique = t.oblique != 0.0 ? t.oblique : 0.0;
    QTransform toEntity;
    toEntity.translate(ref.x, ref.y);
    toEntity.rotate(t.effectiveRotation() * 180.0 / kShxPi);
    toEntity = QTransform(1.0, 0.0, std::tan(oblique), 1.0, 0.0, 0.0) * toEntity;   // x += y * tan
    toEntity = QTransform::fromScale(widthFactor, heightFactor) * toEntity;
    toEntity = QTransform::fromTranslate(dx, dy) * toEntity;
    line.toEntity = toEntity;
    return {line};
}

// Legge i codici di formattazione: i paragrafi (\P) con i loro pezzi di testo.
std::vector<MTextParagraph> parseMText(const QString& s, const MTextFormat& initial)
{
    std::vector<MTextParagraph> out(1);
    std::vector<MTextFormat> stack;
    MTextFormat f = initial;
    const auto append = [&](const QString& text) {
        if (text.isEmpty())
            return;
        auto& paragraph = out.back();
        if (!paragraph.empty() && paragraph.back().format.sameAs(f))
            paragraph.back().text += text;
        else
            paragraph.push_back({f, text});
    };
    // Valore fino al ';' (incluso, che si salta).
    const auto value = [&s](int& i) {
        const int end = s.indexOf(QLatin1Char(';'), i);
        const QString v = s.mid(i, end < 0 ? -1 : end - i);
        i = end < 0 ? s.size() : end + 1;
        return v;
    };
    for (int i = 0; i < s.size();) {
        const QChar c = s[i];
        if (c == QLatin1Char('{')) {
            stack.push_back(f);
            ++i;
            continue;
        }
        if (c == QLatin1Char('}')) {
            if (!stack.empty()) {
                f = stack.back();
                stack.pop_back();
            }
            ++i;
            continue;
        }
        // Codici %% anche nei testi multilinea: %%c diametro, %%d gradi, %%p più/meno, %%% percento, %%nnn carattere.
        if (c == QLatin1Char('%') && i + 2 < s.size() && s[i + 1] == QLatin1Char('%')) {
            const QChar k = s[i + 2].toLower();
            if (k == QLatin1Char('c') || k == QLatin1Char('d') || k == QLatin1Char('p') || k == QLatin1Char('%')) {
                append(k == QLatin1Char('c') ? QString(QChar(0x00D8)) : k == QLatin1Char('d') ? QString(QChar(0x00B0))
                       : k == QLatin1Char('p') ? QString(QChar(0x00B1)) : QStringLiteral("%"));
                i += 3;
                continue;
            }
            if (k == QLatin1Char('u') || k == QLatin1Char('o')) {   // sottolineato / sopralineato (si alternano)
                if (k == QLatin1Char('u'))
                    f.underline = !f.underline;
                else
                    f.overline = !f.overline;
                i += 3;
                continue;
            }
            if (k.isDigit()) {
                int j = i + 2, code = 0;
                while (j < s.size() && j < i + 5 && s[j].isDigit())
                    code = code * 10 + s[j++].digitValue();
                if (code > 0 && code < 256) {
                    append(QString(QChar(static_cast<char16_t>(code))));
                    i = j;
                    continue;
                }
            }
        }
        if (c != QLatin1Char('\\') || i + 1 >= s.size()) {
            append(QString(c));
            ++i;
            continue;
        }
        const QChar code = s[i + 1];
        i += 2;
        switch (code.unicode()) {
        case 'P':
        case 'N':
            out.emplace_back();
            break;
        case 'H': {
            const QString v = value(i);
            bool ok = false;
            if (v.endsWith(QLatin1Char('x'), Qt::CaseInsensitive)) {
                const double k = v.chopped(1).toDouble(&ok);
                if (ok && k > 0.0)
                    f.height *= k;
            } else {
                const double h = v.toDouble(&ok);
                if (ok && h > 0.0)
                    f.height = h;
            }
            break;
        }
        case 'W': {
            bool ok = false;
            const double w = value(i).toDouble(&ok);
            if (ok && w > 0.0)
                f.width = w;
            break;
        }
        case 'Q': {
            bool ok = false;
            const double q = value(i).toDouble(&ok);
            if (ok && std::abs(q) < 85.0)
                f.oblique = q * kShxPi / 180.0;
            break;
        }
        case 'f':
        case 'F': {
            const QStringList parts = value(i).split(QLatin1Char('|'));
            const QString name = parts.value(0).trimmed();
            if (code == QLatin1Char('F') || name.endsWith(QStringLiteral(".shx"), Qt::CaseInsensitive)) {
                f.shxFile = name;
                f.family.clear();
            } else {
                f.family = name;
                f.shxFile.clear();
            }
            f.bold = parts.contains(QStringLiteral("b1"));
            f.italic = parts.contains(QStringLiteral("i1"));
            break;
        }
        case 'C':
            f.aci = value(i).toInt();
            f.rgb = -1;
            break;
        case 'c':
            f.rgb = value(i).toLongLong();
            f.aci = 0;
            break;
        case 'S': {   // frazione: a^b, a/b, a#b
            QString v = value(i);
            v.replace(QLatin1Char('^'), QLatin1Char('/')).replace(QLatin1Char('#'), QLatin1Char('/'));
            append(v.trimmed());
            break;
        }
        case 'p': {   // proprietà del paragrafo: dell'allineamento si tiene q (\pxqc;), il resto no
            const QString v = value(i);
            const int q = v.indexOf(QLatin1Char('q'));
            if (q >= 0 && q + 1 < v.size()) {
                static const QString letters = QStringLiteral("lcrjd");
                const int a = static_cast<int>(letters.indexOf(v[q + 1]));
                if (a >= 0)
                    f.align = a;
            }
            break;
        }
        case 'T':
        case 'A':
        case 'X':
            value(i);   // spaziatura, allineamento verticale: non cambiano il testo
            break;
        case 'L':
            f.underline = true;
            break;
        case 'l':
            f.underline = false;
            break;
        case 'O':
            f.overline = true;
            break;
        case 'o':
            f.overline = false;
            break;
        case 'K':
            f.strike = true;
            break;
        case 'k':
            f.strike = false;
            break;
        case '~':
            append(QStringLiteral(" "));
            break;
        case 'U':
            if (i < s.size() && s[i] == QLatin1Char('+') && i + 5 <= s.size()) {
                bool ok = false;
                const uint u = s.mid(i + 1, 4).toUInt(&ok, 16);
                if (ok) {
                    append(QString(QChar(static_cast<char16_t>(u))));
                    i += 5;
                }
            }
            break;
        default:   // \\ \{ \} e codici sconosciuti: il carattere stesso
            append(QString(code));
            break;
        }
    }
    return out;
}

namespace {

ResolvedFont fontFor(const TextStyle& base, const MTextFormat& f)
{
    if (f.family.isEmpty() && f.shxFile.isEmpty() && !f.bold && !f.italic)
        return FontLibrary::instance().resolve(base);
    TextStyle s = base;
    if (!f.shxFile.isEmpty()) {
        s.font = f.shxFile.toStdString();
        s.family.clear();
    } else if (!f.family.isEmpty()) {
        s.family = f.family.toStdString();
        s.font.clear();
    }
    s.bold = f.bold;
    s.italic = f.italic;
    return FontLibrary::instance().resolve(s);
}

} // namespace

MTextFormat mtextInitialFormat(const Document& doc, const MTextEntity& m)
{
    const TextStyle& style = doc.textStyleOrStandard(m.style);
    MTextFormat initial;
    initial.height = m.height;
    initial.width = style.widthFactor > 0.0 ? style.widthFactor : 1.0;
    initial.oblique = style.oblique;
    return initial;
}

namespace {

// Impaginazione di un testo multilinea; con `caret` anche la posizione di ogni carattere (modifica nel disegno).
std::vector<PlacedText> layoutMTextImpl(const Document& doc, const MTextEntity& m, TextCaretGeometry* caret)
{
    if ((m.text.empty() && !caret) || m.height <= 0.0)
        return {};
    const TextStyle& style = doc.textStyleOrStandard(m.style);
    const MTextFormat initial = mtextInitialFormat(doc, m);
    const auto paragraphs = parseMText(QString::fromStdString(m.text), initial);

    // Pezzi di riga: testo con il suo formato e la posizione orizzontale.
    struct Piece {
        PlacedText text;
        MTextFormat format;
        double x = 0.0;
        double width = 0.0;   // larghezza effettiva (con il fattore di larghezza)
    };
    struct CharPos {
        int index;
        double x, w, h;   // inizio e larghezza (con il fattore di larghezza) dall'inizio della riga, altezza
    };
    struct Line {
        std::vector<Piece> pieces;
        double width = 0.0;
        double height = 0.0;   // altezza del testo più alto della riga
        std::vector<CharPos> chars;
        int startIndex = 0;    // primo carattere della riga
        int endIndex = -1;     // ultima riga di un paragrafo: indice dopo l'ultimo carattere
        int align = -1;        // allineamento del paragrafo (\pxq), -1 = quello del punto d'inserimento
    };
    std::vector<Line> lines;
    const double wrap = m.width > 0.0 ? m.width : 0.0;
    int charIndex = 0;
    for (std::size_t pi = 0; pi < paragraphs.size(); ++pi) {
        const auto& paragraph = paragraphs[pi];
        lines.emplace_back();
        lines.back().height = m.height;
        lines.back().startIndex = charIndex;
        int align = -1;
        for (const MTextRun& run : paragraph)
            if (run.format.align >= 0)
                align = run.format.align;
        if (align < 0 && !paragraph.empty())
            align = paragraph.front().format.align;
        lines.back().align = align;
        bool first = true;
        for (const MTextRun& run : paragraph) {
            const ResolvedFont font = fontFor(style, run.format);
            // Parole con lo spazio che le segue: si va a capo fra una parola e l'altra.
            QStringList words;
            QString word;
            for (const QChar ch : run.text) {
                word += ch;
                if (ch == QLatin1Char(' ')) {
                    words << word;
                    word.clear();
                }
            }
            if (!word.isEmpty())
                words << word;
            for (const QString& w : words) {
                PlacedText piece = makeLine(font, w, run.format.height);
                const double width = piece.advance * run.format.width;
                Line& line = lines.back();
                const double trimmed = makeLine(font, w.trimmed(), run.format.height).advance * run.format.width;
                if (wrap > 0.0 && !first && !line.pieces.empty() && line.width + trimmed > wrap * 1.001) {
                    lines.emplace_back();
                    lines.back().height = 0.0;
                    lines.back().startIndex = charIndex;
                    lines.back().align = align;
                }
                Line& target = lines.back();
                if (caret) {   // posizione di ogni carattere della parola
                    const double startX = target.width;
                    double before = 0.0;
                    for (int k = 0; k < w.size(); ++k) {
                        const double after = measure(font, w.left(k + 1), run.format.height).advance * run.format.width;
                        target.chars.push_back({charIndex + k, startX + before, after - before, run.format.height});
                        before = after;
                    }
                }
                charIndex += static_cast<int>(w.size());
                // Pezzi consecutivi con lo stesso formato si uniscono (meno oggetti da disegnare).
                if (!target.pieces.empty() && target.pieces.back().format.sameAs(run.format)) {
                    Piece& last = target.pieces.back();
                    last.text = makeLine(font, last.text.text + w, run.format.height);
                    last.width = last.text.advance * run.format.width;
                } else {
                    target.pieces.push_back({piece, run.format, target.width, width});
                }
                target.width = target.pieces.back().x + target.pieces.back().width;
                target.height = std::max(target.height, run.format.height);
                first = false;
            }
        }
        if (lines.back().height <= 0.0)
            lines.back().height = m.height;
        lines.back().endIndex = charIndex;
        if (pi + 1 < paragraphs.size())
            ++charIndex;   // l'a capo
    }

    double blockWidth = wrap;
    if (blockWidth <= 0.0)
        for (const Line& l : lines)
            blockWidth = std::max(blockWidth, l.width);
    // Colonne: righe divise in parti uguali, ogni colonna riparte dall'alto spostata di larghezza + distanza.
    const int columns = m.columns > 1 && wrap > 0.0 ? m.columns : 1;
    const std::size_t perColumn = std::max<std::size_t>(1, (lines.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));
    std::vector<double> columnX(lines.size(), 0.0);
    // Linee di base: la prima sotto l'alto del blocco (o della colonna), le altre a 5/3 dell'altezza.
    std::vector<double> baselines;
    double y = 0.0;
    double blockHeight = 0.0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t row = columns > 1 ? i % perColumn : i;
        if (columns > 1)
            columnX[i] = static_cast<double>(i / perColumn) * (wrap + m.gutter());
        if (row == 0)
            y = 0.0;
        y -= row == 0 ? lines[i].height : 1.667 * m.lineSpacing * lines[i].height;
        baselines.push_back(y);
        blockHeight = std::max(blockHeight, -y);
    }
    const double columnWidth = blockWidth;
    if (columns > 1)
        blockWidth = columns * wrap + (columns - 1) * m.gutter();
    const int col = (m.attachment - 1) % 3;
    const int row = (m.attachment - 1) / 3;
    const double left = -col * blockWidth / 2.0;
    const double top = row == 0 ? 0.0 : (row == 1 ? blockHeight / 2.0 : blockHeight);
    QTransform base;
    base.translate(m.position.x, m.position.y);
    base.rotate(m.rotation * 180.0 / kShxPi);

    std::vector<PlacedText> out;
    if (caret) {
        // Cornice: tutto il blocco, con un minimo di larghezza per i testi vuoti.
        const double w = std::max(blockWidth, m.height);
        const double bottom = top - blockHeight - 0.4 * m.height;
        caret->frame = base.map(QPolygonF(QRectF(QPointF(left, bottom), QPointF(left + w, top + 0.25 * m.height))));
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        // Spazi finali esclusi dall'allineamento.
        double width = line.width;
        if (!line.pieces.empty() && line.pieces.back().text.text.endsWith(QLatin1Char(' '))) {
            const Piece& last = line.pieces.back();
            const QString trimmed = last.text.text.trimmed();
            width = last.x + (last.text.advance > 0 && !last.text.text.isEmpty()
                                  ? last.width * makeLine(fontFor(style, last.format), trimmed, last.format.height).advance
                                        / last.text.advance
                                  : 0.0);
        }
        const int lineCol = line.align == 1 ? 1 : line.align == 2 ? 2 : line.align >= 0 ? 0 : col;
        const double x0 = left + columnX[i]
                          + (lineCol == 0 ? 0.0 : lineCol == 1 ? (columnWidth - width) / 2.0 : columnWidth - width);
        if (caret) {
            const double y = top + baselines[i];
            const auto stop = [&](int index, double x, double h) {
                caret->stops.push_back({index, base.map(QPointF(x0 + x, y - 0.3 * h)), base.map(QPointF(x0 + x, y + 1.2 * h)),
                                        static_cast<int>(i)});
            };
            if (line.chars.empty())
                stop(line.startIndex, 0.0, line.height);
            for (const CharPos& c : line.chars) {
                if (static_cast<int>(caret->boxes.size()) <= c.index)
                    caret->boxes.resize(static_cast<std::size_t>(c.index) + 1);
                caret->boxes[static_cast<std::size_t>(c.index)]
                    = base.map(QPolygonF(QRectF(QPointF(x0 + c.x, y - 0.3 * c.h), QPointF(x0 + c.x + c.w, y + 1.2 * c.h))));
                stop(c.index, c.x, c.h);
            }
            if (line.endIndex >= 0 && !line.chars.empty())
                stop(line.endIndex, line.chars.back().x + line.chars.back().w, line.chars.back().h);
        }
        for (const Piece& piece : line.pieces) {
            PlacedText p = piece.text;
            p.aci = piece.format.aci;
            p.rgb = piece.format.rgb;
            const QTransform shear(1.0, 0.0, std::tan(piece.format.oblique), 1.0, 0.0, 0.0);   // x += y * tan
            p.toEntity = QTransform::fromScale(piece.format.width, 1.0) * shear
                         * QTransform::fromTranslate(x0 + piece.x, top + baselines[i]) * base;
            // Sottolineato, sopralineato, barrato: per tutta la larghezza del pezzo (spazi compresi).
            const double h = piece.format.height;
            const auto deco = [&](double y) {
                p.decorations.emplace_back(p.toEntity.map(QPointF(0.0, y)), p.toEntity.map(QPointF(p.advance, y)));
            };
            if (piece.format.underline)
                deco(-0.2 * h);
            if (piece.format.overline)
                deco(1.2 * h);
            if (piece.format.strike)
                deco(0.45 * h);
            out.push_back(std::move(p));
        }
    }
    return out;
}

} // namespace

std::vector<PlacedText> layoutMText(const Document& doc, const MTextEntity& m)
{
    return layoutMTextImpl(doc, m, nullptr);
}

TextCaretGeometry caretGeometry(const Document& doc, const MTextEntity& m)
{
    TextCaretGeometry g;
    layoutMTextImpl(doc, m, &g);
    return g;
}

TextCaretGeometry caretGeometry(const Document& doc, const TextEntity& t)
{
    TextCaretGeometry g;
    const QString s = QString::fromStdString(t.displayText());
    const double h = t.height > 0.0 ? t.height : 1.0;
    const ResolvedFont font = FontLibrary::instance().resolve(doc.textStyleOrStandard(t.style));
    QTransform toEntity;
    if (const auto lines = layoutText(doc, t); !lines.empty()) {
        toEntity = lines.front().toEntity;
    } else {   // testo vuoto: dal punto d'inserimento, con la rotazione
        const Vec2 ref = t.hAlign == TextEntity::Left && t.vAlign == TextEntity::Baseline ? t.position : t.alignPoint;
        toEntity.translate(ref.x, ref.y);
        toEntity.rotate(t.effectiveRotation() * 180.0 / kShxPi);
    }
    std::vector<double> xs{0.0};
    for (int k = 1; k <= s.size(); ++k)
        xs.push_back(measure(font, s.left(k), h).advance);
    for (int k = 0; k <= s.size(); ++k)
        g.stops.push_back({k, toEntity.map(QPointF(xs[k], -0.3 * h)), toEntity.map(QPointF(xs[k], 1.2 * h)), 0});
    for (int k = 0; k < s.size(); ++k)
        g.boxes.push_back(toEntity.map(QPolygonF(QRectF(QPointF(xs[k], -0.3 * h), QPointF(xs[k + 1], 1.2 * h)))));
    g.frame = toEntity.map(QPolygonF(QRectF(QPointF(0.0, -0.4 * h), QPointF(std::max(xs.back(), h), 1.3 * h))));
    return g;
}

} // namespace cartesio

namespace cartesio {

QString plainMText(const std::string& text)
{
    QString t = QString::fromStdString(text);
    t.replace(QStringLiteral("\\P"), QStringLiteral(" "));
    t.replace(QStringLiteral("\\~"), QStringLiteral(" "));
    // Codici con argomento fino al punto e virgola: \W0.7; \H2.5x; \fArial|b0; \C1; \Q15; \A1; \S1/2;
    static const QRegularExpression codes(QStringLiteral(R"(\\[ACcFfHhQTWSp][^;]*;)"));
    t.replace(codes, QString());
    // Codici semplici: \L \l \O \o \K \k (sottolineato, sopralineato, barrato).
    static const QRegularExpression toggles(QStringLiteral(R"(\\[LlOoKk])"));
    t.replace(toggles, QString());
    t.remove(QLatin1Char('{')).remove(QLatin1Char('}'));
    return t.simplified();
}

} // namespace cartesio
