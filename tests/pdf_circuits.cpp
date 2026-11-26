// Strumento di sviluppo: legge uno schema unifilare in PDF e stampa i circuiti trovati.
//   cartesio_pdfcircuits schema.pdf [quadro]
//   cartesio_pdfcircuits schema.pdf --legenda pianta.dxf [--senza-sigle]
#include "app/PdfWords.h"
#include "core/PointLegend.h"
#include "core/SingleLine.h"
#include "core/Switchboard.h"
#include "core/io/Dxf.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

using namespace cartesio;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("uso: cartesio_pdfcircuits schema.pdf [quadro]\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::ifstream in(argv[1], std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::vector<PdfWord> words;
    std::string error;
    if (!readPdfWords(buffer.str(), words, &error)) {
        std::printf("ERRORE: %s\n", error.c_str());
        return 1;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto circuits = parseSingleLineDiagram(words);
    const auto t2 = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::map<std::string, int> boards;
    std::map<CircuitKind, int> kinds;
    for (const auto& c : circuits) {
        ++boards[c.board];
        ++kinds[c.kind];
    }
    std::printf("parole: %zu (%.0f ms)  circuiti: %zu (%.0f ms)  quadri: %zu  luce: %d  FM: %d  utenze: %d\n",
                words.size(), ms(t0, t1), circuits.size(), ms(t1, t2), boards.size(), kinds[CircuitKind::Lighting],
                kinds[CircuitKind::Power], kinds[CircuitKind::Other]);
    // Legenda delle lettere di una pianta (DXF): --legenda pianta.dxf [--senza-sigle]
    if (argc > 3 && std::string(argv[2]) == "--legenda") {
        Document doc;
        const DxfReadResult read = readDxf(argv[3], doc);
        if (!read.ok) {
            std::printf("ERRORE: %s\n", read.error.c_str());
            return 1;
        }
        CircuitScanOptions options;
        if (argc > 4 && std::string(argv[4]) == "--senza-sigle")
            options.tagLayers = "#NESSUNO#";   // solo il testo della legenda
        for (const LegendMatch& m : interpretLegend(doc, circuits, options)) {
            if (m.points == 0)
                continue;
            std::printf("%s = %s\n   [%s] da %s, punti %d (%d con sigla)%s%s\n", m.letter.c_str(), m.legend.c_str(),
                        m.keywords.c_str(), legendSourceName(m.source), m.points, m.tagged,
                        m.warning.empty() ? "" : "\n   ! ", m.warning.c_str());
            std::map<std::string, int> families;
            for (const auto& code : m.circuits)
                for (const auto& c : circuits)
                    if (c.code == code)
                        ++families[c.description];
            for (const auto& [d, n] : families)
                std::printf("     %3d x %s\n", n, d.c_str());
        }
        return 0;
    }
    // Fronte quadro: --fronte [quadro] [uscita.dxf] [uscita.ifc] [distinta.csv]
    if (argc > 2 && std::string(argv[2]) == "--fronte") {
        const std::string only = argc > 3 ? argv[3] : std::string();
        std::vector<BoardLayout> layouts;
        for (const BoardInput& in : boardsFromCircuits(circuits)) {
            if (!only.empty() && only != "*" && in.board != only)
                continue;
            BoardLayout L = layoutBoard(in, BoardOptions{}, builtinEnclosures());
            std::printf("%-12s app. %2zu  In %6.0f A  %s  col. %d  %4.0fx%4.0fx%3.0f  IP%d forma %s  moduli %5.1f/%5.1f  morsetti %d  %s\n",
                        L.board.c_str(), L.devices.size(), L.incomer,
                        L.mounting == BoardMounting::Floor ? "pav" : L.mounting == BoardMounting::Wall ? "par" : "cas",
                        L.enclosureCount, L.width, L.mounting == BoardMounting::Floor ? 2100.0 : L.enclosure.height,
                        L.depth, L.ip, L.form.c_str(), L.modules, L.modulesInstalled, L.terminals,
                        L.enclosureFound ? L.enclosure.id.c_str() : "NESSUNA");
            for (const auto& w : L.warnings)
                std::printf("   ! %s\n", w.c_str());
            if (!only.empty() && only != "*")
                for (const BoardDevice& d : L.devices)
                    std::printf("   %-16s %-22s %dP %6.0f A  mod %4.1f  %4.0fx%-4.0f %s\n", d.circuit.c_str(),
                                boardDeviceKindName(d.kind), d.poles, d.rated, d.modules, d.width, d.height, d.note.c_str());
            layouts.push_back(std::move(L));
        }
        if (argc > 4) {
            Document doc;
            double x = 0.0;
            for (const BoardLayout& L : layouts)
                if (!L.devices.empty()) {
                    BBox b;
                    doc.commit(drawBoardFront(doc, L, {x, 0.0}, &b));
                    x = b.max.x + 600.0;
                }
            writeDxf(doc, argv[4], nullptr, nullptr, DxfVersion::R2000);
        }
        if (argc > 5)
            std::ofstream(argv[5], std::ios::binary) << boardsToIfc(layouts, "Prova fronte quadro");
        if (argc > 6)
            std::ofstream(argv[6], std::ios::binary) << boardBillOfMaterials(layouts);
        return 0;
    }
    const std::string board = argc > 2 ? argv[2] : std::string();
    for (const auto& c : circuits)
        if (board.empty() ? c.page % 40 == 0 : c.board == board)
            std::printf("  %-16s p.%-3d %-6s | %-40s | %s kW | %s | %s | %s | %s\n", c.code.c_str(), c.page,
                        circuitKindName(c.kind), c.description.c_str(), c.power.c_str(), c.phase.c_str(),
                        c.breaker.c_str(), c.cable.c_str(), c.length.c_str());
    return 0;
}
