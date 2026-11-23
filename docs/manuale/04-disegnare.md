# Disegnare

Tutti i comandi di disegno sono in **Disegno > Linee e forme** (alcuni nel menu ▾ del pulsante). I punti si danno con un clic (con gli snap) o scrivendo le coordinate.

## Linea (`LINE`, `L`, `LINEA`)

**Dove:** Disegno > Linee e forme > Linea.

Segmenti consecutivi: ogni tratto è un oggetto a sé.
1. Indicare il primo punto.
2. Indicare i punti successivi; ogni punto chiude un tratto.
3. Invio o Esc per finire.

**Opzioni:** `C` (o `CHIUDI`) chiude tornando al primo punto, dopo almeno due tratti. Un tratto sbagliato si toglie dopo con Ctrl+Z.
**Suggerimento:** con l'orto (F8) o il polare (F10) acceso, basta muovere il cursore nella direzione e scrivere la lunghezza.

## Polilinea (`PLINE`, `PL`, `PLINEA`, `POLILINEA`)

**Dove:** Disegno > Linee e forme > Polilinea.

Un solo oggetto fatto di tratti rettilinei e ad arco, anche con spessore.
1. Indicare il punto iniziale.
2. Indicare i punti successivi; Invio per finire.

**Opzioni:** `[Arco/Mezza larghezza/LArghezza/Annulla]`, più `C` chiude e `L` torna ai tratti rettilinei dopo un arco. `LA` (larghezza) e `M` (mezza larghezza) chiedono il valore iniziale e finale: si ottengono frecce e tratti rastremati; l'ultima larghezza usata si ricorda per la polilinea successiva.
**Note:** la larghezza globale e quella dei singoli tratti si cambiano anche dalla tavolozza Proprietà. Per modificare una polilinea esistente: `PEDIT` o le maniglie.

## Cerchio (`CIRCLE`, `C`, `CERCHIO`)

**Dove:** Disegno > Linee e forme > Cerchio.

1. Indicare il centro.
2. Indicare il raggio (un punto sulla circonferenza o un numero).

## Arco (`ARC`, `A`, `ARCO`)

**Dove:** Disegno > Linee e forme > Arco.

Arco per tre punti: iniziale, un punto intermedio, finale. Se i tre punti sono allineati l'arco non si può fare.

## Rettangolo (`RECTANG`, `REC`, `RECTANGLE`, `RETTANGOLO`)

**Dove:** Disegno > Linee e forme > Rettangolo.

Due angoli opposti; il risultato è una polilinea chiusa. Con le coordinate relative (`@3000,2000`) si danno direttamente le misure.

## Poligono (`POLYGON`, `POL`, `POLIGONO`)

**Dove:** Disegno > Linee e forme > menu di Rettangolo > Poligono.

1. Numero dei lati.
2. Centro del poligono, oppure `L` (Lato) per darlo con i due estremi di un lato.
3. `[Inscritto nel cerchio/Circoscritto attorno al cerchio]`.
4. Raggio del cerchio.

## Ellisse (`ELLIPSE`, `EL`, `ELLISSE`)

**Dove:** Disegno > Linee e forme > Ellisse.

1. Un estremo dell'asse (oppure `C` per partire dal centro).
2. L'altro estremo dell'asse.
3. La distanza dall'altro asse (metà dell'altro diametro).

## Spline (`SPLINE`, `SPL`)

**Dove:** Disegno > Linee e forme > menu di Ellisse > Spline.

Curva morbida che passa per i punti indicati. **Opzioni:** `Chiudi`, `Annulla` (l'ultimo punto). Invio per finire.

## Punto (`POINT`, `PO`, `PUNTO`)

**Dove:** Disegno > Linee e forme > Punto.

Un punto per ogni clic; Invio per finire. I punti si agganciano con lo snap *Nodo*.

## Dividi (`DIVIDE`, `DIV`, `DIVIDI`) e Misura (`MEASURE`, `ME`, `MISURA`)

**Dove:** Disegno > Linee e forme > menu di Punto.

Mettono punti lungo linee, archi, cerchi, polilinee, ellissi e spline: `DIVIDE` in parti uguali (si dà il numero di parti), `MEASURE` a distanza fissa (si dà la distanza).

## Linea di costruzione (`XLINE`, `XL`, `COSTRUZIONE`) e semiretta (`RAY`, `SEMIRETTA`)

**Dove:** Disegno > Linee e forme > Linea di costruzione (la semiretta nel suo menu).

Linee infinite (o infinite da un lato) di riferimento: non contano nelle estensioni del disegno. `XLINE`: un punto e un punto di passaggio, oppure `[Orizz/Vert/Ang]`.

## Multilinea (`MLINE`, `ML`, `MULTILINEA`)

**Dove:** Disegno > Linee e forme > menu di Nuvola di revisione > Multilinea.

Due linee parallele insieme (stile STANDARD), per esempio per i muri. **Opzioni:** `Giustificazione` (Superiore, Zero, Inferiore: da che parte stanno le linee rispetto ai punti), `Scala` (distanza fra le linee), `Annulla`.

## Nuvola di revisione (`REVCLOUD`, `NUVOLA`, `NUVOLAREV`)

**Dove:** Disegno > Linee e forme > Nuvola di revisione; Testi e quote > Revisione.

Contorno ad archi per segnalare una modifica: due angoli di un rettangolo, oppure `Oggetto` per trasformare un oggetto chiuso; `Lunghezza arco` cambia la dimensione degli archi.

## Copertura (`WIPEOUT`, `COPERTURA`; `WIPEOUTFRAME`)

**Dove:** Testi e quote > Revisione > Maschera; Disegno > Linee e forme > menu di Nuvola di revisione.

Area che nasconde gli oggetti che stanno sotto (per esempio dietro un testo): si danno i punti del contorno, oppure `Polilinea` per usare una polilinea chiusa. `Cornici` (o la variabile `WIPEOUTFRAME`) decide se il bordo si vede e si stampa; si sceglie anche da Inserimenti > File collegati > Variante cornice.

## Contorno (`BOUNDARY`, `BO`, `BPOLY`, `CONTORNO`)

Indicando un punto dentro un'area chiusa, crea una polilinea chiusa sul contorno (e sulle isole interne). Utile per misurare un'area o tratteggiarla.

## Tabella (`TABLE`, `TB`, `TABELLA`)

Vedi il capitolo *Tabelle*.
