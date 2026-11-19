#include "app/GpuView.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/ImageLibrary.h"
#include "app/TextRender.h"
#include "app/Theme.h"
#include "core/Document.h"

#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGTexture>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <QSGTextNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QTextLayout>

#include <private/qtriangulator_p.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

namespace cartesio {

namespace {

constexpr std::size_t kMaxBatchVertices = 600000;
constexpr double kGpuPi = 3.14159265358979323846;

GpuVertex vertex(Vec2 p, const QColor& c)
{
    // Il materiale a colori nei vertici vuole i colori premoltiplicati per l'alfa.
    const float a = static_cast<float>(c.alphaF());
    GpuVertex v;
    v.set(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<uchar>(c.red() * a + 0.5f),
          static_cast<uchar>(c.green() * a + 0.5f), static_cast<uchar>(c.blue() * a + 0.5f),
          static_cast<uchar>(c.alpha()));
    return v;
}

QTransform toQt(const Affine& a)
{
    return QTransform(a.m11, a.m12, a.m21, a.m22, a.dx, a.dy);
}


QMatrix4x4 matrixOf(double m11, double m12, double m21, double m22, double dx, double dy)
{
    return QMatrix4x4(static_cast<float>(m11), static_cast<float>(m21), 0.0f, static_cast<float>(dx),
                      static_cast<float>(m12), static_cast<float>(m22), 0.0f, static_cast<float>(dy),
                      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
}


} // namespace

// ================================================================ costruzione delle geometrie

namespace {
int& lastChunksBuilt()
{
    static int value = 0;
    return value;
}
} // namespace

class GpuSceneBuilder {
public:
    explicit GpuSceneBuilder(const CadView& view) : v_(view), doc_(view.doc_) {}

    // Contenuto di uno spazio. `only`: solo questi oggetti (selezione), evidenziati.
    void content(GpuContent& out, int space, bool interactive, const std::vector<int>* frozen,
                 const std::set<EntityId>* only, const BBox* within = nullptr, double annoFactor = 0.0)
    {
        out_ = &out;
        annoFactor_ = annoFactor;
        const BBox ext = doc_.extents(space);
        out.origin = ext.isValid() ? ext.center() : Vec2{};
        extent_ = ext.isValid() ? std::max(std::hypot(ext.width(), ext.height()), 1e-6) : 1000.0;
        BBox all;
        all.expand({-1e300, -1e300});
        all.expand({1e300, 1e300});
        for (const Entity* e : doc_.entitiesInRect(within ? *within : all, 0.0, space)) {
            if (frozen && std::find(frozen->begin(), frozen->end(), e->layer()) != frozen->end())
                continue;
            if (only && !only->count(e->id()))
                continue;
            draw(*e, interactive, only != nullptr);
        }
        out_ = nullptr;
        annoFactor_ = 0.0;
    }

    // Spazio mostrato a blocchi di oggetti consecutivi: i blocchi di `previous` i cui oggetti sono gli stessi (stessi
    // serial, stesso ordine) si riusano, gli altri si costruiscono. Restituisce quanti blocchi sono stati costruiti.
    int chunks(GpuScene& scene, int space, bool interactive, const GpuScene* previous)
    {
        constexpr std::size_t kChunk = 256;
        const BBox ext = doc_.extents(space);
        scene.content.origin = ext.isValid() ? ext.center() : Vec2{};
        extent_ = ext.isValid() ? std::max(std::hypot(ext.width(), ext.height()), 1e-6) : 1000.0;
        BBox all;
        all.expand({-1e300, -1e300});
        all.expand({1e300, 1e300});
        const std::vector<const Entity*> items = doc_.entitiesInRect(all, 0.0, space);

        // Blocchi riusabili, per il serial del loro primo oggetto.
        std::unordered_map<std::uint64_t, std::size_t> firstOf;
        if (previous)
            for (std::size_t k = 0; k < previous->chunks.size(); ++k)
                if (!previous->chunks[k]->serials.empty())
                    firstOf.emplace(previous->chunks[k]->serials.front(), k);
        const auto reusable = [&](std::size_t i) -> std::shared_ptr<const GpuChunk> {
            const auto it = firstOf.find(items[i]->serial());
            if (it == firstOf.end())
                return nullptr;
            const auto& chunk = previous->chunks[it->second];
            if (i + chunk->serials.size() > items.size())
                return nullptr;
            for (std::size_t k = 0; k < chunk->serials.size(); ++k)
                if (items[i + k]->serial() != chunk->serials[k])
                    return nullptr;
            return chunk;
        };
        int built = 0;
        std::vector<const Entity*> pending;
        const auto flush = [&] {
            if (pending.empty())
                return;
            // Un blocco piccolo appena costruito si unisce al precedente se ci sta (niente pulviscolo di blocchi).
            if (!scene.chunks.empty() && scene.chunks.back()->serials.size() + pending.size() <= kChunk
                && scene.chunks.back()->serials.size() < kChunk / 2) {
                std::vector<const Entity*> merged;
                for (std::uint64_t serial : scene.chunks.back()->serials)
                    for (const Entity* e : items)   // raro: si cercano gli oggetti del blocco precedente
                        if (e->serial() == serial) {
                            merged.push_back(e);
                            break;
                        }
                if (merged.size() == scene.chunks.back()->serials.size()) {
                    scene.chunks.pop_back();
                    merged.insert(merged.end(), pending.begin(), pending.end());
                    pending = std::move(merged);
                }
            }
            auto chunk = std::make_shared<GpuChunk>();
            chunk->content.origin = scene.content.origin;
            out_ = &chunk->content;
            for (const Entity* e : pending) {
                chunk->serials.push_back(e->serial());
                draw(*e, interactive, false);
            }
            out_ = nullptr;
            scene.chunks.push_back(std::move(chunk));
            pending.clear();
            ++built;
        };
        for (std::size_t i = 0; i < items.size();) {
            if (auto chunk = reusable(i)) {
                flush();
                i += chunk->serials.size();
                scene.chunks.push_back(std::move(chunk));
                continue;
            }
            pending.push_back(items[i++]);
            if (pending.size() >= kChunk)
                flush();
        }
        flush();
        // Oggetti PUNTO: nelle coordinate locali della scena.
        for (const auto& chunk : scene.chunks)
            for (const auto& [p, c] : chunk->content.points)
                scene.content.points.emplace_back(p + chunk->content.origin - scene.content.origin, c);
        return built;
    }

    // Un oggetto con il suo colore (o evidenziato, o attenuato sui layer bloccati mentre si lavora).
    void draw(const Entity& e, bool interactive, bool highlight)
    {
        // Oggetti annotativi del modello: nascosti se non supportano la scala (senza ANNOALLVISIBLE); nelle finestre
        // dei layout disegnati alla scala della finestra, se la supportano.
        if (e.annotative() && e.space() == 0) {
            const double f = annoFactor_ > 0.0 ? annoFactor_ : doc_.annotationFactor();
            if (!doc_.annotationVisible(e, f))
                return;
            if (annoFactor_ > 0.0 && !sameFactor(e.annoFactor(), f) && doc_.supportsFactor(e, f)) {
                const auto scaled = annotationAt(e, f);
                drawStyled(*scaled, interactive, highlight);
                return;
            }
        }
        drawStyled(e, interactive, highlight);
    }

    void drawStyled(const Entity& e, bool interactive, bool highlight)
    {
        const auto& layers = doc_.layers();
        CadView::Style style;
        style.layer = e.layer();
        style.aci = doc_.displayColor(e);
        const int li = e.layer();
        if (const auto c = !highlight ? v_.compareColor(e) : std::nullopt) {
            style.override = c;   // confronto con un'altra revisione
        } else if (highlight) {
            style.override = QColor(110, 170, 255);
        } else if (interactive && li >= 0 && li < static_cast<int>(layers.size())
                   && layers[static_cast<std::size_t>(li)].locked) {
            const QColor c = v_.aciColor(style.aci);
            style.override = QColor::fromRgbF(c.redF() * 0.5f + 0.1f, c.greenF() * 0.5f + 0.1f, c.blueF() * 0.5f + 0.1f);
        }
        entity(e, style, Affine{}, 0);
    }

public:
    int chunksBuilt = 0;

private:
    using Style = CadView::Style;

    Vec2 local(Vec2 w) const { return w - out_->origin; }
    double annoFactor_ = 0.0;   // scala di annotazione della finestra (0 = modello: la corrente)

    // Batch dove aggiungere un oggetto con ingombro `box` (coordinate locali). Ogni batch è un disegno separato sulla
    // GPU: sono migliaia e costano più dei vertici. Si riusa l'ultimo batch dello stesso tipo se i batch venuti dopo
    // non toccano l'oggetto (disegni che non si toccano si possono scambiare senza differenze visibili); altrimenti
    // se ne apre uno nuovo, in fondo, come prima.
    template <typename Same>
    GpuBatch& placed(const BBox& box, Same&& same, int lookBack = 64)
    {
        auto& b = out_->batches;
        static const bool reorder = !qEnvironmentVariableIsSet("CARTESIO_NOBATCHREORDER");   // prova: ordine di prima
        for (std::size_t i = b.size(); i-- > 0 && lookBack-- > 0;) {
            GpuBatch& c = b[i];
            if (same(c) && c.vertices.size() < kMaxBatchVertices) {
                if (box.isValid())
                    c.box.expand(box);
                else
                    c.box = BBox{};
                return c;
            }
            if (!reorder || !box.isValid() || !c.box.isValid() || overlaps(c.box, box))
                break;
        }
        b.emplace_back();
        b.back().box = box;
        return b.back();
    }

    static bool overlaps(const BBox& a, const BBox& b)
    {
        return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y;
    }

    static bool plain(const GpuBatch& b) { return b.lodSpacing <= 0.0 && b.lodPeriod <= 0.0 && b.imageFile.isEmpty(); }

    GpuBatch& batch(bool triangles, const BBox& box)
    {
        GpuBatch& b = placed(box, [triangles](const GpuBatch& c) { return plain(c) && c.triangles == triangles; });
        b.triangles = triangles;
        return b;
    }

    static BBox boxOf(Vec2 a, Vec2 b)
    {
        BBox box;
        box.expand(a);
        box.expand(b);
        return box;
    }

    void segment(Vec2 a, Vec2 b, const QColor& c)
    {
        if (!clips_.empty()) {
            clipped(a, b, [this, &c](Vec2 p, Vec2 q) {
                auto& v = batch(false, boxOf(local(p), local(q))).vertices;
                v.push_back(vertex(local(p), c));
                v.push_back(vertex(local(q), c));
            });
            return;
        }
        auto& v = batch(false, boxOf(local(a), local(b))).vertices;
        v.push_back(vertex(local(a), c));
        v.push_back(vertex(local(b), c));
    }

    // ------------------------------------------------ ritaglio dei blocchi (XCLIP)
    // Contorni attivi nel disegno (blocchi annidati: tutti); la geometria si taglia prima di andare alla GPU.
    std::vector<std::vector<Vec2>> clips_;

    static bool insidePolygon(const std::vector<Vec2>& poly, Vec2 p)
    {
        bool in = false;
        for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            if ((poly[i].y > p.y) != (poly[j].y > p.y)
                && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
                in = !in;
        return in;
    }

    bool insideClips(Vec2 p) const
    {
        for (const auto& c : clips_)
            if (!insidePolygon(c, p))
                return false;
        return true;
    }

    // Pezzi del segmento dentro tutti i contorni (attenzione: "emit" in Qt è una macro vuota).
    template <typename Sink>
    void clipped(Vec2 a, Vec2 b, Sink&& sink) const
    {
        std::vector<double> ts{0.0, 1.0};
        const Vec2 d = b - a;
        for (const auto& c : clips_)
            for (std::size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
                const Vec2 e = c[i] - c[j];
                const double den = d.x * e.y - d.y * e.x;
                if (std::abs(den) < 1e-300)
                    continue;
                const Vec2 w = c[j] - a;
                const double t = (w.x * e.y - w.y * e.x) / den;
                const double u = (w.x * d.y - w.y * d.x) / den;
                if (t > 0.0 && t < 1.0 && u >= 0.0 && u <= 1.0)
                    ts.push_back(t);
            }
        std::sort(ts.begin(), ts.end());
        for (std::size_t k = 1; k < ts.size(); ++k) {
            if (ts[k] - ts[k - 1] < 1e-12)
                continue;
            if (insideClips(a + d * ((ts[k] + ts[k - 1]) / 2.0)))
                sink(a + d * ts[k - 1], a + d * ts[k]);
        }
    }

    // Archi in spezzate: più lati per gli archi grandi rispetto al disegno.
    void tessellate(const Prim& pr, double scale, std::vector<Vec2>& pts) const
    {
        const double rel = pr.radius * scale / extent_;
        const double perCircle = rel < 0.0005 ? 16.0 : rel < 0.005 ? 32.0 : rel < 0.05 ? 72.0 : 160.0;
        const int n = std::clamp(static_cast<int>(std::ceil(pr.sweep / (2.0 * kGpuPi) * perCircle)), 2, 2048);
        for (int i = 0; i <= n; ++i)
            pts.push_back(pr.pointAt(pr.start + pr.sweep * i / n));
    }

    // Batch per le linee con tipo di linea: tratti e, per quando il motivo è troppo fitto sullo schermo, le
    // stesse linee continue. Le linee di seguito con motivi di lunghezza simile vanno nello stesso batch.
    GpuBatch& linetypeBatch(double period, const BBox& box)
    {
        const double key = std::pow(2.0, std::floor(std::log2(period) * 2.0) / 2.0);
        GpuBatch& b = placed(box, [key](const GpuBatch& c) { return c.lodSpacing <= 0.0 && c.lodPeriod == key && c.imageFile.isEmpty(); });
        b.lodPeriod = key;
        return b;
    }

    // Spezzata in coordinate mondo, con il tipo di linea (motivo lungo `period` unità del mondo).
    void stroke(const std::vector<Vec2>& pts, const Linetype* lt, double k, const QColor& c)
    {
        if (pts.size() < 2)
            return;
        if (!clips_.empty()) {   // ritaglio: i pezzi interni, ognuno con il suo tipo di linea
            std::vector<std::vector<Vec2>> pieces;
            for (std::size_t i = 1; i < pts.size(); ++i)
                clipped(pts[i - 1], pts[i], [&pieces](Vec2 p, Vec2 q) {
                    if (!pieces.empty() && distance(pieces.back().back(), p) < 1e-9)
                        pieces.back().push_back(q);
                    else
                        pieces.push_back({p, q});
                });
            auto saved = std::move(clips_);
            clips_.clear();
            for (const auto& piece : pieces)
                stroke(piece, lt, k, c);
            clips_ = std::move(saved);
            return;
        }
        double total = 0.0;
        for (std::size_t i = 1; i < pts.size(); ++i)
            total += distance(pts[i - 1], pts[i]);
        const double period = lt ? lt->length() * k : 0.0;
        if (!lt || period <= 0.0 || total / period > 4000.0) {
            for (std::size_t i = 1; i < pts.size(); ++i)
                segment(pts[i - 1], pts[i], c);
            return;
        }
        BBox box;
        for (Vec2 p : pts)
            box.expand(local(p));
        GpuBatch& batch = linetypeBatch(period, box);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            batch.solidLines.push_back(vertex(local(pts[i - 1]), c));
            batch.solidLines.push_back(vertex(local(pts[i]), c));
        }
        // Elementi del motivo: tratti (> 0), spazi (< 0), punti (0: un trattino di un ottavo del motivo, visibile).
        std::vector<std::pair<double, bool>> elements;
        for (double d : lt->pattern)
            elements.emplace_back(d == 0.0 ? period * 0.12 : std::abs(d) * k, d >= 0.0);
        std::size_t element = 0;
        double left = elements[0].first;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            Vec2 a = pts[i - 1];
            const Vec2 b = pts[i];
            double len = distance(a, b);
            if (len <= 0.0)
                continue;
            const Vec2 dir = (b - a) / len;
            int guard = 0;
            while (len > 0.0 && ++guard < 1000000) {
                const double step = std::min(left, len);
                const Vec2 next = a + dir * step;
                if (elements[element].second && step > 0.0) {
                    batch.vertices.push_back(vertex(local(a), c));
                    batch.vertices.push_back(vertex(local(next), c));
                }
                a = next;
                len -= step;
                left -= step;
                if (left <= 1e-12) {
                    element = (element + 1) % elements.size();
                    left = elements[element].first;
                }
            }
        }
    }

    void strokePrims(const std::vector<Prim>& prims, const Affine& t, const Linetype* lt, double k, const QColor& c)
    {
        const double scale = std::sqrt(std::abs(t.determinant()));
        std::vector<Vec2> poly;
        const auto flush = [&] {
            if (poly.size() >= 2) {
                for (Vec2& p : poly)
                    p = t.apply(p);
                stroke(poly, lt, k, c);
            }
            poly.clear();
        };
        for (const Prim& pr : prims) {
            const Vec2 start = pr.kind == Prim::Kind::Segment ? pr.a : pr.pointAt(pr.start);
            if (!poly.empty() && distance(poly.back(), start) > 1e-9 * (1.0 + std::abs(start.x) + std::abs(start.y)))
                flush();
            std::vector<Vec2> pts;
            if (pr.kind == Prim::Kind::Segment) {
                pts = {pr.a, pr.b};
            } else {
                tessellate(pr, scale, pts);
            }
            if (poly.empty())
                poly = std::move(pts);
            else
                poly.insert(poly.end(), pts.begin() + 1, pts.end());
        }
        flush();
    }

    // Area piena (pari-dispari), contorni in coordinate mondo.
    void fill(const std::vector<std::vector<Vec2>>& loops, const QColor& c, std::vector<GpuVertex>* target = nullptr)
    {
        if (!clips_.empty()) {   // ritaglio: l'area dentro i contorni
            QPainterPath area;
            area.setFillRule(Qt::OddEvenFill);
            for (const auto& loop : loops) {
                QPolygonF poly;
                for (Vec2 p : loop)
                    poly << QPointF(p.x, p.y);
                area.addPolygon(poly);
            }
            for (const auto& clip : clips_) {
                QPolygonF poly;
                for (Vec2 p : clip)
                    poly << QPointF(p.x, p.y);
                QPainterPath frame;
                frame.addPolygon(poly);
                area = area.intersected(frame);
            }
            std::vector<std::vector<Vec2>> parts;
            for (const QPolygonF& poly : area.toFillPolygons()) {
                std::vector<Vec2> part;
                for (const QPointF& q : poly)
                    part.push_back({q.x(), q.y()});
                parts.push_back(std::move(part));
            }
            auto saved = std::move(clips_);
            clips_.clear();
            if (!parts.empty())
                fill(parts, c, target);
            clips_ = std::move(saved);
            return;
        }
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        BBox box;
        for (const auto& loop : loops) {
            if (loop.size() < 3)
                continue;
            QPolygonF poly;
            for (Vec2 p : loop) {
                const Vec2 l = local(p);
                poly << QPointF(l.x, l.y);
                box.expand(l);
            }
            poly << poly.front();
            path.addPolygon(poly);
        }
        if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
            return;
        // Il triangolatore lavora in punto fisso: si normalizza l'area intorno all'origine.
        const double s = 20000.0 / std::max(box.width(), box.height());
        const QPointF centre(box.center().x, box.center().y);
        QTransform normalize;
        normalize.scale(s, s);
        normalize.translate(-centre.x(), -centre.y());
        const QTriangleSet set = qTriangulate(path, normalize, 1, true);
        const auto at = [&set, s, centre](int i) {
            return Vec2{set.vertices[2 * i] / s + centre.x(), set.vertices[2 * i + 1] / s + centre.y()};
        };
        auto& v = target ? *target : batch(true, box).vertices;
        const int n = set.indices.size();
        const bool wide = set.indices.type() == QVertexIndexVector::UnsignedInt;
        const void* data = set.indices.data();
        for (int i = 0; i < n; ++i) {
            const int index = wide ? static_cast<int>(static_cast<const quint32*>(data)[i])
                                   : static_cast<int>(static_cast<const quint16*>(data)[i]);
            const Vec2 l = at(index);
            v.push_back(vertex(l, c));
        }
    }

    // Linee del motivo di tratteggio ritagliate sui contorni (pari-dispari). Se il motivo è
    // troppo fitto si riempie l'area con una tinta tenue.
    void hatchPattern(const HatchEntity& h, const std::vector<std::vector<Vec2>>& loops, const Affine& t, const QColor& c)
    {
        std::vector<HatchPatternLine> lines = h.patternLines;
        if (lines.empty())
            lines = standardHatchPattern(h.pattern, h.patternScale, h.patternAngle);
        if (lines.empty())
            lines = standardHatchPattern("ANSI31", h.patternScale, h.patternAngle);
        BBox box;
        for (const auto& loop : loops)
            for (Vec2 p : loop)
                box.expand(p);
        if (!box.isValid())
            return;
        const auto dense = [&] {
            QColor faint = c;
            faint.setAlphaF(c.alphaF() * 0.35);
            std::vector<std::vector<Vec2>> world = loops;
            for (auto& loop : world)
                for (Vec2& p : loop)
                    p = t.apply(p);
            fill(world, faint);
        };
        const Vec2 corners[] = {box.min, box.max, {box.min.x, box.max.y}, {box.max.x, box.min.y}};
        std::size_t segments = 0;
        std::vector<std::pair<Vec2, Vec2>> out, solid;   // con i tratti del motivo; continue
        double shortestPeriod = 0.0;
        for (const HatchPatternLine& line : lines) {
            const Vec2 u = Vec2::polar(1.0, line.angle);
            const Vec2 n{-u.y, u.x};
            const double spacing = line.offset.dot(n);
            if (std::abs(spacing) < 1e-12)
                continue;
            double dmin = 1e300, dmax = -1e300;
            for (Vec2 p : corners) {
                dmin = std::min(dmin, p.dot(n));
                dmax = std::max(dmax, p.dot(n));
            }
            const double d0 = line.base.dot(n);
            double k0 = (dmin - d0) / spacing, k1 = (dmax - d0) / spacing;
            if (k0 > k1)
                std::swap(k0, k1);
            const auto first = static_cast<long long>(std::floor(k0));
            const auto last = static_cast<long long>(std::ceil(k1));
            if (last - first > 20000)
                return dense();
            double period = 0.0;
            for (double d : line.dashes)
                period += std::abs(d);
            for (long long k = first; k <= last; ++k) {
                const Vec2 origin = line.base + line.offset * static_cast<double>(k);
                const double dn = origin.dot(n);
                // Intersezioni della retta con i lati dei contorni.
                std::vector<double> ts;
                for (const auto& loop : loops)
                    for (std::size_t i = 0; i < loop.size(); ++i) {
                        const Vec2 p = loop[i], q = loop[(i + 1) % loop.size()];
                        const double dp = p.dot(n) - dn, dq = q.dot(n) - dn;
                        if ((dp > 0.0) == (dq > 0.0))
                            continue;
                        const Vec2 x = p + (q - p) * (dp / (dp - dq));
                        ts.push_back((x - origin).dot(u));
                    }
                if (ts.size() < 2)
                    continue;
                std::sort(ts.begin(), ts.end());
                for (std::size_t i = 0; i + 1 < ts.size(); i += 2) {
                    const double a = ts[i], b = ts[i + 1];
                    solid.emplace_back(origin + u * a, origin + u * b);
                    if (line.dashes.empty() || period <= 0.0) {
                        out.emplace_back(origin + u * a, origin + u * b);
                        ++segments;
                    } else {
                        if (shortestPeriod <= 0.0 || period < shortestPeriod)
                            shortestPeriod = period;
                        double s = std::floor(a / period) * period;
                        while (s < b && segments < 400000) {
                            for (double d : line.dashes) {
                                const double len = std::abs(d);
                                if (d >= 0.0) {
                                    const double from = std::max(s, a);
                                    // Un punto: un trattino di un quinto del motivo, così resta visibile quando i tratti si vedono.
                                    const double to = std::min(s + (d > 0.0 ? len : period * 0.2), b);
                                    if (to > from) {
                                        out.emplace_back(origin + u * from, origin + u * to);
                                        ++segments;
                                    }
                                }
                                s += len;
                                if (s >= b)
                                    break;
                            }
                        }
                    }
                }
                if (segments >= 400000)
                    return dense();
            }
        }
        // Batch a sé: linee del motivo e, per quando sono troppo fitte, l'area a tinta tenue.
        double spacing = 1e300;
        for (const HatchPatternLine& line : lines) {
            const Vec2 u = Vec2::polar(1.0, line.angle);
            const double s = std::abs(line.offset.dot(Vec2{-u.y, u.x}));
            if (s > 1e-12)
                spacing = std::min(spacing, s);
        }
        GpuBatch hatch;
        hatch.lodSpacing = spacing * std::sqrt(std::abs(t.determinant()));
        hatch.lodPeriod = shortestPeriod * std::sqrt(std::abs(t.determinant()));
        hatch.vertices.reserve(out.size() * 2);
        for (const auto& [a, b] : out) {
            hatch.vertices.push_back(vertex(local(t.apply(a)), c));
            hatch.vertices.push_back(vertex(local(t.apply(b)), c));
        }
        if (shortestPeriod > 0.0)
            for (const auto& [a, b] : solid) {
                hatch.solidLines.push_back(vertex(local(t.apply(a)), c));
                hatch.solidLines.push_back(vertex(local(t.apply(b)), c));
            }
        QColor faint = c;
        faint.setAlphaF(c.alphaF() * 0.35);
        std::vector<std::vector<Vec2>> world = loops;
        for (auto& loop : world)
            for (Vec2& p : loop)
                p = t.apply(p);
        fill(world, faint, &hatch.denseFill);
        BBox area;
        for (const auto& loop : world)
            for (Vec2 p : loop)
                area.expand(local(p));
        // Tratteggi con passo e motivo simili (entro un quarto di ottava) nello stesso batch: il livello di dettaglio
        // segue il passo più fitto.
        const auto level = [](double x) { return x > 0.0 ? static_cast<int>(std::floor(std::log2(x) * 4.0)) : -100000; };
        const int spacingLevel = level(hatch.lodSpacing), periodLevel = level(hatch.lodPeriod);
        GpuBatch& target = placed(area, [&](const GpuBatch& b) {
            return b.lodSpacing > 0.0 && b.imageFile.isEmpty() && level(b.lodSpacing) == spacingLevel && level(b.lodPeriod) == periodLevel;
        });
        if (target.lodSpacing <= 0.0) {
            target.lodSpacing = hatch.lodSpacing;
            target.lodPeriod = hatch.lodPeriod;
        } else {
            target.lodSpacing = std::min(target.lodSpacing, hatch.lodSpacing);
            if (hatch.lodPeriod > 0.0)
                target.lodPeriod = std::min(target.lodPeriod, hatch.lodPeriod);
        }
        target.vertices.insert(target.vertices.end(), hatch.vertices.begin(), hatch.vertices.end());
        target.solidLines.insert(target.solidLines.end(), hatch.solidLines.begin(), hatch.solidLines.end());
        target.denseFill.insert(target.denseFill.end(), hatch.denseFill.begin(), hatch.denseFill.end());
    }

    void addText(GpuText&& text)
    {
        out_->texts.push_back(std::move(text));
    }

    // Righe impaginate (TextRender) nelle coordinate locali: tratti SHX, righe TrueType e trattini. I pezzi con un
    // colore proprio (\C dei testi multilinea) diventano testi a sé, se `ownColors`.
    void placed(const std::vector<PlacedText>& lines, double height, const Affine& a, const QColor& color,
                bool ownColors = false)
    {
        if (lines.empty() || height <= 0.0)
            return;
        const QTransform toLocal = toQt(a) * QTransform::fromTranslate(-out_->origin.x, -out_->origin.y);
        std::vector<GpuText> groups;
        const auto groupFor = [&](const QColor& c) -> GpuText& {
            for (GpuText& g : groups)
                if (g.color == c)
                    return g;
            GpuText g;
            g.color = c;
            g.height = height * std::sqrt(std::abs(a.determinant()));
            groups.push_back(std::move(g));
            return groups.back();
        };
        for (const PlacedText& line : lines) {
            QColor c = color;
            if (ownColors && line.rgb >= 0)
                c = v_.visibleOnScreen(QColor::fromRgb(static_cast<QRgb>(line.rgb)));
            else if (ownColors && line.aci >= 1 && line.aci <= 255)
                c = v_.aciColor(line.aci);
            GpuText& g = groupFor(c);
            const auto v = [&toLocal, &c](QPointF p) {
                const QPointF l = toLocal.map(p);
                return vertex({l.x(), l.y()}, c);
            };
            const auto [d0, d1] = line.dash();
            g.dashes.push_back(v(d0));
            g.dashes.push_back(v(d1));
            for (const auto& [a, b] : line.decorations) {   // sottolineato, sopralineato, barrato
                g.strokes.push_back(v(a));
                g.strokes.push_back(v(b));
            }
            if (line.shx) {
                for (const auto& s : line.strokes())
                    for (std::size_t i = 1; i < s.size(); ++i) {
                        g.strokes.push_back(v(s[i - 1]));
                        g.strokes.push_back(v(s[i]));
                    }
            } else {
                g.runs.push_back({line.text, line.font, line.fontToEntity() * toLocal});
            }
        }
        for (GpuText& g : groups)
            addText(std::move(g));
    }

    void text(const TextEntity& t, const Affine& a, const QColor& color)
    {
        if (!clips_.empty() && !insideClips(a.apply(t.position)))
            return;   // ritaglio: i testi restano se il loro punto è dentro
        placed(layoutText(doc_, t), t.height, a, color);
    }

    void mtext(const MTextEntity& m, const Affine& a, const QColor& color, bool ownColors)
    {
        if (!clips_.empty() && !insideClips(a.apply(m.position)))
            return;
        if (m.backgroundFill) {   // maschera: riquadro del colore dello sfondo sotto il testo
            const auto b = m.box();
            const double k = (m.fillScale - 1.0) * m.height;
            const Vec2 dir = Vec2::polar(1.0, m.rotation), up = Vec2::polar(1.0, m.rotation + kGpuPi / 2.0);
            std::vector<Vec2> poly{b[0] - dir * k - up * k, b[1] + dir * k - up * k, b[2] + dir * k + up * k, b[3] - dir * k + up * k};
            for (Vec2& p : poly)
                p = a.apply(p);
            fill({poly}, v_.screenBackground());
        }
        placed(layoutMText(doc_, m), m.height, a, color, ownColors);
    }

    void entity(const Entity& e, const Style& style, const Affine& t, int depth)
    {
        if (depth > 16)
            return;
        const QColor color = v_.resolveColor(e, style);
        const Linetype* lt = style.override ? nullptr : v_.linetypeOf(e, style);
        const double k = doc_.ltscale() * e.linetypeScale() * std::sqrt(std::abs(t.determinant()));

        switch (e.type()) {
        case EntityType::Point:
            if (clips_.empty() || insideClips(t.apply(static_cast<const PointEntity&>(e).position)))
                out_->points.emplace_back(local(t.apply(static_cast<const PointEntity&>(e).position)), color);
            break;
        case EntityType::Solid: {
            std::vector<Vec2> poly;
            for (Vec2 c : static_cast<const SolidEntity&>(e).corners)
                poly.push_back(t.apply(c));
            fill({poly}, color);
            break;
        }
        case EntityType::Hatch: {
            const auto& h = static_cast<const HatchEntity&>(e);
            const auto loops = h.tessellatedLoops();
            if (style.override) {   // evidenziato: solo il contorno
                for (auto loop : loops) {
                    if (loop.empty())
                        continue;
                    loop.push_back(loop.front());
                    for (Vec2& p : loop)
                        p = t.apply(p);
                    stroke(loop, nullptr, 0.0, color);
                }
                break;
            }
            if (h.solid || !h.gradient.empty()) {
                std::vector<std::vector<Vec2>> world = loops;
                for (auto& loop : world)
                    for (Vec2& p : loop)
                        p = t.apply(p);
                fill(world, color);
                break;
            }
            hatchPattern(h, loops, t, color);
            break;
        }
        case EntityType::Text:
            text(static_cast<const TextEntity&>(e), t, color);
            break;
        case EntityType::AttDef:
            if (depth == 0)
                text(static_cast<const AttDefEntity&>(e).tagText(), t, color);
            break;
        case EntityType::MText:
            mtext(static_cast<const MTextEntity&>(e), t, color, !style.override);
            break;
        case EntityType::Insert: {
            const auto& in = static_cast<const InsertEntity&>(e);
            if (!in.block)
                break;
            Style child = style;
            child.layer = e.layer();
            child.aci = style.aci;
            if (const Linetype* blt = v_.linetypeOf(e, style))
                child.blockLinetype = blt->name;
            else
                child.blockLinetype.clear();
            child.blockLineweight = v_.lineweightOf(e, style);
            if (e.color() >= 1 && e.color() <= 255)
                child.aci = e.color();
            else if (e.color() == kColorByLayer || e.color() < 0) {
                const auto& layers = doc_.layers();
                const int l = e.layer() <= 0 ? style.layer : e.layer();
                if (l >= 0 && l < static_cast<int>(layers.size()))
                    child.aci = layers[static_cast<std::size_t>(l)].color;
            }
            const Affine a = t * in.affine();
            if (in.clipped()) {   // XCLIP: il contorno nel disegno
                std::vector<Vec2> frame;
                for (Vec2 v : in.clipPolygon())
                    frame.push_back(a.apply(v));
                clips_.push_back(std::move(frame));
            }
            for (const auto& c : in.block->entities) {
                if (c->type() == EntityType::AttDef)
                    continue;
                if (c->layer() > 0 && !doc_.isLayerVisible(c->layer()))
                    continue;
                entity(*c, child, a, depth + 1);
            }
            if (in.clipped())
                clips_.pop_back();
            for (const Attribute& att : in.attributes) {
                if ((att.flags & AttInvisible) != 0 || (att.text.layer() > 0 && !doc_.isLayerVisible(att.text.layer())))
                    continue;
                text(att.text, t, v_.resolveColor(att.text, child));
            }
            break;
        }
        case EntityType::Dimension:
        case EntityType::Table:
        case EntityType::Leader: {
            Style child = style;
            child.layer = e.layer();
            child.aci = doc_.displayColor(e);
            const auto parts = e.type() == EntityType::Dimension ? static_cast<const DimensionEntity&>(e).geometry()
                               : e.type() == EntityType::Table   ? static_cast<const TableEntity&>(e).geometry()
                                                                  : static_cast<const LeaderEntity&>(e).geometry();
            for (const auto& part : parts)
                entity(*part, child, t, depth + 1);
            break;
        }
        case EntityType::Image: {
            const auto& im = static_cast<const ImageEntity&>(e);
            const QString file = im.show ? ImageLibrary::instance().source(doc_, im) : QString();
            const QImage image = ImageLibrary::instance().image(file);
            if (image.isNull() || style.override) {   // file mancante o evidenziato: la cornice
                strokePrims(e.prims(), t, nullptr, 0.0, color);
                if (image.isNull() && !style.override) {
                    const auto c = im.corners();
                    segment(t.apply(c[0]), t.apply(c[2]), color);
                    segment(t.apply(c[1]), t.apply(c[3]), color);
                }
                break;
            }
            GpuBatch b;
            b.imageFile = file;
            b.imagePixels = QSizeF(im.width, im.height);
            b.imageToLocal = toQt(t * im.pixelToWorld()) * QTransform::fromTranslate(-out_->origin.x, -out_->origin.y);
            b.imageRect = QRectF(0.0, 0.0, im.width, im.height);
            if (im.clipping && im.clip.size() == 2)   // ritaglio rettangolare (quello poligonale non ancora)
                b.imageRect = QRectF(QPointF(im.clip[0].x + 0.5, im.clip[0].y + 0.5), QPointF(im.clip[1].x + 0.5, im.clip[1].y + 0.5))
                                  .normalized()
                                  .intersected(b.imageRect);
            b.imageOpacity = 1.0 - std::clamp(im.fade, 0, 100) / 100.0;
            out_->batches.push_back(std::move(b));
            break;
        }
        case EntityType::Wipeout: {
            const auto& w = static_cast<const WipeoutEntity&>(e);
            std::vector<Vec2> poly;
            for (Vec2 p : w.outline)
                poly.push_back(t.apply(p));
            fill({poly}, v_.screenBackground());
            const int frame = doc_.wipeoutFrame;
            if (frame == 1 || frame == 2 || style.override) {
                if (!poly.empty())
                    poly.push_back(poly.front());
                stroke(poly, nullptr, 0.0, color);
            }
            break;
        }
        case EntityType::XLine: {
            const auto& x = static_cast<const XLineEntity&>(e);
            const double reach = extent_ * 50.0 + distance(x.base, out_->origin);
            const Vec2 a = x.ray ? x.base : x.base - x.direction * reach;
            const Vec2 b = x.base + x.direction * reach;
            segment(t.apply(a), t.apply(b), color);
            break;
        }
        case EntityType::Polyline: {
            // Con spessore: aree piene (una alla volta: tratti e raccordi si sovrappongono) e i tratti
            // senza spessore come linee. Evidenziata: la linea di mezzeria.
            const auto& pl = static_cast<const PolylineEntity&>(e);
            if (!pl.hasWidth() || style.override) {
                strokePrims(e.prims(), t, lt, k, color);
                break;
            }
            for (auto poly : pl.widthPolygons()) {
                for (Vec2& p : poly)
                    p = t.apply(p);
                fill({poly}, color);
            }
            for (const auto& part : pl.thinParts())
                strokePrims(part->prims(), t, lt, k, color);
            break;
        }
        default:
            strokePrims(e.prims(), t, lt, k, color);
            break;
        }
    }

    const CadView& v_;
    const Document& doc_;
    GpuContent* out_ = nullptr;
    double extent_ = 1000.0;
};

GpuScene buildGpuScene(const CadView& view, const GpuScene* previous)
{
    GpuScene scene;
    scene.space = view.shownSpace_;
    scene.paper = view.shownSpace_ != 0;
    scene.chunkKey = view.tableKey();
    const bool savedPaper = view.paperMode_;
    view.paperMode_ = scene.paper;
    GpuSceneBuilder builder(view);
    // Si riusano i blocchi solo se sono cambiati soltanto degli oggetti (tabelle, vista e spazio uguali).
    const bool reuse = previous && previous->chunkKey == scene.chunkKey && previous->space == scene.space;
    builder.chunksBuilt = builder.chunks(scene, view.shownSpace_, view.activeViewport_ == 0, reuse ? previous : nullptr);
    lastChunksBuilt() = builder.chunksBuilt;
    if (scene.paper) {
        for (const Entity* e : view.doc_.entitiesNear({0.0, 0.0}, 1e12, view.shownSpace_)) {
            if (e->type() != EntityType::Viewport)
                continue;
            const auto& vp = static_cast<const ViewportEntity&>(*e);
            GpuViewport g;
            g.id = vp.id();
            g.outline = vp.outline();
            g.modelToPaper = vp.modelToPaper();
            g.scale = std::sqrt(std::abs(g.modelToPaper.determinant()));
            // Solo la parte di modello inquadrata dalla finestra (con un margine per i bordi).
            BBox area = vp.modelRect();
            if (area.isValid()) {
                const double m = std::max(area.width(), area.height()) * 0.02;
                area.expand(area.min - Vec2{m, m});
                area.expand(area.max + Vec2{m, m});
            }
            builder.content(g.model, 0, vp.id() == view.activeViewport_, &vp.frozenLayers, nullptr, area.isValid() ? &area : nullptr,
                            vp.scale() > 0.0 ? 1.0 / vp.scale() : 1.0);
            scene.viewports.push_back(std::move(g));
        }
    }
    view.paperMode_ = savedPaper;
    return scene;
}

GpuContent buildGpuSelection(const CadView& view)
{
    GpuContent out;
    if (view.selection_.empty())
        return out;
    const std::set<EntityId> only(view.selection_.begin(), view.selection_.end());
    const bool savedPaper = view.paperMode_;
    view.paperMode_ = view.shownSpace_ != 0;
    GpuSceneBuilder builder(view);
    // Con una finestra attiva si seleziona nel modello, visto attraverso la finestra.
    builder.content(out, view.activeViewport_ != 0 ? 0 : view.shownSpace_, true, nullptr, &only);
    view.paperMode_ = savedPaper;
    return out;
}

// ================================================================ nodi dello scene graph

namespace {

QSGGeometryNode* lineNode(const std::vector<GpuVertex>& vertices, bool triangles)
{
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), static_cast<int>(vertices.size()));
    geometry->setDrawingMode(triangles ? QSGGeometry::DrawTriangles : QSGGeometry::DrawLines);
    geometry->setLineWidth(1.0f);
    std::memcpy(geometry->vertexData(), vertices.data(), vertices.size() * sizeof(GpuVertex));
    auto* node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    // Niente unione delle geometrie: ogni nodo si disegna con la sua matrice, così cambiando la vista Qt non
    // ritrasforma sulla CPU centinaia di migliaia di vertici. L'unione nei "batch root" era anche la causa dei
    // testi e delle linee che sparivano zoomando (vedi main.cpp).
    auto* material = new QSGVertexColorMaterial;
    material->setFlag(QSGMaterial::RequiresFullMatrix);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// Linee in coordinate dello schermo (griglia, bordo dell'area stampabile, marcatori), rigenerate a ogni vista.
void setScreenLines(QSGGeometryNode* node, const std::vector<GpuVertex>& vertices)
{
    QSGGeometry* g = node->geometry();
    g->allocate(static_cast<int>(vertices.size()));
    if (!vertices.empty())
        std::memcpy(g->vertexData(), vertices.data(), vertices.size() * sizeof(GpuVertex));
    node->markDirty(QSGNode::DirtyGeometry);
}

// Interruttore del livello di dettaglio: il contenuto si stacca e si riattacca (al suo posto, quindi nello stesso
// ordine di disegno). Prima si usava l'opacità 0/1: dopo molti fotogrammi di zoom il motore di Qt non rimetteva in
// disegno i gruppi riaccesi (testi e pezzi di linee sparivano, o nella finestra unica finivano fuori posto).
class LodSwitch final : public QSGNode {
public:
    LodSwitch() : content_(new QSGNode) { appendChildNode(content_); }
    ~LodSwitch() override
    {
        if (!shown_)
            delete content_;   // staccato: non lo elimina il genitore
    }
    void add(QSGNode* node) { content_->appendChildNode(node); }
    void setShown(bool on)
    {
        if (on == shown_)
            return;
        shown_ = on;
        if (on)
            appendChildNode(content_);
        else
            removeChildNode(content_);
    }
    bool shown() const { return shown_; }
    QSGNode* content() const { return content_; }

private:
    QSGNode* content_;
    bool shown_ = true;
};

// Due sottoalberi della scena con la stessa forma (stessi nodi e tipi, geometrie a colori per vertice con lo stesso
// modo di disegno): allora la geometria nuova si copia in quella esistente invece di sostituire i nodi. Aggiungere
// nodi alla scena fa ricostruire a Qt tutte le liste di disegno (circa 20 ms sul Bernardi); cambiare solo i
// vertici ricarica quella geometria. Niente testi TrueType, immagini o altro: per quelli si sostituisce.
bool sameShape(const QSGNode* a, const QSGNode* b)
{
    if (a->type() != b->type())
        return false;
    const auto* la = dynamic_cast<const LodSwitch*>(a);
    const auto* lb = dynamic_cast<const LodSwitch*>(b);
    if ((la == nullptr) != (lb == nullptr))
        return false;
    if (la)
        return sameShape(la->content(), lb->content());
    switch (a->type()) {
    case QSGNode::BasicNodeType:
        break;
    case QSGNode::TransformNodeType:
        if (dynamic_cast<const QSGTextNode*>(a) || dynamic_cast<const QSGTextNode*>(b))
            return false;
        break;
    case QSGNode::GeometryNodeType: {
        const auto* ga = static_cast<const QSGGeometryNode*>(a);
        const auto* gb = static_cast<const QSGGeometryNode*>(b);
        if (!dynamic_cast<const QSGVertexColorMaterial*>(ga->material())
            || !dynamic_cast<const QSGVertexColorMaterial*>(gb->material()) || !ga->geometry() || !gb->geometry()
            || ga->geometry()->drawingMode() != gb->geometry()->drawingMode())
            return false;
        break;
    }
    default:
        return false;
    }
    if (a->childCount() != b->childCount())
        return false;
    for (const QSGNode *x = a->firstChild(), *y = b->firstChild(); x && y; x = x->nextSibling(), y = y->nextSibling())
        if (!sameShape(x, y))
            return false;
    return true;
}

// Copia matrici e vertici di `from` in `into` (stessa forma, vedi sameShape).
void copyShape(QSGNode* into, const QSGNode* from)
{
    if (auto* l = dynamic_cast<LodSwitch*>(into)) {
        copyShape(l->content(), static_cast<const LodSwitch*>(from)->content());
        return;
    }
    if (into->type() == QSGNode::TransformNodeType) {
        auto* t = static_cast<QSGTransformNode*>(into);
        const QMatrix4x4& m = static_cast<const QSGTransformNode*>(from)->matrix();
        if (t->matrix() != m)
            t->setMatrix(m);
    } else if (into->type() == QSGNode::GeometryNodeType) {
        auto* node = static_cast<QSGGeometryNode*>(into);
        const QSGGeometry* src = static_cast<const QSGGeometryNode*>(from)->geometry();
        QSGGeometry* dst = node->geometry();
        const bool sameData = dst->vertexCount() == src->vertexCount()
                              && std::memcmp(dst->vertexData(), src->vertexData(),
                                             static_cast<std::size_t>(src->vertexCount()) * src->sizeOfVertex()) == 0;
        if (!sameData) {
            if (dst->vertexCount() != src->vertexCount())
                dst->allocate(src->vertexCount());
            std::memcpy(dst->vertexData(), src->vertexData(), static_cast<std::size_t>(src->vertexCount()) * src->sizeOfVertex());
            node->markDirty(QSGNode::DirtyGeometry);
        }
    }
    QSGNode* x = into->firstChild();
    const QSGNode* y = from->firstChild();
    for (; x && y; x = x->nextSibling(), y = y->nextSibling())
        copyShape(x, y);
}

// Testi di altezza simile: insieme visibili o insieme sostituiti dai trattini.
struct TextBucket {
    // Stessi livelli di dettaglio (per riusare i nodi di un blocco cambiato: vedi sameShape).
    bool sameLevels(const TextBucket& o) const
    {
        return height == o.height && factor == o.factor && hatch == o.hatch && linetype == o.linetype
               && period == o.period && (solid == nullptr) == (o.solid == nullptr);
    }
    double height = 0.0;          // altezza rappresentativa (unità locali)
    double factor = 1.0;          // pixel per unità locale = scala della vista * factor
    LodSwitch* text = nullptr;
    LodSwitch* dash = nullptr;
    bool hatch = false;           // tratteggio: "text" = linee del motivo, "dash" = tinta tenue
    bool linetype = false;        // linee con tipo di linea: "text" = tratti, "solid" = continue
    double period = 0.0;          // tratteggio: motivo dei tratti (unità locali)
    LodSwitch* solid = nullptr;   // tratteggio: linee continue
};

// Texture delle immagini raster, una per file, condivise fra le immagini e valide finché vive la scena.
struct ImageTextures {
    std::map<QString, QSGTexture*> textures;
    ~ImageTextures()
    {
        for (auto& [file, texture] : textures)
            delete texture;
    }
    QSGTexture* get(QQuickWindow* window, const QString& file)
    {
        if (const auto it = textures.find(file); it != textures.end())
            return it->second;
        const QImage image = ImageLibrary::instance().image(file);
        QSGTexture* texture = image.isNull() ? nullptr : window->createTextureFromImage(image);
        if (texture)
            texture->setFiltering(QSGTexture::Linear);
        textures[file] = texture;
        return texture;
    }
};
class SceneRoot : public QSGNode {
public:
    ImageTextures textures;   // texture delle immagini raster (si eliminano con la scena)
    QSGRectangleNode* background = nullptr;
    QSGRectangleNode* shadow = nullptr;
    QSGRectangleNode* sheet = nullptr;
    QSGGeometryNode* printable = nullptr;
    QSGGeometryNode* grid = nullptr;
    QSGTransformNode* content = nullptr;   // coordinate locali dello spazio -> schermo
    QSGGeometryNode* markers = nullptr;
    std::vector<TextBucket> buckets;
    std::vector<std::pair<EntityId, QSGTransformNode*>> viewportNodes;
    QSGNode* selection = nullptr;
    std::vector<TextBucket> selectionBuckets;
    Vec2 origin;
    std::vector<std::pair<Vec2, QColor>> points;
    // Blocchi della scena: nodo (con la traslazione dalla loro origine a quella della scena) e testi/motivi.
    struct ChunkNode {
        std::shared_ptr<const GpuChunk> chunk;
        QSGTransformNode* node = nullptr;
        std::vector<TextBucket> buckets;
    };
    std::vector<ChunkNode> chunks;
    std::vector<QSGNode*> viewportClips;   // nodi delle finestre del layout (si rifanno a ogni cambiamento)
};



// Immagine raster: nodo con la texture, nella trasformazione dai pixel dell'immagine alle coordinate locali.
QSGNode* imageNode(QQuickWindow* window, ImageTextures& textures, const GpuBatch& b)
{
    QSGTexture* texture = textures.get(window, b.imageFile);
    if (!texture)
        return nullptr;
    auto* transform = new QSGTransformNode;
    transform->setMatrix(QMatrix4x4(b.imageToLocal));
    QSGImageNode* node = window->createImageNode();
    node->setTexture(texture);
    node->setOwnsTexture(false);
    node->setFiltering(QSGTexture::Linear);
    node->setRect(b.imageRect);
    const QSize px = texture->textureSize();
    const double sx = px.width() / std::max(1.0, b.imagePixels.width());
    const double sy = px.height() / std::max(1.0, b.imagePixels.height());
    node->setSourceRect(QRectF(b.imageRect.x() * sx, b.imageRect.y() * sy, b.imageRect.width() * sx, b.imageRect.height() * sy));
    if (b.imageOpacity < 1.0) {
        auto* opacity = new QSGOpacityNode;
        opacity->setOpacity(b.imageOpacity);
        opacity->appendChildNode(node);
        transform->appendChildNode(opacity);
    } else {
        transform->appendChildNode(node);
    }
    return transform;
}
void addContent(QQuickWindow* window, QSGNode* parent, const GpuContent& c, double factor, std::vector<TextBucket>& buckets,
                ImageTextures& textures)
{
    for (const GpuBatch& b : c.batches) {
        if (!b.imageFile.isEmpty()) {
            if (QSGNode* node = imageNode(window, textures, b))
                parent->appendChildNode(node);
            continue;
        }
        if (b.lodSpacing > 0.0) {
            TextBucket hatch;
            hatch.hatch = true;
            hatch.height = b.lodSpacing;
            hatch.factor = factor;
            hatch.text = new LodSwitch;
            hatch.dash = new LodSwitch;
            if (!b.vertices.empty())
                hatch.text->add(lineNode(b.vertices, false));
            if (!b.denseFill.empty())
                hatch.dash->add(lineNode(b.denseFill, true));
            if (!b.solidLines.empty()) {
                hatch.period = b.lodPeriod;
                hatch.solid = new LodSwitch;
                hatch.solid->add(lineNode(b.solidLines, false));
                parent->appendChildNode(hatch.solid);
            }
            parent->appendChildNode(hatch.dash);
            parent->appendChildNode(hatch.text);
            buckets.push_back(hatch);
        } else if (b.lodPeriod > 0.0) {
            TextBucket lt;
            lt.linetype = true;
            lt.period = b.lodPeriod;
            lt.factor = factor;
            lt.text = new LodSwitch;
            lt.dash = new LodSwitch;
            lt.solid = new LodSwitch;
            if (!b.vertices.empty())
                lt.text->add(lineNode(b.vertices, false));
            if (!b.solidLines.empty())
                lt.solid->add(lineNode(b.solidLines, false));
            parent->appendChildNode(lt.solid);
            parent->appendChildNode(lt.text);
            parent->appendChildNode(lt.dash);
            buckets.push_back(lt);
        } else if (!b.vertices.empty()) {
            parent->appendChildNode(lineNode(b.vertices, b.triangles));
        }
    }
    if (c.texts.empty())
        return;
    std::map<int, std::size_t> index;
    std::map<int, std::vector<GpuVertex>> dashes;
    std::map<int, std::vector<GpuVertex>> strokes;   // tratti dei font SHX: una geometria per gruppo
    for (const GpuText& t : c.texts) {
        if (t.height <= 0.0)
            continue;
        const int key = static_cast<int>(std::floor(std::log2(t.height) * 4.0));
        auto it = index.find(key);
        if (it == index.end()) {
            TextBucket b;
            b.height = std::pow(2.0, (key + 0.5) / 4.0);
            b.factor = factor;
            b.text = new LodSwitch;
            b.dash = new LodSwitch;
            parent->appendChildNode(b.dash);
            parent->appendChildNode(b.text);
            it = index.emplace(key, buckets.size()).first;
            buckets.push_back(b);
        }
        for (const GpuText::Run& run : t.runs) {
            QTextLayout layout(run.text, run.font);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            layout.endLayout();
            if (!line.isValid())
                continue;
            auto* transform = new QSGTransformNode;
            transform->setMatrix(QMatrix4x4(run.toLocal));
            QSGTextNode* node = window->createTextNode();
            // Curve: le lettere sono geometria vettoriale, nitide e sempre presenti a ogni zoom (i distance field
            // usano texture di glifi che Qt rigenera zoomando: per qualche fotogramma i testi potevano sparire).
            const QByteArray forced = qgetenv("CARTESIO_TEXT");   // diagnosi: df / curve / native
            const int mode = forced == "df" ? 1 : forced == "curve" ? 0 : forced == "native" ? 2 : appOptions().textRendering;
            node->setRenderType(mode == 1 ? QSGTextNode::QtRendering : mode == 2 ? QSGTextNode::NativeRendering : QSGTextNode::CurveRendering);
            node->setColor(t.color);
            node->addTextLayout(QPointF(0.0, -line.ascent()), &layout);   // linea di base su y = 0
            transform->appendChildNode(node);
            buckets[it->second].text->add(transform);
        }
        auto& s = strokes[key];
        s.insert(s.end(), t.strokes.begin(), t.strokes.end());
        auto& d = dashes[key];
        d.insert(d.end(), t.dashes.begin(), t.dashes.end());
    }
    for (auto& [key, vertices] : dashes)
        if (!vertices.empty())
            buckets[index[key]].dash->add(lineNode(vertices, false));
    for (auto& [key, vertices] : strokes)
        if (!vertices.empty())
            buckets[index[key]].text->add(lineNode(vertices, false));
}

void clearChildren(QSGNode* node)
{
    while (QSGNode* child = node->firstChild()) {
        node->removeChildNode(child);
        delete child;
    }
}

// Matrice dalle coordinate locali di un contenuto del modello (origine `modelOrigin`) a quelle locali del
// foglio (origine `paperOrigin`), attraverso la finestra.
QMatrix4x4 viewportMatrix(const Affine& t, Vec2 modelOrigin, Vec2 paperOrigin)
{
    const double dx = t.m11 * modelOrigin.x + t.m21 * modelOrigin.y + t.dx - paperOrigin.x;
    const double dy = t.m12 * modelOrigin.x + t.m22 * modelOrigin.y + t.dy - paperOrigin.y;
    return matrixOf(t.m11, t.m12, t.m21, t.m22, dx, dy);
}

void updateBuckets(std::vector<TextBucket>& buckets, double scale)
{
    for (TextBucket& b : buckets) {
        const double px = b.height * b.factor * scale;
        // Testi: disegnati da 1 pixel di altezza (si vedono anche minuscoli), sotto un trattino. Prima
        // la soglia era 3 pixel: zoomando indietro i testi piccoli sparivano. Tratteggi: linee se distano almeno 1,2 pixel,
        // altrimenti la tinta tenue. Tipi di linea: sempre le linee (tratti o continue, vedi sotto).
        double text = b.linetype ? 1.0 : b.hatch ? (px >= 1.2 ? 1.0 : 0.0) : (px >= 1.0 ? 1.0 : 0.0);
        const double dash = b.linetype ? 0.0 : b.hatch ? 1.0 - text : (px >= 0.25 && px < 1.0 ? 1.0 : 0.0);
        // Motivi a tratti o punti troppo fitti sullo schermo (tratteggi: 3 pixel; tipi di linea: 4):
        // linee continue.
        double solid = 0.0;
        if (b.solid && text > 0.0 && b.period * b.factor * scale < (b.linetype ? 4.0 : 3.0)) {
            solid = 1.0;
            text = 0.0;
        }
        b.text->setShown(text > 0.5);
        b.dash->setShown(dash > 0.5);
        if (b.solid)
            b.solid->setShown(solid > 0.5);
    }
}

} // namespace

DrawingItem::DrawingItem(CadView& view, QQuickItem* parent, Part part) : QQuickItem(parent), view_(view), part_(part)
{
    setFlag(ItemHasContents, true);
    // Nato già dentro una finestra: l'itemChange del costruttore di QQuickItem non arriva a questa classe.
    if (window())
        watchRendering(window());
}

void DrawingItem::watchRendering(QQuickWindow* window)
{
    if (!window || window == watched_ || part_ == Part::Selection)
        return;
    watched_ = window;
    connect(window, &QQuickWindow::beforeRendering, this, [this] {
        if (measureNext_.exchange(false)) {
            renderTimer_.start();
            measuring_ = true;
        }
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, [this] {
        if (measuring_.exchange(false))
            renderMs_.store(renderTimer_.nsecsElapsed() / 1e6, std::memory_order_relaxed);
    }, Qt::DirectConnection);
}

bool DrawingItem::selectionHere() const
{
    switch (part_) {
    case Part::Full: return true;
    case Part::Scene: return view_.activeViewport() != 0;
    case Part::Selection: return view_.activeViewport() == 0;
    }
    return true;
}

DrawingItem::~DrawingItem() = default;

void DrawingItem::setViewSize(QSizeF view, double margin)
{
    // Margine in multipli di 4 pixel: la texture resta allineata ai pixel dello schermo anche con la scala di
    // Windows al 125%, 150%, 175% (altrimenti a riposo l'immagine sarebbe spostata di una frazione di pixel, sfumata).
    margin_ = margin;
    const double mx = std::round(margin * view.width() / 4.0) * 4.0;
    const double my = std::round(margin * view.height() / 4.0) * 4.0;
    base_ = QPointF(-mx, -my);
    setSize(QSizeF(view.width() + 2.0 * mx, view.height() + 2.0 * my));
    if (scale() == 1.0)
        setPosition(base_);
}

// Tempo del ridisegno vero della scena: dall'inizio alla fine del disegno del fotogramma in cui la scena (texture)
// cambia, sul thread di disegno.
void DrawingItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange)
        watchRendering(data.window);
}

void DrawingItem::refresh()
{
    const bool here = selectionHere();
    if (view_.contentKey() != sceneKey_ || here != selectionWasHere_ || (here && view_.selectionKey() != selectionKey_))
        polish();
    update();
}

void DrawingItem::updatePolish()
{
    const std::uint64_t content = view_.contentKey();
    const bool here = selectionHere();
    const std::uint64_t selection = here ? view_.selectionKey() : 1;
    if (here != selectionWasHere_) {
        selectionWasHere_ = here;
        selectionKey_ = 0;   // la selezione entra o esce da questo item
    }
    if (part_ == Part::Selection) {   // solo la selezione: niente scena
        if (selection != selectionKey_) {
            selection_ = here ? buildGpuSelection(view_) : GpuContent{};
            selectionKey_ = selection;
            selectionChanged_ = true;
        }
        sceneKey_ = content;
        return;
    }
    if (content != sceneKey_) {
        QElapsedTimer timer;
        timer.start();
        scene_ = buildGpuScene(view_, &scene_);
        lastBuildMs_ = timer.nsecsElapsed() / 1e6;
        lastChunksBuilt_ = lastChunksBuilt();
        sceneKey_ = content;
        sceneChanged_ = true;
        selectionKey_ = 0;   // la selezione va riattaccata ai nuovi nodi
    }
    if (selection != selectionKey_) {
        selection_ = here ? buildGpuSelection(view_) : GpuContent{};
        selectionKey_ = selection;
        selectionChanged_ = true;
    }
}

double DrawingItem::rebuildNow()
{
    sceneKey_ = 0;
    updatePolish();
    update();
    return lastBuildMs_;
}

QSGNode* DrawingItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    QQuickWindow* win = window();
    if (!win)
        return old;
    if (part_ != Part::Selection)
        measureNext_ = true;   // la scena cambia (vista o contenuto): si misura quanto costa ridisegnarla
    if (win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        if (!fallbackSent_) {
            fallbackSent_ = true;
            QMetaObject::invokeMethod(this, &DrawingItem::softwareFallback, Qt::QueuedConnection);
        }
        delete old;
        return nullptr;
    }
    auto* root = static_cast<SceneRoot*>(old);
    if (!root && part_ == Part::Selection) {
        root = new SceneRoot;
        root->content = new QSGTransformNode;
        root->appendChildNode(root->content);
        selectionChanged_ = true;
    }
    if (!root) {
        root = new SceneRoot;
        root->background = win->createRectangleNode();
        root->appendChildNode(root->background);
        root->shadow = win->createRectangleNode();
        root->shadow->setColor(QColor(0, 0, 0, 90));
        root->appendChildNode(root->shadow);
        root->sheet = win->createRectangleNode();
        root->sheet->setColor(appOptions().paperColor);
        root->appendChildNode(root->sheet);
        root->printable = lineNode({}, false);
        root->appendChildNode(root->printable);
        root->grid = lineNode({}, false);
        root->appendChildNode(root->grid);
        root->content = new QSGTransformNode;
        root->appendChildNode(root->content);
        root->markers = lineNode({}, false);
        root->appendChildNode(root->markers);
        sceneChanged_ = true;
        selectionChanged_ = true;
    }

    // Contenuto: finestre del layout, poi gli oggetti dello spazio (come renderScene).
    if (sceneChanged_ && part_ == Part::Selection)
        sceneChanged_ = false;
    if (sceneChanged_) {
        // Finestre del layout e selezione si rifanno; i blocchi della scena solo se sono cambiati.
        if (root->selection) {
            root->selection->parent()->removeChildNode(root->selection);
            delete root->selection;
            root->selection = nullptr;
        }
        for (QSGNode* clip : root->viewportClips) {
            root->content->removeChildNode(clip);
            delete clip;
        }
        root->viewportClips.clear();
        root->buckets.clear();
        root->viewportNodes.clear();
        root->origin = scene_.content.origin;
        root->points = scene_.content.points;
        for (const GpuViewport& vp : scene_.viewports) {
            auto* clip = new QSGClipNode;
            // Contorno della finestra (coordinate locali del foglio) come area di ritaglio.
            QPainterPath path;
            QPolygonF poly;
            for (Vec2 p : vp.outline)
                poly << QPointF(p.x - root->origin.x, p.y - root->origin.y);
            const QRectF bounds = poly.boundingRect();
            const bool rect = poly.size() == 4 && std::all_of(poly.begin(), poly.end(), [&bounds](const QPointF& p) {
                                  return (std::abs(p.x() - bounds.left()) < 1e-6 || std::abs(p.x() - bounds.right()) < 1e-6)
                                         && (std::abs(p.y() - bounds.top()) < 1e-6 || std::abs(p.y() - bounds.bottom()) < 1e-6);
                              });
            auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            if (rect) {
                clip->setIsRectangular(true);
                clip->setClipRect(bounds);
                geometry->allocate(4);
                geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
                QSGGeometry::updateRectGeometry(geometry, bounds);
            } else if (poly.size() >= 3) {
                path.addPolygon(poly);
                path.closeSubpath();
                const QTriangleSet set = qTriangulate(path, QTransform(), 1, true);
                const int n = set.indices.size();
                geometry->allocate(n);
                geometry->setDrawingMode(QSGGeometry::DrawTriangles);
                auto* out = geometry->vertexDataAsPoint2D();
                const bool wide = set.indices.type() == QVertexIndexVector::UnsignedInt;
                for (int i = 0; i < n; ++i) {
                    const int idx = wide ? static_cast<int>(static_cast<const quint32*>(set.indices.data())[i])
                                         : static_cast<int>(static_cast<const quint16*>(set.indices.data())[i]);
                    out[i].set(static_cast<float>(set.vertices[2 * idx]), static_cast<float>(set.vertices[2 * idx + 1]));
                }
            }
            clip->setGeometry(geometry);
            clip->setFlag(QSGNode::OwnsGeometry);
            auto* inner = new QSGTransformNode;
            inner->setMatrix(viewportMatrix(vp.modelToPaper, vp.model.origin, root->origin));
            addContent(win, inner, vp.model, vp.scale, root->buckets, root->textures);
            clip->appendChildNode(inner);
            root->viewportClips.push_back(clip);
            root->viewportNodes.emplace_back(vp.id, inner);
        }
        // Blocchi: quelli uguali tengono i loro nodi AL LORO POSTO (staccarli e riattaccarli fa ricostruire a Qt
        // tutte le liste di disegno e ricaricare tutti i vertici: 20-45 ms a ogni modifica sul Bernardi); si
        // staccano solo i blocchi cambiati e i nuovi si inseriscono nella posizione giusta.
        std::unordered_map<const GpuChunk*, std::size_t> old;
        for (std::size_t k = 0; k < root->chunks.size(); ++k)
            old.emplace(root->chunks[k].chunk.get(), k);
        // I riusati devono restare nello stesso ordine relativo; se no (raro) si riattacca tutto.
        bool inOrder = true;
        {
            std::size_t last = 0;
            bool any = false;
            for (const auto& chunk : scene_.chunks)
                if (const auto it = old.find(chunk.get()); it != old.end()) {
                    inOrder = inOrder && (!any || it->second > last);
                    last = it->second;
                    any = true;
                }
        }
        std::set<std::size_t> reusedOld;   // blocchi di prima riusati così come sono
        for (const auto& chunk : scene_.chunks)
            if (const auto it = old.find(chunk.get()); it != old.end())
                reusedOld.insert(it->second);
        std::vector<SceneRoot::ChunkNode> next;
        next.reserve(scene_.chunks.size());
        std::vector<bool> fresh;
        for (const auto& chunk : scene_.chunks) {
            if (const auto it = old.find(chunk.get()); it != old.end()) {
                next.push_back(std::move(root->chunks[it->second]));
                root->chunks[it->second].node = nullptr;
                fresh.push_back(!inOrder);
                continue;
            }
            SceneRoot::ChunkNode n;
            n.chunk = chunk;
            n.node = new QSGTransformNode;
            addContent(win, n.node, chunk->content, 1.0, n.buckets, root->textures);
            // Stesso posto nella lista di prima, blocco non riusato e stessa forma: si copiano i vertici nei suoi
            // nodi (restano nella scena, Qt non ricostruisce tutto).
            const std::size_t k = next.size();
            if (inOrder && k < root->chunks.size() && root->chunks[k].node && !reusedOld.count(k)) {
                SceneRoot::ChunkNode& was = root->chunks[k];
                bool same = was.buckets.size() == n.buckets.size() && sameShape(was.node, n.node);
                for (std::size_t b = 0; same && b < n.buckets.size(); ++b)
                    same = was.buckets[b].sameLevels(n.buckets[b]);
                if (same) {
                    copyShape(was.node, n.node);
                    delete n.node;
                    was.chunk = chunk;
                    next.push_back(std::move(was));
                    was.node = nullptr;
                    fresh.push_back(false);
                    continue;
                }
            }
            next.push_back(std::move(n));
            fresh.push_back(true);
        }
        for (SceneRoot::ChunkNode& n : root->chunks)
            if (n.node) {   // blocchi spariti (nullptr per quelli riusati)
                root->content->removeChildNode(n.node);
                delete n.node;
            }
        if (!inOrder)
            for (SceneRoot::ChunkNode& n : next)
                if (n.node->parent())
                    root->content->removeChildNode(n.node);
        root->chunks = std::move(next);
        // Finestre del layout prima dei blocchi (in testa, nel loro ordine).
        for (auto it = root->viewportClips.rbegin(); it != root->viewportClips.rend(); ++it)
            root->content->prependChildNode(*it);
        QSGNode* prev = root->viewportClips.empty() ? nullptr : root->viewportClips.back();
        for (std::size_t k = 0; k < root->chunks.size(); ++k) {
            SceneRoot::ChunkNode& n = root->chunks[k];
            QMatrix4x4 m;
            m.translate(static_cast<float>(n.chunk->content.origin.x - root->origin.x),
                        static_cast<float>(n.chunk->content.origin.y - root->origin.y));
            if (n.node->matrix() != m)
                n.node->setMatrix(m);
            if (fresh[k]) {
                if (prev)
                    root->content->insertChildNodeAfter(n.node, prev);
                else
                    root->content->prependChildNode(n.node);
            }
            prev = n.node;
        }
        sceneChanged_ = false;
        selectionChanged_ = true;
    }

    // Selezione: sopra il disegno (dentro la finestra attiva, se si lavora attraverso una finestra).
    if (selectionChanged_) {
        if (root->selection) {
            root->selection->parent()->removeChildNode(root->selection);
            delete root->selection;
            root->selection = nullptr;
        }
        root->selectionBuckets.clear();
        if (part_ == Part::Selection)
            root->origin = selection_.origin;   // solo la selezione: la sua origine è quella dell'item
        if (!selection_.empty()) {
            QSGNode* parent = root->content;
            double factor = 1.0;
            QMatrix4x4 matrix;
            const EntityId active = view_.activeViewport();
            const GpuViewport* vp = nullptr;
            for (const GpuViewport& v : scene_.viewports)
                if (v.id == active)
                    vp = &v;
            auto* group = new QSGTransformNode;
            if (vp) {
                for (const auto& [id, node] : root->viewportNodes)
                    if (id == active)
                        parent = node;
                factor = vp->scale;
                // Il contenuto della selezione ha la sua origine: si riporta a quella del modello nella finestra.
                matrix.translate(static_cast<float>(selection_.origin.x - vp->model.origin.x),
                                 static_cast<float>(selection_.origin.y - vp->model.origin.y));
            } else {
                matrix.translate(static_cast<float>(selection_.origin.x - root->origin.x),
                                 static_cast<float>(selection_.origin.y - root->origin.y));
            }
            group->setMatrix(matrix);
            addContent(win, group, selection_, factor, root->selectionBuckets, root->textures);
            parent->appendChildNode(group);
            root->selection = group;
        }
        selectionChanged_ = false;
    }

    // Vista: colori di fondo, foglio, griglia, trasformazione, livello di dettaglio dei testi.
    const double w = width(), h = height();
    const double scale = view_.scale_;
    const Vec2 center = view_.center_;
    if (part_ == Part::Selection) {   // solo la trasformazione della vista e il dettaglio dei testi
        root->content->setMatrix(matrixOf(scale, 0.0, 0.0, -scale, w / 2.0 + (root->origin.x - center.x) * scale,
                                          h / 2.0 - (root->origin.y - center.y) * scale));
        updateBuckets(root->selectionBuckets, scale);
        return root;
    }
    const bool paper = scene_.paper;
    const AppOptions& options = appOptions();
    root->background->setRect(QRectF(0, 0, w, h));
    root->background->setColor(paper ? (options.showPaperBackground ? options.layoutBackground : options.paperColor)
                                     : options.modelBackground);
    root->sheet->setColor(options.paperColor);
    const auto toScreen = [&](Vec2 p) {
        return QPointF(w / 2.0 + (p.x - center.x) * scale, h / 2.0 - (p.y - center.y) * scale);
    };
    const Layout* layout = paper ? view_.doc_.findLayout(scene_.space) : nullptr;
    QRectF sheet;
    if (layout && options.showPaperBackground) {
        const Vec2 o = layout->origin;
        sheet = QRectF(toScreen({o.x, o.y + layout->paperHeight}), toScreen({o.x + layout->paperWidth, o.y})).normalized();
        root->sheet->setRect(sheet);
        root->shadow->setRect(options.showPaperShadow ? sheet.translated(5, 5) : QRectF());
    } else {
        root->sheet->setRect(QRectF());
        root->shadow->setRect(QRectF());
    }
    std::vector<GpuVertex> lines;
    if (layout && options.showPrintableArea) {
        const Vec2 o = layout->origin;
        const double m = layout->margin;
        const QRectF r = QRectF(toScreen({o.x + m, o.y + layout->paperHeight - m}),
                                toScreen({o.x + layout->paperWidth - m, o.y + m})).normalized();
        const QColor gray(150, 150, 150);
        const auto dashed = [&](QPointF a, QPointF b) {
            const double len = QLineF(a, b).length();
            const QPointF d = len > 0 ? (b - a) / len : QPointF();
            for (double s = 0.0; s < len; s += 8.0) {
                const QPointF p = a + d * s, q = a + d * std::min(s + 4.0, len);
                lines.push_back(vertex({p.x(), p.y()}, gray));
                lines.push_back(vertex({q.x(), q.y()}, gray));
            }
        };
        dashed(r.topLeft(), r.topRight());
        dashed(r.topRight(), r.bottomRight());
        dashed(r.bottomRight(), r.bottomLeft());
        dashed(r.bottomLeft(), r.topLeft());
    }
    setScreenLines(root->printable, lines);

    lines.clear();
    if (!paper && view_.settings_.grid) {
        const double g = view_.gridSpacing();
        const Vec2 topLeft{center.x - w / 2.0 / scale, center.y + h / 2.0 / scale};
        const Vec2 bottomRight{center.x + w / 2.0 / scale, center.y - h / 2.0 / scale};
        const auto x0 = static_cast<long long>(std::ceil(topLeft.x / g));
        const auto x1 = static_cast<long long>(std::floor(bottomRight.x / g));
        const auto y0 = static_cast<long long>(std::ceil(bottomRight.y / g));
        const auto y1 = static_cast<long long>(std::floor(topLeft.y / g));
        const bool light = options.modelBackground.lightnessF() > 0.55;
        QColor minor = options.gridColor, major = options.gridColor;
        minor.setAlpha(light ? 30 : 12);
        major.setAlpha(light ? 60 : 30);
        for (long long i = x0; i <= x1 && x1 - x0 < 4000; ++i) {
            const double x = std::round(toScreen({static_cast<double>(i) * g, 0.0}).x()) + 0.5;
            lines.push_back(vertex({x, 0.0}, i % 5 == 0 ? major : minor));
            lines.push_back(vertex({x, h}, i % 5 == 0 ? major : minor));
        }
        for (long long j = y0; j <= y1 && y1 - y0 < 4000; ++j) {
            const double y = std::round(toScreen({0.0, static_cast<double>(j) * g}).y()) + 0.5;
            lines.push_back(vertex({0.0, y}, j % 5 == 0 ? major : minor));
            lines.push_back(vertex({w, y}, j % 5 == 0 ? major : minor));
        }
        const QPointF o = toScreen({0.0, 0.0});
        lines.push_back(vertex({0.0, o.y()}, QColor(200, 60, 60, 100)));
        lines.push_back(vertex({w, o.y()}, QColor(200, 60, 60, 100)));
        lines.push_back(vertex({o.x(), 0.0}, QColor(60, 200, 60, 100)));
        lines.push_back(vertex({o.x(), h}, QColor(60, 200, 60, 100)));
    }
    setScreenLines(root->grid, lines);

    root->content->setMatrix(matrixOf(scale, 0.0, 0.0, -scale, w / 2.0 + (root->origin.x - center.x) * scale,
                                      h / 2.0 - (root->origin.y - center.y) * scale));
    updateBuckets(root->buckets, scale);
    for (SceneRoot::ChunkNode& n : root->chunks)
        updateBuckets(n.buckets, scale);
    updateBuckets(root->selectionBuckets, scale);
    // Oggetti PUNTO: una crocetta di 6 pixel.
    lines.clear();
    for (const auto& [p, c] : root->points) {
        const QPointF s = toScreen(p + root->origin);
        lines.push_back(vertex({s.x() - 3, s.y()}, c));
        lines.push_back(vertex({s.x() + 3, s.y()}, c));
        lines.push_back(vertex({s.x(), s.y() - 3}, c));
        lines.push_back(vertex({s.x(), s.y() + 3}, c));
    }
    setScreenLines(root->markers, lines);
    return root;
}

// ================================================================ livello sopra il disegno

OverlayWidget::OverlayWidget(CadView& view) : QWidget(&view), view_(view)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
}

void OverlayWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (view_.selection_.size() <= CadView::kGripObjectLimit)   // GRIPOBJLIMIT
        for (EntityId id : view_.selection_)
            if (const Entity* e = view_.doc_.find(id))
                view_.drawGrips(p, *e);
    view_.drawOverlays(p);
}

} // namespace cartesio

// ================================================================ immagine fuori schermo (diagnosi)

#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <rhi/qrhi.h>

#include <memory>

namespace cartesio {

QImage renderGpuImage(CadView& view, QSize size, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QImage();
    };
    if (size.isEmpty())
        return fail(QStringLiteral("vista senza dimensioni"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setGeometry(0, 0, size.width(), size.height());
    window.contentItem()->setSize(size);
    auto item = std::make_unique<DrawingItem>(view);
    item->setParentItem(window.contentItem());
    item->setSize(size);
    if (!control.initialize())
        return fail(QStringLiteral("Direct3D 11 non disponibile"));
    QRhi* rhi = control.rhi();
    if (!rhi)
        return fail(QStringLiteral("nessun QRhi"));
    std::unique_ptr<QRhiTexture> texture(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!texture->create())
        return fail(QStringLiteral("texture non creata"));
    std::unique_ptr<QRhiRenderBuffer> depth(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    depth->create();
    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(texture.get())};
    description.setDepthStencilBuffer(depth.get());
    std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget(description));
    std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(pass.get());
    if (!target->create())
        return fail(QStringLiteral("destinazione non creata"));
    window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target.get()));

    item->refresh();
    control.polishItems();
    control.beginFrame();
    control.sync();
    control.render();
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture(QRhiReadbackDescription(texture.get()), &result);
    control.commandBuffer()->resourceUpdate(readback);
    control.endFrame();
    const double buildMs = item->lastBuildMs();
    item.reset();
    if (result.data.isEmpty())
        return fail(QStringLiteral("lettura della texture non riuscita"));
    QImage image(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(),
                 result.pixelSize.height(), QImage::Format_RGBA8888_Premultiplied);
    image = image.copy();
    if (rhi->isYUpInFramebuffer())
        image = image.flipped(Qt::Vertical);
    if (error)
        *error = QStringLiteral("%1, costruzione scena %2 ms").arg(QString::fromLatin1(rhi->backendName())).arg(buildMs, 0, 'f', 1);
    return image;
}

} // namespace cartesio

namespace cartesio {

// Diagnosi: una sola scena GPU (come a schermo) attraverso una sequenza di viste; `step(i)` cambia la vista prima
// del fotogramma i. Serve a trovare i problemi che compaiono solo quando la stessa scena cambia scala più volte.
std::vector<QImage> renderGpuSequence(CadView& view, QSize size, int frames, const std::function<void(int)>& step,
                                      QString* error)
{
    std::vector<QImage> out;
    if (size.isEmpty())
        return out;
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setGeometry(0, 0, size.width(), size.height());
    window.contentItem()->setSize(size);
    auto item = std::make_unique<DrawingItem>(view);
    item->setParentItem(window.contentItem());
    item->setSize(size);
    if (!control.initialize()) {
        if (error)
            *error = QStringLiteral("Direct3D 11 non disponibile");
        return out;
    }
    QRhi* rhi = control.rhi();
    std::unique_ptr<QRhiTexture> texture(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    texture->create();
    std::unique_ptr<QRhiRenderBuffer> depth(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    depth->create();
    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(texture.get())};
    description.setDepthStencilBuffer(depth.get());
    std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget(description));
    std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(pass.get());
    target->create();
    window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target.get()));
    QElapsedTimer timer;
    qint64 renderNs = 0;
    for (int i = 0; i < frames; ++i) {
        step(i);
        timer.start();
        item->refresh();
        control.polishItems();
        control.beginFrame();
        control.sync();
        control.render();
        renderNs += timer.nsecsElapsed();
        if (error)
            *error = QStringLiteral("disegno medio %1 ms per fotogramma").arg(renderNs / 1e6 / (i + 1), 0, 'f', 1);
        QRhiReadbackResult result;
        QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
        readback->readBackTexture(QRhiReadbackDescription(texture.get()), &result);
        control.commandBuffer()->resourceUpdate(readback);
        control.endFrame();
        QImage image(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(),
                     result.pixelSize.height(), QImage::Format_RGBA8888_Premultiplied);
        image = image.copy();
        if (rhi->isYUpInFramebuffer())
            image = image.flipped(Qt::Vertical);
        out.push_back(image);
    }
    item.reset();
    return out;
}

} // namespace cartesio
