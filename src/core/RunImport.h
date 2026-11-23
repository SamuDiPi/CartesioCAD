#pragma once

// Tratte da layer: le linee e polilinee dei layer delle canalizzazioni diventano tratte dell'impianto.
//
// Le linee si raggruppano per stile (tipo di linea e spessore), che nelle piante distingue i tipi di
// canalizzazione. La legenda del disegno (righe come "CANALINA METALLICA ... 400x75mm" con un campione di linea a
// sinistra) dice tipo e misura di ogni stile. Le linee di uno stile si dividono in rami: un ramo finisce dove
// si incontrano tre o più linee, dove cambia lo stile o dove la linea finisce. Una scritta vicina a un ramo
// ("CANALINA METALLICA 200x75mm") vale per quel ramo più della legenda.

#include "core/Document.h"
#include "core/Plant.h"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cartesio::plant {

// Tipo e misura letti da un testo. width x height per canali e passerelle (mm), tube = diametro (mm, 0 se non
// scritto).
struct RunSpec {
    RunKind kind = RunKind::Duct;
    double width = 0.0, height = 0.0;
    double tube = 0.0;
    std::string text;   // testo d'origine, ripulito
};
// "CANALINA METALLICA 400x75mm", "PASSERELLA 300x60", "TUBO Ø32", "N.4 TUBI %%C125", "TUBAZIONI IN ACCIAIO" (tubo,
// diametro non scritto). Senza parola chiave (canalina, canale, passerella, tubo/tubazioni) niente: "600x60mm" di un
// apparecchio non è una canalizzazione.
std::optional<RunSpec> parseRunText(std::string_view text);

// Stile di una linea: tipo di linea effettivo (quello del layer per "DaLayer"), in maiuscolo, e spessore (polilinee).
struct PathStyle {
    std::string linetype;
    double width = 0.0;
    friend bool operator<(const PathStyle& a, const PathStyle& b)
    {
        return a.linetype != b.linetype ? a.linetype < b.linetype : a.width < b.width - 1e-9;
    }
    friend bool operator==(const PathStyle& a, const PathStyle& b)
    {
        return a.linetype == b.linetype && std::abs(a.width - b.width) < 1e-9;
    }
};
PathStyle styleOf(const Document& doc, const Entity& e);

// Linee, polilinee e archi dello spazio modello sui layer dati (indici), non ancora in una tratta, per stile.
struct StyleGroup {
    PathStyle style;
    std::vector<EntityId> ids;
    double length = 0.0;   // unità del disegno
};
std::vector<StyleGroup> pathStyles(const Document& doc, const std::vector<int>& layers);

// Legenda: per ogni testo con tipo e misura, il campione di linea sulla stessa riga, subito a sinistra.
struct LegendItem {
    PathStyle style;
    RunSpec spec;
    EntityId text = 0;   // il testo della riga
};
std::vector<LegendItem> readRunLegend(const Document& doc);

// Rami delle linee date (stesso stile, tra derivazioni ed estremi) e, se c'è, la scritta vicina con tipo e misura.
struct RunChain {
    PathStyle style;
    std::vector<EntityId> ids;
    std::optional<RunSpec> label;
    double length = 0.0;   // unità del disegno
};
// `tolerance`: estremi più vicini di così si toccano; `labelDistance`: distanza massima di una scritta dal ramo.
// Le scritte della legenda (`legend`) non valgono come scritte di un ramo.
std::vector<RunChain> chainRuns(const Document& doc, const std::vector<EntityId>& ids, double tolerance,
                                double labelDistance, const std::vector<LegendItem>& legend = {});

// Layer con un nome da canalizzazioni (canaline, passerelle, tubi, vie cavi) e almeno una linea.
std::vector<int> suggestRunLayers(const Document& doc);

} // namespace cartesio::plant
