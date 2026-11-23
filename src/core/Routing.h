#pragma once

// Percorsi automatici dei circuiti sulle tratte dell'impianto.
//
// Le tratte disegnate (linee e polilinee marcate con il codice della tratta) formano una rete: i tratti si
// collegano dove si toccano gli estremi o dove un estremo cade su un altro tratto (derivazione a T). Due tratti che
// si incrociano senza toccarsi con un estremo NON sono collegati (canaline a quote diverse).
//
// Per ogni circuito: dal quadro (punto di partenza) a ogni utilizzatore con la sua sigla (punti di arrivo). Partenza
// e arrivi si attaccano al punto più vicino della rete, se è entro `maxAccess` (il tratto di attacco conta nella
// lunghezza del cavo ma non appartiene a nessuna tratta). Il percorso è il più breve sulla rete; le tratte usate da
// almeno un arrivo contengono il circuito. La lunghezza per la caduta di tensione è quella dell'arrivo più lontano,
// più `extraPerCircuit` (risalite, discese e scorte).

#include "core/Vec2.h"

#include <string>
#include <vector>

namespace cartesio::plant {

struct RouteSegment {
    Vec2 a, b;
    std::string run;   // codice della tratta
};

struct RouteRequest {
    std::string circuit;
    Vec2 from;                 // quadro
    std::vector<Vec2> to;      // utilizzatori
};

struct RouteOptions {
    double joinTolerance = 1.0;   // unità del disegno: estremi più vicini di così si toccano
    double maxAccess = 5000.0;    // unità del disegno: distanza massima di un punto dalla rete
    double unitsToM = 0.001;      // metri per unità del disegno
    double extraPerCircuit = 0.0; // metri aggiunti alla lunghezza (risalite, discese, scorte)
};

struct RouteResult {
    std::string circuit;
    std::vector<std::string> runs;   // tratte attraversate, senza ripetizioni, in ordine di codice
    double lengthM = 0.0;            // arrivo più lontano, con gli attacchi e l'aggiunta
    int reached = 0;                 // arrivi raggiunti
    int unreached = 0;               // arrivi troppo lontani dalla rete o in una parte non collegata
    bool sourceOnNetwork = false;    // il quadro è abbastanza vicino alla rete
    std::vector<Vec2> unreachedPoints;
};

std::vector<RouteResult> routeCircuits(const std::vector<RouteSegment>& segments, const std::vector<RouteRequest>& requests,
                                       const RouteOptions& options);

} // namespace cartesio::plant
