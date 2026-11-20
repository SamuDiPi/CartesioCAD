#pragma once

// Tavolozza Canalina (comando CANALINA), disegnata in QML (qml/CanaloPanel.qml): le canaline portacavi del disegno
// con i calcoli e la tavola di CanaloCAD (app/CanaloEngine, il codice della pagina).
//
// Flusso: si disegna la canalina (la sezione L×H e/o il percorso in pianta) e la si collega, oppure se ne crea una
// nuova; si scelgono i cavi dal database (sempre visibile: categorie, ricerca, quantità, terna) o si importano da
// una sezione disegnata o da un file DWG/DXF; selezionando un oggetto della canalina si vede il riepilogo dei
// calcoli. La sezione si può disegnare dai cavi scelti, e la tavola che si inserisce è quella di CanaloCAD.
//
// Lo stato di ogni canalina (il JSON della pagina) è nel documento (Document::canaloTrays), gli oggetti hanno il
// codice (Entity::canaloTray): si salva nel DWG/DXF e si annulla con Ctrl+Z.

#include "core/Canalo.h"
#include "core/Entity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QQuickWidget>
#include <QVariant>

#include <map>

namespace cartesio {

class DrawingWidget;
class PlantPanel;

class CanaloPanel : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(bool hasTray READ hasTray NOTIFY changed)
    Q_PROPERTY(QString trayTitle READ trayTitle NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(QString category READ category NOTIFY dbChanged)
    Q_PROPERTY(QString search READ search NOTIFY dbChanged)
    Q_PROPERTY(QVariantList dbRows READ dbRows NOTIFY dbChanged)
    Q_PROPERTY(int dbSelected READ dbSelected NOTIFY dbChanged)
    Q_PROPERTY(QVariantList cableRows READ cableRows NOTIFY changed)
    Q_PROPERTY(int cableSelected READ cableSelected NOTIFY changed)
    Q_PROPERTY(QVariantList summary READ summary NOTIFY changed)
    Q_PROPERTY(bool ownSelected READ ownSelected NOTIFY dbChanged)

public:
    explicit CanaloPanel(QWidget* parent = nullptr);

    void setDrawing(DrawingWidget* drawing);
    void setPlant(PlantPanel* plant) { plant_ = plant; }
    // Selezione o disegno cambiati: la canalina degli oggetti selezionati.
    void refresh();

    bool hasTray() const { return !trayId_.isEmpty(); }
    QString trayId() const { return trayId_; }
    QString trayTitle() const;
    QString statusText() const { return status_; }
    QVariantList categories() const;
    QString category() const { return category_; }
    QString search() const { return search_; }
    QVariantList dbRows() const;
    int dbSelected() const { return dbSelected_; }
    QVariantList cableRows() const;
    int cableSelected() const { return cableSelected_; }
    QVariantList summary() const;
    bool ownSelected() const;

    // Risultati della canalina corrente come righe di testo (riga di comando, script).
    QStringList reportLines() const;

    // Dal QML (e dai comandi).
    Q_INVOKABLE void setCategory(const QString& key);
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void selectDb(int row);
    // Cavi del database nella canalina corrente (se non c'è, se ne crea una nuova).
    Q_INVOKABLE void addFromDb(int row, int quantity, bool trefoil);
    Q_INVOKABLE void selectCable(int row);
    Q_INVOKABLE void editCable(int row);
    Q_INVOKABLE void removeCable(int row);
    Q_INVOKABLE void clearCables();
    // Canalina nuova, collegata agli oggetti selezionati (la sezione o il percorso in pianta, se ci sono).
    Q_INVOKABLE void newTray();
    // Gli oggetti selezionati entrano nella canalina corrente (es. il percorso in pianta).
    Q_INVOKABLE void linkSelection();
    // Cavi e misure dalla sezione selezionata (cerchi, bordo, sigle), con l'importazione di CanaloCAD.
    Q_INVOKABLE bool importSelection();
    // Da un file DWG o DXF (come "Importa DWG/DXF" di CanaloCAD).
    Q_INVOKABLE void importFile(const QString& path = {});
    Q_INVOKABLE void editTray();
    Q_INVOKABLE void autoSize();
    // Sezione al vero nel disegno (profilo, cavi, bandierine, quote di CanaloCAD): sostituisce quella di prima.
    Q_INVOKABLE void drawSection();
    // Tavola di CanaloCAD inserita come blocco (si sceglie il punto); `at` già dato negli script.
    Q_INVOKABLE void insertSheet();
    void insertSheetAt(Vec2 at);
    Q_INVOKABLE void saveSheet();
    // Libreria personale dei cavi (impostazioni del programma): nuovo (dal cavo scelto), modifica, elimina.
    Q_INVOKABLE void newOwnCable();
    Q_INVOKABLE void editOwnCable();
    Q_INVOKABLE void removeOwnCable();
    Q_INVOKABLE void showGuide();

    // Script: canalina nuova con i cavi del database (codici, quantità dopo *), dimensionata, con la sezione.
    bool composeFromIds(const QString& list, QString* error);

signals:
    void changed();
    void dbChanged();
    void message(const QString& text);
    void guideRequested();

private:
    QJsonObject state() const;            // della canalina corrente (o quello iniziale)
    void store(const QJsonObject& state, const QString& what);   // nel documento, annullabile
    QString createTray(const QJsonObject& state, bool linkSelected);
    void syncOwnCables();
    QJsonArray ownCablesJson() const;
    QJsonObject dbItem(int row) const;
    std::vector<EntityId> trayEntities(const QString& id, bool sectionOnly) const;
    void updateSummary();
    double drawingUnit() const;           // unità di disegno per millimetro

    DrawingWidget* drawing_ = nullptr;
    PlantPanel* plant_ = nullptr;
    QString trayId_;
    QString status_;
    QString category_ = QStringLiteral("bt_multi");
    QString search_;
    int dbSelected_ = -1;
    int cableSelected_ = -1;
    QJsonArray db_;                       // catalogo della pagina più i cavi propri
    std::vector<int> dbFiltered_;
    QJsonObject metrics_;                 // risultati della canalina corrente
    QString metricsFor_;                  // stato a cui si riferiscono
};

} // namespace cartesio
