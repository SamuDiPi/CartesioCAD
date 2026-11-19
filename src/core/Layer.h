#pragma once

#include <string>
#include <vector>

namespace cartesio {

// Valori speciali del colore di un oggetto.
constexpr int kColorByBlock = 0;
constexpr int kColorByLayer = 256;

// Spessori di linea (centesimi di mm, codice DXF 370) e valori speciali.
constexpr int kLineweightByLayer = -1;
constexpr int kLineweightByBlock = -2;
constexpr int kLineweightDefault = -3;
constexpr int kLineweightUnset = -9;     // oggetto nuovo: riceverà lo spessore corrente
constexpr int kDefaultLineweight = 25;   // "Default" = 0,25 mm (LWDEFAULT)
// Spessori standard.
inline const std::vector<int>& standardLineweights()
{
    static const std::vector<int> values = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100,
                                            106, 120, 140, 158, 200, 211};
    return values;
}

// Tipo di linea: sequenza di tratti (> 0), spazi (< 0) e punti (0), in unità del disegno.
struct Linetype {
    std::string name;
    std::string description;
    std::vector<double> pattern;   // vuoto = continua

    double length() const
    {
        double total = 0.0;
        for (double d : pattern)
            total += d < 0 ? -d : d;
        return total;
    }
};

// Tipi di linea di CartesioCAD (valori in mm).
const std::vector<Linetype>& standardLinetypes();

struct Layer {
    std::string name;
    int color = 7;        // Indice colore (ACI) 1..255: 1 rosso, 2 giallo, 3 verde, ... 7 bianco/nero
    bool visible = true;  // acceso/spento
    bool frozen = false;  // congelato: non visibile e non selezionabile
    bool locked = false;  // bloccato: visibile ma non selezionabile né modificabile
    std::string linetype = "Continuous";
    int lineweight = kLineweightDefault;
    bool plot = true;     // stampabile
    int trueColor = -1;   // colore RGB (420), -1 = solo ACI
};

} // namespace cartesio

namespace cartesio {

// Stile di testo (tabella STYLE): font SHX o TrueType, larghezza, obliquità.
struct TextStyle {
    std::string name;
    std::string font;          // file del font (codice 3): "romans.shx", "arial.ttf"; può mancare l'estensione
    std::string bigFont;       // font grande per le lingue asiatiche (codice 4)
    std::string family;        // famiglia TrueType (XDATA ACAD 1000), es. "Arial Narrow"
    double height = 0.0;       // altezza fissa (40); 0 = variabile
    double widthFactor = 1.0;  // (41)
    double oblique = 0.0;      // angolo di obliquità in radianti (50)
    bool backwards = false;    // (71) bit 2: testo specchiato
    bool upsideDown = false;   // (71) bit 4: testo capovolto
    bool bold = false;         // XDATA ACAD 1071
    bool italic = false;
    bool annotative = false;   // altezza sulla carta: i testi si scalano con la scala di annotazione
    bool operator==(const TextStyle&) const = default;

    // Font a forma (SHX): estensione .shx, oppure nessuna estensione e nessuna famiglia TrueType.
    bool isShx() const
    {
        std::string f;
        for (char c : font)
            f += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (f.size() > 4 && f.compare(f.size() - 4, 4, ".shx") == 0)
            return true;
        const bool noExtension = f.find('.') == std::string::npos;
        return !f.empty() && noExtension && family.empty();
    }
};

} // namespace cartesio
