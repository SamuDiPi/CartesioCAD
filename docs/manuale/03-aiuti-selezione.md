# Aiuti al disegno e selezione

## Snap ad oggetto (F3, `OSMODE`)

Aggancia il cursore ai punti notevoli degli oggetti: **punto finale, medio, centro, quadrante, nodo, inserimento, intersezione, perpendicolare, tangente, vicino**. Un marcatore e un'etichetta mostrano il punto trovato.
- Si accende e si spegne con F3 o con il pulsante nella barra di stato; la freccetta accanto al pulsante apre il menu dei tipi di snap.
- `OSMODE` imposta i tipi con i valori standard (somma: 1 finale, 2 medio, 4 centro, 8 nodo, 16 quadrante, 32 intersezione, 64 inserimento, 128 perpendicolare, 256 tangente, 512 vicino).
- Dimensione e colore del marcatore e del mirino si scelgono in Opzioni > Disegno.

## Puntamento snap a oggetto (F11, OTRACK)

Fermando il cursore su un punto di snap per mezzo secondo lo si **aggancia** (compare una crocetta; fino a 7 punti; di nuovo sullo stesso punto per sganciarlo). Dai punti agganciati partono allineamenti orizzontali, verticali e polari, e l'incrocio di due allineamenti: si può prendere un punto "in linea con" altri senza disegnare costruzioni.

## Orto (F8, `ORTHOMODE`)

Le linee e gli spostamenti restano orizzontali o verticali. Con l'orto acceso, un numero scritto nella riga di comando è la distanza nella direzione del cursore.

## Puntamento polare (F10)

Il cursore si allinea agli angoli multipli dell'incremento scelto (menu accanto al pulsante: 90, 45, 30, 22,5, 18, 15, 10 o 5°), con una linea di guida e la distanza.

## Griglia (F7, `GRIDMODE`) e snap alla griglia (F9, `SNAPMODE`)

La griglia è un riferimento visivo (colore nelle Opzioni > Visualizza); lo snap alla griglia muove il cursore a passi regolari.

## Input dinamico (F12, `DYNMODE`)

Accanto al cursore compaiono il messaggio del comando, le coordinate e i campi per distanza e angolo: si scrive lì invece che nella riga di comando.

## Selezionare gli oggetti

- **Clic** su un oggetto: lo aggiunge alla selezione. Con **Maiusc** premuto lo toglie.
- **Finestra**: trascinare (o due clic) **da sinistra a destra** seleziona solo gli oggetti interamente dentro (riquadro azzurro).
- **Intersezione**: **da destra a sinistra** seleziona anche gli oggetti toccati (riquadro verde).
- **Lazo**: tenendo premuto e trascinando nel vuoto si disegna un contorno libero; un trascinamento quasi diritto resta un rettangolo.
- **Oggetti sovrapposti**: Maiusc+Spazio passa all'oggetto successivo sotto il cursore.
- Passando sopra un oggetto lo si vede evidenziato prima di cliccare.
- Durante un comando che chiede oggetti: `[Tutto/Ultimo/Precedente/Rimuovi]`: tutti, l'ultimo disegnato, la selezione precedente, oppure si passa a togliere oggetti. Invio termina la selezione.
- **Esc** toglie la selezione. **Ctrl+A** (o `SELECTALL`) seleziona tutto.
- Oltre 100 oggetti selezionati le maniglie non si mostrano (per velocità).
- Nelle Opzioni > Selezione: dimensione del mirino, selezione prima del comando (nome/verbo), selezione a trascinamento, dimensione e colore delle maniglie.

## Maniglie

Gli oggetti selezionati mostrano dei quadratini (maniglie) nei punti principali:
- **trascinare e rilasciare** una maniglia (oppure clic sulla maniglia e clic sul punto d'arrivo) stira l'oggetto o lo sposta;
- le maniglie **multifunzione** delle polilinee hanno un menu (tenendo il cursore sopra): aggiungi o rimuovi vertice, converti in arco, allunga;
- i testi multilinea hanno le frecce della **larghezza** ai lati;
- le quote hanno la maniglia del testo per spostarlo.

## Selezione precedente (`SELECTPREVIOUS`, `SELEZIONEPRECEDENTE`)

Riseleziona gli oggetti usati dall'ultimo comando.

## Seleziona simili (`SELECTSIMILAR`, `SIMILI`)

**Dove:** Strumenti > Pulizia > Seleziona simili; menu del tasto destro.

Partendo dagli oggetti selezionati, seleziona tutti gli oggetti dello stesso tipo e dello stesso layer.

## Selezione rapida (`QSELECT`, `SELRAPIDA`)

Seleziona per tipo di oggetto e proprietà: colore, layer, tipo e spessore di linea, nome del blocco (anche dei blocchi dinamici), contenuto e altezza dei testi, valore di un attributo. Operatori `=`, `<>`, `>`, `<` e corrispondenza con `*` e `?`; si può includere o escludere e aggiungere alla selezione corrente.

## Aggiungi selezionato (`ADDSELECTED`, `AGGIUNGISELEZIONATO`)

Si seleziona un oggetto e si avvia il comando che lo crea con le stesse proprietà (layer, colore, tipo di linea, stile): per esempio una linea uguale, un testo con lo stesso stile, un blocco uguale. Finito quel comando, le proprietà correnti tornano quelle di prima.

## Gruppi (`GROUP`, `G`, `GRUPPO`; `UNGROUP`, `SCIOGLIGRUPPO`; `PICKSTYLE`)

**Dove:** Disegno > Gruppi.

`GROUP`: si selezionano gli oggetti e si dà un nome (Invio = gruppo senza nome). Poi un clic su un oggetto seleziona tutto il gruppo. `UNGROUP` scioglie il gruppo degli oggetti selezionati. *Selezione gruppi* (variabile `PICKSTYLE`) accende o spegne la selezione per gruppo.

## Nascondere e isolare oggetti (`HIDEOBJECTS`, `ISOLATEOBJECTS`, `UNISOLATEOBJECTS`)

`HIDEOBJECTS` (`NASCONDIOGGETTI`) nasconde temporaneamente gli oggetti selezionati; `ISOLATEOBJECTS` (`ISOLAOGGETTI`) mostra solo quelli selezionati; `UNISOLATEOBJECTS` (`TERMINAISOLAMENTO`) mostra di nuovo tutto. Non è una modifica del disegno: non si salva e non serve annullare.

## Ordine di visualizzazione (`DRAWORDER`, `DR`, `ORDINE`; `TEXTTOFRONT`; `HATCHTOBACK`)

**Dove:** menu del tasto destro sugli oggetti selezionati.

`DRAWORDER`: gli oggetti selezionati vanno `[Sopra oggetti/sOtto oggetti/Primo piano/Dietro]`. `TEXTTOFRONT` (`TESTIDAVANTI`) porta in primo piano testi, quote e direttrici; `HATCHTOBACK` (`TRATTEGGIDIETRO`) porta dietro i tratteggi. L'ordine si salva nel file.
