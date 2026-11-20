# CanaloCAD

CanaloCAD progetta e verifica le canaline portacavi. Il suo codice è anche dentro CartesioCAD: calcoli, importazione dei disegni e tavola sono quelli della pagina, non una copia rifatta, e la tavola DXF che esce da CartesioCAD è identica, byte per byte, a quella che esporta CanaloCAD. L'applicazione completa (una pagina web, inclusa) si apre dal pulsante *Apri CanaloCAD*.

Il database contiene solo tipologie, senza costruttori: per i cavi le designazioni normative (FG16OM16, FG16M16, FTG18OM16, FG16OH2R16, LiYCY, U/UTP Cat.6, fibra ottica...), per le canaline passerella asolata, canale cieco, passerella a filo e passerella a scala, con codici generici nella distinta.

## Canalina (`CANALINA`, `CANALINE`, `CABLETRAY`)

**Dove:** Plugin > CanaloCAD > Canalina. Si apre una finestra a sé, che si sposta e si allarga (anche su un altro schermo) e resta aperta mentre si lavora nel disegno.

1. **Disegnare la canalina**: la sezione (un rettangolo o un profilo a U di L×H, anche con i cerchi dei cavi e le sigle) e/o il percorso in pianta (linee, polilinee).
2. **Selezionarla e premere Usa selezione.** Una sezione diventa una canalina con **le misure disegnate** (al millimetro: un rettangolo di 320×80 dà una canalina di 320×80) e i cavi letti dal disegno con l'importazione di CanaloCAD (cerchi = cavi, testi = sigle); al posto degli oggetti disegnati CanaloCAD ridisegna la sezione al vero: profilo, cavi con i conduttori, bandierine, quote, staffaggio (Ctrl+Z torna al disegno di prima). Un percorso in pianta si collega alla canalina scelta; un oggetto di una canalina la mostra. Ogni canalina ha un codice (CN1, CN2...).
   Si può anche partire senza disegnare: **Nuova**, o direttamente un cavo dal database.
3. **Scheda Cavi**: a sinistra il **database** (categorie: BT multipolari e unipolari, fuoco e sicurezza, segnale e bus, dati e fibra, cavi propri, tutti; ricerca), a destra i **cavi della canalina**. Quantità (con più cavi la sigla prende le fasi L1, L2, L3, N, PE), **Terna a trifoglio** per gli unipolari, poi **Aggiungi** o doppio clic. **Modifica...** (o doppio clic) cambia sigla, descrizione e corrente d'impiego Ib; **Togli**, **Svuota**. La sezione nel disegno si aggiorna a ogni cambio.
   **Misura automatica dai cavi** (con la **riserva** in %): la larghezza è la più stretta, non meno dell'altezza, che lascia la riserva e tiene i cavi dentro l'altezza, come Dimensiona di CanaloCAD. È attiva per le canaline nate dal database; per quelle disegnate la misura è quella del disegno, e se è piccola per i cavi il riepilogo lo dice in rosso con la larghezza che servirebbe.
   In basso, sempre visibili: misura, riempimento, Kr, segregazione.
4. **Scheda Riepilogo e tavola**: i calcoli di CanaloCAD (riempimento e riserva, coefficiente di riduzione della portata Kr con fascio, strati e posa, segregazione CEI 64-8 art. 528.1, peso, carico e freccia dello staffaggio, tiro di posa, raggio di curvatura, portata con le correnti d'impiego) e la distinta dei materiali. In rosso quello che CanaloCAD segnala. **Disegna sezione** mette la sezione di una canalina nata dal database a destra del disegno (poi si aggiorna da sola; **Mostra la sezione** la inquadra). **Inserisci tavola** inserisce la tavola completa come blocco `Tavola_CN1` (si sceglie il punto); se c'è già, la aggiorna insieme alle copie inserite. **Salva tavola DXF...** salva il file della tavola, lo stesso che esporta CanaloCAD.

Selezionando nel disegno un oggetto di una canalina (sezione, percorso, tavola) la finestra passa a quella canalina.

**Dati canalina...**: titolo, misure, spessore, tipo di profilo, materiale e finitura, coperchio, setti, disposizione dei cavi (per gravità o come disegnati), conduttore PE, paraspigoli, fascette, distanza dal soffitto; tratto (lunghezza, curve, derivazioni, riduzioni, attraversamenti EI120); staffaggio e passo, antisismico; per la tavola scala, dimensione dei testi, impaginazione, vista 3D, curva di carico, distinta, cartiglio, descrizione delle utenze, bandierine, cartellino in pianta e i dati del cartiglio. Cambiando a mano larghezza o altezza la misura non segue più i cavi.

**Importa DWG/DXF...**: uno o più DWG o DXF di progetto, letti interi come fa Importa DWG/DXF di CanaloCAD (un DXF passa così com'è; un DWG si converte con LibreDWG, la stessa libreria di CanaloCAD), una sezione per file come le schede di CanaloCAD. Per ogni file si sceglie **a cosa si riferisce il tratto**:
- **a una polilinea del disegno**: la finestra chiede di cliccarla (una striscia gialla, con **Annulla**); la canalina si collega a quella polilinea (o a quelle linee) e la lunghezza del tratto è la sua. Se la polilinea è già di una canalina, prende misure e cavi importati;
- **a una canalina esistente** (CN1, CN2...): ne prende misure, cavi e lunghezza, e il resto (staffaggio, tavola, sezione nel disegno) resta;
- **particolare a sé stante**: la canalina nuova con la sua sezione disegnata a destra del disegno;
- **solo i calcoli**: la canalina nuova, senza niente nel disegno.

Negli script: `-CANALOIMPORTA file POLILINEA|DETTAGLIO|NIENTE|CN1` (POLILINEA = la polilinea selezionata).

Le canaline (dati e cavi) si salvano nel disegno (DWG/DXF) e ogni modifica si annulla con Ctrl+Z.

**Cavi propri.** Un cavo che non c'è nel database si crea con **Nuovo cavo...** (parte dai dati del cavo scelto): codice, tipo, formazione, descrizione, diametro esterno, peso (0 = stimato dal diametro), sezione, portata di riferimento e sistema (energia, sicurezza, segnale: conta per la segregazione). Va nella **libreria personale**, salvata nelle impostazioni del programma e valida per tutti i disegni; **Modifica...** ed **Elimina** compaiono scegliendo un cavo personale. Nella categoria *Cavi propri* ci sono anche i cavi propri del progetto dell'impianto aperto (tavolozza Impianto).

## Verifica sezione (`CANALOVERIFICA`, `CANALOCHECK`)

**Dove:** Plugin > CanaloCAD > Verifica sezione. Apre la finestra Canalina con la canalina degli oggetti selezionati; una sezione disegnata non ancora collegata diventa una canalina nuova, come con Usa selezione.

Negli script: `-CANALOVERIFICA` (risultati nella riga di comando) e `-CANALOTAVOLA` (anche la tavola di CanaloCAD accanto alla sezione).

## Sezione dal database (`CANALOSEZIONE`, `CANALOCAVI`)

Apre la finestra Canalina. Negli script: `-CANALOSEZIONE FG16_4G10*2,FG16_3G2.5,DATA_CAT6*4` (codici del database, i cavi personali come `USR_codice`, la quantità dopo `*`, senza spazi) crea una canalina con quei cavi, la dimensiona (misura automatica, riserva 30%) e ne disegna la sezione.

## Apri CanaloCAD (`CANALOCAD`, `CANALO`)

Apre l'applicazione CanaloCAD nel browser (la copia inclusa in CartesioCAD, o quella indicata nelle Opzioni > Plugin).

## Invia sezione (`CANALOINVIA`, `CANALOSEND`)

Salva gli oggetti selezionati in un DXF in millimetri da importare in CanaloCAD; il percorso del file va negli appunti.

## Inserisci tavola (`CANALOINS`, `CANALOINSERISCI`) e Ultima tavola (`CANALOULTIMA`, `CANALOLAST`)

Inseriscono come blocco una tavola DXF esportata da CanaloCAD (scelta dal file, oppure l'ultima della cartella di scambio), scalata con le unità del disegno. La cartella di scambio (i Download, o quella delle Opzioni) è sorvegliata: una tavola nuova viene segnalata nella riga di comando.
