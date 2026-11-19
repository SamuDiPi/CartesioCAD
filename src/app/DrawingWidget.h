#pragma once

#include "app/DraftSettings.h"
#include "core/Document.h"
#include "core/commands/CommandManager.h"
#include "core/io/Dxf.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

namespace cartesio {

class CadView;

// Un disegno aperto: documento, selezione, comandi in corso e vista.
// La finestra principale ne tiene uno per ogni scheda.
class DrawingWidget : public QWidget {
    Q_OBJECT

public:
    DrawingWidget(const DraftSettings& settings, const QString& untitledName, QWidget* parent = nullptr);

    Document& document() { return doc_; }
    const Document& document() const { return doc_; }
    std::vector<EntityId>& selection() { return selection_; }
    const std::vector<EntityId>& selection() const { return selection_; }
    const CommandManager& commands() const { return commands_; }
    CadView* view() const { return view_; }   // la vista attiva
    // Finestre del modello affiancate (VPORTS): 1, 2 verticali, 2 orizzontali, 3 (una grande a sinistra), 4.
    enum class Tiles { One, TwoVertical, TwoHorizontal, Three, Four };
    void setTiles(Tiles tiles);
    Tiles tiles() const { return tiles_; }
    const std::vector<CadView*>& views() const { return views_; }

    QString filePath() const { return filePath_; }
    // Editor blocchi (BEDIT): questa scheda modifica il blocco `name` del disegno `host`.
    void setBlockEdit(DrawingWidget* host, const QString& name) { blockHost_ = host; blockName_ = name; }
    bool isBlockEditor() const { return !blockName_.isEmpty(); }
    DrawingWidget* blockHost() const { return blockHost_; }
    QString blockName() const { return blockName_; }
    // True se il disegno è stato aperto o salvato come DWG.
    bool isDwg() const { return fromDwg_; }
    QString displayName() const;
    bool isModified() const { return doc_.revision() != savedRevision_; }
    // "Comando:" oppure la richiesta del comando attivo.
    QString prompt() const;

    // Variabili di sistema dell'interfaccia (OSMODE, ORTHOMODE...), lette e scritte dai comandi.
    void setVariableHandlers(std::function<std::string(const std::string&)> get,
                             std::function<bool(const std::string&, const std::string&)> set)
    {
        ctx_.getVar = std::move(get);
        ctx_.setVar = std::move(set);
    }

    // HATCH, opzione Motivo: finestra con le anteprime (restituisce false per tornare alla riga di comando).
    void setHatchPatternChooser(std::function<bool()> choose) { ctx_.chooseHatchPattern = std::move(choose); }
    // TEXT e MTEXT scrivono il testo nuovo direttamente nel disegno se `allowed` dice di sì (non negli script).
    void setTextInPlaceAllowed(std::function<bool()> allowed);

    // Comandi dell'interfaccia (es. LAYER) gestiti dalla finestra principale invece che dal nucleo.
    void setUiCommandHandler(std::function<bool(const QString&)> handler) { uiHandler_ = std::move(handler); }

    // --- Riga di comando
    // True se il comando attivo aspetta testo libero (lo spazio non vale come Invio).
    bool wantsText() const { return commands_.active() && commands_.active()->wantsText(); }
    // Testo fino a fine riga: lo spazio è un carattere normale.
    bool wantsLine() const { return wantsText() && commands_.active()->textIsLine(); }
    void submit(const QString& text);
    void runCommand(const QString& name);
    // Modifica di un testo nel disegno in corso: va confermata prima di un comando o di un Esc dall'esterno.
    void setTextEditorCommit(std::function<void()> commit) { textEditorCommit_ = std::move(commit); }
    // Avvia un comando creato dall'interfaccia (es. la scelta della finestra di stampa).
    void startCommand(std::unique_ptr<Command> command, const QString& name);
    void cancel();

    // --- Modifiche dalle tavolozze (annullabili)
    void modifyEntities(const std::vector<EntityId>& ids, const std::function<void(Entity&)>& change);
    void setSelection(std::vector<EntityId> ids);
    // Passa allo spazio modello (0) o a un layout.
    void setSpace(int space);
    // Layout mostrato (0 = modello).
    int shownSpace() const;
    // Lavoro sul modello attraverso una finestra del layout (0 = si torna al foglio).
    void setViewport(EntityId id);
    bool inViewport() const { return shownSpace() != 0 && doc_.activeSpace() == 0; }
    // Da chiamare dopo modifiche dirette al documento (es. proprietà dei layer).
    void notifyChanged();

    // --- File
    bool load(const QString& path, QString* error, QStringList* notes);
    // Mostra un documento già letto (anche in un altro thread con readFile) come contenuto di questa scheda.
    void adopt(Document&& document, const QString& path, const DxfReadResult& result, QStringList* notes);
    // Disegno aperto da un modello: resta senza nome (Salva chiede dove), il modello non si tocca.
    void forgetFile();
    // Legge un DXF o un DWG in un documento (senza aprirlo in una scheda). Non tocca l'interfaccia: si può
    // chiamare da un altro thread; `phase` riceve il nome della fase in corso (conversione, lettura...).
    static bool readFile(const QString& path, Document& out, QString* error, DxfReadResult* details = nullptr,
                         const std::function<void(const QString&)>& phase = {});
    // Carica i riferimenti esterni (XREF) di un disegno appena letto da `hostPath`.
    static void loadXrefs(const QString& hostPath, Document& doc, DxfReadResult& result);
    // Inserisce un altro disegno come blocco e avvia INSERT per posizionarlo.
    bool insertFile(const QString& path, QString* error);
    // Collega un disegno come riferimento esterno (XATTACH) e avvia INSERT per posizionarlo.
    // `pathType`: RefPathType (relativo, completo, solo nome); -1 = quello delle Opzioni.
    bool attachXref(const QString& path, QString* error, int pathType = -1);
    // I DWG vengono creati passando da un DXF 2000; per i DXF si usa la versione indicata.
    bool saveTo(const QString& path, QString* error, DxfVersion version);
    bool saveTo(const QString& path, QString* error) { return saveTo(path, error, dxfVersion_); }
    DxfVersion dxfVersion() const { return dxfVersion_; }

signals:
    // Una vista nuova (finestre affiancate): la finestra principale le collega i suoi gestori.
    void viewAdded(CadView* view);
    // È cambiata la vista attiva o la disposizione delle viste.
    void viewsChanged();
    void message(const QString& text);
    // EATTEDIT: va aperta la modifica degli attributi di un inserimento.
    void attributesRequested(cartesio::EntityId id);
    void changed();
    void cursorMoved(cartesio::Vec2 world);
    // Un comando è finito (percorsi guidati): nome e true se annullato.
    void commandFinished(const QString& name, bool cancelled);

private:
    void onPointPicked(Vec2 world);
    bool startGripEdit(Vec2 p);
    bool startCommandWord(const std::string& word);
    void afterStep();

    Document doc_;
    std::vector<EntityId> selection_;
    CommandManager commands_;
    CommandContext ctx_;
    SelectionPicker idlePicker_;
    CadView* view_ = nullptr;
    std::vector<CadView*> views_;
    Tiles tiles_ = Tiles::One;
    const DraftSettings* settings_ = nullptr;
    void connectView(CadView* view);
    void layoutTiles();

    QString untitledName_;
    QString filePath_;
    bool fromDwg_ = false;
    DxfVersion dxfVersion_ = DxfVersion::R12;
    std::uint64_t savedRevision_ = 0;
public:
    // Il disegno com'è adesso non ha modifiche da salvare (es. dopo le impostazioni dei disegni nuovi).
    void markSaved() { savedRevision_ = doc_.revision(); emit changed(); }
private:
    QPointer<DrawingWidget> blockHost_;
    QString blockName_;
    std::function<bool(const QString&)> uiHandler_;
    bool followUpRunning_ = false;
    std::function<void()> textEditorCommit_;

public:
    // Conferma la modifica di un testo in corso (es. cambiando scheda).
    void commitTextEditor()
    {
        if (textEditorCommit_) {
            const auto commit = std::move(textEditorCommit_);
            textEditorCommit_ = nullptr;
            commit();
        }
    }
};

} // namespace cartesio
