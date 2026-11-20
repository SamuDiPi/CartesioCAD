# CanaloCAD

CanaloCAD progetta e verifica le canaline portacavi. I suoi calcoli sono anche dentro CartesioCAD; l'applicazione completa (una pagina web, inclusa) si apre dal pulsante *Apri CanaloCAD*.

## Verifica sezione (`CANALOVERIFICA`, `CANALOCHECK`)

**Dove:** Plugin > CanaloCAD > Verifica sezione.

1. Selezionare la sezione disegnata: i **cerchi** sono i cavi, le **linee del bordo** la canalina, i **testi** le sigle (anche con le bandierine). Le misure della canalina si leggono dai testi (`L=300 H=75`, `CANALINA 75x300`) o dalla geometria.
2. Nella finestra si scelgono i dati del tratto: misure, spessore, serie, coperchio, setti, posa dei cavi (per gravità come CanaloCAD, o come disegnati), lunghezza, curve, derivazioni, riduzioni, attraversamenti EI120, staffaggio e passo, antisismico. Vengono ricordati per la volta dopo.
3. I risultati vanno nella riga di comando: riempimento e riserva, coefficiente di riduzione della portata (Kr), segregazione CEI 64-8 art. 528.1, peso, carico e freccia dello staffaggio, tiro di posa, raggio di curvatura minimo, distinta dei materiali.
4. Con **Disegna la tavola A3** si disegna accanto alla sezione la tavola completa: sezione in scala con i cavi numerati, risultati, tabella dei cavi, distinta e cartiglio (layer `CANALO-TAVOLA`, Ctrl+Z la toglie).

Negli script: `-CANALOVERIFICA` (senza finestra) e `-CANALOTAVOLA` (anche la tavola).

Se non è selezionata nessuna sezione, si apre **Sezione da catalogo**.

## Sezione da catalogo (`CANALOSEZIONE`, `CANALOCAVI`)

**Dove:** Plugin > CanaloCAD > Sezione da catalogo (oppure *Verifica sezione* senza niente selezionato).

Invece di disegnare i cerchi dei cavi, si scelgono dal catalogo di CanaloCAD: cavi BT multipolari (FG16OM16) e unipolari (FG16M16), resistenti al fuoco (FTG18OM16, rivelazione incendio, EVAC), segnale, comando e bus (FG16OH2R16, LiYCY, KNX), dati e fibra ottica. Ogni cavo ha diametro, peso, portata e raggio di curvatura.

1. Scheda **Cavi**: scegliere categoria e cavo, la quantità e, se si vuole, la sigla (`QE1 C-3`; con più cavi diventa `QE1 C-3/1`, `/2`...; vuota: `C-1`, `C-2`...), poi **Aggiungi**. **Togli** e **Svuota** modificano l'elenco. Sotto si leggono numero di cavi, peso, riempimento della canalina scelta e la **larghezza consigliata**: la più stretta (non più stretta che alta) con riempimento entro il 50%. **Larghezza consigliata** la imposta.
2. Scheda **Canalina e tratto**: gli stessi dati di *Verifica sezione* (i cavi si dispongono per gravità, come in CanaloCAD).
3. Con OK i risultati vanno nella riga di comando e la sezione viene disegnata al vero a destra del disegno, già selezionata (si sposta con `MOVE`): bordo della canalina sul layer `CANALO-CANALINA`, cavi su `CAVI` (resistenti al fuoco in rosso, segnale e dati in blu), bandierine su `BANDIERINE` e sigle con tipo e formazione su `TESTI-CAVI`. Selezionandola, `CANALOVERIFICA` la riverifica con altri dati del tratto. Con **Disegna la tavola A3** si aggiunge accanto la tavola completa.

I cavi scelti e i dati del tratto restano per la volta dopo, finché il programma è aperto. Ctrl+Z toglie la sezione.

Negli script: `-CANALOSEZIONE FG16_4G10*2,FG16_3G2.5,DATA_CAT6*4` (codici del catalogo, la quantità dopo `*`, senza spazi), con la larghezza consigliata e gli ultimi dati del tratto.

## Apri CanaloCAD (`CANALOCAD`, `CANALO`)

Apre l'applicazione CanaloCAD nel browser (la copia inclusa in CartesioCAD, o quella indicata nelle Opzioni > Plugin).

## Invia sezione (`CANALOINVIA`, `CANALOSEND`)

Salva gli oggetti selezionati in un DXF in millimetri da importare in CanaloCAD; il percorso del file va negli appunti.

## Inserisci tavola (`CANALOINS`, `CANALOINSERISCI`) e Ultima tavola (`CANALOULTIMA`, `CANALOLAST`)

Inseriscono come blocco una tavola DXF esportata da CanaloCAD (scelta dal file, oppure l'ultima della cartella di scambio), scalata con le unità del disegno. La cartella di scambio (i Download, o quella delle Opzioni) è sorvegliata: una tavola nuova viene segnalata nella riga di comando.
