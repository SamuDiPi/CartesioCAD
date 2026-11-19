#pragma once

#include "app/AppOptions.h"
#include "app/FormDialog.h"

namespace cartesio {

// Finestra Opzioni (comando OPZIONI), con le schede: Visualizza, Apri e salva, Stampa e
// pubblicazione, Preferenze utente, Disegno, Selezione, Plugin. In QML (FormDialog); "Applica" e "OK" salvano.
class OptionsDialog : public FormDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent = nullptr);

    // Scheda da mostrare all'apertura (0...6).
    void setCurrentTab(int index) { setCurrentPage(index); }

signals:
    // Le opzioni sono cambiate (dopo "Applica" o "OK").
    void applied();

private:
    void displayTab();
    void openSaveTab();
    void plotTab();
    void userTab();
    void draftingTab();
    void selectionTab();
    void pluginTab();
    void apply();

    AppOptions options_;   // copia modificata dai controlli
    bool stampOn_ = false;  // timbro di stampa (preferenza del dialogo Stampa)
};

} // namespace cartesio
