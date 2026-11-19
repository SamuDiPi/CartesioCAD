#pragma once

// Finestre degli stili (in QML, con FormDialog):
//  - STYLE: stili di testo (font SHX o TrueType, altezza, larghezza, obliquità, grassetto, corsivo);
//  - MLEADERSTYLE: stili delle direttrici (altezza del testo, freccia, stile di testo, distanza dall'approdo);
//  - motivo di tratteggio di HATCH: anteprime dei motivi, scala e angolo.

#include "app/FormDialog.h"
#include "core/Document.h"

namespace cartesio {

class DrawingWidget;

class TextStyleDialog : public FormDialog {
    Q_OBJECT

public:
    explicit TextStyleDialog(DrawingWidget* drawing, QWidget* parent = nullptr);
    // Modifica di uno stile; false se annullata.
    static bool edit(TextStyle& style, QWidget* parent);

private:
    void refresh();
    QString selectedName() const;
    void makeCurrent();
    void newStyle();
    void modifyStyle();
    void renameStyle();
    void removeStyle();

    DrawingWidget* drawing_;
    QStringList names_;
    int selected_ = -1;
    int currentNote_ = -1;
    int list_ = -1;
};

class MLeaderStyleDialog : public FormDialog {
    Q_OBJECT

public:
    explicit MLeaderStyleDialog(DrawingWidget* drawing, QWidget* parent = nullptr);

private:
    void refresh();
    QString selectedName() const;
    void makeCurrent();
    void newStyle();
    void modifyStyle();
    void removeStyle();
    bool edit(Document::MLeaderStyle& style);

    DrawingWidget* drawing_;
    QStringList names_;
    int selected_ = -1;
    int currentNote_ = -1;
    int list_ = -1;
};

// Scelta del motivo di tratteggio (anteprime, scala, angolo): cambia i valori proposti da HATCH.
// False se annullata.
bool chooseHatchPattern(QWidget* parent);

} // namespace cartesio
