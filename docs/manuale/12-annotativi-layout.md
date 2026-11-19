# Scale annotative, layout e finestre

## Scale annotative (`CANNOSCALE`)

**Dove:** barra di stato, accanto a MODELLO (la scala, per esempio 1:50); Annota > Scale di annotazione.

Testi, testi multilinea, quote e direttrici creati con uno stile **annotativo** (casella *Annotativo* negli stili di testo, di quota e delle direttrici, o proprietà *Annotativo*) hanno le misure **sulla carta**: un testo alto 2,5 a 1:50 è alto 125 nel modello. Cambiando la scala di annotazione gli oggetti che la supportano si ridimensionano da soli; nei layout ogni finestra li mostra alla sua scala, anche in stampa.

## Mostra tutti gli annotativi (`ANNOALLVISIBLE`)

**Dove:** pulsante **A** nella barra di stato.

Con 1 si vedono tutti gli oggetti annotativi (alla loro scala); con 0 si nascondono quelli che non supportano la scala corrente.

## Scale degli oggetti (`OBJECTSCALE`, `SCALAOGGETTO`, `-OBJECTSCALE`)

**Dove:** Annota > Scale di annotazione > Aggiungi/Elimina scale; *Aggiungi scala corrente*.

Aggiunge o toglie scale di annotazione agli oggetti annotativi selezionati: un oggetto con più scale si vede in tutte, ciascuna con la sua dimensione.

## Elenco delle scale (`SCALELISTEDIT`, `-SCALELISTEDIT`, `MODIFICAELENCOSCALE`)

**Dove:** Annota > Scale di annotazione > Elenco scale.

`[?/Aggiungi/Elimina/Reimposta]`: elenco, aggiungi (per esempio `1:75`), elimina (se non usata), torna all'elenco standard.

## Layout e spazio carta

In basso ci sono le schede **Modello** e dei **layout** (le tavole da stampare). In un layout si vede il foglio con l'area stampabile; il modello si mostra attraverso le **finestre**.
- **Nuovo layout**: Layout > Layout > Nuovo layout, o il **+** accanto alle schede. Formato, orientamento e margine del foglio dei layout nuovi si scelgono nelle Opzioni > Visualizza.
- **Doppio clic dentro una finestra**: si lavora nel modello attraverso di essa (disegnare, modificare, zoom e pan); **doppio clic fuori** o `PSPACE` per tornare alla carta. Lo stesso con il pulsante MODELLO/CARTA della barra di stato.
- Nella barra di stato, con una finestra scelta: la **scala** della finestra (1:1 … 1:1000) e il **blocco** (una finestra bloccata non cambia zoom).
- Tasto destro su una scheda: *Rinomina*, *Elimina*, *Sposta nel gruppo*, *Raggruppa le schede dei layout*.

## Gruppi di layout

Con molti layout, il pulsante **≡** in basso a sinistra li elenca tutti per gruppo. Tasto destro su una scheda > *Sposta nel gruppo* (nuovo o esistente, per esempio "Piano terra"); *Raggruppa le schede dei layout* mostra una scheda per gruppo con il menu dei suoi layout. I gruppi si salvano nel DXF.

## Finestra di layout (`MVIEW`, `MV`, `FINESTRA`)

**Dove:** Layout > Finestre di layout > Finestra.

Nello spazio carta di un layout: due angoli della finestra, oppure `A` (adatta al foglio), `P` (poligonale), `O` (da un oggetto chiuso). Poi la scala si sceglie nella barra di stato o nelle Proprietà.

## Ritaglia finestra (`VPCLIP`, `RITAGLIAFINESTRA`)

**Dove:** Layout > Finestre di layout > Ritaglia.

Ritaglia una finestra su un poligono o su un oggetto chiuso; `E` toglie il ritaglio.

## Layer nelle finestre (`VPLAYER`, `LAYERFINESTRA`)

`[Congela/Scongela]` layer solo in alcune finestre (`Tutte`, `Seleziona`, `Corrente`): lo stesso layer si vede in una tavola e non in un'altra. Anche dalla colonna *Congela nella finestra* del gestore layer.

## Spazio modello e carta (`MSPACE`, `MS`; `PSPACE`, `PS`; `TILEMODE`)

`MSPACE` lavora nel modello attraverso la finestra scelta; `PSPACE` torna alla carta. `TILEMODE` 1 = scheda Modello, 0 = layout (negli script).

## Cartiglio (`CARTIGLIO`, `CART`, `TITLEBLOCK`; `AGGCARTIGLIO`, `AGGIORNACARTIGLIO`, `UPDATEFIELD`)

**Dove:** Layout > Cartiglio.

Nello spazio carta: formato del foglio (A4…A0), cornice e cartiglio con attributi (titolo, autore, data, scala, formato, società, numero, revisione). *Aggiorna cartiglio* rimette nei cartigli scala, data e formato correnti.

## Impostazione pagina (`PAGESETUP`)

**Dove:** Layout > Layout > Imposta pagina; Output > Stampa > Imposta pagina.

Gestisce le impostazioni di pagina con nome: nuova, modifica (le stesse voci della Stampa), imposta corrente, importa da un altro disegno, elimina. Si salvano nel DXF insieme al disegno.
