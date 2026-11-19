#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace cartesio::theme {

// Palette del tema scuro.
inline const QColor kDrawingBackground{33, 40, 48};
inline const QColor kWindow{59, 68, 83};
inline const QColor kPanel{50, 58, 71};
inline const QColor kPanelDark{40, 46, 57};
inline const QColor kBorder{28, 32, 40};
inline const QColor kText{230, 233, 238};
inline const QColor kTextMuted{154, 163, 176};
inline const QColor kAccent{59, 142, 234};
inline const QColor kIconStroke{214, 220, 230};
inline const QColor kIconYellow{245, 197, 66};
inline const QColor kIconBlue{88, 166, 255};
inline const QColor kIconRed{232, 86, 86};
inline const QColor kIconGreen{92, 196, 110};

// Colori dell'indice colore ACI (1..255). Il 7 è bianco su sfondo scuro.
QColor aciColor(int index);
// Nome del colore: "Rosso", "Giallo", ..., "Colore 34", "DaLayer", "DaBlocco".
QString aciName(int index);

// Applica stile Fusion, palette scura, foglio di stile e carattere.
void apply(QApplication& app);

} // namespace cartesio::theme
