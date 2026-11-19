#pragma once

#include "app/FormDialog.h"

#include <optional>

namespace cartesio {

// Finestra "Seleziona colore" con la tavolozza dell'indice colore (ACI), in QML (FormDialog).
class ColorDialog : public FormDialog {
    Q_OBJECT

public:
    // allowByLayer: mostra i pulsanti DaLayer/DaBlocco (per gli oggetti, non per i layer).
    ColorDialog(int initial, bool allowByLayer, QWidget* parent = nullptr);

    int color() const { return color_; }

    static std::optional<int> pick(int initial, bool allowByLayer, QWidget* parent);

private:
    int color_;
};

} // namespace cartesio
