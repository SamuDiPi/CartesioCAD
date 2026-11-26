# Impianto: tubi, canali e passerelle

L'Impianto è il database del progetto elettrico, condiviso da tutte le piante: circuiti, cavi, tratte di tubi, canali e passerelle, con le verifiche.

## La tavolozza Impianto (`IMPIANTO`)

**Dove:** Impianto > Impianti elettrici > Impianto.

In alto: *Nuovo...*, *Apri...*, *Importa schema PDF...*, *Verifica*, **?** (apre la guida dell'Impianto). Sotto: nome del progetto e riepilogo (circuiti, tratte, quante non verificate). Tre schede: **Circuiti**, **Tratte**, **Cavi propri**. Doppio clic su una riga per modificarla; *Aggiungi*, *Modifica*, *Elimina* in basso.

## Il file di progetto (.cdb)

*Nuovo...* crea il file di progetto (per esempio `Impianto.cdb`) accanto ai disegni; *Apri...* ne apre uno esistente. Il disegno corrente si **collega** al progetto e lo ricorda: riaprendo il disegno, il progetto si riapre da solo (se la cartella è stata spostata, lo cerca accanto al disegno). Tutte le piante dello stesso impianto usano lo stesso file; ogni modifica si salva subito.

## Circuiti

- **Importa schema PDF**: legge lo schema unifilare dei quadri: sigla, descrizione, tipo di cavo e formazione (per esempio `FG16OM16 1(3G2,5)`, `3(1x185)+(1x95)+(1PE95)`), corrente d'impiego Ib, corrente nominale In dell'interruttore, lunghezza. Una nuova importazione aggiorna i circuiti già presenti e conserva i dati scritti a mano.
- **Aggiungi** (o doppio clic per modificare): sigla, descrizione, **sistema** (energia, sicurezza resistente al fuoco, segnale), tipo e formazione del cavo, diametro imposto (0 = dal catalogo), Ib, In, portata Iz imposta (0 = dalle tabelle), lunghezza (0 = somma delle tratte disegnate), interruttore, note.
- La tabella mostra per ogni circuito: esito, sistema, cavo, diametro, Ib, In, Iz', caduta di tensione ΔV%, tratte che lo contengono, note.
- **Formazioni riconosciute**: `3G2,5`, `4(1x95)`, `3x(1x120)+70`, `2(1x1,5)+(1PE1,5)` (conduttore di protezione), `3(1x185)+(1x95)+(1PE95)` (neutro tra parentesi), `3(3x1x240)+(2x240)` (terne in parallelo). Con un tipo come `FG16M16/FG17 PE` le fasi sono FG16M16 e il PE è FG17.
- I diametri dei cavi vengono dal catalogo per tipo e formazione; se il tipo non c'è si usa un cavo con la stessa formazione o si stima, e la nota lo dice (arancione).

## Cavi propri

Tipi di cavo che mancano nel catalogo: codice, tipo, formazione, diametro, peso (0 = stimato), sezione, portata di riferimento, sistema. Valgono per i circuiti con lo stesso tipo e la stessa formazione.

## Tratte (`TRATTA`)

**Dove:** Impianto > Impianti elettrici > Nuova tratta; Impianto > Tratte > *Nuova tratta dalla selezione...*

1. Disegnare il percorso del tubo, del canale o della passerella con linee o polilinee (anche più pezzi).
2. Selezionarlo.
3. *Nuova tratta dalla selezione*: tipo (**tubo**, **canale**, **passerella**), nome, misure, circuiti.
   - Tubo: rigido o corrugato, diametro esterno (16…63), diametro interno (0 = valore indicativo della serie pesante in PVC; scrivere quello del costruttore), **posa** (a vista o incassato in parete isolante).
   - Canale o passerella: larghezza e altezza, setti separatori, coperchio; per la passerella anche serie, staffaggio e passo.
   - Temperatura ambiente.
   - Circuiti: sigle separate da `;`, oppure doppio clic nell'elenco. Un circuito ripetuto sono due linee uguali.

La lunghezza della tratta si **misura dal disegno**. *Aggiungi la selezione* unisce altre linee a una tratta esistente; *Mostra* la seleziona e la inquadra.

## Tratte da layer (`TRATTEDALAYER`)

**Dove:** tavolozza Impianto, scheda Tratte > Tratte da layer.

Le linee e polilinee dei layer delle canalizzazioni (proposti quelli con nomi come canaline, passerelle, tubi) diventano tratte, senza selezionarle una per una.

- **Rami:** ogni tratta è un ramo, fra derivazioni (dove si incontrano tre o più linee, o una linea finisce su un'altra) ed estremi. Due linee in fila con lo stesso stile restano la stessa tratta.
- **Tipo e misura per stile:** le linee si raggruppano per stile (tipo di linea e spessore), come nelle legende. Per ogni stile la finestra mostra quante linee e quanti metri contiene e propone tipo e misura letti dalla **legenda** del disegno (righe come "CANALINA METALLICA ... 400x75mm" con il campione di linea a sinistra). Si può scegliere un'altra misura o *Ignora*.
- **Scritte vicine:** una scritta con tipo e misura vicino a un ramo (entro 2 m, per esempio "CANALINA METALLICA 200x75mm") vale per quel ramo più della legenda.
- **Unità reali del disegno:** nella prima finestra. Se il file dichiara unità sbagliate (per esempio millimetri, ma le porte da 90 cm misurano 9 unità: il disegno è in decimetri), si sceglie qui l'unità vera. Vale per tratte, percorsi, distinta e CanaloCAD, e si salva con il disegno. La seconda finestra mostra i metri totali delle linee, per controllo.

I pezzi sotto i 50 cm (trattini, polilinee con punti doppi) si scartano. Le linee già in una tratta non si toccano. Ctrl+Z toglie in un passo il collegamento delle linee alle tratte create; le tratte restano nel progetto e si possono eliminare dalla tavolozza.

## Percorsi automatici (`PERCORSIIMPIANTO`, `PERCORSI`)

**Dove:** tavolozza Impianto, scheda Tratte > Percorsi automatici.

Assegna da solo ogni circuito alle tratte del suo percorso e ne calcola la lunghezza, dal quadro agli utilizzatori.

- **Rete delle tratte:** le linee e polilinee delle tratte di questo disegno. Si collegano dove si toccano gli estremi o dove un estremo cade su un'altra tratta (derivazione a T). Due tratte che si incrociano e basta **non** sono collegate: possono essere a quote diverse. Per collegarle si spezza una delle due nel punto d'incrocio.
- **Partenza:** il quadro, cioè la prima parte della sigla del circuito ("QPT-SX-N" in "QPT-SX-N C-14"). Nel disegno è l'oggetto segnato con *Quadro dalla selezione*, oppure un testo che contiene solo il nome del quadro.
- **Arrivi:** le sigle del circuito scritte in pianta, sui layer delle sigle della tavolozza Circuiti. Se c'è un punto (utilizzatore) vicino alla sigla, si parte da quello.
- **Percorso:** il più breve sulla rete. Quadro e utilizzatori si attaccano al punto più vicino delle tratte, se sono entro la distanza massima scelta (5 m di partenza). Il tratto di attacco conta nella lunghezza ma non entra in nessuna tratta.
- **Lunghezza:** quella dell'utilizzatore più lontano più l'aggiunta per risalite, discese e scorte (3 m di partenza). Si usa per la caduta di tensione e per i metri di cavo della distinta quando lo schema non dà una lunghezza.

Prima di applicare compare il riepilogo: circuiti con percorso, utilizzatori non raggiunti, quadri non trovati, e nei dettagli le tratte e la lunghezza di ogni circuito. Applicando, ogni circuito calcolato esce dalle tratte di questo disegno ed entra in quelle del suo percorso. Le tratte di altri disegni e i circuiti non calcolati non cambiano.

## Quadro dalla selezione (`QUADROIMPIANTO`)

**Dove:** tavolozza Impianto, scheda Tratte > Quadro dalla selezione.

Si seleziona nel disegno il simbolo del quadro e si sceglie il suo nome: diventa il punto di partenza dei suoi circuiti (il centro dell'ingombro degli oggetti). Un quadro ha un solo segno: segnandone un altro, il precedente si toglie. Il segno si salva con il disegno.

## Verifiche (`VERIFICAIMPIANTO`)

**Dove:** Impianto > Impianti elettrici > Verifica impianto; Impianto > *Verifica*.

Per ogni tratta:
- **tubo**: diametro interno di almeno **1,3 volte il fascio** di cavi (il cerchio più piccolo che li contiene, CEI 64-8), oppure 1,5 volte se nella tratta si sceglie *Diametro interno minimo: 1,5 volte il fascio* (capitolati che lo chiedono, tratti lunghi con molte curve); se non basta indica il diametro giusto;
- **canale**: cavi al massimo al **50%** della sezione;
- **passerella**: riempimento e calcoli di CanaloCAD, **staffaggio** (carico sul passo e freccia);
- **segregazione**: segnali non schermati insieme a circuiti di energia nello stesso tubo o scomparto (CEI 64-8 art. 528.1); avviso per i circuiti di sicurezza insieme agli ordinari.

Per ogni circuito:
- **portata**: Ib ≤ In ≤ Iz'. Iz' parte dalla portata di tabella (IEC 60364-5-52:2009, ripresa dalla CEI 64-8:2024 Parte 5 Cap. 52, e CEI-UNEL 35024/1 in vigore; rame, PVC o EPR secondo il tipo di cavo, 2 o 3 conduttori caricati) per il metodo di posa della tratta: tubo a vista B1/B2, incassato A1/A2, canale B1/B2, passerella E (multipolari) o C (unipolari, cautelativo); poi raggruppamento dei circuiti e temperatura ambiente. Vale la tratta più sfavorevole. La portata si può imporre a mano nel circuito;
- **caduta di tensione**: con la lunghezza dello schema o, se manca, delle tratte disegnate; avviso oltre il 4%.

L'esito è colorato nella tavolozza (verde, arancione, rosso) e il dettaglio va nella riga di comando. I valori sono indicativi: le tabelle di portata di CartesioCAD non sono ancora state confrontate con il testo della norma (CEI 64-8:2024 Cap. 52 = IEC 60364-5-52:2009 con il corrigendum 2011; CEI-UNEL 35024/1) e, come i diametri, vanno verificate con le norme e i dati dei costruttori prima di usarle in un progetto. Lo ricordano una riga sotto la tabella dei circuiti e la fine di ogni verifica. Non sono gestiti i cavi interrati (metodi D1/D2, CEI-UNEL 35026), gli unipolari in aria libera distanziati (F, G) e la riduzione per le armoniche; con 2 conduttori caricati le sezioni da 150 mm² in su vanno imposte a mano.

## Etichette in pianta (`ETICHETTETRATTE`)

**Dove:** Impianto > Tratte > *Etichette in pianta*.

Scrive accanto a ogni tratta tipo, misura, numero di circuiti, riempimento ed esito, colorati per esito, sul layer `IMP-TRATTE-ETICHETTE`. Ripetendo il comando le etichette si aggiornano.

## Sezione della tratta (`SEZIONETRATTA`)

**Dove:** Impianto > Tratte > *Disegna sezione*.

Accanto al percorso disegna la sezione ingrandita della tratta: tubo con il fascio di cavi appoggiato sul fondo, oppure canale o passerella con i cavi posati; il numero del circuito su ogni cavo, il titolo con l'esito e la legenda. Layer `IMP-SEZIONI`; ridisegnandola si sostituisce.

## Distinta materiali (`DISTINTAIMPIANTO`)

**Dove:** Impianto > Tratte > *Distinta...*

Conta i materiali delle tratte disegnate nel disegno corrente: tubi (con curve, manicotti e cassette di derivazione), canali e passerelle (con coperchi, setti, curve, derivazioni, giunzioni e staffe al passo), metri di cavo per tipo e formazione. Curve e derivazioni si ricavano dal percorso (cambi di direzione oltre 15°, punti dove si incontrano tre o più tratti). Si salva per **Excel** (CSV) e/o come **tabella nel disegno** (layer `IMP-DISTINTA`). Quantità senza sfrido.

## Fronte quadro (`FRONTEQUADRO`, `FQ`)

**Dove:** Impianto > Impianti elettrici > *Fronte quadro*.

Dallo schema unifilare in PDF (lo stesso dei *Circuiti*) prepara il fronte di ogni quadro: ingombro, carpenteria, grado IP e forma di segregazione, disegnati in scala 1:1 in millimetri con le quote e i dati sopra il fronte (layer `FQ-CARPENTERIA`, `FQ-APPARECCHI`, `FQ-BLOCCHI`, `FQ-SBARRE`, `FQ-RISALITE`, `FQ-TESTI`, `FQ-QUOTE`). Insieme si salvano, accanto al PDF, la **distinta** in CSV per Excel (carpenterie, zoccoli, colonne sbarre e cavi, sbarre con la portata, file DIN, coprimoduli di riserva, morsetti, apparecchi con il riferimento) e il file **IFC4** per il BIM: un `IfcElectricDistributionBoard` per quadro con il suo ingombro e le proprietà (grado IP, forma, carpenteria, riferimento, corrente d'arrivo, moduli occupati e disponibili).

Gli apparecchi si riconoscono da quanto è scritto nello schema, mai dal nome del prodotto:
- l'**esecuzione** (modulare, scatolato, aperto, anche con un accessorio modulare come `SCATOLATO/MODULARE`), i **poli** e la **corrente nominale**;
- la **curva**: `gL`, `gG` o `aM` sono fusibili (il formato come `10.3x38` dà il portafusibili), nessuna curva è un sezionatore;
- gli **SPD** (classe `T1+T2` o `T2`, o la descrizione), il **differenziale** e gli accessori indicati con `+`;
- i **morsetti** dalla formazione dei cavi fino a 35 mm²; oltre, il cavo arriva all'apparecchio.

Gli ingombri sono **valori tipici** (modulo DIN da 18 mm, file da 150 mm, scatolati e aperti per taglia di corrente): servono a dimensionare il quadro e vanno confermati sul catalogo prima dell'ordine. I quadri piccoli vanno a parete o in cassetta, quelli con scatolati grandi, aperti o molte file a pavimento in colonne affiancate: gli aperti oltre 1600 A (gli arrivi) hanno una colonna ciascuno, gli altri si impilano nella stessa colonna finché c'è altezza.

Nella finestra si scelgono:
- **Quadri**: tutti, o le sigle separate da virgola;
- **Ambiente**, che propone il grado IP (locale tecnico IP31, ambiente ordinario IP43, esterno IP55, lavaggi o polveri IP65), oppure il **grado IP** scritto a mano;
- **Forma di segregazione** (CEI EN 61439-2): proposta dalla corrente d'arrivo (1 fino a 250 A, 2b fino a 1600 A, 3b oltre) o scelta;
- **Installazione**: automatica, a parete, a pavimento, cassetta;
- **Spazio di riserva** (25% di partenza) e **morsettiera** nelle file in basso;
- **Risalita cavi** e **sbarre verticali** nei quadri a pavimento (vedi sotto);
- il **catalogo delle carpenterie** e il **catalogo degli apparecchi** con i blocchi dei costruttori.

### Salite: sbarre verticali e risalita dei cavi

Nei quadri a pavimento sbarre e cavi hanno **colonne proprie**, ognuna con la sua carpenteria del catalogo (layer `FQ-SBARRE` e `FQ-RISALITE`):

- **Sbarre principali orizzontali**, in alto sul retro di tutte le colonne, dimensionate sulla corrente di arrivo.
- **Colonna delle sbarre verticali** a sinistra di ogni colonna apparecchi quando la forma di segregazione è 2a o superiore (nella forma 1 le sbarre stanno sul fondo della colonna; le colonne degli interruttori aperti si collegano direttamente alle sbarre principali). La corrente della colonna è la somma delle correnti nominali delle sue partenze per il **fattore di contemporaneità** della IEC 61439-2 (2–3 circuiti 0,9; 4–5 0,8; 6–9 0,7; da 10 in su 0,6), almeno quella della partenza più grande e al più quella di arrivo. La larghezza è la barra più 100 mm per lato (almeno 300 mm); la profondità deve contenere i 4 poli affiancati (pacco di barre + 50 mm ciascuno, più 100 mm davanti e dietro), altrimenti il quadro passa a 600 o 800 mm.
- **Colonna di risalita dei cavi** a destra: l'area di ogni cavo si stima dalla formazione dello schema (diametro tipico 1,35·√S + 4,5 mm per gli unipolari, 1,7·√(n·S) + 5 mm per i multipolari) e la colonna si riempie al massimo al **35%** della sezione larghezza × (profondità − 100 mm). La larghezza minima segue il raggio di curvatura del cavo più grande: 300 mm fino a 95 mm², 400 mm fino a 185 mm², 600 mm oltre; poi 800, 1000 o 1200 mm se serve per il riempimento.

**Portata delle sbarre.** Rame nudo 3F+N con il neutro pieno, dalla tabella **DIN 43671** (corrente alternata, aria a 35 °C, sbarra a 65 °C) per 1, 2 o 3 barre per fase da 20×5 a 120×10 mm, ridotta per l'aria dentro il quadro con k = √((65 − Ti)/30): Ti = 45 °C fino a IP31 (k 0,82), 50 °C oltre (k 0,71). Si sceglie la configurazione con la sezione più piccola che basta; la barra PE segue la tabella 5 della IEC 61439-1 (S/2 fino a 400 mm², 200 mm² fino a 800 mm², S/4 oltre). Nel disegno ogni colonna sbarre mostra i poli L1, L2, L3, N e la barra PE, la configurazione e la scritta «I colonna … A – portata … A (tabella × k)»; i dati del quadro riportano le sbarre principali con la loro portata. Sono valori **prudenziali e indicativi**: il sistema sbarre certificato del costruttore (prove IEC 61439) e la tenuta al cortocircuito vanno sempre verificati. Oltre 3 barre 120×10 il programma avvisa di usare un condotto sbarre o il sistema del costruttore.

La **larghezza del vano cavi** si può anche scegliere (300 o 400 mm, come le colonne del sistema usato): il programma la usa per tutti i vani e avvisa se il riempimento supera il 35% o se il vano è stretto per il raggio di curvatura del cavo più grande.

Con *Risalita cavi* **automatica**, due colonne apparecchi vicine condividono la colonna cavi in mezzo (sbarre – apparecchi – cavi – apparecchi – sbarre) quando i loro cavi stanno in 600 mm; altrimenti ogni colonna apparecchi ha la sua. Si può anche scegliere una colonna cavi per ogni colonna apparecchi, una sola colonna cavi in fondo al quadro per tutti i cavi, o nessuna risalita. Le colonne possono quindi essere di larghezze diverse e la distinta elenca le carpenterie per tipo (apparecchi, sbarre verticali, risalita cavi), le sbarre principali e quelle di ogni colonna con corrente e portata, e i vani cavi con riempimento e cavo più grande.

### Catalogo delle carpenterie

CartesioCAD contiene solo tipi generici con misure tipiche (per esempio `PAV-800x2000x400-IP31`). Per avere nel disegno e nella distinta il riferimento commerciale della carpenteria si indica un proprio file CSV, una riga per carpenteria:

```
codice;tipo;larghezza;altezza;profondita;ip;moduli_per_fila;riferimento;fonte
P600-1050;parete;600;1050;200;43;24;Serie e codice del costruttore;catalogo 2026 pag. 12
```

Due colonne facoltative in fondo, `blocco;nome_blocco`, indicano il disegno del fronte della carpenteria (DWG o DXF in mm, percorso relativo alla cartella del CSV) e, se il file contiene più blocchi, quale usare: la colonna si disegna con quel blocco dall'angolo in basso a sinistra.

`tipo` vale `parete`, `pavimento` o `cassetta`; misure in mm; `moduli_per_fila` vuoto o 0 = 24 fino a 600 mm di larghezza, 36 da 800 mm. Il separatore può essere `;` o `,`; le righe non valide si saltano e se ne indica il numero. Fra quelle adatte si sceglie la carpenteria più piccola con grado IP almeno pari a quello richiesto.

Fra le carpenterie che vanno bene, quella del **catalogo dell'utente** vince su quella interna se è al più 100 mm più larga (650 invece di 600 sì, 800 invece di 300 no). Il blocco del fronte della carpenteria si inserisce dall'angolo in basso a sinistra: sull'appoggio a terra se comprende lo zoccolo (alto quanto colonna e zoccolo), altrimenti sopra lo zoccolo.

### Catalogo degli apparecchi e blocchi dei costruttori

Per disegnare il fronte con i **blocchi veri** degli apparecchi (le viste frontali 2D che i costruttori pubblicano nei loro siti) si indica un CSV, una riga per tipo di apparecchio:

```
codice;tipo;poli;in_min;in_max;moduli;larghezza;altezza;blocco;nome_blocco;riferimento;fonte;modello;piastra;famiglia
LIB1;famiglia;;;;;;;;;;;parola1|parola2;;
SC630-4P;scatolato;4;251;630;;;;costruttore/scatolato630-4p-piastra.dxf;;Serie e codice;sito del costruttore;;si
MOD-1P;modulare;1;0;63;1;;;costruttore/modulare-1p-fronte.dxf;;Serie e codice;sito del costruttore;;
PM-3;piastra-modulare;;;;;;;costruttore/piastra-modulare-3-moduli.dxf;;;;;
PC-3;piastra-cieca;;;;;;;costruttore/piastra-cieca-3-moduli.dxf;;;;;
```

- `tipo`: `modulare`, `differenziale` (modulare con il differenziale, moduli compresi), `sezionatore`, `fusibile`, `spd`, `strumento`, `scatolato`, `scatolato-differenziale` (solo per gli scatolati con differenziale), `sezionatore-scatolato`, `aperto`, `alimentatore`; inoltre `piastra-modulare`, `piastra-cieca` e `sbarre-verticali` (vedi sotto);
- `poli`: `4`, un intervallo come `3-4`, o vuoto per tutti; `in_min` e `in_max` in A (vuoti = qualsiasi corrente);
- `moduli`, `larghezza`, `altezza`: vuoti = presi dall'ingombro del blocco (in mm; i moduli dalla larghezza a passo di 18 mm);
- `blocco`: file DWG o DXF del fronte, relativo alla cartella del CSV; `nome_blocco` se il file contiene più blocchi.
- `modello` (facoltativo): testo cercato, senza distinguere maiuscole, nel modello scritto nello schema (per esempio la sigla della serie): la voce che lo trova vince su quelle senza modello.
- `piastra` (facoltativo): `si` quando il blocco dell'apparecchio comprende la sua **piastra di colonna** (il fronte com'è nel quadro, piastra e apparecchio): la fila è alta quanto la piastra, a passo di 50 mm, una piastra per fila, senza margini; le riserve sono piastre della stessa altezza.

Le voci `piastra-modulare` e `piastra-cieca` sono piastre senza apparecchio. Alle file DIN e alle morsettiere va la piastra modulare alta come la fila (150 mm) più stretta che contiene la guida; le riserve si riempiono con piastre cieche impilate, della stessa larghezza delle altre piastre della colonna (se non si riesce, la cieca singola della stessa altezza). Così il fronte è fatto di piastre come nel quadro vero. Le voci `sbarre-verticali` sono i disegni dei sistemi di sbarre: alla colonna sbarre va quello con la corrente della colonna fra `in_min` e `in_max` (l'intervallo più stretto), inserito al centro al posto dei poli schematici; configurazione e portata restano scritte accanto.

**Famiglie di blocchi.** Con più librerie (una per costruttore) ogni libreria è una famiglia: si dichiara con una riga di tipo `famiglia` (codice = nome della famiglia, colonna `modello` = parole che la riconoscono nello schema, separate da `|`) e ogni voce indica la sua nella colonna `famiglia`. Ogni apparecchio prende la prima famiglia le cui parole compaiono nel suo modello o nella descrizione; gli altri apparecchi del quadro prendono la famiglia più frequente del quadro, e se lo schema non ne nomina nessuna vale la **prima dichiarata** (la predefinita). Si usano prima le voci della famiglia dell'apparecchio (o senza famiglia), poi, se mancano, quelle delle altre; anche le piastre seguono la famiglia del quadro. La famiglia di ogni quadro compare nei messaggi. Anche il **catalogo delle carpenterie** ha la colonna `famiglia` (dopo `nome_blocco`): per un quadro si cercano prima le carpenterie della sua famiglia o senza famiglia. L'**altezza delle file DIN** è quella della piastra modulare più bassa della famiglia (per esempio 150 o 200 mm; 150 mm senza piastre). Il blocco della carpenteria si centra sulla colonna (alcuni comprendono i pannelli laterali). Nomi e parole stanno solo nel catalogo dell'utente.

Una voce il cui file del blocco **non c'è** viene saltata (vale la voce successiva) e il programma dice quante sono: si può quindi tenere nel catalogo anche ciò che si scaricherà più avanti.

**Nessun simbolo senza blocco.** Se nessuna voce ha poli e corrente adatti, l'apparecchio prende la voce **più vicina dello stesso tipo** (prima per corrente, poi per poli; prima nella sua famiglia, poi nelle altre): il programma lo dice per ogni quadro e lo scrive nella nota della distinta. Resta un apparecchio disegnato in modo generico solo se il catalogo non ha nessuna voce del suo tipo.

A ogni apparecchio dello schema va la voce più specifica (prima quelle con il modello trovato nello schema, poi il tipo `differenziale` per i circuiti con differenziale, poi l'intervallo di corrente più stretto); le sue misure sostituiscono quelle tipiche e servono a impaginare il quadro, e il blocco si inserisce centrato al suo posto (layer `FQ-BLOCCHI`), con la sigla del circuito sotto. Gli accessori in più (blocchi differenziali, strumenti) restano. Gli apparecchi senza voce mantengono gli ingombri tipici e il disegno generico; il programma dice quanti sono. Riferimento e fonte vanno nella distinta.

Se i due campi dei cataloghi sono vuoti, la finestra propone i file `CartesioCAD-cataloghi/carpenterie.csv` e `CartesioCAD-blocchi/apparecchi.csv` nella cartella Documenti (anche quella locale del profilo, se Documenti è sincronizzata altrove), quando esistono.

I blocchi dei costruttori sono protetti dal loro diritto d'autore: CartesioCAD non ne contiene, si scaricano dai siti dei costruttori e si usano secondo le loro condizioni, tenendoli in una cartella propria.

Negli script: `-FRONTEQUADRO schema.pdf [QUADRI a,b] [IFC file.ifc] [CSV file.csv] [CATALOGO file.csv] [APPARECCHI file.csv] [IP n] [FORMA 3b] [RISERVA 25] [AMBIENTE 0..3] [MONTAGGIO 0..3] [RISALITA 0..3] [VANOCAVI 300] [SENZAMORSETTI] [SENZASBARRE] [QUI] [NODISEGNO]` (`RISALITA`: 0 automatica, 1 nessuna, 2 una colonna cavi per colonna apparecchi, 3 una sola colonna cavi; `VANOCAVI`: larghezza del vano cavi in mm, 0 calcolata; `QUI` disegna nel disegno aperto invece che in uno nuovo).
