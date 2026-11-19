#pragma once

// Controlli per i dialoghi in QML con la stessa interfaccia dei widget Qt (QCheckBox, QComboBox, QSpinBox,
// QLabel, QPushButton): la logica dei dialoghi resta scritta come prima, il QML (qml/Ui*.qml nei file dei
// dialoghi) li mostra e li modifica. Gli elenchi si aprono come menu nativi.

#include <QColor>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>
#include <vector>

#include "core/Geometry.h"
#include "app/PlotEngine.h"

namespace cartesio {

// Proprietà comuni: testo, suggerimento, attivo, visibile.
class UiControl : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString tooltip READ toolTip NOTIFY changed)
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY changed)
    Q_PROPERTY(bool visible READ isVisible NOTIFY changed)

public:
    explicit UiControl(const QString& text = {}, QObject* parent = nullptr) : QObject(parent), text_(text) {}

    QString text() const { return text_; }
    void setText(const QString& t) { if (t != text_) { text_ = t; emit changed(); } }
    QString toolTip() const { return tooltip_; }
    void setToolTip(const QString& t) { tooltip_ = t; emit changed(); }
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool on) { if (on != enabled_) { enabled_ = on; emit changed(); } }
    bool isVisible() const { return visible_; }
    void setVisible(bool on) { if (on != visible_) { visible_ = on; emit changed(); } }
    void hide() { setVisible(false); }
    void show() { setVisible(true); }

signals:
    void changed();

private:
    QString text_;
    QString tooltip_;
    bool enabled_ = true;
    bool visible_ = true;
};

// Etichetta (testo con formattazione semplice); `error`: in rosso.
class UiLabel : public UiControl {
    Q_OBJECT
    Q_PROPERTY(bool error READ error NOTIFY changed)

public:
    using UiControl::UiControl;
    void clear() { setText({}); }
    bool error() const { return error_; }
    void setError(bool on) { if (on != error_) { error_ = on; emit changed(); } }

private:
    bool error_ = false;
};

class UiButton : public UiControl {
    Q_OBJECT

public:
    using UiControl::UiControl;
    Q_INVOKABLE void click() { if (isEnabled()) emit clicked(); }

signals:
    void clicked();
};

// Casella di spunta, o pulsante di opzione se ha un "gruppo" (le altre del gruppo si tolgono).
class UiCheck : public UiControl {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked NOTIFY changed)
    Q_PROPERTY(bool radio READ isRadio CONSTANT)

public:
    using UiControl::UiControl;
    bool isChecked() const { return checked_; }
    void setChecked(bool on);
    bool isRadio() const { return !group_.empty(); }
    // Pulsanti di opzione esclusivi.
    static void makeGroup(const std::vector<UiCheck*>& checks);

    Q_INVOKABLE void toggle();

signals:
    void toggled(bool on);

private:
    bool checked_ = false;
    std::vector<UiCheck*> group_;
};

// Numero con campo modificabile (QSpinBox / QDoubleSpinBox).
class UiSpin : public UiControl {
    Q_OBJECT
    Q_PROPERTY(double value READ value NOTIFY valueChangedSignal)
    Q_PROPERTY(QString display READ display NOTIFY valueChangedSignal)
    Q_PROPERTY(double minimum READ minimum CONSTANT)
    Q_PROPERTY(double maximum READ maximum CONSTANT)

public:
    explicit UiSpin(double lo = 0, double hi = 99, int decimals = 0, const QString& suffix = {}, QObject* parent = nullptr)
        : UiControl({}, parent), min_(lo), max_(hi), decimals_(decimals), suffix_(suffix) {}

    double value() const { return value_; }
    void setValue(double v);
    void setRange(double lo, double hi) { min_ = lo; max_ = hi; setValue(value_); }
    void setDecimals(int d) { decimals_ = d; emit valueChangedSignal(); }
    void setSuffix(const QString& s) { suffix_ = s; emit valueChangedSignal(); }
    double minimum() const { return min_; }
    double maximum() const { return max_; }
    QString display() const;

    // Dal QML: testo scritto (virgola o punto, suffisso facoltativo) o passo della rotellina.
    Q_INVOKABLE void commit(const QString& text);
    Q_INVOKABLE void step(int steps);

signals:
    void valueChanged(double value);
    void valueChangedSignal();

private:
    double value_ = 0.0;
    double min_, max_;
    int decimals_;
    QString suffix_;
};

// Elenco a discesa (QComboBox): voci con dato, attive o no, con suggerimento.
class UiCombo : public UiControl {
    Q_OBJECT
    Q_PROPERTY(QString currentText READ currentText NOTIFY itemsChanged)

public:
    using UiControl::UiControl;

    void clear();
    void addItem(const QString& text, const QVariant& data = {});
    void addItems(const QStringList& texts);
    void insertItem(int index, const QString& text, const QVariant& data = {});
    void setItemEnabled(int index, bool on);
    void setItemToolTip(int index, const QString& tip);
    int count() const { return static_cast<int>(items_.size()); }
    QVariant itemData(int index) const;
    QString itemText(int index) const;
    int findData(const QVariant& data) const;
    // Confronto senza maiuscole/minuscole fra testi (come Qt::MatchFixedString).
    int findDataNoCase(const QString& data) const;
    int currentIndex() const { return current_; }
    QVariant currentData() const { return itemData(current_); }
    QString currentText() const { return itemText(current_); }
    void setCurrentIndex(int index);

    // Dal QML: apre l'elenco sotto il punto dello schermo; la scelta emette activated.
    Q_INVOKABLE void popup(int globalX, int globalY, int width);

signals:
    void currentIndexChanged(int index);
    void activated(int index);
    void itemsChanged();

private:
    struct Item {
        QString text;
        QVariant data;
        bool enabled = true;
        QString tooltip;
    };
    std::vector<Item> items_;
    int current_ = -1;
};

// Campo di testo (QLineEdit): `edit` dal QML (ogni modifica), `textEdited` come in Qt.
class UiText : public UiControl {
    Q_OBJECT
    Q_PROPERTY(QString placeholder READ placeholder CONSTANT)

public:
    explicit UiText(const QString& text = {}, const QString& placeholder = {}, QObject* parent = nullptr)
        : UiControl(text, parent), placeholder_(placeholder) {}
    QString placeholder() const { return placeholder_; }
    Q_INVOKABLE void edit(const QString& text)
    {
        if (text == this->text())
            return;
        setText(text);
        emit textEdited(text);
    }

signals:
    void textEdited(const QString& text);

private:
    QString placeholder_;
};

// Elenco con selezione multipla (Ctrl e Maiusc come nei programmi Windows) e un campione di colore per voce.
class UiSelectList : public UiControl {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList selected READ selectedList NOTIFY selectionChanged)
    Q_PROPERTY(int current READ current NOTIFY selectionChanged)

public:
    using UiControl::UiControl;
    void addItem(const QString& text, const QColor& color, const QVariant& data = {});
    QVariantList items() const { return items_; }
    QVariant itemData(int i) const { return i >= 0 && i < static_cast<int>(data_.size()) ? data_[static_cast<std::size_t>(i)] : QVariant(); }
    std::vector<int> selectedRows() const;
    QVariantList selectedList() const;
    int current() const { return current_; }
    void setCurrentRow(int row);
    // Dal QML: clic su una voce con i tasti modificatori (Qt::KeyboardModifiers).
    Q_INVOKABLE void click(int row, int modifiers);

signals:
    void itemsChanged();
    void selectionChanged();

private:
    QVariantList items_;
    std::vector<QVariant> data_;
    std::vector<bool> selected_;
    int current_ = -1;
    int anchor_ = -1;
};

// Anteprima parziale del foglio (dialogo Stampa): foglio, area stampabile e disegno, in proporzione.
class UiSheet : public QObject {
    Q_OBJECT
    Q_PROPERTY(double sheetWidth READ sheetWidth NOTIFY changed)
    Q_PROPERTY(double sheetHeight READ sheetHeight NOTIFY changed)
    Q_PROPERTY(QVariantList printable READ printable NOTIFY changed)   // [x0, y0, x1, y1] in mm
    Q_PROPERTY(QVariantList placed READ placed NOTIFY changed)         // vuoto se niente
    Q_PROPERTY(bool outside READ outside NOTIFY changed)

public:
    using QObject::QObject;
    void setFrame(const SheetSize& sheet, const BBox& printable, std::optional<BBox> placed, bool outside);

    double sheetWidth() const { return sheet_.width; }
    double sheetHeight() const { return sheet_.height; }
    QVariantList printable() const;
    QVariantList placed() const;
    bool outside() const { return outside_; }

signals:
    void changed();

private:
    SheetSize sheet_;
    BBox printable_;
    std::optional<BBox> placed_;
    bool outside_ = false;
};

} // namespace cartesio
