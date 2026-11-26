#pragma once

// Finestre di dialogo in QML (qml/FormDialog.qml): il C++ descrive schede, gruppi e campi (spunte, numeri,
// cursori, elenchi a discesa, testi, percorsi con "Sfoglia...", colori, note, elenchi, pulsanti) collegati ai
// valori da modificare; il QML li disegna con il tema di CartesioCAD. I dialoghi di sistema (file, cartelle)
// restano nativi.

#include <QColor>
#include <QDialog>
#include <QMap>
#include <QVariant>

#include <functional>
#include <vector>

class QQuickWidget;

namespace cartesio {

class FormDialog : public QDialog {
    Q_OBJECT
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QVariantList buttons READ buttonList CONSTANT)
    Q_PROPERTY(QVariantMap enabledFields READ enabledFields NOTIFY enabledChanged)

public:
    enum Button { Ok = 1, Cancel = 2, Apply = 4, Close = 8 };
    enum PathMode { Folder, OpenFile, SaveFile, AddFolder };   // AddFolder: aggiunge con ";" all'elenco

    FormDialog(const QString& title, int buttons, QWidget* parent = nullptr);

    // Costruzione: le schede (una sola = senza linguette), i gruppi e i campi, nell'ordine.
    void page(const QString& title);
    void group(const QString& title);
    int check(const QString& label, bool& value, int indent = 0);
    int check(const QString& label, bool value, std::function<void(bool)> set, int indent = 0);
    int integer(const QString& label, int lo, int hi, int& value, const QString& suffix = {});
    int number(const QString& label, double lo, double hi, int decimals, double& value, const QString& suffix = {});
    int number(const QString& label, double lo, double hi, int decimals, double value, std::function<void(double)> set,
               const QString& suffix = {});
    int slider(const QString& label, int lo, int hi, int& value, const QString& suffix = QStringLiteral(" px"));
    int color(const QString& label, QColor& value);
    // Elenco a discesa: voci (testo, dato) e dato scelto.
    int choice(const QString& label, const QList<QPair<QString, QVariant>>& items, const QVariant& current,
               std::function<void(const QVariant&)> set);
    int text(const QString& label, QString& value, const QString& placeholder = {});
    // Testo su più righe (`lines` righe visibili, poi si scorre). Senza `set` è in sola lettura (anteprime); il testo
    // si cambia con setFieldValue.
    int textArea(const QString& label, const QString& value, std::function<void(const QString&)> set, int lines = 5,
                 const QString& placeholder = {});
    int text(const QString& label, const QString& value, std::function<void(const QString&)> set,
             const QString& placeholder = {});
    int path(const QString& label, QString& value, PathMode mode, const QString& placeholder = {},
             const QString& filter = {});
    int note(const QString& text);   // testo a capo automatico; i link <a href> si aprono nel browser
    // Elenco di voci con selezione (doppio clic = `activated`).
    int list(const QStringList& items, int current, std::function<void(int)> select,
             std::function<void(int)> activated = {});
    // Riga di pulsanti (ognuno con la sua azione).
    int buttonsRow(const QList<QPair<QString, std::function<void()>>>& buttons);
    // Galleria di riquadri con un'immagine (URL del provider delle icone) e il nome sotto: clic = scelta,
    // doppio clic = `activated` (es. motivi di tratteggio).
    int gallery(const QStringList& names, const QStringList& images, int current, std::function<void(int)> select,
                std::function<void(int)> activated = {});
    // Tavolozza dell'indice colore ACI (con DaLayer/DaBlocco se `byLayer`).
    int aci(int value, bool byLayer, std::function<void(int)> set);

    // Abilita `field` solo quando la spunta `checkField` è attiva.
    void enableWhen(int field, int checkField);
    // Cambi dal C++ (elenchi aggiornati, testo di una nota, valore...).
    void setItems(int field, const QStringList& items, int current);
    // Nuove voci di un elenco a discesa (testo, dato) con il dato scelto.
    void setChoices(int field, const QList<QPair<QString, QVariant>>& items, const QVariant& current);
    void setFieldText(int field, const QString& text);
    void setFieldValue(int field, const QVariant& value);
    // Larghezza e altezza della finestra in pixel.
    void setSize(int width, int height);
    // Testo di un pulsante al posto di quello normale (es. "Apri su GitHub" invece di "OK").
    void setButtonText(Button button, const QString& text);

    // Diagnosi senza schermo: il prossimo dialogo che si apre si salva come immagine in `path` e si chiude.
    static void grabNext(const QString& path);

    // Chiamata su OK e Applica.
    std::function<void()> onApply;

    QVariantList pages() const;
    int currentPage() const { return currentPage_; }
    void setCurrentPage(int index);
    QVariantList buttonList() const;
    QVariantMap enabledFields() const;

    // Dal QML.
    Q_INVOKABLE void edit(int field, const QVariant& value);
    Q_INVOKABLE void browse(int field);
    Q_INVOKABLE void pickColor(int field);
    Q_INVOKABLE void activate(int field, int index);
    Q_INVOKABLE void press(int field, int index);
    Q_INVOKABLE void popupChoice(int field, int globalX, int globalY, int width);
    Q_INVOKABLE void button(int which);
    Q_INVOKABLE void openLink(const QString& url);

signals:
    void pagesChanged();
    void currentPageChanged();
    void enabledChanged();
    // Un campo cambiato dal C++: il QML aggiorna la riga.
    void fieldChanged(int field, const QVariantMap& data);

protected:
    void showEvent(QShowEvent* event) override;

private:
    struct Field {
        int page = 0;
        QVariantMap data;   // kind, label, value, ... (mostrati dal QML)
        std::function<void(const QVariant&)> set;
        std::function<void(int)> activated;
        std::vector<std::function<void()>> actions;
        int enabledBy = -1;
    };
    int add(QVariantMap data, std::function<void(const QVariant&)> set);
    QMap<int, QString> buttonTexts_;
    void changed(int field);

    QStringList pageTitles_;
    std::vector<Field> fields_;
    int currentPage_ = 0;
    int buttons_;
    QQuickWidget* view_ = nullptr;
    bool loaded_ = false;   // il QML si carica alla prima apertura, a campi già descritti
};

} // namespace cartesio
