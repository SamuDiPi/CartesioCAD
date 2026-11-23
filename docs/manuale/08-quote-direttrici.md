# Quote e direttrici

Le quote sono in **Disegno > Annotazioni > Quota** e in **Testi e quote > Quote**. Usano lo stile di quota corrente (vedi `DIMSTYLE`).

## Quota lineare (`DIMLINEAR`, `DLI`, `QUOTALIN`)

**Dove:** Testi e quote > Quote > Quota; Disegno > Annotazioni > Quota.

Quota orizzontale o verticale.
1. Origine della prima linea di estensione (un punto, meglio con lo snap).
2. Origine della seconda linea di estensione.
3. Posizione della linea di quota: muovendo il cursore sopra/sotto la quota è orizzontale, a destra/sinistra verticale.

## Quota allineata (`DIMALIGNED`, `DAL`, `QUOTALL`)

Come la lineare, ma parallela ai due punti (per i lati inclinati).

## Quota di raggio e di diametro (`DIMRADIUS`, `DRA`, `QUOTARAG`; `DIMDIAMETER`, `DDI`, `QUOTADIA`)

Selezionare un arco o un cerchio, poi la posizione del testo.

## Quota angolare (`DIMANGULAR`, `DAN`, `QUOTANG`)

Selezionare due linee (il lato dell'angolo si sceglie con la posizione del clic), oppure un arco; oppure Invio e tre punti: vertice, primo e secondo punto finale.

## Quota a coordinata (`DIMORDINATE`, `DOR`, `QUOTACOORD`)

Posizione del punto, poi punto finale della direttrice: la direzione decide se si quota la X o la Y (oppure `Xdato` / `Ydato`).

## Quote in serie (`DIMCONTINUE`, `DCO`, `QUOTACONT`; `DIMBASELINE`, `DBA`, `QUOTABASE`)

**Dove:** Testi e quote > Quote > Continua (Linea di base nel suo menu).

Partono dall'ultima quota lineare (o da una scelta con `Seleziona`): *Continua* aggiunge quote una dopo l'altra sulla stessa linea; *Linea di base* le misura tutte dalla stessa origine, una sopra l'altra.

## Quotatura rapida (`QDIM`, `QUOTARAPIDA`)

**Dove:** Testi e quote > Quote > Quotatura rapida.

Si selezionano gli oggetti, poi la posizione della linea di quota: quota tutti gli estremi in una volta. `[Continua/Linea di base]`; orizzontale o verticale secondo dove si mette la linea.

## Spostare il testo di una quota (`DIMTEDIT`, `DIMTED`, `MODTESTOQUOTA`)

**Dove:** Testi e quote > Quote > Sposta testo di quota. Anche trascinando la maniglia del testo.

Sceglie la quota e il nuovo punto del testo; oppure `Sinistra`, `Destra`, `Centro`, `Origine` (torna al suo posto), `ANgolo` (lo ruota).

## Modificare le quote (`DIMEDIT`, `DED`, `MODQUOTA`)

**Dove:** Testi e quote > Quote > Modifica quote.

Su più quote insieme: `[Origine/Nuovo/Ruota/Obliqua]`: testo al suo posto, testo sostitutivo (`<>` sta per la misura vera), rotazione del testo, linee di estensione oblique.

## Stili di quota (`DIMSTYLE`, `D`, `DST`, `STILEQUOTA`; `-DIMSTYLE`)

**Dove:** Testi e quote > Quote > Stile di quota.

Gestione degli stili: testo (stile, altezza, posizione), frecce o trattini e loro dimensione, distanze delle linee, scala globale, fattore di misura, decimali, suffisso (per esempio " mm"), *Annotativo*. Nuovo, modifica, rinomina, elimina, corrente. `-DIMSTYLE` imposta lo stile corrente dalla riga di comando. Le variabili `DIMSCALE` (scala), `DIMTXT` (altezza del testo), `DIMASZ` (frecce), `DIMDEC` (decimali) cambiano lo stile corrente (utili negli script).

## Direttrici (`LEADER`, `LE`, `MLEADER`, `MLD`, `QLEADER`, `DIRETTRICE`)

**Dove:** Testi e quote > Direttrici > Direttrice; Disegno > Annotazioni > Direttrice.

Freccia con un testo su più righe: punta della freccia, uno o più punti, Invio, poi si scrive il testo direttamente nel disegno. Usa lo stile delle direttrici corrente.

## Multidirettrici (`MLEADERADD`, `AGGIUNGIDIRETTRICE`; `MLEADERREMOVE`, `RIMUOVIDIRETTRICE`)

**Dove:** Testi e quote > Direttrici > Aggiungi direttrice, Rimuovi direttrice.

Aggiungono o tolgono frecce allo stesso testo: si sceglie la direttrice, poi le punte delle nuove frecce (o le frecce da togliere). Ogni maniglia sposta il suo punto.

## Allinea e raccogli direttrici (`MLEADERALIGN`, `MLA`, `ALLINEADIRETTRICI`; `MLEADERCOLLECT`, `MLC`, `RACCOGLIDIRETTRICI`)

**Dove:** Testi e quote > Direttrici > Allinea, Raccogli.

*Allinea*: si selezionano le direttrici e quella di riferimento: i testi si mettono in colonna o in riga con lei. *Raccogli*: più direttrici diventano una sola con tutte le frecce.

## Stili delle direttrici (`MLEADERSTYLE`, `MLS`, `STILEDIRETTRICE`)

**Dove:** Testi e quote > Direttrici > Stili direttrici.

Altezza del testo, freccia, stile di testo, distanza dall'approdo, *Annotativo*. Le direttrici nuove usano lo stile corrente; modificandolo si aggiornano quelle esistenti.

## Segno di centro e linea d'asse (`CENTERMARK`, `CM`, `SEGNOCENTRO`; `CENTERLINE`, `CL`, `LINEAASSE`)

**Dove:** Testi e quote > Assi.

*Centro*: croce al centro dei cerchi e degli archi selezionati. *Linea d'asse*: si scelgono due linee e si disegna la linea in mezzo (tipo di linea CENTER).
