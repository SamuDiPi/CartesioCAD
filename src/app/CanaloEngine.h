#pragma once

// Il codice di CanaloCAD dentro CartesioCAD: i moduli di calcolo, importazione, scena e DXF della pagina
// (plugins/CanaloCAD/engine, generati da scripts/update-canalo.sh) eseguiti con il motore JavaScript di Qt.
// Calcoli e tavola DXF sono quelli della pagina, non una traduzione: la tavola è identica a quella che CanaloCAD
// esporta. Dal browser serve solo la larghezza dei testi in Arial, misurata qui con lo stesso font.
//
// Lo stato di una sezione è il JSON della pagina (misure, tipologia, staffaggio, cavi con le posizioni...), lo
// stesso dei progetti salvati da CanaloCAD.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJSValue>
#include <QString>

#include <memory>

class QJSEngine;

namespace cartesio {

class CanaloEngine {
public:
    // Il motore condiviso (si carica la prima volta; serve un QGuiApplication per misurare i testi).
    static CanaloEngine& instance();
    ~CanaloEngine();

    // Motore caricato; error(): perché no, o l'errore dell'ultima chiamata.
    bool ok() const { return loadError_.isEmpty(); }
    QString error() const;

    // Stato di una sezione nuova.
    QJsonObject defaultState();
    // Database della pagina: "cables" (catalogo dei cavi), "series" (tipologie di canalina), "supports".
    QJsonObject database();
    // Cavi propri (stessi campi del catalogo, id unici): entrano nel database della pagina al posto dei precedenti.
    void setOwnCables(const QJsonArray& cables);

    // Le funzioni che seguono restituiscono lo stato normalizzato e ricalcolato (posizioni dei cavi) e in `metrics` i
    // risultati della pagina (riempimento, Kr, segregazione, carico, freccia, tiro, distinta...); stato vuoto con
    // error() se il calcolo non riesce.
    QJsonObject calculate(const QJsonObject& state, QJsonObject* metrics = nullptr);
    // Cavi dal database come il "+" della pagina: sigla, fasi L1/L2/L3, terne a trifoglio.
    QJsonObject addCable(const QJsonObject& state, const QString& id, int quantity, bool trefoil, QJsonObject* metrics = nullptr);
    // Larghezza per la riserva voluta (%), come "Dimensiona" della pagina; per le tubazioni numero e diametro dei
    // tubi proposti.
    QJsonObject autoSize(const QJsonObject& state, double reservePct, QJsonObject* metrics = nullptr);
    // Tubazioni: diametro (e numero di tubi, se `keepCount` è falso) proposti per i cavi; `found` falso se nessuna
    // combinazione del tipo di tubo basta (lo stato resta quello dato, ricalcolato).
    QJsonObject proposeConduit(const QJsonObject& state, bool keepCount, bool* found = nullptr, QJsonObject* metrics = nullptr);
    // Importazione di una sezione disegnata (testo DXF) come "Importa DWG/DXF" della pagina.
    QJsonObject importDxf(const QJsonObject& state, const QString& dxfText, const QString& fileName, QJsonObject* metrics = nullptr);
    // Un file DXF (i suoi byte) come "Importa DWG/DXF" della pagina: stessi passi (extractRawAscii, parseDXFTextContent).
    QJsonObject importFileBytes(const QJsonObject& state, const QByteArray& bytes, const QString& fileName, QJsonObject* metrics = nullptr);
    // Scala della tavola ("1:5") e dei testi (1,2) come le scelte della pagina (la scala va anche nel cartiglio).
    QJsonObject setScales(const QJsonObject& state, const QString& scale, double textScale, QJsonObject* metrics = nullptr);
    // Tavola DXF della sezione, testo come quello scaricato dalla pagina (righe CRLF, caratteri fino a U+00FF).
    QString dxf(const QJsonObject& state, const QString& title);
    // Gli oggetti della tavola sui layer indicati come li scrive l'export DXF della pagina, senza il testo DXF (più
    // veloce): "entities" (t line/circle/text/hatch, l layer, p coordinate, c ACI, tc colore RGB...), "layers".
    QJsonObject sectionEntities(const QJsonObject& state, const QStringList& layers);
    // Lo stesso in byte Windows-1252 / Latin-1, come il file della pagina.
    QByteArray dxfBytes(const QJsonObject& state, const QString& title);
    // Diagnosi: valuta un'espressione nel contesto della pagina (testo del risultato).
    QString evaluate(const QString& expression);

private:
    CanaloEngine();
    QString call(const QString& function, const QJSValueList& args);
    QJsonObject stateCall(const QString& function, const QJSValueList& args, QJsonObject* metrics);

    std::unique_ptr<QJSEngine> js_;
    QString loadError_, lastError_;
};

} // namespace cartesio
