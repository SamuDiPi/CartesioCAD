#pragma once

#include "core/Document.h"
#include "core/Vec2.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cartesio {

// Stato condiviso fra l'interfaccia e i comandi.
struct CommandContext {
    CommandContext(Document& doc, std::vector<EntityId>& sel) : document(doc), selection(sel) {}

    Document& document;
    std::vector<EntityId>& selection;
    Vec2 cursor;                       // posizione corrente del cursore (già con snap), coordinate mondo
    std::optional<Vec2> lastPoint;     // ultimo punto inserito, base per le coordinate relative "@"
    double pickTolerance = 0.5;        // tolleranza di selezione, in unità mondo
    bool shiftPressed = false;         // Maiusc premuto: i clic di selezione tolgono invece di aggiungere
    std::function<void(const std::string&)> print;   // scrive nella cronologia della riga di comando
    std::function<void()> zoomExtents;
    std::function<void(const BBox&)> zoomWindow;
    // Area inquadrata ora (coordinate in cui si lavora) e spazio mostrato (0 modello, id del layout).
    std::function<BBox()> visibleArea;
    std::function<int()> shownSpace;
    std::function<bool()> zoomPrevious;
    // Variabili di sistema gestite dall'interfaccia (OSMODE, ORTHOMODE...): lettura e scrittura.
    std::function<std::string(const std::string&)> getVar;
    std::function<bool(const std::string&, const std::string&)> setVar;
    // EATTEDIT: l'interfaccia apre la modifica degli attributi dell'inserimento indicato.
    std::function<void(EntityId)> editAttributes;
    // Selezione usata dall'ultimo comando (opzione "P" nelle selezioni, SELECTPREVIOUS).
    std::vector<EntityId> previousSelection;
    // Oggetto scelto con il ciclo di selezione (Maiusc+Spazio sugli oggetti sovrapposti): un clic lì prende
    // questo invece di quello che vincerebbe da solo.
    EntityId pickHint = 0;
    // Comando da avviare appena finito quello attivo (ADDSELECTED): la parola del comando e gli input da
    // passargli (es. il nome del blocco). afterFollowUp viene chiamata quando quel comando finisce o si annulla.
    std::vector<std::string> followUp;
    std::function<void()> afterFollowUp;
    // HATCH, opzione Motivo: l'interfaccia mostra la scelta del motivo con le anteprime (false = riga di comando).
    std::function<bool()> chooseHatchPattern;
    // TEXT e MTEXT: l'interfaccia aggiunge il testo nuovo (vuoto) e lo fa scrivere direttamente nel disegno.
    // Restituisce false se non può (script, senza schermo): allora il testo resta al comando, che lo chiede
    // nella riga di comando. Se restituisce true ha preso l'oggetto.
    std::function<bool(std::unique_ptr<Entity>&)> addTextInPlace;

    std::string variable(const std::string& name) const { return getVar ? getVar(name) : std::string{}; }

    void message(const std::string& text) const
    {
        if (print)
            print(text);
    }
};

enum class CommandStatus { Continue, Finished };

// Un comando interattivo (LINE, CIRCLE, ...). Riceve punti, valori, opzioni e Invio
// dalla riga di comando o dal mouse e decide quando ha finito.
class Command {
public:
    virtual ~Command() = default;

    // Testo mostrato nella riga di comando, es. "Primo punto:".
    virtual std::string prompt() const = 0;

    virtual CommandStatus start(CommandContext&) { return CommandStatus::Continue; }

    virtual CommandStatus onPoint(CommandContext& ctx, Vec2)
    {
        ctx.message("Un punto non è valido qui.");
        return CommandStatus::Continue;
    }

    virtual CommandStatus onValue(CommandContext& ctx, double)
    {
        ctx.message("Un valore numerico non è valido qui.");
        return CommandStatus::Continue;
    }

    virtual CommandStatus onKeyword(CommandContext& ctx, const std::string&)
    {
        ctx.message("Opzione non riconosciuta.");
        return CommandStatus::Continue;
    }

    // Testo libero (es. il contenuto di un testo): richiesto solo se wantsText() è vero.
    virtual CommandStatus onText(CommandContext& ctx, const std::string&)
    {
        ctx.message("Testo non previsto qui.");
        return CommandStatus::Continue;
    }

    // True se il comando aspetta testo libero: la riga di comando non interpreta l'input
    // e lo spazio è un carattere normale invece di Invio.
    virtual bool wantsText() const { return false; }
    // Con wantsText(): true = testo fino a fine riga (contenuto di un testo, valore di un attributo);
    // false = una sola parola (nome di blocco o di layer): lo spazio vale come Invio.
    virtual bool textIsLine() const { return true; }

    // Invio (o spazio, o tasto destro) senza testo.
    virtual CommandStatus onEnter(CommandContext&) { return CommandStatus::Finished; }

    // Esc.
    virtual void cancel(CommandContext&) {}

    // Entità temporanee da disegnare mentre il cursore si muove (effetto "elastico").
    virtual std::vector<std::unique_ptr<Entity>> preview(Vec2 /*cursor*/) const { return {}; }

    // Punto di riferimento per la modalità ORTHO.
    virtual std::optional<Vec2> basePoint() const { return std::nullopt; }

    // True se il comando sta chiedendo di selezionare oggetti (il cursore mostra il quadratino).
    virtual bool wantsSelection() const { return false; }

    // Primo angolo della finestra di selezione in corso, se c'è (la vista disegna il rettangolo).
    virtual std::optional<Vec2> selectionCorner() const { return std::nullopt; }

    // MEASUREGEOM rapido: la vista mostra le misure attorno al cursore (niente snap).
    virtual bool quickMeasure() const { return false; }

    // Selezione a lazo (trascinamento libero) mentre il comando chiede oggetti; false = non gestita.
    virtual bool lassoSelect(CommandContext&, const std::vector<Vec2>&, bool /*crossing*/) { return false; }
};

// Gestisce la selezione con clic singoli e con finestre (due clic, o trascinamento):
// da sinistra a destra = finestra (oggetti interamente dentro),
// da destra a sinistra = intersezione (oggetti anche solo toccati).
class SelectionPicker {
public:
    // Restituisce quanti oggetti sono stati trovati, oppure -1 se il clic ha aperto una finestra.
    int click(CommandContext& ctx, Vec2 p);
    // Lazo: poligono disegnato trascinando. Finestra = oggetti interamente dentro, intersezione = anche toccati.
    int lasso(CommandContext& ctx, const std::vector<Vec2>& polygon, bool crossing);
    void reset() { corner_.reset(); }
    std::optional<Vec2> corner() const { return corner_; }

private:
    int apply(CommandContext& ctx, std::vector<EntityId> ids);
    std::optional<Vec2> corner_;
};

// Base per i comandi che prima chiedono una selezione (ERASE, MOVE, COPY...).
// Se c'è già una selezione quando il comando parte, la usa direttamente.
class SelectingCommand : public Command {
public:
    std::string prompt() const final;
    CommandStatus start(CommandContext& ctx) final;
    CommandStatus onPoint(CommandContext& ctx, Vec2 p) final;
    CommandStatus onValue(CommandContext& ctx, double v) final;
    CommandStatus onKeyword(CommandContext& ctx, const std::string& word) final;
    CommandStatus onEnter(CommandContext& ctx) final;
    void cancel(CommandContext& ctx) override;
    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const final;
    std::optional<Vec2> basePoint() const final;
    bool wantsSelection() const final { return selecting_; }
    std::optional<Vec2> selectionCorner() const final;
    bool lassoSelect(CommandContext& ctx, const std::vector<Vec2>& polygon, bool crossing) final;

protected:
    // Chiamato quando la selezione è completa. ctx.selection contiene gli oggetti.
    virtual CommandStatus onSelected(CommandContext& ctx) = 0;
    virtual std::string actionPrompt() const { return {}; }
    virtual CommandStatus actionPoint(CommandContext& ctx, Vec2 p) { return Command::onPoint(ctx, p); }
    virtual CommandStatus actionValue(CommandContext& ctx, double v) { return Command::onValue(ctx, v); }
    virtual CommandStatus actionKeyword(CommandContext& ctx, const std::string& w) { return Command::onKeyword(ctx, w); }
    virtual CommandStatus actionEnter(CommandContext&) { return CommandStatus::Finished; }
    virtual std::vector<std::unique_ptr<Entity>> actionPreview(Vec2) const { return {}; }
    virtual std::optional<Vec2> actionBasePoint() const { return std::nullopt; }

private:
    void report(CommandContext& ctx, int found) const;
    bool selecting_ = true;
    bool removing_ = false;   // opzione Rimuovi: i clic tolgono dalla selezione
    SelectionPicker picker_;
};

// Opzioni comuni delle richieste "Oggetti da selezionare" (Tutto, Ultimo, Precedente): aggiunge (o toglie, con
// `remove`) gli oggetti indicati. Restituisce quanti ne ha trovati, -1 se la parola non è un'opzione.
int selectionKeyword(CommandContext& ctx, const std::string& word, bool remove);

} // namespace cartesio
