#pragma once

// Dialoghi della stampa, disposti: Stampa (PLOT), Imposta pagina (PAGESETUP),
// anteprima (PREVIEW), editor delle tabelle stili (.ctb) e timbro di stampa.

#include "app/PlotEngine.h"

#include <QDialog>

#include <optional>
#include <vector>

class QQuickWidget;

namespace cartesio {

class DrawingWidget;
class UiButton;
class UiCheck;
class UiCombo;
class UiLabel;
class UiSheet;
class UiSpin;

// Stato del dialogo Stampa (anche fra un'apertura e l'altra, quando si sceglie la finestra).
struct PlotDialogState {
    PlotJob job;
    bool plotToFile = true;
    bool saveChanges = false;   // "Salva modifiche nel layout"
    QString pageSetup;          // impostazione di pagina scelta (vuoto = nessuna)
};

// In QML (qml/PlotDialog.qml): i controlli sono UiControls con l'interfaccia dei widget Qt.
class PlotDialog : public QDialog {
    Q_OBJECT
    Q_PROPERTY(bool expanded READ expanded NOTIFY expandedChanged)
    Q_PROPERTY(bool pageSetupMode READ pageSetupMode CONSTANT)
    Q_PROPERTY(bool model READ isModel CONSTANT)

public:
    // pageSetupMode: dialogo "Imposta pagina" (niente copie, timbro, stampa su file).
    PlotDialog(DrawingWidget& drawing, int space, bool pageSetupMode, QWidget* parent = nullptr);

    void setState(const PlotDialogState& state);
    PlotDialogState state() const;
    PlotSettings settings() const;

    enum class Outcome { Cancel, Plot, PickWindow };
    Outcome outcome() const { return outcome_; }
    // Mostra o nasconde il pannello con le altre opzioni (pulsante ">").
    void setExpanded(bool on, bool remember = true);
    bool expanded() const { return expanded_; }
    bool pageSetupMode() const { return pageSetupMode_; }
    bool isModel() const { return space_ == 0; }

    // Dal QML.
    Q_INVOKABLE void toggleExpanded() { setExpanded(!expanded_); }
    Q_INVOKABLE void plotClicked();
    Q_INVOKABLE void cancelClicked() { reject(); }

signals:
    void expandedChanged();

private:
    void buildUi();
    void fillDevices();
    void fillPapers();
    void fillScales();
    void fillStyles(const QString& select);
    void fillPageSetups(const QString& select);
    void loadSettings(const PlotSettings& s);
    void updateUi();
    void onDeviceChanged();
    void onPaperChanged();
    void onScaleChoice();
    void onScaleEdited();
    void onPageSetupChosen();
    void addPageSetup();
    void editStyles();
    void editStamp();
    void deviceProperties();
    void preview();
    void applyToLayout();
    PlotJob currentJob() const;

    DrawingWidget& drawing_;
    int space_;
    bool pageSetupMode_;
    Outcome outcome_ = Outcome::Cancel;
    bool loading_ = false;
    PlotSettings base_;            // campi che il dialogo non mostra (finestra, nome...)
    PlotStampSettings stamp_;
    std::vector<PrinterPaper> printerPapers_;
    struct ScaleChoice {
        QString label;
        int standardType;   // -1 = scala personalizzata con nome
        double paper;
        double drawing;
    };
    std::vector<ScaleChoice> scales_;

    QQuickWidget* view_ = nullptr;
    bool expanded_ = false;
    UiCombo* setupCombo_ = nullptr;
    UiButton* addSetupButton_ = nullptr;
    UiCombo* deviceCombo_ = nullptr;
    UiButton* propertiesButton_ = nullptr;
    UiLabel* deviceInfo_ = nullptr;
    UiCheck* toFileCheck_ = nullptr;
    UiSheet* sheet_ = nullptr;
    UiLabel* sheetLabel_ = nullptr;
    UiCombo* paperCombo_ = nullptr;
    UiSpin* copiesSpin_ = nullptr;
    UiCombo* whatCombo_ = nullptr;
    UiButton* windowButton_ = nullptr;
    UiSpin* offsetX_ = nullptr;
    UiSpin* offsetY_ = nullptr;
    UiCheck* centerCheck_ = nullptr;
    UiCheck* fitCheck_ = nullptr;
    UiCombo* scaleCombo_ = nullptr;
    UiSpin* scalePaper_ = nullptr;
    UiCombo* unitsCombo_ = nullptr;
    UiSpin* scaleDrawing_ = nullptr;
    UiCheck* lwScaleCheck_ = nullptr;
    UiCombo* styleCombo_ = nullptr;
    UiButton* styleEdit_ = nullptr;
    UiCheck* showStylesCheck_ = nullptr;
    UiCombo* shadeCombo_ = nullptr;
    UiCombo* qualityCombo_ = nullptr;
    UiSpin* dpiSpin_ = nullptr;
    UiCheck* backgroundCheck_ = nullptr;
    UiCheck* lineweightsCheck_ = nullptr;
    UiCheck* transparencyCheck_ = nullptr;
    UiCheck* stylesCheck_ = nullptr;
    UiCheck* paperLastCheck_ = nullptr;
    UiCheck* hideCheck_ = nullptr;
    UiCheck* stampCheck_ = nullptr;
    UiButton* stampButton_ = nullptr;
    UiCheck* saveCheck_ = nullptr;
    UiCheck* portraitRadio_ = nullptr;
    UiCheck* landscapeRadio_ = nullptr;
    UiCheck* upsideDownCheck_ = nullptr;
    UiButton* previewButton_ = nullptr;
    UiButton* applyButton_ = nullptr;
};

// Anteprima a tutto schermo; true se si è scelto "Stampa".
bool showPlotPreview(QWidget* parent, DrawingWidget& drawing, const PlotJob& job);
// Editor di una tabella stili; restituisce il nome con cui è stata salvata (vuoto se annullato).
QString editPlotStyleTable(QWidget* parent, const QString& name);
// Gestione delle tabelle stili di stampa (.ctb): nuova, modifica, copia, rinomina, elimina, importa.
void showPlotStyleManager(QWidget* parent);
// Diagnosi: la prossima apertura del gestore salva l'immagine in `path` e si chiude.
void grabNextPlotStyleManager(const QString& path);
// Impostazioni del timbro di stampa.
bool editPlotStamp(QWidget* parent, PlotStampSettings& settings);
// Gestore delle impostazioni di pagina (PAGESETUP) per lo spazio indicato.
void showPageSetupManager(QWidget* parent, DrawingWidget& drawing, int space);

} // namespace cartesio
