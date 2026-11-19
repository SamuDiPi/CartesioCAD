// Strumento di diagnosi: legge un DXF e stampa cosa è stato importato.
// Uso: cartesio_dxfinfo file.dxf [uscita.dxf]   (con il secondo argomento riscrive il disegno)
#include "core/Document.h"
#include "core/Osnap.h"
#include "core/io/Dxf.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <cstdio>
#include <map>

using namespace cartesio;

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::printf("Uso: cartesio_dxfinfo file.dxf [uscita.dxf]\n");
        return 2;
    }
    Document doc;
    const DxfReadResult r = readDxf(argv[1], doc);
    if (!r.ok) {
        std::printf("ERRORE: %s\n", r.error.c_str());
        return 1;
    }
    // --bench: tempi di ciò che succede a ogni movimento del mouse (evidenziazione e snap).
    if (argc >= 3 && std::string(argv[2]) == "--bench") {
        const BBox ext = doc.extents(0);
        const double tol = std::max(ext.width(), ext.height()) / 1500.0;   // circa 5 pixel a schermo intero
        int hits = 0, snaps = 0;
        const int n = 40;
        const auto t0 = std::chrono::steady_clock::now();
        // Il cursore sopra gli oggetti (caso peggiore): un punto di ogni oggetto.
        std::vector<Vec2> probes;
        for (const auto& e : doc.entities())
            if (e->space() == 0 && !e->snapPoints().empty())
                probes.push_back(e->snapPoints().front().position);
        if (probes.size() > static_cast<std::size_t>(n * n))
            probes.resize(static_cast<std::size_t>(n * n));
        const double ptol = 2.0;   // circa 5 pixel con uno zoom di lavoro
        for (Vec2 p : probes)
            hits += doc.pick(p, ptol) != nullptr;
        const auto t1 = std::chrono::steady_clock::now();
        for (Vec2 p : probes)
            snaps += findSnap(doc.entitiesNear(p, ptol * 2), p, ptol * 2, Osnap::Default).has_value();
        const auto t2 = std::chrono::steady_clock::now();
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        std::printf("pick: %.3f ms per movimento (%d trovati)\nsnap: %.3f ms per movimento (%d trovati)\n",
                    ms(t0, t1) / probes.size(), hits, ms(t1, t2) / probes.size(), snaps);
        return 0;
    }
    static const char* names[] = {"Line", "Circle", "Arc", "Point", "Polyline", "Ellipse", "Spline",
                                   "Text", "MText", "Solid", "Hatch", "Insert", "Dimension", "AttDef", "Viewport", "Leader", "XLine", "Wipeout", "Mline", "Table", "Image"};
    std::map<std::string, int> counts;
    int unlinked = 0;
    std::map<std::string, int> unlinkedNames;
    for (const auto& e : doc.entities()) {
        ++counts[names[static_cast<int>(e->type())]];
        if (e->type() == EntityType::Insert && !static_cast<const InsertEntity&>(*e).block) {
            ++unlinked;
            ++unlinkedNames[static_cast<const InsertEntity&>(*e).blockName];
        }
    }
    std::size_t inBlocks = 0;
    for (const auto& b : doc.blocks())
        inBlocks += b->entities.size();
    std::printf("Oggetti: %d  layer: %zu  blocchi: %zu (%zu oggetti nei blocchi)  inserimenti senza blocco: %d\n",
                r.imported, doc.layers().size(), doc.blocks().size(), inBlocks, unlinked);
    int shown = 0;
    for (const auto& [name, n] : unlinkedNames)
        if (shown++ < 8)
            std::printf("    senza blocco: \"%s\" x%d\n", name.c_str(), n);
    for (const auto& [name, n] : counts)
        std::printf("  %-10s %d\n", name.c_str(), n);
    if (!r.skippedTypes.empty()) {
        std::printf("Ignorati:");
        for (const auto& t : r.skippedTypes)
            std::printf(" %s", t.c_str());
        std::printf("\n");
    }
    const BBox b = doc.extents();
    std::printf("Estensioni: (%g, %g) - (%g, %g)\n", b.min.x, b.min.y, b.max.x, b.max.y);
    // Oggetti più grandi: aiutano a capire estensioni inattese.
    std::vector<std::pair<double, const Entity*>> sizes;
    for (const auto& e : doc.entities()) {
        const BBox eb = e->bounds();
        if (eb.isValid())
            sizes.emplace_back(std::max(eb.width(), eb.height()), e.get());
    }
    std::sort(sizes.begin(), sizes.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    for (std::size_t i = 0; i < std::min<std::size_t>(5, sizes.size()); ++i) {
        const BBox eb = sizes[i].second->bounds();
        std::printf("  grande: %-10s (%g, %g) - (%g, %g)\n", names[static_cast<int>(sizes[i].second->type())],
                    eb.min.x, eb.min.y, eb.max.x, eb.max.y);
        if (sizes[i].second->type() == EntityType::Text) {
            const auto& t = static_cast<const TextEntity&>(*sizes[i].second);
            std::printf("    testo \"%s\" pos (%g, %g) allin. (%g, %g) h %g rot %g h/v %d/%d\n", t.text.c_str(),
                        t.position.x, t.position.y, t.alignPoint.x, t.alignPoint.y, t.height, t.rotation, t.hAlign,
                        t.vAlign);
        }
    }

    if (argc > 2) {
        std::vector<std::string> warnings;
        std::string error;
        const std::string out = argv[2];
        const DxfVersion version = out.find("r12") != std::string::npos ? DxfVersion::R12 : DxfVersion::R2000;
        if (!writeDxf(doc, out, &error, &warnings, version)) {
            std::printf("ERRORE scrittura: %s\n", error.c_str());
            return 1;
        }
        for (const auto& w : warnings)
            std::printf("Avviso: %s\n", w.c_str());
    }
    return 0;
}
