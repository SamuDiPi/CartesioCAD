#pragma once

// Tavolozza Impianto (comando IMPIANTO), disegnata in QML (qml/PlantPanel.qml): il file di progetto (.cdb)
// collegato al disegno, con i circuiti (dallo schema unifilare in PDF o scritti a mano), le tratte (tubi,
// canali, passerelle disegnati come linee del percorso) e i cavi propri, e le verifiche delle tratte.

#include "core/Entity.h"
#include "core/Plant.h"
#include "core/Routing.h"

#include <set>
#include "core/SingleLine.h"

#include <QQuickWidget>
#include <QVariant>

#include <map>
#include <memory>

namespace cartesio {

class DrawingWidget;

class PlantPanel : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString projectName READ projectName NOTIFY changed)
    Q_PROPERTY(QString projectFile READ projectFile NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY tableChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY changed)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY tableChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY tableChanged)

public:
    explicit PlantPanel(QWidget* parent = nullptr);

    void setDrawing(DrawingWidget* drawing);
    void refresh();

    // Progetto: nuovo (collegato al disegno corrente), apertura, importazione dello schema.
    bool createProject(const QString& path, QString* error);
    bool openProject(const QString& path, QString* error);
    bool importSchema(const QString& path, QString* error);
    // Circuito scritto a mano o modificato (stessa sigla = sostituzione).
    void putCircuit(const plant::Circuit& circuit);
    // Nuova tratta con gli oggetti selezionati (linee e polilinee del percorso); restituisce il codice.
    QString newRun(plant::Run run, QString* error);
    // Verifica di tutte le tratte e dei circuiti: righe di testo per la riga di comando.
    QStringList checkAll();
    // Percorsi automatici (PlantRoutes.cpp): calcolo dal disegno e applicazione al progetto.
    struct RouteReport {
        std::vector<plant::RouteResult> results;
        std::set<std::string> runsInDrawing;
        QStringList missingBoards;
        QStringList lines;
        int circuitsWithTags = 0;
    };
    RouteReport computeRoutes(double maxAccessM, double extraM) const;
    int applyRoutes(const RouteReport& report);
    // Tratte da layer (PlantRunImport.cpp). `choices`: per stile ("TIPOLINEA|spessore") il codice di tipo e misura
    // ("" = ignora); gli stili non elencati prendono la legenda se `useLegend`.
    struct LayerImport {
        int runs = 0;
        int fromLabels = 0;
        int tooShort = 0;   // rami sotto la lunghezza minima, scartati
        std::map<QString, int> byKind;
    };
    LayerImport importRunsFromLayers(const QStringList& layers, const std::map<std::string, std::string>& choices, bool useLegend);
    // Unità reali del disegno per l'impianto (metri per unità, 0 = quelle del file).
    void setDrawingUnits(double metersPerUnit);
    // Segna gli oggetti selezionati come quadro `name` (partenza dei suoi circuiti).
    bool tagBoard(const QString& name, QString* error);
    // Etichette delle tratte nel disegno corrente (sostituiscono quelle di prima): quante.
    int writeLabels();
    // Sezione della tratta disegnata accanto al suo percorso (sostituisce quella di prima); false con il motivo.
    bool drawSection(const QString& runId, QString* error);
    // Distinta materiali dalle tratte di questo disegno: salva il CSV (se `csvPath` non è vuoto) e/o inserisce la
    // tabella nel disegno; restituisce le voci (vuoto con `error` se non c'è niente da contare).
    std::vector<plant::BomItem> bom(const QString& csvPath, bool table, QString* error);
    // Circuiti del progetto come uno schema (per la tavolozza Circuiti).
    std::vector<cartesio::Circuit> asSchema() const;
    // Circuiti letti dalla tavolozza Circuiti: entrano nel progetto aperto.
    void mergeFromSchema(const std::vector<cartesio::Circuit>& schema, const QString& path);
    const plant::Project& project() const { return project_; }
    // Finestra di un cavo proprio (codice, tipo, formazione, diametro, peso, sezione, portata, sistema): usata anche
    // dalla libreria personale della Sezione da catalogo. Imposta id "CST_" e "Cavo del progetto".
    static bool editCableType(canalo::CableType& t, bool isNew, QWidget* parent);
    int verifyCount() const { return verifyCount_; }

    QString statusText() const { return status_; }
    QString projectName() const;
    QString projectFile() const;
    QString summary() const { return summary_; }
    bool hasProject() const { return !path_.isEmpty(); }
    int currentTab() const { return tab_; }
    QVariantList rows() const { return rows_; }
    int selectedRow() const { return selected_; }

    // Dal QML.
    Q_INVOKABLE void newProjectDialog();
    Q_INVOKABLE void openProjectDialog();
    Q_INVOKABLE void importSchemaDialog();
    Q_INVOKABLE void setTab(int tab);
    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE void editRow(int row);      // doppio clic o Modifica
    Q_INVOKABLE void addItem();             // Aggiungi (circuito o cavo) / Nuova tratta dalla selezione
    Q_INVOKABLE void removeRow(int row);
    Q_INVOKABLE void addSelectionToRun(int row);
    Q_INVOKABLE void showRun(int row);
    Q_INVOKABLE void verify();
    Q_INVOKABLE void labels();
    Q_INVOKABLE void routesDialog();
    Q_INVOKABLE void runsFromLayersDialog();
    Q_INVOKABLE void boardFromSelection();
    Q_INVOKABLE void showGuide() { emit guideRequested(); }
    Q_INVOKABLE void bomDialog();
    Q_INVOKABLE void section(int row);

signals:
    void message(const QString& text);
    // Schema letto qui dal PDF, o progetto aperto (per la tavolozza Circuiti).
    void schemaImported(const std::vector<cartesio::Circuit>& circuits, const QString& path);
    void projectOpened();
    void guideRequested();
    void changed();
    void tableChanged();

private:
    void fill();
    bool save();
    bool editCircuit(plant::Circuit& c, bool isNew);
    bool editRun(plant::Run& r, bool isNew);
    bool editCable(canalo::CableType& t, bool isNew);
    double runLengthM(const std::string& id) const;
    std::map<std::string, double> runLengths() const;
    std::map<std::string, plant::RunGeometry> runGeometry() const;
    double labelHeight() const;
    void replaceTagged(const std::string& prefix, std::vector<std::unique_ptr<Entity>> added);
    std::vector<EntityId> runEntities(const std::string& id) const;
    std::vector<EntityId> selectedPathObjects() const;
    void tagObjects(const std::vector<EntityId>& ids, const std::string& runId);
    QString resolvedPath(const QString& stored) const;
    QString storedPath(const QString& absolute) const;
    void addUnitsField(class FormDialog& dialog, double& metersPerUnit) const;
    void loadLinked();

    DrawingWidget* drawing_ = nullptr;
    plant::Project project_;
    QString path_;                                   // file di progetto aperto (assoluto)
    std::map<std::string, plant::RunCheck> checks_;  // esiti dell'ultima verifica
    QString status_;
    QString summary_;
    int verifyCount_ = 0;
    int tab_ = 0;
    QVariantList rows_;
    std::vector<int> rowIndex_;   // indice nell'elenco del progetto di ogni riga
    int selected_ = -1;
};

} // namespace cartesio
