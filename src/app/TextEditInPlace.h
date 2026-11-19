#pragma once

// Modifica dei testi direttamente nel disegno: il testo resta al suo posto, con il
// suo carattere, la sua altezza e la sua rotazione, e cambia mentre si scrive; cursore e selezione stanno sulle
// lettere disegnate. Vale per testi (TEXT), testi multilinea (MTEXT), testo delle direttrici, attributi dei
// blocchi, testo delle quote e celle delle tabelle.
//
// Tasti come in un editor normale (frecce, Home/Fine, Ctrl+frecce, Maiusc per selezionare, Ctrl+A/C/X/V/Z/Y).
// Invio va a capo nei testi multilinea e conferma negli altri; Ctrl+Invio o un clic fuori dal testo
// confermano; Esc annulla; Tab passa alla cella successiva di una tabella. Nei testi multilinea una barra
// sopra il testo cambia carattere, altezza, grassetto, corsivo e colore della parte selezionata.
// La modifica confermata si annulla con Ctrl+Z come le altre.

#include "core/Entity.h"
#include "core/Vec2.h"

#include <QObject>
#include <QString>

#include <optional>

class QWidget;

namespace cartesio {

class DrawingWidget;

// Editor di testo multilinea aperto nel disegno: la scheda "Editor di testo" della barra multifunzione lo comanda.
class RichTextControls {
public:
    struct State {
        bool entity = false;          // testo multilinea (stile, giustificazione, interlinea, maschera, righello)
        QString style;
        bool annotative = false;
        bool mask = false;
        double height = 0.0;
        QString family;
        bool bold = false, italic = false, underline = false, overline = false, strike = false;
        int aci = 0;
        long long rgb = -1;
        int align = -1;               // del paragrafo: -1 come la giustificazione, 0 sx, 1 centro, 2 dx, 3 giustificato
        int attachment = 1;           // giustificazione (1..9)
        double lineSpacing = 1.0;
        bool ruler = true;
        bool selection = false;       // c'è del testo selezionato
        int columns = 1;
        bool spell = true;            // controllo ortografico attivo
        bool spellAvailable = false;  // correttore di Windows presente
    };
    enum class Flag { Bold, Italic, Underline, Overline, Strike };

    virtual ~RichTextControls() = default;
    virtual State state() const = 0;
    virtual void setStyle(const QString& name) = 0;
    virtual void setAnnotative(bool on) = 0;
    virtual void setMask(bool on) = 0;
    virtual void setHeight(double h) = 0;
    virtual void setFamily(const QString& family) = 0;
    virtual void toggle(Flag flag) = 0;
    virtual void setColor(int aci, long long rgb) = 0;
    virtual void clearFormatting() = 0;
    virtual void changeCase(bool upper) = 0;
    virtual void setAlign(int align) = 0;
    virtual void setAttachment(int attachment) = 0;
    virtual void setLineSpacing(double factor) = 0;
    // Elenco dei paragrafi selezionati: 0 nessuno, 1 puntato, 2 numerato, 3 lettere.
    virtual void setList(int kind) = 0;
    virtual void insertText(const QString& text) = 0;
    // Trova e sostituisce nel testo (tutte le volte); restituisce quante.
    virtual int replaceAll(const QString& find, const QString& replace, bool matchCase) = 0;
    virtual void setRuler(bool on) = 0;
    virtual void setColumns(int count) = 0;
    virtual void setSpellCheck(bool on) = 0;
    virtual void close(bool apply) = 0;
    virtual void refocus() = 0;
};

// Avvisi all'interfaccia: un editor di testo multilinea si apre (o si chiude: nullptr) e cambia stato.
class TextEditorHub : public QObject {
    Q_OBJECT
public:
    static TextEditorHub& instance();
    RichTextControls* active() const { return active_; }
    void setActive(RichTextControls* controls)
    {
        active_ = controls;
        emit activeChanged();
    }
    void notify() { emit stateChanged(); }

signals:
    void activeChanged();
    void stateChanged();

private:
    RichTextControls* active_ = nullptr;
};

// True se l'oggetto ha un testo modificabile così.
bool canEditTextInPlace(const Entity& e);
// Avvia la modifica. `at`: punto cliccato (sceglie l'attributo o la cella e dove mettere il cursore).
// Restituisce false se lì non c'è un testo da modificare (es. un blocco cliccato fuori dagli attributi).
bool editTextInPlace(DrawingWidget& drawing, EntityId id, std::optional<Vec2> at = std::nullopt);
// Testo appena creato da TEXT o MTEXT: si scrive subito. TEXT: Invio conferma. MTEXT: Invio va a capo, Ctrl+Invio o
// un clic fuori confermano. Anche Esc tiene quello che si è scritto; lasciato vuoto, il testo sparisce.
bool editNewTextInPlace(DrawingWidget& drawing, EntityId id);
// Attributo visibile di un inserimento sotto il punto (indice), oppure -1.
int attributeAt(const InsertEntity& insert, Vec2 p, double tolerance);

} // namespace cartesio
