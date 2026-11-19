# Confronto di revisioni e circuiti

## Confronto di revisioni (`COMPARE`, `DWGCOMPARE`, `CONFRONTADWG`, `CONFRONTA`)

**Dove:** Revisioni > Confronta > Confronta disegni.

Confronta il disegno aperto con un'altra revisione (si sceglie il file):
- in **grigio** gli oggetti uguali, in **verde** quelli solo nel disegno corrente (aggiunti), in **rosso** quelli solo nell'altro (tolti);
- le **zone** delle modifiche sono riquadrate: nella scheda *Confronto*, *Precedente* e *Successiva* le percorrono una alla volta;
- *Solo in questo* e *Solo nell'altro* mostrano o nascondono le due parti;
- *Chiudi confronto* torna al disegno normale.

Il confronto tollera le piccole differenze dovute alla conversione dei formati (spline, polilinee, blocchi riscritti).

## Circuiti (`CIRCUITI`, `CIRCUITS`)

**Dove:** Plugin > Impianti elettrici > Circuiti.

Tavolozza per le sigle dei circuiti elettrici in pianta, confrontate con lo schema unifilare dei quadri in PDF.

**Importa schema PDF**: legge i circuiti di tutti i quadri (sigla, descrizione, tipo luce/FM/utenza, potenza, fase, interruttore, cavo, lunghezza). La tabella si filtra per quadro, tipo e testo.

**Verifica**: confronta le sigle scritte in pianta (testi come `QPT-SX-N C-4` sui layer delle sigle) con lo schema e divide i risultati in gruppi:
- sigle sconosciute (scritte in pianta ma non nello schema);
- circuiti dei quadri presenti in pianta che non hanno nessuna sigla;
- punti (blocchi sui layer dei punti) senza una sigla vicina.

Doppio clic su una voce per vederla nel disegno.

**Assegna la sigla ai punti selezionati**: si selezionano i punti in pianta, si sceglie il circuito nella tabella e la sigla viene scritta accanto a ogni punto, con l'altezza delle sigle già presenti (Ctrl+Z annulla).

**Legenda**: legge la legenda delle lettere dei punti (righe come `Tipo "D": alimentazione lama d'aria`, oppure lettera e descrizione affiancate), ovunque sia: modello, layout o blocchi. Per ogni lettera propone i circuiti corrispondenti: dalle sigle già scritte accanto ad altri punti con la stessa lettera, altrimenti interpretando il testo (radici delle parole e sinonimi, come UE = unità esterna). Le parole chiave si correggono nella tabella (doppio clic) e restano salvate per il disegno; in arancione le lettere da verificare.

**Layer**: in fondo alla tavolozza si scelgono i layer dei punti, delle lettere e delle sigle (prefissi separati da `;`).

Lo schema letto qui arriva anche alla tavolozza **Impianto**, e viceversa.
