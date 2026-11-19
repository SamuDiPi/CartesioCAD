#include "app/ColorDialog.h"

namespace cartesio {

ColorDialog::ColorDialog(int initial, bool allowByLayer, QWidget* parent)
    : FormDialog(tr("Seleziona colore"), Ok | Cancel, parent)
    , color_(initial)
{
    aci(initial, allowByLayer, [this](int c) { color_ = c; });
    setSize(500, 380);
}

std::optional<int> ColorDialog::pick(int initial, bool allowByLayer, QWidget* parent)
{
    ColorDialog dialog(initial, allowByLayer, parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.color();
}

} // namespace cartesio
