#pragma once

#include "core/commands/Command.h"

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cartesio {

// Registro dei comandi e gestione del comando attivo.
class CommandManager {
public:
    using Factory = std::function<std::unique_ptr<Command>()>;

    // Registra un comando con il suo nome e gli alias (es. "LINE", {"L", "LINEA"}).
    void add(const std::string& name, std::initializer_list<std::string> aliases, Factory factory);

    // Avvia un comando dato il nome o un alias. Restituisce false se non esiste.
    bool execute(const std::string& nameOrAlias, CommandContext& ctx);
    // Avvia un comando creato dal programma (es. la modifica con le maniglie).
    void start(std::unique_ptr<Command> command, const std::string& name, CommandContext& ctx);

    bool isActive() const { return active_ != nullptr; }
    const Command* active() const { return active_.get(); }
    const std::string& activeName() const { return activeName_; }
    const std::string& lastCommand() const { return last_; }
    std::string prompt() const;
    std::vector<std::string> commandNames() const;
    // Nomi e alias di tutti i comandi (per l'autocompletamento).
    std::vector<std::string> commandWords() const;
    // Chiamata quando un comando finisce: nome e true se è stato annullato (Esc).
    std::function<void(const std::string& name, bool cancelled)> finished;

    void point(CommandContext& ctx, Vec2 p);
    void value(CommandContext& ctx, double v);
    void keyword(CommandContext& ctx, const std::string& word);
    void enter(CommandContext& ctx);
    void text(CommandContext& ctx, const std::string& text);
    void cancel(CommandContext& ctx);
    // Selezione a lazo per il comando attivo (se sta chiedendo oggetti).
    bool lasso(CommandContext& ctx, const std::vector<Vec2>& polygon, bool crossing)
    {
        return active_ && active_->lassoSelect(ctx, polygon, crossing);
    }

private:
    void handle(CommandStatus status);

    std::map<std::string, Factory> factories_;
    std::map<std::string, std::string> aliases_;   // alias -> nome
    std::unique_ptr<Command> active_;
    std::string activeName_;
    std::string last_;
};

// Registra tutti i comandi di base (disegno, modifica, visualizzazione).
void registerBuiltinCommands(CommandManager& manager);

// Registra i comandi di modifica (ERASE, MOVE, COPY, OFFSET, TRIM). Chiamata da registerBuiltinCommands.
void registerModifyCommands(CommandManager& manager);

// Comandi di annotazione (TEXT, MTEXT, quote, HATCH) e dei blocchi (BLOCK, INSERT, EXPLODE).
void registerAnnotateCommands(CommandManager& manager);
void registerBlockCommands(CommandManager& manager);
void registerLayoutCommands(CommandManager& manager);
// Variabili di sistema (SETVAR, CLAYER, OSMODE...), -LAYER e COLOR: usati soprattutto dagli script.
void registerSystemCommands(CommandManager& manager);
// Altezza predefinita dei testi (variabile TEXTSIZE).
double& textSizeSetting();
// Motivo, scala e angolo (gradi) proposti da HATCH: li cambia anche la finestra di scelta del motivo.
struct HatchDefaults {
    std::string& pattern;
    double& scale;
    double& angle;
};
HatchDefaults hatchDefaults();
// ROTATE, SCALE, MIRROR, ARRAY, EXTEND, FILLET, MATCHPROP, DIST, ID, AREA, LIST, COPYCLIP, PASTECLIP.
void registerEditCommands(CommandManager& manager);
// Mette negli appunti del programma gli oggetti di `source` indicati (come COPYCLIP): PASTECLIP / Ctrl+V li incolla in
// qualsiasi disegno aperto, con layer, stili di testo, tipi di linea e blocchi di `source`.
void setClipboard(const Document& source, const std::vector<EntityId>& ids, Vec2 base);
// Altri comandi: POLYGON, REVCLOUD, XLINE, RAY, DIVIDE, MEASURE, LENGTHEN, ALIGN, OVERKILL, PURGE, SELECTSIMILAR.
void registerExtraCommands(CommandManager& manager);
// Comando che sposta la maniglia `gripIndex` (indice in snapPoints()) dell'entità.
std::unique_ptr<Command> makeGripEditCommand(EntityId id, std::size_t gripIndex, Vec2 from);
// Azione del menu di una maniglia multifunzione (valore di GripAction): chiede il punto se serve.
std::unique_ptr<Command> makeGripActionCommand(EntityId id, std::size_t gripIndex, int action, Vec2 from);
// Chiede i due angoli di una finestra (stampa "Finestra"); `done` riceve il rettangolo o nullopt se annullato.
std::unique_ptr<Command> makeWindowPickCommand(std::function<void(std::optional<BBox>)> done);

} // namespace cartesio
