// Prove con file malformati (fuzzing) dei lettori di file che arrivano da altri: DXF e tabelle di stampa .ctb.
//
//   cartesio_fuzz dxf|ctb <cartella dei file di partenza> <primo caso> <numero di casi> <cartella di lavoro>
//
// Ogni caso è un file di partenza alterato a caso (seme = numero del caso, quindi ripetibile): byte cambiati,
// righe tolte, duplicate o scambiate, numeri estremi, codici di gruppo inventati, file troncato. Prima di leggerlo
// il caso si scrive in <lavoro>/caso.<ext> e il suo numero in <lavoro>/stato.txt: se il programma si ferma
// (errore di memoria, controllo dei limiti, eccezione non gestita) restano il file e il numero per riprodurlo.
// Compilato con -D_GLIBCXX_ASSERTIONS i contenitori della libreria standard controllano gli indici.

#include "core/Document.h"
#include "core/Plot.h"
#include "core/io/Dxf.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace cartesio;
namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeAll(const fs::path& p, const std::string& s)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::vector<std::string> splitLines(const std::string& s)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t end = s.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, end - start + 1));
        start = end + 1;
    }
    return lines;
}

std::string join(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines)
        out += l;
    return out;
}

const char* const kExtremes[] = {"0",   "-1",  "1e308", "-1e308", "nan",        "inf",  "-inf",    "2147483647", "2147483648",
                                  "-2147483649", "99999999999999999999", "1e-320", "",    "   ", "0x7fffffff", "abc", "-0", "1.5"};

// Un caso: il file di partenza alterato con 1-6 operazioni.
std::string mutateText(const std::string& seed, std::mt19937_64& rng)
{
    std::vector<std::string> lines = splitLines(seed);
    auto pick = [&rng](std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(rng() % n); };
    const int ops = 1 + static_cast<int>(rng() % 6);
    for (int k = 0; k < ops && !lines.empty(); ++k) {
        // Tre volte su quattro si lavora su coppie codice-valore intere: il file resta leggibile fino in fondo e il
        // caso arriva al lettore degli oggetti invece di fermarsi alla prima riga fuori posto.
        const bool pairs = rng() % 4 != 0;
        auto even = [pairs](std::size_t n) { return pairs ? n & ~std::size_t(1) : n; };
        auto count = [&](std::size_t max) { return pairs ? 2 * (1 + pick(max / 2)) : 1 + pick(max); };
        const std::size_t i = even(pick(lines.size()));
        const std::size_t value = pairs && i + 1 < lines.size() ? i + 1 : i;   // riga del valore della coppia
        switch (rng() % 9) {
        case 0: {   // byte cambiato
            std::string& l = lines[value];
            if (!l.empty())
                l[pick(l.size())] = static_cast<char>(rng() % 256);
            break;
        }
        case 1:   // righe tolte (anche coppie codice-valore spezzate)
            lines.erase(lines.begin() + static_cast<long>(i), lines.begin() + static_cast<long>(std::min(lines.size(), i + count(4))));
            break;
        case 2: {   // righe duplicate (copiate prima: inserire un pezzo dello stesso vettore non è permesso)
            const std::vector<std::string> copy(lines.begin() + static_cast<long>(i),
                                                lines.begin() + static_cast<long>(std::min(lines.size(), i + count(6))));
            lines.insert(lines.begin() + static_cast<long>(i), copy.begin(), copy.end());
            break;
        }
        case 3:   // valore estremo
            lines[value] = std::string(kExtremes[pick(std::size(kExtremes))]) + "\r\n";
            break;
        case 4:   // righe scambiate
            { const std::size_t j = even(pick(lines.size())); std::swap(lines[i], lines[j]); if (pairs && i + 1 < lines.size() && j + 1 < lines.size()) std::swap(lines[i + 1], lines[j + 1]); }
            break;
        case 5: {   // coppia codice-valore inventata
            const int code = static_cast<int>(rng() % 1072);
            lines.insert(lines.begin() + static_cast<long>(i),
                         {std::to_string(code) + "\r\n", std::string(kExtremes[pick(std::size(kExtremes))]) + "\r\n"});
            break;
        }
        case 6:   // codice di gruppo cambiato
            lines[i] = std::to_string(static_cast<int>(rng() % 1072) - 5) + "\r\n";
            break;
        case 7:   // file troncato
            lines.resize(i);
            break;
        default: {   // blocco di righe spostato altrove
            const std::size_t n = std::min(lines.size() - i, count(40));
            std::vector<std::string> block(lines.begin() + static_cast<long>(i), lines.begin() + static_cast<long>(i + n));
            lines.erase(lines.begin() + static_cast<long>(i), lines.begin() + static_cast<long>(i + n));
            const std::size_t at = even(pick(lines.size() + 1));
            lines.insert(lines.begin() + static_cast<long>(at), block.begin(), block.end());
            break;
        }
        }
    }
    return join(lines);
}

std::string mutateBinary(std::string data, std::mt19937_64& rng)
{
    auto pick = [&rng](std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(rng() % n); };
    const int ops = 1 + static_cast<int>(rng() % 8);
    for (int k = 0; k < ops; ++k) {
        switch (rng() % 5) {
        case 0:
            if (!data.empty())
                data[pick(data.size())] = static_cast<char>(rng() % 256);
            break;
        case 1:
            if (!data.empty())
                data.erase(pick(data.size()), 1 + pick(16));
            break;
        case 2:
            data.insert(pick(data.size() + 1), std::string(1 + pick(16), static_cast<char>(rng() % 256)));
            break;
        case 3:
            data.resize(pick(data.size() + 1));
            break;
        default:   // numeri a 32 bit estremi
            if (data.size() >= 4) {
                const std::size_t at = pick(data.size() - 3);
                const unsigned v = (rng() % 2) ? 0xFFFFFFFFu : 0x7FFFFFFFu;
                for (int b = 0; b < 4; ++b)
                    data[at + b] = static_cast<char>((v >> (8 * b)) & 0xFF);
            }
            break;
        }
    }
    return data;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 6) {
        std::cerr << "uso: cartesio_fuzz dxf|ctb <partenza> <primo> <numero> <lavoro>\n";
        return 2;
    }
    const std::string kind = argv[1];
    const fs::path seeds = argv[2], work = argv[5];
    const unsigned long long first = std::stoull(argv[3]), count = std::stoull(argv[4]);
    fs::create_directories(work);
    struct Seed { bool binary; std::string data; };
    std::vector<Seed> seedData;
    for (const auto& e : fs::directory_iterator(seeds))
        if (e.is_regular_file())
            seedData.push_back({e.path().extension() == ".ctb", readAll(e.path())});
    if (seedData.empty()) {
        std::cerr << "nessun file di partenza\n";
        return 2;
    }
    const fs::path casePath = work / (kind == "dxf" ? "caso.dxf" : "caso.ctb");
    const fs::path state = work / "stato.txt";
    unsigned long long errors = 0;
    for (unsigned long long n = first; n < first + count; ++n) {
        std::mt19937_64 rng(n * 0x9E3779B97F4A7C15ull + 12345);
        const Seed& seed = seedData[rng() % seedData.size()];
        const std::string data = seed.binary ? mutateBinary(seed.data, rng) : mutateText(seed.data, rng);
        writeAll(casePath, data);
        writeAll(state, std::to_string(n) + "\n");
        try {
            if (kind == "dxf") {
                Document doc;
                const DxfReadResult r = readDxf(casePath, doc);
                errors += r.ok ? 0 : 1;
                if (!r.ok && std::getenv("FUZZ_DETTAGLI"))
                    std::cout << n << ": " << r.error << '\n';
            } else {
                // File .ctb (intestazione, decompressione e testo) oppure direttamente il testo della tabella.
                std::string error;
                if (seed.binary) {
                    const auto text = ctbText(data, &error);
                    errors += text && PlotStyleTable::fromText(*text, &error) ? 0 : 1;
                } else
                    errors += PlotStyleTable::fromText(data, &error) ? 0 : 1;
            }
        } catch (const std::exception& ex) {
            std::cout << "ECCEZIONE caso " << n << ": " << ex.what() << "\n";
            return 3;
        }
    }
    std::cout << "ok " << first << ".." << first + count - 1 << " (letture rifiutate con un errore: " << errors << ")\n";
    return 0;
}
