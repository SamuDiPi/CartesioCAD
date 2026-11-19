# Modificare

I comandi di modifica sono in **Inizio > Edita**. Quasi tutti funzionano in due modi: si selezionano prima gli oggetti e poi si avvia il comando, oppure si avvia il comando e poi si selezionano (Invio per finire la selezione).

## Cancella (`ERASE`, `E`, `CANCELLA`)

**Dove:** Inizio > Edita > Cancella; tasto Canc.

Cancella gli oggetti selezionati.

## Sposta (`MOVE`, `M`, `SPOSTA`)

**Dove:** Inizio > Edita > Sposta.

1. Selezionare gli oggetti, Invio.
2. Punto base (un punto di riferimento, meglio con lo snap).
3. Secondo punto (dove va il punto base); oppure una distanza nella direzione del cursore.

## Copia (`COPY`, `CO`, `CP`, `COPIA`)

**Dove:** Inizio > Edita > Copia.

Come Sposta, ma lascia l'originale; dopo il primo secondo punto continua a fare copie dallo stesso punto base finché non si preme Invio.

## Ruota (`ROTATE`, `RO`, `RUOTA`)

**Dove:** Inizio > Edita > Ruota.

Oggetti, punto base (centro di rotazione), angolo (numero in gradi, antiorario, o un punto). I testi restano leggibili.

## Scala (`SCALE`, `SC`, `SCALA`)

**Dove:** Inizio > Edita > Scala.

Oggetti, punto base (resta fermo), fattore di scala (2 = doppio, 0,5 = metà).

## Specchio (`MIRROR`, `MI`, `SPECCHIO`)

**Dove:** Inizio > Edita > Specchio.

Oggetti, due punti della linea di specchiatura, poi `Cancellare gli oggetti di origine? [Sì/No]`. I testi restano leggibili (non si scrivono al contrario).

## Offset (`OFFSET`, `O`)

**Dove:** Inizio > Edita > Offset.

Copia parallela a una distanza: si dà la distanza, si sceglie l'oggetto (linea, arco, cerchio, polilinea) e si clicca dalla parte in cui fare la copia; si continua con altri oggetti, Invio per finire.

## Serie (`ARRAY`, `AR`, `SERIE`)

**Dove:** Inizio > Edita > Serie.

Copie disposte in modo regolare: `[Rettangolare/Polare]`. Rettangolare: righe, colonne e distanze. Polare: centro, numero di elementi e angolo da coprire.

## Taglia (`TRIM`, `TR`, `TAGLIA`)

**Dove:** Inizio > Edita > Taglia.

Taglio rapido: si clicca la parte di oggetto da togliere, che viene tagliata alle intersezioni con gli altri oggetti (anche polilinee). Invio per finire.

## Estendi (`EXTEND`, `EX`, `ESTENDI`)

**Dove:** Inizio > Edita > menu di Taglia > Estendi.

Si clicca vicino all'estremo da allungare di una linea o di un arco: si estende fino al primo oggetto che incontra.

## Allunga (`LENGTHEN`, `LEN`, `ALLUNGA`)

**Dove:** Inizio > Edita > menu di Taglia > Allunga.

Allunga o accorcia linee e archi: `[DElta/Percentuale/Totale]` (di quanto, in percentuale, o a una lunghezza totale), poi si cliccano gli oggetti dalla parte da modificare. Senza opzione, selezionando un oggetto ne mostra la lunghezza.

## Raccordo (`FILLET`, `F`, `RACCORDO`)

**Dove:** Inizio > Edita > Raccordo.

Raccorda due linee con un arco. `R` (Raggio) imposta il raggio; con raggio 0 le linee si incontrano a spigolo vivo (utile per chiudere un angolo).

## Cima (`CHAMFER`, `CHA`, `CIMA`, `SMUSSO`)

**Dove:** Inizio > Edita > menu di Raccordo > Cima.

Smusso fra due linee. `D` (Distanza) imposta le due distanze dallo spigolo; con 0 le linee si incontrano a spigolo vivo.

## Stira (`STRETCH`, `S`, `STIRA`)

**Dove:** Inizio > Edita > Stira.

Si seleziona con una **finestra di intersezione** (da destra a sinistra) la parte da spostare: i punti dentro la finestra si spostano, quelli fuori restano fermi, gli oggetti interamente dentro si muovono. Poi punto base e secondo punto.

## Interrompi (`BREAK`, `BR`, `INTERROMPI`, `SPEZZA`) e Spezza in un punto (`BREAKATPOINT`, `SPEZZAINPUNTO`)

**Dove:** Inizio > Edita > Interrompi (Spezza in un punto nel suo menu).

Toglie il pezzo fra due punti di una linea, un arco, un cerchio o una polilinea. Con `@` come secondo punto (o con Spezza in un punto) l'oggetto si divide in due senza togliere niente.

## Unisci (`JOIN`, `J`, `UNISCI`)

**Dove:** Inizio > Edita > Unisci.

Unisce linee allineate, archi dello stesso cerchio e tratti che si toccano agli estremi in un solo oggetto (polilinea).

## Modifica polilinea (`PEDIT`, `PE`, `EDITPL`, `MODPL`)

**Dove:** Inizio > Edita > menu di Unisci > Modifica polilinea.

Si sceglie una polilinea (una linea o un arco si convertono). Opzioni: `Chiudi`/`Apri`, `Unisci` (aggiunge tratti che si toccano), `Larghezza` (larghezza uguale per tutti i tratti), `Inverti` (direzione), `Spline` (curva morbida), `Rettifica` (toglie la curvatura), `Annulla`.

## Allinea (`ALIGN`, `AL`, `ALLINEA`)

**Dove:** Inizio > Edita > menu di Ruota > Allinea.

Sposta e ruota gli oggetti facendo coincidere una o due coppie di punti (origine → destinazione); con due coppie chiede se scalare gli oggetti alla distanza delle destinazioni. Invio dopo la prima coppia = solo spostamento.

## Esplodi (`EXPLODE`, `X`, `ESPLODI`)

**Dove:** Inizio > Edita > Esplodi.

Scompone polilinee (in linee e archi), blocchi (negli oggetti che li compongono), quote e testi multilinea.

## Copia proprietà (`MATCHPROP`, `MA`, `CORRISPROP`)

**Dove:** Inizio > Proprietà > Copia proprietà.

Si sceglie l'oggetto di origine, poi gli oggetti da modificare: prendono layer e colore dell'origine.

## Copia e incolla (`COPYCLIP`, `CUTCLIP`, `PASTECLIP`, `COPYBASE`, `PASTEORIG`, `PASTEBLOCK`)

**Dove:** Inizio > Appunti; menu del tasto destro > Appunti; Ctrl+C, Ctrl+V.

- `COPYCLIP` (Ctrl+C) copia gli oggetti selezionati, `CUTCLIP` (`TAGLIA`) li taglia.
- `PASTECLIP` (Ctrl+V) incolla chiedendo il punto di inserimento; `PASTEORIG` (`INCOLLAORIG`) incolla alle stesse coordinate dell'originale (comodo fra due disegni allineati).
- `COPYBASE` (`COPIABASE`, Ctrl+Maiusc+C) copia con un punto base scelto: incollando, il cursore tiene quel punto.
- `PASTEBLOCK` (`INCOLLABLOCCO`, Ctrl+Maiusc+V) incolla gli oggetti come un blocco unico.
- Funziona anche fra disegni diversi, con blocchi e layer.
