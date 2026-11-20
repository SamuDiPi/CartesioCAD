#pragma once

// Finestra Canalina (comando CANALINA), disegnata in QML (qml/CanaloPanel.qml): le canaline portacavi del disegno con
// i calcoli e la tavola di CanaloCAD (app/CanaloEngine, il codice della pagina). Una finestra a sé, grande e
// spostabile (anche su un altro schermo), con due schede: Cavi (database e cavi della canalina) e Riepilogo.
//
// Flusso: si disegna la canalina (la sezione L×H, anche con i cavi, e/o il percorso in pianta), la si seleziona e
// Usa selezione la collega: le misure della sezione sono quelle disegnate e la sezione di CanaloCAD si disegna al
// suo posto, aggiornata a ogni cambio. Oppure si sceglie direttamente dal database: la canalina nasce dalla lista
// dei cavi con la misura scelta in automatico (riserva voluta).
//
// Lo stato di ogni canalina (il JSON della pagina, più i dati di CartesioCAD "cartesio...") è nel documento
// (Document::canaloTrays); gli oggetti hanno il codice (Entity::canaloTray: "CN1" per il percorso, "CN1/sezione" per
// la sezione). Si salva nel DWG/DXF e si annulla con Ctrl+Z.

#include "core/Entity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QQuickWidget>
#include <QVariant>

namespace cartesio {

class DrawingWidget;
class PlantPanel;

class CanaloPanel : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(bool hasTray READ hasTray NOTIFY changed)
    Q_PROPERTY(QString trayTitle READ trayTitle NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString quickSummary READ quickSummary NOTIFY changed)
    Q_PROPERTY(int quickLevel READ quickLevel NOTIFY changed)
    Q_PROPERTY(bool autoSize READ autoSize NOTIFY changed)
    Q_PROPERTY(double reserve READ reserve NOTIFY changed)
    Q_PROPERTY(bool sectionPlaced READ sectionPlaced NOTIFY changed)
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(QString category READ category NOTIFY dbChanged)
    Q_PROPERTY(QVariantList dbRows READ dbRows NOTIFY dbChanged)
    Q_PROPERTY(int dbSelected READ dbSelected NOTIFY dbChanged)
    Q_PROPERTY(bool ownSelected READ ownSelected NOTIFY dbChanged)
    Q_PROPERTY(QVariantList cableRows READ cableRows NOTIFY changed)
    Q_PROPERTY(int cableSelected READ cableSelected NOTIFY changed)
    Q_PROPERTY(QVariantList summary READ summary NOTIFY changed)
    // Importazione in attesa della polilinea da cliccare nel disegno: il testo della richiesta (vuoto se nessuna).
    Q_PROPERTY(QString pendingText READ pendingText NOTIFY changed)

public:
    explicit CanaloPanel(QWidget* parent = nullptr);

    void setDrawing(DrawingWidget* drawing);
    void setPlant(PlantPanel* plant) { plant_ = plant; }
    // Selezione o disegno cambiati: la canalina degli oggetti selezionati.
    void refresh();
    // Apre (o porta davanti) la finestra.
    void showWindow();

    bool hasTray() const { return !trayId_.isEmpty(); }
    QString trayTitle() const;
    QString statusText() const { return status_; }
    QString quickSummary() const;
    int quickLevel() const;
    bool autoSize() const;
    double reserve() const;
    bool sectionPlaced() const;
    QVariantList categories() const;
    QString category() const { return category_; }
    QVariantList dbRows() const;
    int dbSelected() const { return dbSelected_; }
    bool ownSelected() const;
    QVariantList cableRows() const;
    int cableSelected() const { return cableSelected_; }
    QVariantList summary() const;

    // Risultati della canalina corrente come righe di testo (riga di comando, script).
    QStringList reportLines() const;

    // Dal QML (e dai comandi).
    // Gli oggetti selezionati: una canalina già collegata si mostra; una sezione disegnata (profilo, cerchi, sigle)
    // diventa una canalina nuova con le sue misure; un percorso in pianta si collega alla canalina corrente (o a una
    // nuova).
    Q_INVOKABLE void useSelection();
    Q_INVOKABLE void newTray();
    Q_INVOKABLE void setCategory(const QString& key);
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void selectDb(int row);
    // Cavi del database nella canalina corrente (se non c'è, se ne crea una nuova).
    Q_INVOKABLE void addFromDb(int row, int quantity, bool trefoil);
    Q_INVOKABLE void selectCable(int row);
    Q_INVOKABLE void editCable(int row);
    Q_INVOKABLE void removeCable(int row);
    Q_INVOKABLE void clearCables();
    Q_INVOKABLE void setAutoSize(bool on);
    Q_INVOKABLE void setReserve(double percent);
    // Sezione dalla selezione (anche già collegata); da un file DWG o DXF (come Importa DWG/DXF di CanaloCAD).
    bool importSelection();
    // DWG o DXF di progetto (anche più file: una sezione per file), letti come Importa DWG/DXF di CanaloCAD; per
    // ognuno si chiede a cosa si riferisce: una polilinea del disegno (da cliccare), una canalina, un particolare a
    // sé stante, o solo i calcoli.
    Q_INVOKABLE void importFile(const QString& path = {});
    Q_INVOKABLE void cancelPending();
    // Negli script: dove va ogni file importato, senza finestra ("pick" = la selezione attuale, "detail", "none", o il
    // codice di una canalina).
    void setScriptPlacement(const QString& placement) { scriptPlacement_ = placement; }
    QString pendingText() const { return pendingText_; }
    Q_INVOKABLE void editTray();
    // Sezione di CanaloCAD al vero nel disegno: sulla sezione disegnata, o a destra del disegno.
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

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QJsonObject state() const;   // della canalina corrente (o quello iniziale)
    // Nel documento, annullabile in un passo: misura automatica, poi la sezione disegnata (se c'è) aggiornata.
    void store(QJsonObject state, const QString& what);
    QString createTray(QJsonObject state);
    void tag(const std::vector<EntityId>& ids, const QString& role);
    void syncOwnCables();
    QJsonArray ownCablesJson() const;
    QJsonObject dbItem(int row) const;
    std::vector<EntityId> sectionEntities() const;
    // La sezione di CanaloCAD al punto `cartesioSection` (unità del disegno), al posto di quella di prima.
    std::vector<EntityId> commitSection(const QJsonObject& state);
    void updateSummary();
    double drawingUnit() const;   // unità di disegno per millimetro
    int minimumWidth(const QJsonObject& state) const;   // per i cavi, con la riserva (0 se ci stanno)
    // Importazioni da collocare, una alla volta.
    struct Import {
        QJsonObject state;
        QString file;
    };
    void placeNextImport();
    bool linkPending();   // la polilinea scelta per l'importazione in attesa (dalla selezione)
    void applyImportTo(const QString& id, const QJsonObject& imported);
    double routeLengthM(const std::vector<EntityId>& ids) const;   // lunghezza di linee e polilinee, in metri

    DrawingWidget* drawing_ = nullptr;
    PlantPanel* plant_ = nullptr;
    QString trayId_;
    QString status_;
    QString category_ = QStringLiteral("bt_multi");
    QString search_;
    int dbSelected_ = -1;
    int cableSelected_ = -1;
    QJsonArray db_;   // catalogo della pagina più i cavi propri
    std::vector<int> dbFiltered_;
    QJsonObject metrics_;   // risultati della canalina corrente
    QString metricsFor_;    // stato a cui si riferiscono
    std::vector<Import> imports_;
    Import pending_;
    QString pendingText_;
    QString scriptPlacement_;
};

} // namespace cartesio
