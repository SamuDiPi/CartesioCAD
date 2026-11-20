#pragma once

#include "core/Annotation.h"
#include "core/Entity.h"
#include "core/Layer.h"
#include "core/Plot.h"

#include <string>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cartesio {

// Il disegno: entità, layer e cronologia annulla/ripeti.
// Tutte le modifiche fatte dai comandi passano da commit(), così ogni passo è annullabile.
// Layout (spazio carta): un foglio con finestre sul modello. Lo spazio modello ha id 0.
struct Layout {
    int id = 0;
    std::string name;
    std::string paperName = "A3";
    double paperWidth = 420.0;    // mm
    double paperHeight = 297.0;   // mm
    double margin = 10.0;         // margine non stampabile (mm)
    // Angolo in basso a sinistra del foglio nello spazio carta (i layout non partono
    // sempre da 0,0: sono i limiti del layout).
    Vec2 origin;
    // Impostazioni di stampa. Le misure del foglio e il margine valgono quelli qui sopra:
    // usare layoutPlotSettings() e applyPlotSettings().
    PlotSettings plot = PlotSettings::forLayout(420.0, 297.0, 10.0);
    // Gruppo delle schede (es. "Piano terra"): i layout dello stesso gruppo stanno in una scheda sola quando le
    // schede sono raggruppate. Vuoto = nessun gruppo.
    std::string group;
};

// Impostazioni di stampa del layout con il suo foglio (misure, orientamento e margine del layout).
PlotSettings layoutPlotSettings(const Layout& layout);
// Applica le impostazioni al layout: cambia anche foglio, formato e margine.
void applyPlotSettings(Layout& layout, const PlotSettings& settings);

class Document {
public:
    Document();

    // --- Entità
    const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }
    const Entity* find(EntityId id) const;
    // Entità selezionabile più vicina a p entro la tolleranza (unità mondo), oppure nullptr.
    const Entity* pick(Vec2 p, double tolerance) const;
    // Tutte le entità selezionabili entro la tolleranza, nell'ordine in cui pick() le preferisce (la prima è
    // quella di pick): per il ciclo di selezione sugli oggetti sovrapposti.
    std::vector<const Entity*> pickAll(Vec2 p, double tolerance) const;
    // Selezione a rettangolo. Finestra: solo oggetti interamente dentro.
    // Intersezione (crossing): anche gli oggetti che toccano il rettangolo.
    std::vector<EntityId> selectInRect(const BBox& rect, bool crossing) const;
    // Selezione a lazo (poligono chiuso), con le stesse regole del rettangolo.
    std::vector<EntityId> selectInPolygon(const std::vector<Vec2>& polygon, bool crossing) const;

    // --- Isolamento degli oggetti (ISOLATEOBJECTS, HIDEOBJECTS): vale per la sessione, non si salva e non si
    // annulla (come OBJECTISOLATIONMODE = 0). Gli oggetti nascosti non si vedono, non si selezionano, non si
    // agganciano.
    void hideObjects(const std::vector<EntityId>& ids);
    // Nasconde tutti gli altri oggetti dello spazio attivo.
    void isolateObjects(const std::vector<EntityId>& ids);
    // Mostra di nuovo tutto; restituisce quanti oggetti erano nascosti.
    std::size_t unisolateObjects();
    // Mostra di nuovo solo questi (es. il testo alla fine della modifica nel disegno).
    void showObjects(const std::vector<EntityId>& ids);
    bool isHidden(EntityId id) const { return !hidden_.empty() && hidden_.count(id) != 0; }
    std::size_t hiddenCount() const { return hidden_.size(); }
    // Cresce quando cambia l'isolamento (il disegno va ridisegnato ma non è modificato).
    std::uint64_t visibilityRevision() const { return visibilityRevision_; }
    // Visibile: layer acceso e oggetto non nascosto. Selezionabile: in più, layer non bloccato.
    bool isShown(const Entity& e) const { return isLayerVisible(e.layer()) && !isHidden(e.id()); }
    bool isSelectable(const Entity& e) const { return isLayerSelectable(e.layer()) && !isHidden(e.id()); }
    // Entità visibili il cui ingombro, allargato della tolleranza, contiene p (ricerca veloce).
    // `space` = -1: lo spazio attivo.
    std::vector<const Entity*> entitiesNear(Vec2 p, double tolerance, int space = -1) const;
    // Entità visibili che toccano il rettangolo, tralasciando quelle con ingombro (non nullo) più piccolo di
    // `minSize`: per il ridisegno, che non disegna ciò che sullo schermo è più piccolo di un pixel.
    std::vector<const Entity*> entitiesInRect(const BBox& rect, double minSize = 0.0, int space = -1) const;
    BBox extents(int space = -1) const;
    // Colore ACI con cui va disegnata l'entità (risolve DaLayer / DaBlocco).
    int displayColor(const Entity& e) const;

    // --- Layer
    const std::vector<Layer>& layers() const { return layers_; }
    // Accesso diretto per modificare le proprietà di un layer: chiamare touch() dopo.
    Layer& layer(int index) { return layers_.at(static_cast<std::size_t>(index)); }
    // Acceso e non congelato.
    bool isLayerVisible(int index) const;
    // Visibile e non bloccato.
    bool isLayerSelectable(int index) const;
    // Aggiunge un layer e ne restituisce l'indice; se il nome esiste già restituisce quello esistente.
    int addLayer(Layer layer);
    int findLayer(std::string_view name) const;
    bool renameLayer(int index, const std::string& name, std::string* error = nullptr);
    // Elimina un layer vuoto (non lo 0, non il corrente, non usato da oggetti o dalla cronologia).
    bool removeLayer(int index, std::string* error = nullptr);
    int currentLayer() const { return currentLayer_; }
    void setCurrentLayer(int index);

    // Colore assegnato ai nuovi oggetti (kColorByLayer per default).
    int currentColor() const { return currentColor_; }
    void setCurrentColor(int color) { currentColor_ = color; }

    // --- Stati dei layer (LAYERSTATE): proprietà di tutti i layer salvate con un nome e ripristinabili.
    struct LayerState {
        std::string name;
        std::string description;
        std::string currentLayer;
        std::vector<Layer> layers;
    };
    const std::vector<LayerState>& layerStates() const { return layerStates_; }
    // Salva (o sovrascrive) lo stato attuale dei layer con questo nome.
    void saveLayerState(const std::string& name, const std::string& description = {});
    // Ripristina: i layer con lo stesso nome riprendono le proprietà salvate (gli altri restano come sono).
    // Restituisce quanti layer ha aggiornato, -1 se lo stato non c'è. Si annulla con U (checkpoint).
    int restoreLayerState(const std::string& name);
    bool removeLayerState(const std::string& name);
    void addLoadedLayerState(LayerState state) { layerStates_.push_back(std::move(state)); }

    // --- Gruppi (GROUP): selezionando un oggetto si seleziona tutto il gruppo (PICKSTYLE).
    struct Group {
        std::string name;          // "*A1"... per i gruppi senza nome
        std::string description;
        std::vector<EntityId> ids;
        bool selectable = true;
    };
    const std::vector<Group>& groups() const { return groups_; }
    std::vector<Group>& groups() { return groups_; }
    // Crea un gruppo e ne restituisce il nome (vuoto = senza nome).
    std::string addGroup(std::vector<EntityId> ids, std::string name = {}, std::string description = {});
    // Scioglie i gruppi che contengono questi oggetti; restituisce quanti.
    int removeGroupsOf(const std::vector<EntityId>& ids);
    // Aggiunge alla selezione gli altri oggetti dei gruppi selezionabili.
    void expandGroups(std::vector<EntityId>& ids) const;
    bool groupSelection = true;   // PICKSTYLE
    int wipeoutFrame = 1;         // WIPEOUTFRAME: 0 nascosta, 1 visibile e stampata, 2 visibile non stampata

    // --- Stili di quota (DIMSTYLE)
    const std::vector<DimStyle>& dimStyles() const { return dimStyles_; }
    const DimStyle* findDimStyle(std::string_view name) const;
    // Aggiunge o sostituisce (per nome) uno stile di quota.
    void setDimStyle(DimStyle style);
    // Stile corrente (DIMSTYLE): sempre valido, al limite "Standard".
    const DimStyle& currentDimStyle() const;
    void setCurrentDimStyle(const std::string& name) { currentDimStyle_ = name; }

    // --- Stili delle direttrici (MLEADERSTYLE): altezza del testo, freccia, stile di testo, distanza del testo.
    struct MLeaderStyle {
        std::string name;
        double textHeight = 2.5;
        double arrowSize = 2.5;
        std::string textStyle = "Standard";
        double landingGap = -1.0;   // < 0 = metà altezza del testo
        bool annotative = false;    // misure sulla carta, scalate con la scala di annotazione
        bool operator==(const MLeaderStyle&) const = default;
    };
    const std::vector<MLeaderStyle>& mleaderStyles() const { return mleaderStyles_; }
    const MLeaderStyle* findMLeaderStyle(std::string_view name) const;
    // Aggiunge o sostituisce (per nome).
    void setMLeaderStyle(MLeaderStyle style);
    bool removeMLeaderStyle(const std::string& name, std::string* error = nullptr);
    // Corrente: vuoto = nessuno (le direttrici prendono le misure dallo stile di quota corrente).
    const std::string& currentMLeaderStyle() const { return currentMLeaderStyle_; }
    void setCurrentMLeaderStyle(std::string name) { currentMLeaderStyle_ = std::move(name); }
    // Le direttrici con questo stile ne prendono i valori (passo annullabile); restituisce quante.
    int restyleLeaders(const std::string& name);

    // --- Viste con nome (VIEW): centro e misure dell'area inquadrata, nello spazio indicato.
    struct NamedView {
        std::string name;
        Vec2 center;
        double width = 0.0;
        double height = 0.0;
        int space = 0;   // 0 modello, altrimenti l'id del layout
        bool operator==(const NamedView&) const = default;
    };
    const std::vector<NamedView>& namedViews() const { return namedViews_; }
    const NamedView* findNamedView(std::string_view name) const;
    // Aggiunge o sostituisce (per nome).
    void setNamedView(NamedView view);
    bool removeNamedView(const std::string& name);

    // --- Scale di annotazione (testi, quote e direttrici annotativi)
    const std::vector<AnnoScale>& annoScales() const { return annoScales_; }
    const AnnoScale* findAnnoScale(std::string_view name) const;
    // Scala della lista con questo fattore (unità di disegno per unità di carta), oppure nullptr.
    const AnnoScale* annoScaleForFactor(double factor) const;
    // Sostituisce la lista (SCALELISTEDIT); la scala corrente resta se c'è ancora, altrimenti la prima.
    void setAnnoScales(std::vector<AnnoScale> scales);
    // CANNOSCALE: scala corrente dello spazio modello.
    const std::string& annotationScale() const { return annotationScale_; }
    double annotationFactor() const;
    // Cambia la scala corrente: gli oggetti annotativi del modello che la supportano si ridimensionano (un passo
    // annullabile). False se la scala non è nella lista.
    bool setAnnotationScale(const std::string& name);
    // ANNOALLVISIBLE: solo memorizzata (si legge e si salva con il file). Gli oggetti annotativi si vedono sempre:
    // quelli che non supportano la scala restano alla loro.
    bool annoAllVisible() const { return annoAllVisible_; }
    void setAnnoAllVisible(bool on);
    // L'oggetto annotativo supporta la scala con questo fattore?
    bool supportsFactor(const Entity& e, double factor) const;
    // Oggetto nuovo di un comando: se il suo stile è annotativo diventa annotativo alla scala corrente (le misure
    // date sono quelle sulla carta e si scalano).
    void prepareAnnotation(Entity& e) const;
    // Marca come annotativo alla scala corrente un oggetto già alle misure del modello.
    void markAnnotative(Entity& e) const;
    // Stile di quota per una quota nuova: se è annotativo, DIMSCALE = scala di annotazione corrente.
    DimStyle dimStyleForNew() const;
    // Aggiunge o toglie una scala agli oggetti annotativi (OBJECTSCALE, passo annullabile). Restituisce quanti.
    int addObjectScale(const std::vector<EntityId>& ids, const std::string& scale);
    int removeObjectScale(const std::vector<EntityId>& ids, const std::string& scale);
    // Rende annotativi (o no) gli oggetti (proprietà "Annotativo", passo annullabile). Restituisce quanti.
    int setAnnotative(const std::vector<EntityId>& ids, bool on);
    // Dopo la lettura di un file: gli oggetti del modello che supportano la scala corrente si portano a quella.
    void normalizeAnnotations();
    // Lettori di file: scala corrente e ANNOALLVISIBLE senza toccare gli oggetti né la cronologia.
    void setLoadedAnnotationScale(const std::string& name, bool allVisible);

    // --- Stili di testo: eliminazione (se non usato) e cambio di nome (anche negli oggetti che lo usano).
    bool textStyleUsed(const std::string& name) const;
    bool removeTextStyle(const std::string& name, std::string* error = nullptr);
    bool renameTextStyle(const std::string& from, const std::string& to, std::string* error = nullptr);

    // --- AUDIT: controllo del disegno. Con `fix` gli errori si correggono (un passo annullabile per gli oggetti).
    struct AuditReport {
        int found = 0;
        int fixed = 0;
        std::vector<std::string> lines;
    };
    AuditReport audit(bool fix);
    // PURGE: elimina definizioni non più usate (non annullabile). Restituiscono i nomi eliminati.
    std::vector<std::string> purgeBlocks();
    std::vector<std::string> purgeLayers();
    std::vector<std::string> purgeLinetypes();
    std::vector<std::string> purgeDimStyles();
    // Copia i valori dello stile nelle quote che lo usano (passo annullabile). Restituisce quante.
    int restyleDimensions(const std::string& name);
    // --- Stili di testo
    const std::vector<TextStyle>& textStyles() const { return textStyles_; }
    // Stile per nome (senza distinzione fra maiuscole e minuscole), oppure nullptr.
    const TextStyle* findTextStyle(std::string_view name) const;
    // Stile di un testo: quello col suo nome, altrimenti "Standard".
    const TextStyle& textStyleOrStandard(std::string_view name) const;
    // Aggiunge (o sostituisce, stesso nome) uno stile di testo.
    void setTextStyle(TextStyle style);
    const std::string& currentTextStyle() const { return currentTextStyle_; }
    // File da cui è stato letto il disegno (per i riferimenti esterni con percorso relativo: immagini, XREF).
    const std::string& filePath() const { return filePath_; }
    void setFilePath(std::string path) { filePath_ = std::move(path); }
    void setCurrentTextStyle(std::string name) { currentTextStyle_ = std::move(name); }


    // --- Tipi e spessori di linea
    const std::vector<Linetype>& linetypes() const { return linetypes_; }
    // Tipo di linea per nome (senza distinzione fra maiuscole e minuscole), oppure nullptr.
    const Linetype* findLinetype(std::string_view name) const;
    // Aggiunge (o sostituisce) un tipo di linea.
    void addLinetype(Linetype linetype);
    // Carica un tipo di linea standard (DASHED, CENTER...) se non c'è già. False se sconosciuto.
    bool loadStandardLinetype(std::string_view name);
    // Tipo di linea e spessore assegnati ai nuovi oggetti (CELTYPE, CELWEIGHT).
    const std::string& currentLinetype() const { return currentLinetype_; }
    void setCurrentLinetype(std::string name) { currentLinetype_ = std::move(name); }
    int currentLineweight() const { return currentLineweight_; }
    void setCurrentLineweight(int lw) { currentLineweight_ = lw; }
    // Scala globale dei tipi di linea (LTSCALE).
    double ltscale() const { return ltscale_; }
    void setLtscale(double s) { if (s > 0.0) { ltscale_ = s; ++revision_; } }
    // Tipo di linea effettivo (nullptr = continua). `blockLinetype` serve agli oggetti DaBlocco.
    const Linetype* effectiveLinetype(const Entity& e, const std::string& blockLinetype = {}) const;
    // Spessore effettivo in centesimi di mm (Default = 25). `blockLineweight` per gli oggetti DaBlocco.
    int effectiveLineweight(const Entity& e, int blockLineweight = kLineweightDefault) const;

    // --- Layout
    const std::vector<Layout>& layouts() const { return layouts_; }
    Layout* findLayout(int id);
    const Layout* findLayout(int id) const;
    // Crea un layout con un nome libero (es. "Layout2") e ne restituisce l'id.
    int addLayout(const std::string& name = {});
    bool renameLayout(int id, const std::string& name, std::string* error = nullptr);
    // Elimina il layout e i suoi oggetti (l'eliminazione degli oggetti si può annullare).
    bool removeLayout(int id, std::string* error = nullptr);
    // Spazio in cui si disegna e che si vede: 0 = modello, altrimenti l'id di un layout.
    int activeSpace() const { return activeSpace_; }
    void setActiveSpace(int space) { activeSpace_ = space; }
    // Sposta tutti gli oggetti di uno spazio in un altro (usato dai lettori di file).
    void reassignSpace(int from, int to)
    {
        for (auto& e : entities_)
            if (e->space() == from)
                e->setSpace(to);
        ++revision_;
    }
    int resolveSpace(int space) const { return space < 0 ? activeSpace_ : space; }
    // Usato dai lettori di file: elimina i layout predefiniti prima di caricare quelli del file.
    void clearLayouts()
    {
        layouts_.clear();
        nextLayoutId_ = 1;
        activeSpace_ = 0;
    }

    // --- Stampa
    // Impostazioni di stampa dello spazio modello.
    const PlotSettings& modelPlot() const { return modelPlot_; }
    void setModelPlot(const PlotSettings& s)
    {
        modelPlot_ = s;
        modelPlot_.setFlag(kPlotModelType, true);
        ++revision_;
    }
    // Impostazioni di pagina con nome (PAGESETUP), per il modello o per i layout.
    const std::vector<PlotSettings>& pageSetups() const { return pageSetups_; }
    const PlotSettings* findPageSetup(std::string_view name) const;
    // Aggiunge o sostituisce (stesso nome, maiuscole ignorate) un'impostazione di pagina.
    void setPageSetup(const PlotSettings& s);
    bool removePageSetup(std::string_view name);
    // Unità del disegno ($INSUNITS): 0 senza unità, 1 pollici, 4 mm, 5 cm, 6 m...
    // File di progetto dell'impianto (.cdb) collegato al disegno, come scritto (relativo alla cartella del disegno
    // o assoluto); vuoto se nessuno.
    const std::string& plantFile() const { return plantFile_; }
    void setPlantFile(std::string path)
    {
        plantFile_ = std::move(path);
        ++revision_;
    }
    // Canaline portacavi del disegno: codice (CN1, CN2...) -> stato della sezione di CanaloCAD in JSON (misure,
    // tipologia, staffaggio, cavi...). Gli oggetti della canalina hanno il codice (Entity::canaloTray). Le modifiche
    // si annullano come quelle dei layer: dopo il cambio, checkpoint().
    const std::map<std::string, std::string>& canaloTrays() const { return canaloTrays_; }
    const std::string* canaloTray(const std::string& id) const
    {
        const auto it = canaloTrays_.find(id);
        return it == canaloTrays_.end() ? nullptr : &it->second;
    }
    void setCanaloTray(const std::string& id, std::string json)
    {
        canaloTrays_[id] = std::move(json);
        ++revision_;
    }
    void removeCanaloTray(const std::string& id)
    {
        if (canaloTrays_.erase(id))
            ++revision_;
    }
    // Prossimo codice libero ("CN1", "CN2"...).
    std::string nextCanaloTrayId() const
    {
        for (int n = 1;; ++n)
            if (!canaloTrays_.count("CN" + std::to_string(n)))
                return "CN" + std::to_string(n);
    }
    int insUnits() const { return insUnits_; }
    void setInsUnits(int units) { insUnits_ = units; }
    // Limiti del disegno nello spazio modello ($LIMMIN, $LIMMAX).
    Vec2 limitsMin() const { return limitsMin_; }
    Vec2 limitsMax() const { return limitsMax_; }
    void setLimits(Vec2 min, Vec2 max)
    {
        limitsMin_ = min;
        limitsMax_ = max;
        ++revision_;
    }

    // --- Blocchi
    const std::vector<std::shared_ptr<BlockDef>>& blocks() const { return blocks_; }
    std::shared_ptr<BlockDef> findBlock(std::string_view name) const;
    // Elimina la definizione di un blocco (lettori di file e PURGE).
    bool eraseBlock(const std::string& name)
    {
        const auto before = blocks_.size();
        std::erase_if(blocks_, [&name](const std::shared_ptr<BlockDef>& b) { return b->name == name; });
        return blocks_.size() != before;
    }
    // Aggiunge una definizione di blocco (sostituisce quella con lo stesso nome) e ricollega i riferimenti.
    void addBlock(std::shared_ptr<BlockDef> block);
    // Collega inserimenti e quote alle definizioni di blocco, per nome (dopo la lettura di un file).
    void linkBlocks();

    // --- Modifiche annullabili
    // Aggiunge e rimuove entità in un unico passo di annullamento.
    // Le entità nuove (id == 0) ricevono un id; quelle senza layer o colore (-1) ricevono quelli correnti.
    // Per modificare un'entità: commit({copia modificata con lo stesso id}, {id}).
    void commit(std::vector<std::unique_ptr<Entity>> added, const std::vector<EntityId>& removed = {});
    // L'ultimo passo ha solo aggiunto l'oggetto `id` (un testo nuovo scritto nel disegno): lo sostituisce con
    // `replacement` nello stesso passo, o con nullptr toglie oggetto e passo (testo lasciato vuoto). False se
    // l'ultimo passo non è quello.
    bool amendLastAdd(EntityId id, std::unique_ptr<Entity> replacement);
    EntityId add(std::unique_ptr<Entity> entity);
    void erase(const std::vector<EntityId>& ids);
    // Ordine di visualizzazione (DRAWORDER), annullabile: gli oggetti `ids` in primo piano, dietro, oppure
    // subito sopra o sotto l'oggetto `reference`. L'ordine è anche quello di scrittura del file.
    enum class DrawOrder { Front, Back, Above, Under };
    void reorder(const std::vector<EntityId>& ids, DrawOrder where, EntityId reference = 0);
    // Dopo la lettura di un file: ordine secondo una chiave (tabella SORTENTSTABLE), senza passo da
    // annullare. Gli oggetti senza chiave restano dopo quello che li precede.
    void sortLoaded(const std::function<std::optional<std::uint64_t>(const Entity&)>& key);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

    // --- Annullamento di layer e blocchi
    // Le modifiche dirette a layer, layer corrente e definizioni dei blocchi (gestore layer, BATTMAN, BSAVE,
    // riferimenti esterni...) diventano annullabili qui: si confronta lo stato con quello dell'ultimo checkpoint
    // e la differenza si unisce all'ultimo passo, se è nato nel frattempo (un solo Annulla per un comando),
    // altrimenti diventa un passo da sola. Da chiamare dopo ogni passo (DrawingWidget::afterStep).
    // L'eliminazione di layer (sposta gli indici degli oggetti) non si annulla.
    void checkpoint();
    // Lo stato attuale diventa il riferimento (dopo la lettura di un file).
    void resetCheckpoint();
    // Definizione da modificare sul posto: se serve alla cronologia se ne fa prima una copia (gli inserimenti
    // passano alla copia), così Annulla ritrova quella di prima.
    std::shared_ptr<BlockDef> editableBlock(std::string_view name);

    // --- Caricamento da file (non annullabile)
    EntityId insertLoaded(std::unique_ptr<Entity> entity);

    // Cresce a ogni modifica: serve a capire se il disegno va salvato.
    std::uint64_t revision() const { return revision_; }
    // Parte delle revisioni che non riguarda solo gli oggetti (layer, blocchi, stili, caricamento...): se non cambia,
    // fra due revisioni sono cambiati solo degli oggetti (riconoscibili dal loro serial()), e le cache possono
    // aggiornare solo quelli.
    std::uint64_t tableRevision() const { return revision_ - entityRevision_; }
    // Sostituisce un'entità con una sua copia modificata (stesso id) senza cronologia:
    // per zoom e pan dentro le finestre dei layout.
    void replaceWithoutUndo(std::unique_ptr<Entity> entity)
    {
        for (auto& e : entities_)
            if (e->id() == entity->id()) {
                e = std::move(entity);
                ++revision_;
                ++entityRevision_;   // solo un oggetto
                return;
            }
    }
    // Rimuove un oggetto senza cronologia (lettori di file).
    void eraseLoaded(EntityId id)
    {
        removeById(id);
        ++revision_;
    }
    // Segnala una modifica non annullabile (es. proprietà dei layer).
    void touch() { ++revision_; }

private:
    // Layer, layer corrente e blocchi (le definizioni si condividono: chi le cambia ne fa una copia).
    // Tabelle annullabili: layer, blocchi, stili (testo, quota, direttrice) e scale di annotazione.
    struct TableState {
        std::vector<Layer> layers;
        int currentLayer = 0;
        std::vector<std::shared_ptr<BlockDef>> blocks;
        std::vector<TextStyle> textStyles;
        std::string currentTextStyle;
        std::vector<DimStyle> dimStyles;
        std::string currentDimStyle;
        std::vector<MLeaderStyle> mleaderStyles;
        std::string currentMLeaderStyle;
        std::vector<AnnoScale> annoScales;
        std::string annotationScale;
        bool annoAllVisible = true;
        std::map<std::string, std::string> canaloTrays;
        // Stili e scale uguali (i layer e i blocchi si confrontano a parte).
        bool sameStyles(const TableState& o) const
        {
            return textStyles == o.textStyles && currentTextStyle == o.currentTextStyle && dimStyles == o.dimStyles
                   && currentDimStyle == o.currentDimStyle && mleaderStyles == o.mleaderStyles
                   && currentMLeaderStyle == o.currentMLeaderStyle && annoScales == o.annoScales
                   && annotationScale == o.annotationScale && annoAllVisible == o.annoAllVisible
                   && canaloTrays == o.canaloTrays;
        }
    };
    struct Step {
        std::vector<std::unique_ptr<Entity>> added;    // copie delle entità aggiunte
        std::vector<std::unique_ptr<Entity>> removed;  // copie delle entità rimosse
        std::vector<std::size_t> removedAt;            // loro posizione nell'ordine di disegno (Annulla le rimette lì)
        std::vector<EntityId> orderBefore;             // DRAWORDER: ordine prima e dopo
        std::vector<EntityId> orderAfter;
        std::shared_ptr<const TableState> tablesBefore;   // layer e blocchi prima e dopo (se cambiati)
        std::shared_ptr<const TableState> tablesAfter;
    };
    TableState currentTables() const;
    void restoreTables(const TableState& state);
    TableState checkpoint_;
    bool hasCheckpoint_ = false;
    std::size_t checkpointUndo_ = 0;
    void setOrder(const std::vector<EntityId>& order);

    void apply(const Step& step, bool forward);
    void applyEntities(const Step& step, bool forward);
    void removeById(EntityId id);
    void prepareNew(Entity& e);

    std::vector<std::unique_ptr<Entity>> entities_;
    std::vector<Layer> layers_;
    std::vector<std::shared_ptr<BlockDef>> blocks_;
    std::vector<Layout> layouts_;
    PlotSettings modelPlot_ = PlotSettings::forModel();
    int insUnits_ = 0;
    std::vector<PlotSettings> pageSetups_;
    Vec2 limitsMin_{0.0, 0.0};
    Vec2 limitsMax_{420.0, 297.0};
    int activeSpace_ = 0;
    int nextLayoutId_ = 1;
    int currentLayer_ = 0;
    int currentColor_ = kColorByLayer;
    std::vector<DimStyle> dimStyles_;
    std::vector<Group> groups_;
    std::vector<LayerState> layerStates_;
    std::string currentDimStyle_ = "Standard";
    std::vector<MLeaderStyle> mleaderStyles_;
    std::string currentMLeaderStyle_;
    std::vector<Linetype> linetypes_;
    std::vector<TextStyle> textStyles_;
    std::string currentTextStyle_ = "Standard";
    std::vector<NamedView> namedViews_;
    std::vector<AnnoScale> annoScales_ = defaultAnnoScales();
    std::string annotationScale_ = "1:1";
    bool annoAllVisible_ = true;
    std::string filePath_;
    std::string currentLinetype_ = "ByLayer";
    std::unordered_set<EntityId> hidden_;
    std::uint64_t visibilityRevision_ = 0;
    int currentLineweight_ = kLineweightByLayer;
    double ltscale_ = 1.0;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    EntityId nextId_ = 1;
    std::uint64_t revision_ = 0;
    std::string plantFile_;
    std::map<std::string, std::string> canaloTrays_;
    // Ingombri delle entità, ricalcolati quando cambia la revisione.
    // Ingombri degli oggetti, allineati a entities_, con il serial dell'oggetto di cui sono l'ingombro.
    struct CachedBounds {
        std::uint64_t serial = 0;
        BBox box;
    };
    mutable std::vector<CachedBounds> boundsCache_;
    mutable std::uint64_t boundsRevision_ = ~std::uint64_t{0};
    mutable std::uint64_t boundsTableRevision_ = ~std::uint64_t{0};
    std::uint64_t entityRevision_ = 0;   // revisioni che hanno cambiato solo oggetti (vedi tableRevision)
    // Indice id -> posizione in entities_ per find(): rifatto quando cambia la revisione o non torna più.
    // Senza, find() scorreva tutto il disegno: con 16.000 oggetti selezionati un clic costava secondi.
    mutable std::unordered_map<EntityId, std::size_t> idIndex_;
    mutable std::uint64_t idRevision_ = ~std::uint64_t{0};
    void rebuildIdIndex() const;
    void ensureBounds() const;
};

} // namespace cartesio
