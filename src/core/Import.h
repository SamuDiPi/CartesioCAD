#pragma once

#include "core/Document.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cartesio {

// Le tabelle di un disegno di origine (layer, tipi di linea, stili di quota, blocchi), per copiarne
// gli oggetti in un altro disegno: appunti fra disegni e inserimento di un file come blocco.
struct ImportSource {
    std::vector<Layer> layers;
    std::vector<Linetype> linetypes;
    std::vector<DimStyle> dimStyles;
    std::vector<TextStyle> textStyles;
    std::vector<std::shared_ptr<BlockDef>> blocks;

    static ImportSource from(const Document& doc);
    std::shared_ptr<const BlockDef> findBlock(const std::string& name) const;
};

// Copia oggetti nel disegno di destinazione ritrovando per nome layer, tipi di linea, stili di quota e
// blocchi (anche annidati). Quelli che mancano vengono aggiunti; un blocco con lo stesso nome già
// presente nel disegno di destinazione viene usato com'è.
class Importer {
public:
    Importer(Document& target, const ImportSource& source) : doc_(target), src_(source) {}

    // Copia nuova (id 0) dell'oggetto, con i riferimenti del disegno di destinazione.
    std::unique_ptr<Entity> copy(const Entity& e);

    // Riferimenti esterni: layer e blocchi del disegno di origine prendono il prefisso "NOME|" (il layer 0 no).
    void setPrefix(std::string prefix) { prefix_ = std::move(prefix); }

private:
    std::string named(const std::string& name) const;
    void fix(Entity& e);
    int layer(int sourceIndex);
    void linetype(const std::string& name);
    void textStyle(const std::string& name);   // lo stile di testo di origine, se manca
    std::shared_ptr<const BlockDef> block(const std::string& name);

    Document& doc_;
    const ImportSource& src_;
    std::map<int, int> layers_;
    std::string prefix_;
    std::set<std::string> importing_;   // blocchi in corso di copia (difesa dai riferimenti circolari)
};

// INSERT di un file: crea (o ridefinisce) nel disegno un blocco `name` con tutto lo spazio modello
// del disegno di origine, con i suoi layer, tipi di linea, stili e blocchi.
// `scale`: fattore per portare le unità del file in quelle del disegno (opzione "Scala di inserimento").
void importDrawingAsBlock(Document& target, const Document& source, const std::string& name, double scale = 1.0);

// Riferimento esterno: riempie il blocco `xref` (che conserva percorso e opzioni) con lo spazio modello di
// `source`; layer e blocchi del file esterno si chiamano "NOME|layer".
void loadXref(Document& target, const BlockDef& xref, const Document& source, double scale = 1.0);

// Associa (BIND) un riferimento esterno caricato: diventa un blocco del disegno, che non dipende più dal file.
// Layer e blocchi "NOME|x" diventano "NOME$0$x" (Associa) oppure, con `insert`, prendono il nome "x" unendosi a
// quelli del disegno che si chiamano così (Inserisci). Falso (con `error`) se non si può.
bool bindXref(Document& doc, const std::string& name, bool insert, std::string* error = nullptr);

// Editor blocchi (BEDIT): gli oggetti del blocco `name` di `host` diventano lo spazio modello di `out` (nuovo),
// con tutti i layer di `host` per poterli usare; le coordinate restano quelle del blocco (punto base compreso).
bool copyBlockForEditing(const Document& host, const std::string& name, Document& out, std::string* error = nullptr);
// BSAVE: il blocco `name` di `host` si ridefinisce con lo spazio modello di `edited` (stesso punto base); gli
// inserimenti lo seguono. Restituisce il numero di inserimenti nel disegno, -1 se non si può (con `error`).
int saveEditedBlock(Document& host, const std::string& name, const Document& edited, std::string* error = nullptr);
// Importa nel disegno il blocco `name` di un altro disegno (con layer e blocchi annidati). Se il disegno ha già
// un blocco con quel nome si usa il suo. Falso se `source` non ha il blocco.
// `scale`: unità del file libreria in quelle del disegno (il blocco importato si scala, punto base compreso).
bool importBlock(Document& target, const Document& source, const std::string& name, double scale = 1.0);
// Blocchi che si possono inserire o modificare (non anonimi, non riferimenti esterni, non layout).
std::vector<std::string> userBlockNames(const Document& doc);

// Due conversioni dello stesso DWG (LibreDWG 0.14 perde cose diverse nelle due versioni di DXF): in `base`
// ogni blocco diventa la versione con più oggetti e i layout vuoti prendono il contenuto da `alt`.
struct DwgMergeReport {
    int blocks = 0;          // blocchi completati
    int layouts = 0;         // layout riempiti
    std::size_t entities = 0;   // oggetti aggiunti ai layout
};
DwgMergeReport mergeAlternativeConversion(Document& base, const Document& alt);

} // namespace cartesio
