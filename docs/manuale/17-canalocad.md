# CanaloCAD

CanaloCAD progetta e verifica le canaline portacavi. Il suo codice è anche dentro CartesioCAD: calcoli, importazione dei disegni e tavola sono quelli della pagina, non una copia rifatta, e la tavola DXF che esce da CartesioCAD è identica, byte per byte, a quella che esporta CanaloCAD. L'applicazione completa (una pagina web, inclusa) si apre dal pulsante *Apri CanaloCAD*.

Il database contiene solo tipologie, senza costruttori: per i cavi le designazioni normative (FG16OM16, FG16M16, FTG18OM16, FG16OH2R16, LiYCY, U/UTP Cat.6, fibra ottica...), per le canaline passerella asolata, canale cieco, passerella a filo e passerella a scala, con codici generici nella distinta.

## Canalina (`CANALINA`, `CANALINE`, `CABLETRAY`)

**Dove:** Plugin > CanaloCAD > Canalina. La tavolozza resta aperta di fianco al disegno.

1. **Disegnare la canalina**: la sezione (un rettangolo o un profilo a U di L×H mm, anche con i cerchi dei cavi e le sigle) e/o il percorso in pianta (linee, polilinee).
2. **Collegarla**: selezionare gli oggetti e premere **Nuova canalina**. Se è una sezione, misure e cavi si leggono dal disegno con l'importazione di CanaloCAD (cerchi = cavi, bordo = canalina, testi = sigle; misure anche dai testi come `L=300 H=75`, `CANALINA 75x300`). **Collega selezione** aggiunge altri oggetti a una canalina già creata, per esempio il percorso in pianta della sua sezione. Ogni canalina ha un codice (CN1, CN2...).
3. **Scegliere i cavi** dal **database**, sempre visibile in alto: categorie (BT multipolari e unipolari, fuoco e sicurezza, segnale e bus, dati e fibra, cavi propri, tutti) e ricerca. Si indica la quantità (con più cavi la sigla prende le fasi L1, L2, L3, N, PE) e **Terna a trifoglio** per gli unipolari, poi **Aggiungi** o doppio clic. Senza una canalina scelta, ne nasce una nuova.
   In alternativa **Importa selezione** (la sezione selezionata) o **Importa DWG/DXF...** (un file, come Importa DWG/DXF di CanaloCAD).
4. **Selezionando un oggetto della canalina** (sezione, percorso, tavola) la tavolozza mostra i suoi **cavi** e il **riepilogo** dei calcoli di CanaloCAD: riempimento e riserva, coefficiente di riduzione della portata Kr (fascio, strati, posa), segregazione CEI 64-8 art. 528.1, peso, carico e freccia dello staffaggio, tiro di posa, raggio di curvatura, portata (con le correnti d'impiego) e distinta dei materiali. In rosso quello che CanaloCAD segnala (segregazione, sovraccarico, tiro, portata).

Nella tabella dei cavi: **Modifica...** (o doppio clic) cambia sigla, descrizione e corrente d'impiego Ib; **Togli** e **Svuota**. **Dimensiona...** sceglie la larghezza per la riserva voluta, come Dimensiona di CanaloCAD.

**Dati canalina...**: titolo, misure, spessore, tipo di profilo, materiale e finitura, coperchio, setti, disposizione dei cavi (per gravità o come disegnati), conduttore PE, paraspigoli, fascette, distanza dal soffitto; tratto (lunghezza, curve, derivazioni, riduzioni, attraversamenti EI120); staffaggio e passo, antisismico; per la tavola scala, dimensione dei testi, impaginazione, vista 3D, curva di carico, distinta, cartiglio, descrizione delle utenze, bandierine, cartellino in pianta e i dati del cartiglio.

**Disegna sezione** mette nel disegno la sezione di CanaloCAD al vero (profilo, cavi con i conduttori, bandierine e sigle, quote, staffaggio), con i layer di CanaloCAD, a destra del disegno; si sposta con `MOVE` e, ridisegnandola dopo una modifica, si aggiorna al suo posto. **Inserisci tavola** inserisce la tavola completa come blocco `Tavola_CN1` (si sceglie il punto); se c'è già, la aggiorna insieme alle copie inserite. **Salva tavola DXF...** salva il file della tavola, lo stesso che esporta CanaloCAD.

Le canaline (dati e cavi) si salvano nel disegno (DWG/DXF) e ogni modifica si annulla con Ctrl+Z.

**Cavi propri.** Un cavo che non c'è nel database si crea con **Nuovo cavo...** (parte dai dati del cavo scelto): codice, tipo, formazione, descrizione, diametro esterno, peso (0 = stimato dal diametro), sezione, portata di riferimento e sistema (energia, sicurezza, segnale: conta per la segregazione). Va nella **libreria personale**, salvata nelle impostazioni del programma e valida per tutti i disegni; **Modifica...** ed **Elimina** valgono per i cavi personali. Nella categoria *Cavi propri* ci sono anche i cavi propri del progetto dell'impianto aperto (tavolozza Impianto).

## Verifica sezione (`CANALOVERIFICA`, `CANALOCHECK`)

**Dove:** Plugin > CanaloCAD > Verifica sezione. Apre la tavolozza Canalina con la canalina degli oggetti selezionati; una sezione disegnata non ancora collegata diventa una canalina nuova, con misure e cavi letti dal disegno.

Negli script: `-CANALOVERIFICA` (risultati nella riga di comando) e `-CANALOTAVOLA` (anche la tavola di CanaloCAD accanto alla sezione).

## Sezione dal database (`CANALOSEZIONE`, `CANALOCAVI`)

Apre la tavolozza Canalina. Negli script: `-CANALOSEZIONE FG16_4G10*2,FG16_3G2.5,DATA_CAT6*4` (codici del database, i cavi personali come `USR_codice`, la quantità dopo `*`, senza spazi) crea una canalina con quei cavi, la dimensiona (riserva 30%) e ne disegna la sezione.

## Apri CanaloCAD (`CANALOCAD`, `CANALO`)

Apre l'applicazione CanaloCAD nel browser (la copia inclusa in CartesioCAD, o quella indicata nelle Opzioni > Plugin).

## Invia sezione (`CANALOINVIA`, `CANALOSEND`)

Salva gli oggetti selezionati in un DXF in millimetri da importare in CanaloCAD; il percorso del file va negli appunti.

## Inserisci tavola (`CANALOINS`, `CANALOINSERISCI`) e Ultima tavola (`CANALOULTIMA`, `CANALOLAST`)

Inseriscono come blocco una tavola DXF esportata da CanaloCAD (scelta dal file, oppure l'ultima della cartella di scambio), scalata con le unità del disegno. La cartella di scambio (i Download, o quella delle Opzioni) è sorvegliata: una tavola nuova viene segnalata nella riga di comando.
