#pragma once

#include "app/FormDialog.h"

namespace cartesio {

class DrawingWidget;
struct DimStyle;

// Gestione stili di quota (comando DIMSTYLE), in QML: elenco, stile corrente, nuovo stile, modifica.
// Modificare uno stile aggiorna tutte le quote che lo usano (passo annullabile).
class DimStyleDialog : public FormDialog {
    Q_OBJECT

public:
    explicit DimStyleDialog(DrawingWidget* drawing, QWidget* parent = nullptr);

    // Finestra di modifica di uno stile; false se annullata.
    static bool edit(DimStyle& style, QWidget* parent);

private:
    void refresh();
    QString selectedName() const;
    void makeCurrent();
    void newStyle();
    void modifyStyle();

    DrawingWidget* drawing_;
    QStringList names_;
    int selected_ = -1;
    int currentNote_ = -1;
    int list_ = -1;
};

} // namespace cartesio
