#pragma once

#include "core/Entity.h"

#include <QQuickWidget>
#include <QSet>
#include <QString>
#include <QVariant>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cartesio {

class DrawingWidget;
class Document;

// Tavolozza Proprietà (PROPERTIES, Ctrl+1), disegnata in QML (qml/PropertiesPalette.qml).
// In alto la tendina con la selezione divisa per tipo: scegliendo un tipo si vedono e si modificano solo quegli
// oggetti. Sezioni comprimibili: Generale, Stile di stampa, Geometria (o Testo, Attributi...), Vista, Varie. Con
// più oggetti dello stesso tipo i valori diversi compaiono come *VARIA* e una modifica vale per tutti.
//
// Qui si costruiscono le righe (modello per il QML), ognuna con l'azione da eseguire quando cambia.
class PropertiesPalette : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    // Modello della lista: il numero di righe. Le righe restano e cambiano solo i valori (ricrearle tutte a ogni
    // clic costava circa 10 ms).
    Q_PROPERTY(int rowCount READ rowCount NOTIFY rowsChanged)
    Q_PROPERTY(QStringList summary READ summary NOTIFY rowsChanged)
    Q_PROPERTY(int summaryIndex READ summaryIndex NOTIFY rowsChanged)
    Q_PROPERTY(bool hasDrawing READ hasDrawing NOTIFY rowsChanged)

public:
    // `compact`: Proprietà rapide (solo colore, layer, tipo di linea e la geometria principale, senza sezioni).
    explicit PropertiesPalette(QWidget* parent = nullptr, bool compact = false);
    int rowCount() const { return static_cast<int>(rows_.size()); }

    void setDrawing(DrawingWidget* drawing);
    void refresh();

    QVariantList rows() const { return rows_; }
    QStringList summary() const { return summary_; }
    int summaryIndex() const { return summaryIndex_; }
    bool hasDrawing() const { return drawing_ != nullptr; }

    // Dal QML.
    Q_INVOKABLE void chooseSummary(int index);
    Q_INVOKABLE void popupSummary(int globalX, int globalY);
    Q_INVOKABLE void toggleSection(const QString& title);
    Q_INVOKABLE void commit(int row, const QString& value);           // testo o numero
    Q_INVOKABLE void popupChoice(int row, int globalX, int globalY);  // elenco
    Q_INVOKABLE void press(int row);                                  // pulsante (es. colore)
    Q_INVOKABLE void step(int row, int delta);                        // contatore (vertice)
    Q_INVOKABLE void selectSimilar();
    Q_INVOKABLE void selectAllInSpace();
    Q_INVOKABLE void selectNone();

signals:
    void rowsChanged();

private:
    using NumGet = std::function<double(const Entity&)>;
    using NumSet = std::function<void(Entity&, double)>;
    using TextGet = std::function<std::string(const Entity&)>;
    using TextSet = std::function<void(Entity&, const std::string&)>;

    struct RowAction {
        std::function<void(const QString&)> text;   // testo/numero confermato
        std::function<void(int)> choose;            // voce scelta in un elenco
        std::vector<QString> options;
        std::function<void()> press;
        std::function<void(int)> step;
    };

    void rebuild();
    void fillSummary();
    // Una sezione con l'intestazione che la apre e la chiude.
    void addSection(const QString& title);
    bool sectionOpen() const { return !collapsed_.contains(section_); }
    void addReadOnly(const QString& label, const QString& value);
    void addEdit(const QString& label, const QString& value, std::function<void(const QString&)> commit);
    void addList(const QString& label, const QString& shown, const QString& icon, std::vector<QString> options,
                 std::function<void(int)> choose);
    void addButton(const QString& label, const QString& text, const QString& icon, std::function<void()> press);
    void addCounter(const QString& label, const QString& text, std::function<void(int)> step);
    // Valore calcolato sugli oggetti (uguale per tutti, o *VARIA*), non modificabile.
    void addInfo(const QString& label, const std::function<QString(const Entity&)>& get);
    // Numero modificabile: con più oggetti mostra il valore comune o *VARIA*; la modifica vale per tutti.
    void addNumber(const QString& label, const NumGet& get, const NumSet& set, int decimals = 4);
    void addText(const QString& label, const TextGet& get, const TextSet& set);
    // Scelta in un elenco (valori interi).
    void addChoice(const QString& label, const std::vector<std::pair<QString, int>>& options,
                   const std::function<int(const Entity&)>& get, const std::function<void(Entity&, int)>& set);
    // Modifica annullabile degli oggetti mostrati (dopo il ritorno dal segnale: la tavolozza si ricostruisce).
    void modify(const std::function<void(Entity&)>& change);
    // Modifica del disegno (proprietà correnti, stampa, unità), poi aggiornamento.
    void later(std::function<void(Document&)> change);

    void addGeneral();
    void addCurrentGeneral();
    void addPlotStyle(bool selection);
    // Stile di testo dei testi selezionati (TEXT, MTEXT).
    void addTextStyleChoice();
    // "Annotativo" Sì/No e le scale di annotazione dell'oggetto (testi, quote, direttrici).
    void addAnnotative();
    void addGeometry(EntityType type);
    void addViewAndMisc();

    void compactRows();

    bool compact_ = false;
    DrawingWidget* drawing_ = nullptr;
    QVariantList rows_;
    std::vector<RowAction> actions_;
    QStringList summary_;
    std::vector<int> summaryTypes_;
    int summaryIndex_ = 0;
    bool pending_ = false;

    std::vector<const Entity*> items_;   // oggetti mostrati (selezione filtrata per tipo)
    std::vector<EntityId> ids_;
    std::optional<EntityType> filter_;   // tipo scelto nella tendina
    QSet<QString> collapsed_;            // sezioni chiuse (restano chiuse fra una selezione e l'altra)
    QString section_;
    int vertex_ = 0;                     // vertice mostrato di una polilinea
};

} // namespace cartesio
