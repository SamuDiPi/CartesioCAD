# CanaloCAD

CanaloCAD progetta e verifica le canaline portacavi. I suoi calcoli sono anche dentro CartesioCAD; l'applicazione completa (una pagina web, inclusa) si apre dal pulsante *Apri CanaloCAD*.

## Verifica sezione (`CANALOVERIFICA`, `CANALOCHECK`)

**Dove:** Plugin > CanaloCAD > Verifica sezione.

1. Selezionare la sezione disegnata: i **cerchi** sono i cavi, le **linee del bordo** la canalina, i **testi** le sigle (anche con le bandierine). Le misure della canalina si leggono dai testi (`L=300 H=75`, `CANALINA 75x300`) o dalla geometria.
2. Nella finestra si scelgono i dati del tratto: misure, spessore, serie, coperchio, setti, posa dei cavi (per gravità come CanaloCAD, o come disegnati), lunghezza, curve, derivazioni, riduzioni, attraversamenti EI120, staffaggio e passo, antisismico. Vengono ricordati per la volta dopo.
3. I risultati vanno nella riga di comando: riempimento e riserva, coefficiente di riduzione della portata (Kr), segregazione CEI 64-8 art. 528.1, peso, carico e freccia dello staffaggio, tiro di posa, raggio di curvatura minimo, distinta dei materiali.
4. Con **Disegna la tavola A3** si disegna accanto alla sezione la tavola completa: sezione in scala con i cavi numerati, risultati, tabella dei cavi, distinta e cartiglio (layer `CANALO-TAVOLA`, Ctrl+Z la toglie).

Negli script: `-CANALOVERIFICA` (senza finestra) e `-CANALOTAVOLA` (anche la tavola).

## Apri CanaloCAD (`CANALOCAD`, `CANALO`)

Apre l'applicazione CanaloCAD nel browser (la copia inclusa in CartesioCAD, o quella indicata nelle Opzioni > Plugin).

## Invia sezione (`CANALOINVIA`, `CANALOSEND`)

Salva gli oggetti selezionati in un DXF in millimetri da importare in CanaloCAD; il percorso del file va negli appunti.

## Inserisci tavola (`CANALOINS`, `CANALOINSERISCI`) e Ultima tavola (`CANALOULTIMA`, `CANALOLAST`)

Inseriscono come blocco una tavola DXF esportata da CanaloCAD (scelta dal file, oppure l'ultima della cartella di scambio), scalata con le unità del disegno. La cartella di scambio (i Download, o quella delle Opzioni) è sorvegliata: una tavola nuova viene segnalata nella riga di comando.
