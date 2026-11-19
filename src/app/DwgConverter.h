#pragma once

#include <QString>

namespace cartesio {

// Conversione DWG <-> DXF con un programma esterno, eseguito come processo separato: un file DWG
// che manda in crash il convertitore non chiude CartesioCAD.
//  - LibreDWG (dwg2dxf, dxf2dwg), distribuito con CartesioCAD nella cartella "libredwg" accanto
//    all'eseguibile, oppure in quella indicata da CARTESIO_LIBREDWG_DIR;
//  - ODA File Converter (o quello indicato da CARTESIO_ODA_PATH), gratuito ma non open source, se
//    installato dall'utente: legge meglio i DWG recenti e scrive DWG fino alla versione 2018.
// Quale usare lo decide l'opzione "Convertitore DWG" (automatico: ODA se c'è).
class DwgConverter {
public:
    static bool available();
    static QString toolsDirectory();
    static bool libreDwgAvailable();
    // Percorso di ODAFileConverter.exe (opzione, oppure ricerca in Program Files\ODA), vuoto se manca.
    static QString odaPath();
    static bool odaAvailable() { return !odaPath().isEmpty(); }
    // Nome del convertitore che verrà usato.
    static QString engineName();

    // Converte un DWG in DXF. `used` riceve il nome del convertitore usato.
    static bool dwgToDxf(const QString& dwgPath, const QString& dxfPath, QString* error, QString* used = nullptr);
    // Converte un DXF in DWG (R2000 con LibreDWG, la versione scelta nelle opzioni con ODA).
    static bool dxfToDwg(const QString& dxfPath, const QString& dwgPath, QString* error, QString* used = nullptr);
    // LibreDWG 0.14 perde cose diverse scrivendo il DXF nella versione del DWG e in R2000 (layout vuoti in un
    // caso, blocchi vuoti nell'altro): le due conversioni insieme, da unire (mergeAlternativeConversion).
    // `r2000Dxf` può mancare se la seconda conversione non riesce.
    static bool libreDwgToDxfPair(const QString& dwgPath, const QString& nativeDxf, const QString& r2000Dxf,
                                  QString* error);
};

} // namespace cartesio
