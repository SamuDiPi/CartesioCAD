#pragma once

#include "core/Osnap.h"

namespace cartesio {

// Impostazioni degli aiuti al disegno, condivise da tutti i disegni aperti (barra di stato).
struct DraftSettings {
    bool grid = true;             // F7
    bool snap = false;            // F9  snap alla griglia
    bool ortho = false;           // F8
    bool polar = true;            // F10 puntamento polare
    bool osnap = true;            // F3
    bool otrack = true;           // F11 puntamento snap a oggetto (allineamenti dai punti di snap agganciati)
    int osmode = Osnap::Default;  // modalità di snap attive (bit di OSMODE)
    bool dynInput = true;         // F12 input dinamico vicino al cursore
    bool lwDisplay = false;       // spessori di linea visibili a schermo (LWDISPLAY)
    double polarAngleDeg = 45.0;  // incremento del puntamento polare
};

} // namespace cartesio
