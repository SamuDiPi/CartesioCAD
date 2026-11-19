# Testi

## Testo multilinea (`MTEXT`, `T`, `MT`, `TESTOM`)

**Dove:** Inizio > Annotazione > Testo; Annota > Testo > Testo multilinea.

Testo con a capo automatico dentro un riquadro.
1. Indicare il primo angolo e l'angolo opposto del riquadro (la larghezza decide dove si va a capo).
2. Scrivere direttamente nel disegno: **Invio** va a capo, **Ctrl+Invio** o un clic fuori confermano, **Esc** annulla.

Mentre si scrive compare la scheda **Editor di testo** (vedi sotto). Sopra il testo c'è il **righello** con la maniglia della larghezza. Un testo lasciato vuoto non resta nel disegno.

## Testo su una riga (`TEXT`, `DT`, `DTEXT`, `TESTO`)

**Dove:** Inizio > Annotazione > menu di Testo > Testo riga singola; Annota > Testo.

1. Punto iniziale del testo.
2. Altezza (proposta l'ultima usata, variabile `TEXTSIZE`).
3. Angolo di rotazione.
4. Scrivere direttamente nel disegno; Invio conferma (non serve un secondo Invio a vuoto). Esc tiene quello che si è scritto.

Negli script il testo si scrive nella riga di comando, fino a fine riga.

## Modificare un testo (`TEXTEDIT`, `DDEDIT`, `ED`, `MODTESTO`)

**Doppio clic su un testo** (anche il testo di una direttrice, di una quota, di un attributo o di una cella di tabella), oppure `TEXTEDIT` con l'oggetto selezionato: si modifica direttamente nel disegno con il suo carattere e la sua altezza.
- Cursore e selezione sulle lettere, con i tasti di un editor normale (frecce, Home, Fine, Ctrl+frecce per le parole, Maiusc per selezionare, Ctrl+A per tutto, Ctrl+C/X/V).
- Nei testi multilinea Invio va a capo; Ctrl+Invio o un clic fuori confermano; Esc annulla le modifiche.
- Nelle tabelle Tab passa alla cella successiva.

## La scheda Editor di testo

Compare mentre si scrive o si modifica un testo multilinea. Le modifiche valgono per la parte selezionata (o per quello che si scrive dopo).
- **Stile**: stile di testo, altezza, *Annotativo* (vedi *Scale annotative*), *Maschera* (riempimento dietro il testo con il colore dello sfondo, per leggerlo sopra le linee).
- **Formattazione**: grassetto, corsivo, sottolineato, sopralineato, barrato, MAIUSCOLE/minuscole, carattere, colore, *Cancella formattazione*.
- **Paragrafo**: *Giustificazione* (il punto di attacco del riquadro: in alto/al centro/in basso, a sinistra/al centro/a destra), *Elenchi puntati e numerati* (puntato, numerato, lettere), *Spaziatura linea* (1, 1,5, 2, 2,5 o un valore), allineamento del paragrafo (a sinistra, centrato, a destra, giustificato, distribuito).
- **Inserisci**: *Simbolo* (gradi °, più/meno ±, diametro Ø, circa ≈, angolo ∠, delta Δ, diverso ≠, ohm Ω, quadrato ², cubo ³, ≤, ≥, micro µ, linea d'asse, spazio non divisibile); *Campo dati* (data di oggi, ora, nome e percorso del file); *Colonne* (nessuna, due, tre, quattro: le righe si dividono in colonne della larghezza del riquadro).
- **Ortografia**: *Controllo ortografia* acceso o spento (vedi sotto).
- **Strumenti**: *Trova e sostituisci* nel testo.
- **Opzioni**: *Righello* acceso o spento.
- **Chiudi**: *Chiudi editor di testo* conferma.

## Controllo ortografico

Usa il correttore di Windows (italiano se è installata la lingua). Le parole sconosciute sono sottolineate con un'onda rossa. **Tasto destro sulla parola**: i suggerimenti (clic per sostituire), *Aggiungi al dizionario* (vale anche per gli altri programmi che usano il correttore di Windows), *Ignora* (fino alla chiusura). Si spegne dal pannello *Ortografia*.

## Stili di testo (`STYLE`, `ST`, `STILE`; `TEXTSTYLE`)

**Dove:** Annota > Testo > Stili di testo; Inizio > Annotazione > menu di Tabella.

Gestione degli stili: nuovo, modifica (font SHX o TrueType, altezza, fattore di larghezza, obliquità, grassetto, corsivo, *Annotativo*), rinomina, elimina, imposta corrente. Le modifiche aggiornano i testi che usano lo stile e si annullano con Ctrl+Z. `TEXTSTYLE` imposta lo stile corrente dalla riga di comando; `TEXTSIZE` l'altezza proposta per i testi nuovi.

**Font:** i font SHX (romans, simplex, isocp...) si cercano nelle cartelle indicate nelle Opzioni (Apri e salva) e nella cartella `fonts` accanto al programma, e si disegnano a tratti; un SHX mancante diventa simplex, o Arial se non c'è. CartesioCAD non distribuisce font SHX. Quelli installati da altri programmi si usano solo spuntando *Usa anche i font SHX degli altri programmi installati* nelle Opzioni: appartengono a chi li ha prodotti, e va controllato che la loro licenza lo permetta. I TrueType si cercano per famiglia o per file.

## Trova e sostituisci (`FIND`, `TROVA`)

**Dove:** Annota > Testo > Trova testo.

Cerca (e sostituisce) in testi, testi multilinea, attributi, quote e direttrici: maiuscole/minuscole, parole intere, dove cercare. Zoom sul risultato; le sostituzioni si annullano con Ctrl+Z.

## Perché un testo non si vede (`TESTINASCOSTI`, `HIDDENTEXT`)

Per i testi nella vista corrente dice perché non si vedono: troppo piccoli, layer spento o congelato, colore uguale allo sfondo, font non trovato... Sullo schermo un colore uguale allo sfondo si mostra come il colore 7 (bianco o nero); in stampa resta il colore vero.
