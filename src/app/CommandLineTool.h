#pragma once

// CartesioCAD dalla riga di comando del sistema, senza finestra: conversione di disegni e dati in CSV.
//   CartesioCAD --converti <disegno> <uscita.dxf|dwg|pdf> [--r12] [--verifica] [--sovrascrivi]
//   CartesioCAD --converti <cartella> <cartella di uscita> --formato dxf|dxf12|dwg|pdf [--sottocartelle] ...
//   CartesioCAD --csv <disegno> <uscita.csv> [--modo blocchi|conteggio|oggetti] [--solo-modello] [--separatore ,]
// Su Windows il programma è senza console: dal terminale si usa cartesio-cli.exe (accanto a CartesioCAD.exe), che
// lo avvia, ne mostra i messaggi e restituisce il codice di uscita.

#include <QStringList>

namespace cartesio {

// Vero se gli argomenti chiedono un lavoro da riga di comando (--converti, --csv, --aiuto, --versione).
bool isCommandLineTool(const QStringList& arguments);

// Esegue il lavoro (serve un QApplication già creato). Codice di uscita: 0 riuscito, 1 errore, 2 argomenti non
// validi, 3 convertito ma con differenze alla verifica.
int runCommandLineTool(const QStringList& arguments);

} // namespace cartesio
