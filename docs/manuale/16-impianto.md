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

## Verifiche (`VERIFICAIMPIANTO`)

**Dove:** Impianto > Impianti elettrici > Verifica impianto; Impianto > *Verifica*.

Per ogni tratta:
- **tubo**: diametro interno di almeno **1,3 volte il fascio** di cavi (il cerchio più piccolo che li contiene); se non basta indica il diametro giusto;
- **canale**: cavi al massimo al **50%** della sezione;
- **passerella**: riempimento e calcoli di CanaloCAD, **staffaggio** (carico sul passo e freccia);
- **segregazione**: segnali non schermati insieme a circuiti di energia nello stesso tubo o scomparto (CEI 64-8 art. 528.1); avviso per i circuiti di sicurezza insieme agli ordinari.

Per ogni circuito:
- **portata**: Ib ≤ In ≤ Iz'. Iz' parte dalla portata di tabella (IEC 60364-5-52 / CEI-UNEL 35024/1, rame, PVC o EPR secondo il tipo di cavo, 2 o 3 conduttori caricati) per il metodo di posa della tratta: tubo a vista B1/B2, incassato A1/A2, canale B1/B2, passerella E (multipolari) o C (unipolari, cautelativo); poi raggruppamento dei circuiti e temperatura ambiente. Vale la tratta più sfavorevole. La portata si può imporre a mano nel circuito;
- **caduta di tensione**: con la lunghezza dello schema o, se manca, delle tratte disegnate; avviso oltre il 4%.

L'esito è colorato nella tavolozza (verde, arancione, rosso) e il dettaglio va nella riga di comando. I valori sono indicativi: le tabelle di portata di CartesioCAD non sono ancora state confrontate con la norma (CEI 64-8 / IEC 60364-5-52) e, come i diametri, vanno verificate con le norme e i dati dei costruttori prima di usarle in un progetto. Lo ricordano una riga sotto la tabella dei circuiti e la fine di ogni verifica.

## Etichette in pianta (`ETICHETTETRATTE`)

**Dove:** Impianto > Tratte > *Etichette in pianta*.

Scrive accanto a ogni tratta tipo, misura, numero di circuiti, riempimento ed esito, colorati per esito, sul layer `IMP-TRATTE-ETICHETTE`. Ripetendo il comando le etichette si aggiornano.

## Sezione della tratta (`SEZIONETRATTA`)

**Dove:** Impianto > Tratte > *Disegna sezione*.

Accanto al percorso disegna la sezione ingrandita della tratta: tubo con il fascio di cavi appoggiato sul fondo, oppure canale o passerella con i cavi posati; il numero del circuito su ogni cavo, il titolo con l'esito e la legenda. Layer `IMP-SEZIONI`; ridisegnandola si sostituisce.

## Distinta materiali (`DISTINTAIMPIANTO`)

**Dove:** Impianto > Tratte > *Distinta...*

Conta i materiali delle tratte disegnate nel disegno corrente: tubi (con curve, manicotti e cassette di derivazione), canali e passerelle (con coperchi, setti, curve, derivazioni, giunzioni e staffe al passo), metri di cavo per tipo e formazione. Curve e derivazioni si ricavano dal percorso (cambi di direzione oltre 15°, punti dove si incontrano tre o più tratti). Si salva per **Excel** (CSV) e/o come **tabella nel disegno** (layer `IMP-DISTINTA`). Quantità senza sfrido.
