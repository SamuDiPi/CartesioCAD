#pragma once

// Dialoghi della stampa in QML: anteprima (PlotPreview.qml) ed editor delle tabelle stili (StyleEditor.qml).
// Il timbro di stampa e le richieste di nome usano FormDialog. Le funzioni pubbliche sono in PlotDialog.h.

#include "core/Plot.h"

#include <QDialog>
#include <QImage>
#include <QObject>

class QQuickWidget;

namespace cartesio {

class UiButton;
class UiCheck;
class UiCombo;
class UiSelectList;
class UiSpin;
class UiText;

// Anteprima: l'immagine del foglio con zoom (rotellina, pulsanti) e trascinamento.
class PreviewDialog : public QDialog {
    Q_OBJECT
    Q_PROPERTY(int imageWidth READ imageWidth CONSTANT)
    Q_PROPERTY(int imageHeight READ imageHeight CONSTANT)
    Q_PROPERTY(QString source READ source CONSTANT)

public:
    PreviewDialog(QWidget* parent, const QImage& image, const QString& title);
    ~PreviewDialog() override;
    int imageWidth() const { return image_.width(); }
    int imageHeight() const { return image_.height(); }
    QString source() const { return source_; }
    Q_INVOKABLE void plot() { accept(); }
    Q_INVOKABLE void close() { reject(); }

private:
    QImage image_;
    QString source_;
};

// Editor di una tabella stili di stampa (.ctb): colori 1-255 a sinistra (selezione multipla), proprietà a destra.
class StyleEditor : public QDialog {
    Q_OBJECT

public:
    StyleEditor(QWidget* parent, PlotStyleTable table, const QString& name);
    QString savedName() const { return saved_; }

private:
    void showStyle();
    void storeStyle();
    bool saveTo(const QString& name);

    PlotStyleTable table_;
    QString name_;
    QString saved_;
    bool loading_ = false;
    UiText* description_;
    UiSelectList* list_;
    UiCombo* color_;
    UiCheck* dither_;
    UiCheck* gray_;
    UiSpin* pen_;
    UiSpin* virtualPen_;
    UiSpin* screen_;
    UiCombo* linetype_;
    UiCheck* adaptive_;
    UiCombo* lineweight_;
    UiCombo* end_;
    UiCombo* join_;
    UiCombo* fill_;
    UiText* styleDescription_;
    UiButton* save_;
    UiButton* saveAs_;
    UiButton* cancel_;
};

} // namespace cartesio
