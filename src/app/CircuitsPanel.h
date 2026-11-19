#pragma once

// Tavolozza Circuiti (comando CIRCUITI), disegnata in QML (qml/CircuitsPanel.qml): schema unifilare in PDF,
// verifica delle sigle in pianta, legenda delle lettere e assegnazione di una sigla ai punti selezionati.

#include "core/CircuitTags.h"
#include "core/PointLegend.h"
#include "core/SingleLine.h"

#include <QQuickWidget>
#include <QVariant>

#include <map>
#include <set>

namespace cartesio {

class ComboModel;
class DrawingWidget;

class CircuitsPanel : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QObject* boardCombo READ boardCombo CONSTANT)
    Q_PROPERTY(QObject* kindCombo READ kindCombo CONSTANT)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY changed)
    Q_PROPERTY(QVariantList circuitRows READ circuitRows NOTIFY tableChanged)
    Q_PROPERTY(int selectedCircuit READ selectedCircuit NOTIFY tableChanged)
    Q_PROPERTY(QVariantList checkGroups READ checkGroups NOTIFY checkChanged)
    Q_PROPERTY(QVariantList legendRows READ legendRows NOTIFY legendChanged)
    Q_PROPERTY(QString pointLayers READ pointLayers NOTIFY changed)
    Q_PROPERTY(QString letterLayers READ letterLayers NOTIFY changed)
    Q_PROPERTY(QString tagLayers READ tagLayers NOTIFY changed)

public:
    explicit CircuitsPanel(QWidget* parent = nullptr);

    void setDrawing(DrawingWidget* drawing);
    // Da chiamare quando il disegno cambia (conteggi "in pianta").
    void refresh();

    // Legge lo schema; false con il messaggio d'errore.
    bool importSchema(const QString& path, QString* error);
    // Usa circuiti già letti (dalla tavolozza Impianto o dal suo progetto); `source` per la riga di stato.
    void useSchema(std::vector<Circuit> circuits, const QString& source);
    // Verifica e restituisce il riepilogo (anche per gli script).
    QString runCheck();
    // Assegna la sigla scelta ai punti selezionati nel disegno; restituisce quanti.
    int assignToSelection(const QString& code);
    const std::vector<Circuit>& circuits() const { return circuits_; }
    // Legge e interpreta la legenda delle lettere; restituisce il riepilogo (anche per gli script).
    QString fillLegend();
    void showLegend();

    QString statusText() const { return status_; }
    QObject* boardCombo() const;
    QObject* kindCombo() const;
    int currentTab() const { return tab_; }
    QVariantList circuitRows() const { return circuitRows_; }
    int selectedCircuit() const { return selected_; }
    QVariantList checkGroups() const { return checkGroups_; }
    QVariantList legendRows() const { return legendRows_; }
    QString pointLayers() const { return pointLayers_; }
    QString letterLayers() const { return letterLayers_; }
    QString tagLayers() const { return tagLayers_; }

    // Dal QML.
    Q_INVOKABLE void chooseFile();
    Q_INVOKABLE void verify();
    Q_INVOKABLE void setTab(int tab);
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void selectCircuit(int row);
    Q_INVOKABLE void openCircuit(int row);          // doppio clic: le sue sigle in pianta
    Q_INVOKABLE void toggleGroup(int group);
    Q_INVOKABLE void openCheckItem(int group, int item);
    Q_INVOKABLE void editKeywords(int row, const QString& text);
    Q_INVOKABLE void openLegend(int row, int column);  // doppio clic su una lettera o sui suoi circuiti
    Q_INVOKABLE void assignSelected();
    Q_INVOKABLE void setLayers(const QString& points, const QString& letters, const QString& tags);

signals:
    void message(const QString& text);
    // Schema letto qui dal PDF (per l'Impianto).
    void schemaImported(const std::vector<cartesio::Circuit>& circuits, const QString& path);
    void changed();
    void tableChanged();
    void checkChanged();
    void legendChanged();

private:
    void fillTable();
    CircuitScanOptions scanOptions() const;
    QString selectedCode() const;
    void showEntities(const std::vector<EntityId>& ids);
    QString legendKey() const;
    std::map<std::string, std::string> userKeywords() const;

    DrawingWidget* drawing_ = nullptr;
    std::vector<Circuit> circuits_;
    QString source_;

    QString status_;
    ComboModel* board_ = nullptr;
    ComboModel* kind_ = nullptr;
    QString search_;
    int tab_ = 0;
    QVariantList circuitRows_;
    std::vector<std::string> rowCodes_;   // sigla di ogni riga della tabella
    int selected_ = -1;
    QVariantList checkGroups_;
    std::vector<std::vector<EntityId>> checkIds_;   // oggetti di ogni voce della verifica (0 = nessuno)
    QVariantList legendRows_;
    std::vector<LegendMatch> matches_;
    std::set<std::string> codeFilter_;   // circuiti di una lettera (doppio clic nella legenda)
    QString pointLayers_;
    QString letterLayers_;
    QString tagLayers_;
};

} // namespace cartesio
