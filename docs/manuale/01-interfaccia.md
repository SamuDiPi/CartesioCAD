# L'interfaccia

## La finestra

- **Barra multifunzione** in alto: le schede *Inizio, Inserisci, Annota, Vista, Gestisci, Output, Layout, Revisioni, Plugin* raccolgono i comandi per argomento. Alcune schede compaiono solo quando servono (*Editor di testo* mentre si scrive un testo, *Confronto* durante il confronto di due disegni). Se la finestra è stretta, i pannelli che non ci stanno si chiudono in un pulsante con il loro nome: cliccandolo si apre il menu con i comandi.
- **Pulsante dell'applicazione** (il logo rosso in alto a sinistra): Nuovo, Apri, Salva, Salva con nome, Utilità disegno (Controlla, Recupera, Archivia per l'invio), Documenti recenti, Opzioni, Guida, Informazioni, Chiudi, Esci.
- **Barra di accesso rapido** accanto al logo: Nuovo, Apri, Salva, Salva con nome, Annulla, Ripristina.
- **Schede dei disegni**: un disegno aperto per scheda; la scheda *Inizia* è la pagina iniziale. Si possono trascinare file DWG, DXF o script sulla finestra per aprirli.
- **Area di disegno** al centro, con i pulsanti di navigazione (Pan, Zoom estensioni, Zoom avanti, Zoom indietro) e l'icona degli assi.
- **Riga di comando** sopra la barra di stato: qui si scrivono comandi, coordinate e opzioni e si leggono i messaggi.
- **Schede Modello e Layout** in basso a sinistra.
- **Barra di stato** in basso: coordinate del cursore, MODELLO/CARTA, scala di annotazione, aiuti al disegno.
- **Pannelli** ai lati: Proprietà, Proprietà layer, Circuiti, Impianto, Blocchi, Guida. Si aprono dalla scheda *Vista > Pannelli* o con i loro comandi; si chiudono con la ×; il bordo si trascina per allargarle.

## La pagina Inizia

- **Inizia disegno** crea un disegno nuovo; sotto ci sono i **modelli** (file .dwt, .dwg o .dxf della cartella dei modelli indicata nelle Opzioni): il disegno nuovo parte da una copia senza nome.
- **Documenti recenti** con l'anteprima, raggruppati per data. La puntina tiene un documento in cima; il tasto destro apre la cartella o lo toglie dall'elenco; in alto si sceglie la vista (miniature grandi, piccole, elenco).
- La scheda **Apprendi** elenca i comandi principali da provare.
- La pagina si può non mostrare all'avvio (Opzioni > Apri e salva).

## La riga di comando (`TEXTSCR`)

- Scrivendo, compare l'**autocompletamento** dei comandi: frecce e Invio per scegliere.
- **F2** (o `TEXTSCR`) apre la cronologia completa dei messaggi.
- Il messaggio indica cosa serve (un punto, un numero, un'opzione); le opzioni tra `[ ]` si cliccano.
- La riga si sposta trascinandola; si nasconde e si mostra da *Vista > Pannelli > Riga di comando* (Ctrl+9).

## Coordinate e valori

- **Assolute**: `10,20` (X,Y nel sistema del disegno).
- **Relative** all'ultimo punto: `@5,0`.
- **Polari** relative: `@50<30` (distanza 50 con angolo 30°, antiorario dall'asse X).
- **Distanza diretta**: un numero da solo vale come distanza nella direzione in cui punta il cursore (comodo con l'orto o il puntamento polare).
- Gli angoli sono in gradi, positivi in senso antiorario.

## Tasti funzione e scorciatoie

| Tasto | Funzione |
|---|---|
| F1 | Guida (manuale e percorsi guidati); durante un comando apre il manuale su quel comando |
| F2 | Cronologia della riga di comando |
| F3 | Snap ad oggetto acceso/spento |
| F7 | Griglia |
| F8 | Orto (solo orizzontale e verticale) |
| F9 | Snap alla griglia |
| F10 | Puntamento polare |
| F11 | Puntamento snap a oggetto (OTRACK) |
| F12 | Input dinamico accanto al cursore |
| Ctrl+1 | Tavolozza Proprietà |
| Ctrl+N, Ctrl+O, Ctrl+S | Nuovo, Apri, Salva |
| Ctrl+Maiusc+S | Salva con nome |
| Ctrl+F4, Ctrl+Q | Chiudi il disegno, esci da CartesioCAD |
| Ctrl+9 | Mostra o nasconde la riga di comando |
| Ctrl+P | Stampa |
| Ctrl+Z, Ctrl+Y | Annulla, Ripristina |
| Ctrl+C, Ctrl+V | Copia e incolla negli appunti |
| Ctrl+Maiusc+C, Ctrl+Maiusc+V | Copia con punto base, incolla come blocco |
| Ctrl+A | Seleziona tutto |
| Ctrl+Tab | Disegno successivo |
| Canc | Cancella gli oggetti selezionati |
| Esc | Annulla il comando o toglie la selezione |
| Invio, Spazio | Conferma; a vuoto ripete l'ultimo comando |
| Maiusc+Spazio | Oggetto successivo fra quelli sovrapposti sotto il cursore |

## Il tasto destro

Con il tasto destro nell'area di disegno si apre un menu che cambia con la situazione: senza comando e senza selezione propone di ripetere l'ultimo comando e le operazioni più comuni; con oggetti selezionati le modifiche (Sposta, Copia, Ruota, Cancella, Ordine di visualizzazione, Proprietà...); durante un comando Invio, Annulla e le opzioni del comando. Nelle Opzioni (Preferenze utente) si può far valere il tasto destro come Invio.

## La barra di stato

Da sinistra: coordinate del cursore; **MODELLO/CARTA** (in un layout, passa fra spazio carta e modello attraverso la finestra); **scala di annotazione** (vedi *Scale annotative*); griglia, snap, orto, polare (con il menu degli incrementi d'angolo), snap ad oggetto (con il menu dei tipi di snap), puntamento snap a oggetto, input dinamico, spessori di linea; il pulsante della riga di comando.

## I pulsanti di navigazione

In alto a destra sul disegno: Pan (manina), Zoom estensioni, Zoom avanti, Zoom indietro. Nell'angolo in basso a sinistra c'è l'icona degli assi X e Y. Si possono nascondere da *Vista > Elementi della vista* (Icona degli assi, Pulsanti di navigazione).

## Annullare e ripristinare (`UNDO`, `U`, `ANNULLA`; `REDO`, `RIPETI`)

**Dove:** barra di accesso rapido, frecce Annulla e Ripristina; Ctrl+Z e Ctrl+Y.

Annulla l'ultimo passo, anche più volte. Oltre agli oggetti si annullano le modifiche a layer (proprietà, layer nuovi, layer corrente), agli stili di testo, di quota e delle direttrici, alla scala di annotazione e alle definizioni dei blocchi. Un comando che crea un layer e ci disegna sopra si annulla in un solo passo. Non si annulla l'eliminazione dei layer con `PURGE`.

## Recupero dopo una chiusura anomala

Il salvataggio automatico (Opzioni > Apri e salva, ogni quanti minuti) scrive una copia dei disegni modificati. Se CartesioCAD si chiude male, al riavvio propone di riaprire le copie come disegni da salvare. Le copie si cancellano salvando e chiudendo normalmente.
