#pragma once

// Conversione di un disegno (usata da DWGCONVERT e dalla riga di comando del sistema, CartesioCAD --converti):
// lettura del DWG/DXF con i riferimenti esterni, scrittura in DXF 2000, DXF R12, DWG o PDF con gli stessi percorsi
// dei comandi normali, e verifica facoltativa (il file scritto si rilegge e si confronta con l'originale).

#include "app/DraftSettings.h"

#include <QString>
#include <QStringList>

namespace cartesio {

// Stessi valori di MainWindow::ConvertFormat.
enum ConvertFileFormat { ConvertFileDxf2000 = 0, ConvertFileDxfR12 = 1, ConvertFileDwg = 2, ConvertFilePdf = 3 };

struct ConvertFileResult {
    enum Stage { Read, Write, Done };
    Stage stage = Read;           // dove si è fermata: Done = scritto
    QString error;                // lettura o scrittura non riuscita
    int objects = 0;              // oggetti del disegno letto
    bool verified = false;        // riletto e identico
    QString verifyProblem;        // vuoto = nessun problema (o verifica non chiesta)
    QStringList differences;      // differenze trovate dalla verifica (al massimo 8)
    bool ok() const { return stage == Done; }
};

// `inThread`: le letture in un altro thread, con il ciclo degli eventi vivo (finestra che si ridisegna).
ConvertFileResult convertDrawingFile(const QString& source, const QString& destination, int format, bool verify,
                                     const DraftSettings& settings, bool inThread);

// Formato dall'estensione del file di destinazione (dxf -> DXF 2000, dwg, pdf); -1 se non è uno di questi.
int convertFormatFromPath(const QString& path);

} // namespace cartesio
