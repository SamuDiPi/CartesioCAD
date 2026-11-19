#pragma once

// Riga di comando mobile, disegnata in QML (qml/CommandLine.qml): richiesta del comando,
// campo di testo e suggerimenti dei nomi dei comandi mentre si digita. Le ultime righe della cronologia le
// disegna la vista sopra il disegno (così i clic passano al disegno).

#include <QQuickWidget>
#include <QStringList>

#include <functional>

class QKeyEvent;

namespace cartesio {

// Il campo di testo visto dalla finestra principale (stessa interfaccia di QLineEdit per quello che serve).
class CommandInput : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textReset)
    Q_PROPERTY(QString placeholder READ placeholder NOTIFY changed)

public:
    using QObject::QObject;
    QString text() const { return text_; }
    void setText(const QString& text);
    void clear() { setText({}); }
    void insert(const QString& text) { setText(text_ + text); }
    void setPlaceholderText(const QString& text);
    QString placeholder() const { return placeholder_; }
    void setFocus();

    // Dal QML.
    Q_INVOKABLE void edited(const QString& text);
    Q_INVOKABLE void submit() { emit returnPressed(); }

signals:
    void returnPressed();
    void textChanged(const QString& text);
    void textReset();          // testo cambiato da C++: il QML lo ricopia nel campo
    void changed();
    void focusRequested();

private:
    QString text_;
    QString placeholder_;
};

class CommandLine : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QObject* input READ inputObject CONSTANT)
    Q_PROPERTY(QString prompt READ prompt NOTIFY changed)
    Q_PROPERTY(QStringList matches READ matches NOTIFY matchesChanged)
    Q_PROPERTY(QString closeIcon READ closeIcon CONSTANT)
    Q_PROPERTY(QString historyIcon READ historyIcon CONSTANT)

public:
    explicit CommandLine(QWidget* parent = nullptr);

    CommandInput* input() const { return input_; }
    void setPrompt(const QString& prompt);
    QString prompt() const { return prompt_; }
    void appendHistory(const QString& line);
    void clearHistory();
    const QStringList& history() const { return lines_; }

    // Nomi dei comandi e alias suggeriti mentre si digita (solo quando nessun comando è attivo).
    void setCompletions(const QStringList& words) { words_ = words; }
    void setCompletionEnabled(bool enabled);
    QStringList matches() const { return matches_; }
    QString closeIcon() const;
    QString historyIcon() const;

    // Tasti speciali (Esc, spazio, Ctrl+Z con il campo vuoto...): li gestisce la finestra principale.
    void setKeyHandler(std::function<bool(QKeyEvent&)> handler) { keyHandler_ = std::move(handler); }
    // Altezza voluta (cresce verso l'alto quando ci sono suggerimenti).
    int wantedHeight() const;

    Q_INVOKABLE bool handleKey(int key, int modifiers, const QString& text);
    Q_INVOKABLE void choose(const QString& word) { emit completionChosen(word); }
    Q_INVOKABLE void requestHistory() { emit historyRequested(); }
    Q_INVOKABLE void requestClose() { emit closeRequested(); }

signals:
    void completionChosen(const QString& word);
    void historyRequested();
    void closeRequested();
    void historyChanged();
    void changed();
    void matchesChanged();
    void heightChanged();

private:
    QObject* inputObject() const { return input_; }
    void updateMatches();

    CommandInput* input_;
    QString prompt_;
    QStringList lines_;
    QStringList words_;
    QStringList matches_;
    bool completion_ = true;
    std::function<bool(QKeyEvent&)> keyHandler_;

signals:
    // Interfaccia tutta in QML: va dato il fuoco al campo della riga di comando nella scena principale.
    void focusInRootRequested();
};

} // namespace cartesio
