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
