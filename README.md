# CartesioCAD

CAD 2D open source con l'interfaccia e il modo di lavorare dei CAD professionali più diffusi: barra multifunzione,
riga di comando, aiuti al disegno, layer, blocchi, quote e file **DWG/DXF**.
Scritto in C++20 con Qt 6.

> Stato: **in sviluppo attivo**.

## Interfaccia

- **Barra multifunzione** con le schede Inizio, Inserisci, Layout, Output, Gestisci, Annota e Vista; barra di accesso rapido e menu dell'applicazione.
- **Più disegni aperti** in schede. Si possono trascinare file DWG/DXF sulla finestra.
- **Italiano e inglese**: Opzioni > Generale > Lingua (automatica = inglese se la lingua del sistema non è l'italiano; vale al prossimo avvio). Tradotte barra multifunzione, finestre, tavolozze e messaggi del programma (`translations/cartesiocad_en.ts`, aggiornato con `scripts/update-translations.sh`); restano in italiano le richieste dei comandi nella riga di comando, il manuale e la pagina di CanaloCAD. I nomi dei comandi sono quelli inglesi (LINE, TRIM...), con gli alias italiani dove ci sono.
- **Annulla completo**: oltre agli oggetti si annullano le modifiche a layer (proprietà, layer nuovi, layer corrente), agli stili di testo, di quota e delle direttrici (nuovi, modificati, rinominati, eliminati, corrente), alla scala di annotazione e alle definizioni dei blocchi (`BSAVE`, gestione attributi, riferimenti esterni: Associa, Stacca, percorsi). Un comando che crea un layer e disegna sopra si annulla con un solo `U`. Non si annulla l'eliminazione dei layer (`PURGE`).
- **Polilinee con spessore** (larghezza costante o diversa per ogni tratto, letta e scritta nei DXF/DWG): aree piene con gli spigoli raccordati; *Larghezza globale* e larghezze dei tratti nella tavolozza Proprietà.
- **Pagina *Inizia***, mostrata all'avvio (si può spegnere in Opzioni > Apri e salva):
  - *Inizia disegno* con l'elenco dei **modelli** (file .dwt, .dwg o .dxf della cartella indicata nelle Opzioni; il disegno nuovo parte da una copia senza nome);
  - **documenti recenti** con miniatura (l'anteprima salvata nel DWG/DXF dal programma che l'ha scritto, o quella disegnata da CartesioCAD al salvataggio), raggruppati per data, con la puntina per bloccarli in cima, tre modi di vista e il menu del tasto destro (apri cartella, rimuovi dall'elenco);
  - scheda *APPRENDI* con i comandi principali da provare.
- **Riga di comando mobile** con cronologia (F2), autocompletamento dei comandi ed elenco dei comandi (`AIUTO`).
- **Input dinamico** accanto al cursore (F12), **puntamento polare** (F10) con incrementi a scelta, **snap ad oggetto** (F3) con etichette: punto finale, medio, centro, quadrante, nodo, inserimento, intersezione, perpendicolare, tangente e vicino (menu accanto al pulsante, variabile `OSMODE` con i valori standard), **ortho** (F8), **griglia** (F7), **snap alla griglia** (F9).
- **Pulsanti di navigazione** sul disegno (pan e zoom), icona degli assi, zoom con la rotellina, pan con la rotellina premuta.
- **Selezione** con clic, finestra (sinistra → destra) o intersezione (destra → sinistra), lazo (trascinando nel vuoto), ciclo degli oggetti sovrapposti (Maiusc+Spazio), anteprima dell'oggetto sotto il cursore; maniglie multifunzione (aggiungi/rimuovi vertice, converti in arco, allunga); opzioni Tutto/Ultimo/Precedente/Rimuovi nelle richieste di selezione.
- **Gestore proprietà layer**: acceso/spento, congelato, bloccato, colore, rinomina, elimina. Nell'elenco dei layer della barra multifunzione le stesse icone si cliccano direttamente.
- **Testi**: gli stili di testo dei DWG/DXF (tabella STYLE) si leggono e si salvano con font, larghezza, obliquità, grassetto e corsivo. I font SHX (romans, simplex, isocp...) si leggono dalle cartelle indicate nelle Opzioni (Apri e salva) e dalla cartella `fonts` accanto al programma, e si disegnano a tratti; un SHX mancante diventa simplex.shx o, se non c'è, il font a tratti incorporato (caratteri Hershey). CartesioCAD non distribuisce font SHX; quelli installati da altri programmi si usano solo spuntando l'opzione apposita, dopo aver controllato che la loro licenza lo permetta. I TrueType si cercano per famiglia o per file. **Doppio clic su un testo** (anche testo di direttrici, attributi dei blocchi, testo delle quote, celle delle tabelle; oppure `TEXTEDIT`, `DDEDIT`, `ED` con l'oggetto selezionato) per modificarlo direttamente nel disegno, con il suo carattere e la sua altezza: cursore e selezione sulle lettere, tasti di un editor normale, Invio va a capo nei testi multilinea, Ctrl+Invio o un clic fuori conferma, Esc annulla, Tab passa alla cella successiva; nei testi multilinea una barra cambia carattere, altezza, grassetto, corsivo e colore della parte selezionata. Nella tavolozza Proprietà anche stile e angolo obliquo.
- **Tavolozza Proprietà** (Ctrl+1, o doppio clic su un oggetto che non è un testo): tendina con la selezione divisa per tipo (scegliendo un tipo si modificano solo quelli), sezioni comprimibili Generale, Stile di stampa, Geometria/Testo/Quota/Attributi, Vista e Varie; colore, layer, tipo, scala e spessore di linea, trasparenza e geometria modificabili, anche su più oggetti insieme (i valori diversi compaiono come `*VARIA*`). Senza selezione mostra le proprietà correnti, la tabella stili dello spazio, la vista e le unità del disegno.
- **Schede dei layout**: il pulsante ≡ in basso a sinistra elenca tutti i layout per gruppo. Con il tasto destro su una scheda: *Sposta nel gruppo* (nuovo o esistente, es. "Piano terra") e *Raggruppa le schede dei layout*: una scheda per gruppo con il menu dei suoi layout (senza gruppi definiti, tutti i layout in una scheda sola). I gruppi si salvano nel DXF.
- **Opzioni** (`OPZIONI`, menu dell'applicazione o tasto destro): Visualizza (elementi del layout: colori di sfondo del modello, dei layout e del foglio, colore della griglia; con uno sfondo chiaro gli oggetti bianchi si disegnano neri; schede Layout e Modello, area stampabile, sfondo e ombreggiatura carta, Gestione impostazioni pagina e finestra nei layout nuovi; formato, orientamento e margine del foglio dei layout nuovi; colore dello sfondo dei layout), Apri e salva (formato predefinito, salvataggio automatico, copie .bak, file recenti, convertitore DWG, cartelle dei font SHX e TrueType), Stampa e pubblicazione (stampante, tabella stili, spessori di linea, stili di stampa, spazio carta per ultimo e oggetti dello spazio carta nascosti per i disegni e i layout nuovi; stampanti di sistema nascoste, timbro di stampa; cartella proposta per i PDF, nome del layout nel nome del file, apertura del PDF dopo la stampa; registro delle stampe in CSV, una riga per foglio; cartella delle tabelle stili), Preferenze utente (doppio clic, menu del tasto destro, scala di inserimento fra unità diverse), Disegno (marcatore e mirino dello snap), Selezione (mirino, selezione nome/verbo e a trascinamento, maniglie), Plugin.
- **Circuiti** (`CIRCUITI`, scheda Impianto > Impianti elettrici): si importa lo schema unifilare dei quadri in PDF e si ottiene la tabella dei circuiti con sigla, descrizione, tipo (luce, FM, utenza), potenza, fase, interruttore e cavo. *Verifica* confronta le sigle scritte in pianta (testi come `QPT-SX-N C-4` sul layer `E-ELEC-CIRC`) con lo schema: sigle sconosciute, circuiti dei quadri in pianta senza sigla, punti (blocchi sul layer `E-ELEC-FIXT`, anche esportati da programmi BIM) senza una sigla vicina; doppio clic per vederli nel disegno. *Assegna* scrive la sigla del circuito scelto accanto ai punti selezionati, con l'altezza delle sigle già presenti (si annulla con Ctrl+Z). La scheda *Legenda* legge la legenda delle lettere di identificazione dei punti (righe come `Tipo "D": alimentazione lama d'aria...`, oppure lettera e descrizione in due testi affiancati) ovunque sia: modello, layout o blocchi. Per ogni lettera propone i circuiti corrispondenti dello schema: dalle sigle già scritte accanto ad altri punti con la stessa lettera, altrimenti interpretando il testo della legenda (radici delle parole, sinonimi come UE = unità esterna o lama = barriera d'aria; a una lettera vanno i circuiti che descrive meglio delle altre). Le parole chiave si correggono nella tabella e restano salvate per il disegno; in arancione le lettere da verificare, per esempio quando la legenda e le sigle vicine ai punti non concordano. I layer si cambiano nella tavolozza. La lettura dei PDF usa [Poppler](https://poppler.freedesktop.org/) (GPL-2.0-or-later); senza Poppler CartesioCAD si compila lo stesso, senza questa funzione.
- **Guida** (`GUIDA`, F1, pulsante **? Guida** a destra delle schede, menu dell'applicazione), solo a richiesta:
  - **percorsi guidati** (il primo disegno; testi, quote e direttrici; layout e PDF; impianto; CanaloCAD; circuiti; confronto di revisioni): un passo alla volta, con il pulsante da usare evidenziato in arancione nella barra multifunzione; il passo si chiude da solo quando è fatto (oggetti disegnati, selezione, comando finito, scheda, tavolozza, file salvato...). Indietro, salta, esci in qualsiasi momento. I percorsi sono in `src/app/guide/percorsi.json`;
  - **manuale** di tutte le funzioni (`docs/manuale`, leggibile anche su GitHub), con indice e ricerca per comando, alias o argomento; durante un comando F1 apre la sua pagina. Un test controlla che ogni comando abbia la sua voce.
- **Impianto** (`IMPIANTO`, scheda Impianto > Impianti elettrici): un file di progetto (`.cdb`, accanto ai disegni) condiviso da tutte le piante dello stesso impianto; ogni disegno ricorda a quale progetto è collegato e lo riapre da solo. Contiene i **circuiti** (sigla, descrizione, sistema energia/sicurezza/segnale, tipo di cavo e formazione, corrente d'impiego, lunghezza, interruttore), importati dallo schema unifilare in PDF (formazioni come `1(3G2,5)`, `4(1x95)`, `3(1x185)+(1x95)+(1PE95)`, `3(3x1x240)+(2x240)+(1PE120)`; una nuova importazione aggiorna i circuiti e conserva i dati scritti a mano) oppure scritti a mano; i **cavi propri** (diametro, peso, sezione dei tipi che mancano nel catalogo); le **tratte**: tubi, canali e passerelle disegnati come linee o polilinee del percorso. *Nuova tratta dalla selezione* (`TRATTA`) chiede tipo, misure e circuiti; la lunghezza si misura dal disegno; *Mostra* la inquadra. **Verifiche** (`VERIFICAIMPIANTO`): tubi con diametro interno di almeno 1,3 volte il fascio di cavi (cerchio minimo che li contiene; se non basta indica il diametro giusto), canali con i cavi al massimo al 50% della sezione, passerelle con i calcoli di CanaloCAD (riempimento, staffaggio: carico e freccia); segregazione dei segnali non schermati dai circuiti di energia (CEI 64-8 art. 528.1) e avviso per i circuiti di sicurezza insieme agli ordinari. Per i circuiti: portata Ib ≤ In ≤ Iz' con le portate di tabella del metodo di posa della tratta (IEC 60364-5-52:2009, ripresa dalla CEI 64-8:2024 Cap. 52, / CEI-UNEL 35024/1; rame, PVC o EPR, 2 o 3 conduttori caricati: tubo a vista B1/B2, incassato A1/A2, canale B1/B2, passerella E o C per gli unipolari), il raggruppamento e la temperatura ambiente della tratta (la portata si può imporre a mano), e caduta di tensione con la lunghezza dello schema o delle tratte disegnate (raccomandato al massimo 4%). Esito colorato nella tavolozza e dettaglio nella riga di comando. *Etichette in pianta* (`ETICHETTETRATTE`) scrive accanto a ogni tratta tipo, misura, riempimento ed esito; *Disegna sezione* (`SEZIONETRATTA`) disegna la sezione della tratta con i cavi, il titolo con l'esito e la legenda dei circuiti. Lo schema letto nella tavolozza Circuiti o Impianto arriva anche all'altra. *Distinta...* (`DISTINTAIMPIANTO`) conta i materiali delle tratte disegnate (tubi con curve, manicotti e cassette; canali e passerelle con coperchi, setti, curve, derivazioni, giunzioni e staffe; metri di cavo per tipo e formazione) e li salva per Excel (CSV) o li mette in una tabella nel disegno. I diametri dei cavi vengono dal catalogo per tipo e formazione (con nota se presi da un tipo simile o stimati); quelli interni dei tubi sono indicativi della serie pesante in PVC, si scrive quello del costruttore nella tratta.
- **Tratte da layer** (`TRATTEDALAYER`): le linee dei layer delle canalizzazioni diventano tratte, una per ramo fra le derivazioni, con tipo e misura per stile di linea letti dalla legenda del disegno (o dalle scritte vicine ai rami); unità reali del disegno impostabili quando il file dichiara quelle sbagliate.
- **Percorsi automatici** (`PERCORSIIMPIANTO`, scheda Tratte della tavolozza Impianto): ogni circuito con la sigla in pianta viene assegnato da solo alle tratte del percorso più breve dal quadro (segnato con *Quadro dalla selezione*, `QUADROIMPIANTO`, o scritto come testo) agli utilizzatori, con la lunghezza dell'utilizzatore più lontano più un'aggiunta per risalite e scorte (usata per caduta di tensione e distinta). Le tratte si collegano agli estremi e alle derivazioni a T, non dove si incrociano soltanto; riepilogo prima di applicare.
- **Fronte quadro** (`FRONTEQUADRO`, `FQ`, scheda Impianto > Impianti elettrici): dallo schema unifilare in PDF il fronte di ogni quadro disegnato in scala 1:1 con quote, carpenteria, grado IP (proposto dall'ambiente) e forma di segregazione (proposta dalla corrente d'arrivo), più la distinta in CSV e un file IFC4 per il BIM (`IfcElectricDistributionBoard` con ingombro e proprietà). Gli apparecchi si riconoscono da esecuzione, poli, corrente, curva, SPD e differenziale scritti nello schema; gli ingombri sono valori tipici da confermare sul catalogo. Nei quadri a pavimento sbarre e cavi hanno colonne proprie: sbarre principali sulla corrente di arrivo e sbarre verticali (forme 2a e oltre) sulla corrente della colonna con il fattore di contemporaneità, scelte dalla tabella DIN 43671 ridotta per la temperatura interna, con configurazione e portata scritte nel disegno; colonne di risalita dimensionate sui cavi delle partenze (riempimento massimo 35%, larghezza minima per il raggio di curvatura). Si scelgono spazio di riserva, morsettiera, risalite, installazione; le carpenterie sono tipi generici oppure vengono da un proprio catalogo CSV con il riferimento commerciale. Un catalogo CSV degli apparecchi collega tipo, poli e corrente ai blocchi dei costruttori (viste frontali DWG/DXF scaricate dai loro siti, non incluse nel programma): il fronte si disegna con i blocchi veri e le loro misure, anche con le piastre di colonna (apparecchio su piastra, piastre modulari e cieche) impilate come nel quadro. Senza catalogo valgono i blocchi generici di CartesioCAD (uno per ogni tipo), così ogni simbolo dello schema ha il suo blocco. **Importa libreria** (`IMPORTALIBRERIA`) prepara una libreria di blocchi del costruttore già installata sul PC: disegni in mm senza loghi, sigle e codici nella cartella dell'utente, indice, registro di ciò che è stato tolto e cataloghi aggiornati per la sua famiglia; CartesioCAD non contiene né distribuisce nulla delle librerie. Negli script `-FRONTEQUADRO schema.pdf [QUADRI a,b] [IFC file] [CSV file] [CATALOGO file] [APPARECCHI file] [RISALITA 0..3]`.
- **Canalina** (`CANALINA`, scheda Impianto > CanaloCAD): le canaline portacavi con i calcoli e la tavola di [CanaloCAD](https://github.com/SamuDiPi/Canalo), l'app web per le canaline. Si disegna la canalina (la sezione L×H e/o il percorso in pianta), la si seleziona e *Usa selezione* la collega: le misure sono quelle disegnate e CanaloCAD ridisegna la sezione al suo posto, aggiornata a ogni cambio. Nella finestra Canalina (a sé, spostabile anche su un altro schermo) il **database dei cavi** è sempre visibile (solo tipologie, senza costruttori: categorie, ricerca, quantità con le fasi, terne a trifoglio, cavi propri in una libreria personale) e i cavi si possono anche importare dalla sezione selezionata o da uno o più DWG/DXF di progetto, letti interi come in CanaloCAD, scegliendo per ognuno la polilinea del disegno a cui si riferisce (la lunghezza del tratto è la sua), una canalina già presente o un particolare a sé stante. Selezionando un oggetto della canalina si vedono i suoi cavi e il **riepilogo** di CanaloCAD: riempimento, Kr, segregazione CEI 64-8 art. 528.1, peso, carico e freccia dello staffaggio, tiro, raggio di curvatura, portata e distinta. una canalina nata dal database ha la misura scelta dai cavi (riserva voluta), una disegnata troppo piccola viene segnalata; *Disegna sezione* mette nel disegno la sezione al vero; *Inserisci tavola* e *Salva tavola DXF* danno la tavola completa (sezione quotata, tabella cavi, distinta, vista 3D, curva di carico, cartiglio). Il codice di CanaloCAD (calcoli, importazione, scena, DXF) gira dentro CartesioCAD con il motore JavaScript di Qt (`src/app/CanaloEngine`, `plugins/CanaloCAD/engine`): la tavola è identica, byte per byte, a quella che esporta la pagina, e un test lo controlla su casi generati dalla pagina vera in Microsoft Edge (`scripts/canalo/gendxf.js`). Le canaline si salvano nel disegno e si annullano con Ctrl+Z. *Verifica sezione* (`CANALOVERIFICA`) porta nella finestra la sezione selezionata.
- **Plugin CanaloCAD** (scheda Impianto): su Windows *Apri CanaloCAD* (`CANALOCAD`) lo apre nella scheda **CanaloCAD** accanto ai disegni, con Microsoft Edge WebView2 (già presente in Windows 10 e 11; `WebView2Loader.dll` accanto al programma): *Invia sezione* vi apre direttamente gli oggetti selezionati e in CanaloCAD *Esporta > Inserisci nel disegno* riporta la tavola nel disegno di partenza, e *Copia come blocco* la mette negli appunti (con caratteri e stili di testo) da incollare con Ctrl+V in qualsiasi disegno aperto; *Copia come immagine* la mette negli appunti di Windows (300 dpi, fondo bianco) per Word e le relazioni. Gli stili di testo ora seguono gli oggetti anche nel copia/incolla tra disegni e negli inserimenti di file come blocco. Senza WebView2 (o su Linux) CanaloCAD si apre nel browser e lo scambio passa dai file: *Invia sezione* salva gli oggetti selezionati in un DXF in millimetri da importare in CanaloCAD (il percorso va negli appunti); *Inserisci tavola* e *Ultima tavola* inseriscono come blocco le tavole DXF esportate da CanaloCAD, scalate con le unità del disegno; la cartella dei download viene sorvegliata e una tavola nuova viene segnalata nella riga di comando. Comandi `CANALOCAD`, `CANALOINVIA`, `CANALOINS`, `CANALOULTIMA`. CanaloCAD (un solo file, `Canalo.html`) è incluso in `plugins/CanaloCAD` e viene copiato accanto all'eseguibile; `scripts/update-canalo.sh [cartella di CanaloCAD]` lo aggiorna alla build più recente insieme al motore, poi `scripts/canalo/regen.sh` (catalogo e calcoli delle tratte dell'Impianto, tradotti in C++ e confrontati numero per numero con la pagina) e `scripts/canalo/gendxf.js` (tavole di prova).
- **Modifica con le maniglie**: su una maniglia di un oggetto selezionato si trascina e si rilascia (oppure clic e clic sul punto d'arrivo) per stirarlo o spostarlo. Il testo multilinea ha anche le frecce della larghezza ai lati: trascinandole il testo si allarga o si restringe e va a capo di conseguenza.
- **Layout e spazio carta**: schede Modello/Layout in basso, foglio con area stampabile, finestre in scala (1:1 … 1:1000), doppio clic in una finestra per lavorare nel modello attraverso di essa (disegnare, modificare, zoom e pan; doppio clic fuori o `PSPACE` per tornare alla carta), pulsante MODELLO/CARTA nella barra di stato, scala e blocco della finestra nella barra di stato, finestre poligonali o ricavate da un oggetto chiuso, layer congelati nelle singole finestre (colonna «Congela nella finestra» del gestore layer), impostazione pagina (A4…A0).
- **Stampa** (`PLOT`, Ctrl+P) con un dialogo completo: impostazioni di pagina con nome, PDF o stampanti di Windows (proprietà, stampa su file, copie), formati ISO/ANSI/ARCH, quelli della stampante o personalizzati, area (Layout, Estensioni, Limiti, Schermo, Finestra scelta nel disegno), offset e centratura, scala adattata, standard o personalizzata (anche in pollici), scala degli spessori, tabelle stili `.ctb` (Colori, Nero, Grigi, Retino 25–100%, più quelle create o importate) con editor, qualità/DPI, spessori, trasparenza, spazio carta per ultimo, timbro di stampa, orientamento e stampa capovolta, anteprima parziale e completa (`PREVIEW`). `PAGESETUP` gestisce le impostazioni di pagina (nuova, modifica, imposta corrente, importa da un altro disegno, elimina); `PUBLISH` mette tutti i layout in un solo PDF. Le impostazioni si salvano nel DXF insieme al disegno; trasparenza e timbro restano nelle preferenze del programma, perché il DXF 2000 non li prevede. Non hanno senso in 2D, e sono disattivate: stampa ombreggiata, nascondi oggetti, stampa in background. Delle tabelle stili non si usano tipi di linea e motivi di riempimento.
- **Tipi di linea** (DASHED, CENTER, HIDDEN, DASHDOT, PHANTOM, DOT, DIVIDE, BORDER) e **spessori di linea** per layer e per oggetto, con `LTSCALE` e scala nelle finestre (`PSLTSCALE`); pulsante per mostrare gli spessori a schermo (LWDISPLAY).
- **Copia/incolla** fra disegni (Ctrl+C / Ctrl+V), con blocchi e layer.
- **Recupero dopo una chiusura anomala**: i salvataggi automatici si cancellano salvando e chiudendo il programma; se all'avvio ne restano, si propone di riaprirli come disegni da salvare.

Le icone sono disegnate appositamente: CartesioCAD non usa marchi, icone o altri materiali di terzi. Motivi di tratteggio, tipi di linea e tabelle di stampa predefinite sono definiti nel codice di CartesioCAD; non si distribuiscono font SHX.

**Progetto indipendente.** CartesioCAD non è collegato, approvato né sostenuto da altri produttori di software CAD. I nomi dei formati di file (DWG, DXF, PDF...) indicano solo la compatibilità in lettura e scrittura; i marchi citati appartengono ai rispettivi titolari. I file DWG si leggono e si scrivono con GNU LibreDWG.

## Comandi

| Comando | Alias | Descrizione |
|---|---|---|
| `LINE` | `L` | Linee consecutive (`C` chiude). |
| `PLINE` | `PL` | Polilinea con tratti rettilinei e ad arco (`A` arco, `L` linea, `C` chiude, `U` annulla), con spessore: `LA` larghezza e `M` mezza larghezza, iniziale e finale (frecce, tratti rastremati; si ricorda come PLINEWID). |
| `CIRCLE` | `C` | Cerchio per centro e raggio. |
| `ARC` | `A` | Arco per tre punti. |
| `RECTANG` | `REC` | Rettangolo per due angoli. |
| `ELLIPSE` | `EL` | Ellisse per asse e distanza (`C` per centro). |
| `POINT` | `PO` | Punti. |
| `HATCH` | `H` | Tratteggio con punto interno (contorno e isole trovati in automatico, anche fra linee che si incrociano) o selezionando oggetti chiusi; l'opzione *Motivo* apre la scelta con le anteprime dei 17 motivi, scala e angolo. |
| `SPLINE` | `SPL` | Curva che passa per i punti indicati (B-spline cubica con punti di controllo e nodi, come nei DXF); `Chiudi`, `Annulla`. |
| `BEDIT` | `BE`, `EDITBLOCCO` | Editor blocchi: il blocco selezionato (o scelto dall'elenco) si apre in una scheda e si modifica con i comandi normali; `BSAVE` (o Salva) aggiorna la definizione e tutti gli inserimenti, `BCLOSE` chiude. Anche con il doppio clic su un blocco senza attributi. |
| `BLOCKSPALETTE` | `BLOCCHI`, `LIBRERIA`, `SIMBOLI`, `ADC` | Tavolozza Blocchi (libreria simboli) con anteprime: blocchi del disegno, di un file libreria DWG/DXF o di una cartella (ogni disegno è un simbolo), recenti; clic per inserire o trascinamento sul disegno (inserito nel punto di rilascio), tasto destro per modificare (BEDIT) o aprire il file. I simboli di un file in altre unità si scalano (es. libreria in cm, disegno in mm); i blocchi tecnici (`_ArchTick`, `_Dot`...) non compaiono. |
| `DWGCONVERT` | `CONVERTI`, `CONVERSIONE` | Conversione in serie: tutti i DWG/DXF di una cartella (anche le sottocartelle, ricreate nella destinazione) in DXF 2000, DXF R12, DWG o PDF (i layout con qualcosa sopra, altrimenti il modello alle estensioni adattato al foglio). Verifica facoltativa (ogni file scritto si rilegge e si confronta) e *Rapporto conversione.txt* con l'esito di ogni file; gli originali non si toccano. Negli script: `-DWGCONVERT sorgente destinazione DXF\|DXF12\|DWG\|PDF [SUB] [SOVRASCRIVI]`. |
| `ESTRAIDATI` | `ESPORTADATI`, `DATICSV` | Dati del disegno in una tabella CSV per Excel: blocchi con i loro attributi (una colonna per attributo), distinta dei blocchi con le quantità, oppure tutti gli oggetti con layer, lunghezza, area e testo. Solo la selezione a scelta; separatore `;` con la virgola decimale o CSV standard. Negli script `-ESTRAIDATI file.csv [BLOCCHI\|CONTEGGIO\|OGGETTI] [SELEZIONE] [VIRGOLA] [SOLOMODELLO]`. |
| `FRONTEQUADRO` | `FRONTEQ`, `FQ` | Fronte dei quadri dallo schema unifilare in PDF: disegno con quote, carpenteria, IP e forma, distinta CSV e IFC per il BIM. |
| `IMPORTALIBRERIA` | `LIBRERIACOSTRUTTORE` | Blocchi per il fronte quadro da una libreria del costruttore installata sul PC (in mm, senza marchi, per uso personale) e cataloghi aggiornati. |
| `XCLIP` | `XC`, `RITAGLIO` | Ritaglio di blocchi e riferimenti esterni con un contorno rettangolare o poligonale; `ON`/`OFF`/`Elimina`. Il ritaglio si legge e si scrive nei DXF/DWG (SPATIAL_FILTER) e segue il blocco se si sposta o ruota. |
| `LAYERSTATE` | `LAS`, `STATILAYER` | Stati dei layer: salva con un nome accensione, congelamento, blocco, colore, tipo e spessore di linea e stampa di tutti i layer, e li ripristina (con Annulla). Salvati nel DXF 2000. |
| `HATCHEDIT` | `HE`, `MODTRATT` | Cambia motivo, scala e angolo di un tratteggio esistente (anche dalla tavolozza Proprietà). |
| `BOUNDARY` | `BO`, `BPOLY`, `CONTORNO` | Polilinee chiuse sul contorno di un'area (e sulle isole) indicando un punto interno. |
| `MTEXT` | `T` | Testo multilinea con a capo automatico: dopo i due angoli del riquadro si scrive direttamente nel disegno (Invio va a capo, Ctrl+Invio o un clic fuori confermano). Mentre si scrive compare la scheda **Editor di testo**: stile, altezza, annotativo, maschera di sfondo; grassetto, corsivo, sottolineato, sopralineato, barrato, maiuscole/minuscole, carattere, colore, cancella formattazione; giustificazione, elenchi puntati e numerati, spaziatura linea, allineamento dei paragrafi; simboli e campi (data, ora, file); colonne (da una a quattro, salvate nel DXF); trova e sostituisci; righello sopra il testo con la maniglia della larghezza. Il controllo ortografico (correttore di Windows, italiano se installato) sottolinea in rosso le parole sbagliate: tasto destro sulla parola per i suggerimenti, "Aggiungi al dizionario" e "Ignora"; si spegne dal pannello Ortografia. |
| `TEXT` | `DT` | Testo su una riga: dopo punto, altezza e rotazione si scrive direttamente nel disegno; Invio conferma (niente Invio a vuoto). Esc tiene quello che si è scritto; un testo lasciato vuoto non resta. Negli script il testo si dà nella riga di comando come prima. |
| `DIMLINEAR` | `DLI` | Quota orizzontale o verticale. |
| `DIMALIGNED` | `DAL` | Quota allineata. |
| `DIMRADIUS` / `DIMDIAMETER` | `DRA` / `DDI` | Quota di raggio / diametro. |
| `DIMTEDIT` | `DIMTED` | Sposta il testo di una quota (o `Sinistra`/`Destra`/`Centro`/`Origine`, `ANgolo` per ruotarlo). Anche trascinando la maniglia del testo. |
| `DIMEDIT` | `DED` | Su più quote: `Origine` (testo al suo posto), `Nuovo` (testo sostitutivo, `<>` = misura), `Ruota` (testo), `Obliqua` (linee di estensione oblique). |
| `STYLE` | `ST`, `STILE` | Stili di testo: nuovo, modifica (font SHX o TrueType, altezza, larghezza, obliquità, grassetto, corsivo), rinomina, elimina, corrente. |
| `MLEADERSTYLE` | `MLS` | Stili delle direttrici (altezza del testo, freccia, stile di testo, distanza dall'approdo): le nuove direttrici usano quello corrente, modificandolo si aggiornano quelle esistenti. Salvati nel DXF. |
| `CANNOSCALE` | | **Scale annotative**: scala di annotazione dello spazio modello (anche dalla barra di stato, accanto a MODELLO). Testi, testi multilinea, quote e direttrici creati con uno stile annotativo (casella *Annotativo* negli stili di testo, di quota e delle direttrici) hanno le misure sulla carta: un testo di 2,5 a 1:50 è alto 125 nel modello. Cambiando scala gli oggetti che la supportano si ridimensionano, gli altri restano come sono e si vedono sempre; nei layout ogni finestra li mostra alla sua scala, anche in stampa. `ANNOALLVISIBLE` è solo memorizzata. Proprietà *Annotativo* nella tavolozza. Si leggono gli oggetti e gli stili annotativi dei DWG/DXF. |
| `CENTERMARK` / `CENTERLINE` | `CM` / `CL` | Segno di centro sui cerchi e sugli archi; linea d'asse fra due linee (tipo di linea CENTER). |
| `VPORTS` | `FINESTREMODELLO` | Finestre del modello affiancate (scheda Schermo): una, due verticali o orizzontali, tre, quattro; ogni vista con il suo zoom, un clic la rende attiva (bordo azzurro). Unisci torna a una. |
| `-VIEW` | `-V` | Viste con nome: salva, ripristina, elimina (anche dalla scheda Schermo: Nuova vista, Gestione viste, elenco delle viste). Salvate nel DXF (tabella VIEW). |
| `OBJECTSCALE` | `SCALAOGGETTO` | Aggiunge o toglie scale di annotazione agli oggetti annotativi selezionati. |
| `SCALELISTEDIT` | | Lista delle scale del disegno: elenco, aggiungi (es. `1:75`), elimina (se non usata), reimposta. |
| `DIMSTYLE` | `D` | Gestione stili di quota (testo, frecce o trattini, distanze, scala, fattore, decimali, suffisso); `-DIMSTYLE` e `DIMSCALE`, `DIMTXT`, `DIMASZ`, `DIMDEC` negli script. |
| `MLEADER` / `LEADER` | `MLD` / `LE` | Direttrice con freccia e testo su più righe (salvata come LEADER + MTEXT). |
| `MLEADERADD` / `MLEADERREMOVE` | | Multidirettrici: aggiunge o toglie frecce allo stesso testo (le frecce in più si salvano come LEADER legati alla principale). Ogni maniglia sposta il suo punto. |
| `MLEADERALIGN` / `MLEADERCOLLECT` | `MLA` / `MLC` | Allinea i testi di più direttrici a quello di una di riferimento (in colonna o in riga); raccoglie più direttrici in una sola con tutte le frecce. |
| `COMPARE` | `CONFRONTADWG` | Confronto con un'altra revisione del disegno (scheda Strumenti): uguali in grigio, solo nel disegno corrente in azzurro, solo nell'altro in magenta, zone delle modifiche da percorrere con Precedente/Successiva. |
| `MOVE` | `M` | Sposta. |
| `COPY` | `CO` | Copia (copie multiple). |
| `OFFSET` | `O` | Copia parallela (linee, archi, cerchi, polilinee). |
| `TRIM` | `TR` | Taglio rapido alle intersezioni (anche polilinee). |
| `EXPLODE` | `X` | Scompone polilinee, blocchi, quote, testi multilinea. |
| `ERASE` | `E` | Cancella (anche tasto Canc). |
| `BLOCK` / `INSERT` | `B` / `I` | Crea / inserisce blocchi; INSERT chiede i valori degli attributi. |
| `ATTDEF` | `ATT` | Definizione di attributo (etichetta, richiesta, valore predefinito). |
| `ROTATE` / `SCALE` / `MIRROR` | `RO` / `SC` / `MI` | Ruota, scala, specchia (i testi restano leggibili). |
| `ARRAY` | `AR` | Serie rettangolare o polare. |
| `EXTEND` | `EX` | Estende linee e archi fino al primo oggetto. |
| `FILLET` | `F` | Raccordo fra due linee (`R` per il raggio; 0 = spigolo vivo). |
| `CHAMFER` | `CHA` | Cimatura fra due linee (`D` per le due distanze; 0 = spigolo vivo). |
| `STRETCH` | `S` | Stira con una finestra di intersezione: i punti dentro si spostano, gli oggetti interi si muovono. |
| `BREAK` / `BREAKATPOINT` | `BR` | Interrompe linee, archi, cerchi e polilinee fra due punti (`@` = nello stesso punto). |
| `JOIN` | `J` | Unisce linee allineate, archi dello stesso cerchio e tratti che si toccano in una polilinea. |
| `MATCHPROP` | `MA` | Copia layer e colore da un oggetto ad altri. |
| `DIST` / `AREA` / `ID` / `LIST` | `DI` / `AA` / `ID` / `LI` | Distanza, area e perimetro, coordinate, informazioni sugli oggetti. |
| `MVIEW` | `MV` | Finestra in un layout (`A` adatta al foglio, `P` poligonale, `O` da un oggetto chiuso). |
| `VPCLIP` | | Ritaglia una finestra su un poligono o un oggetto chiuso (`E` toglie il ritaglio). |
| `VPLAYER` | | Congela o scongela layer nelle finestre (`Tutte`, `Seleziona`, `Corrente`). |
| `CARTIGLIO` | `CART` | Formato del foglio (A4…A0), cornice e cartiglio con attributi (titolo, autore, data, scala, formato, società, numero, revisione). |
| `AGGCARTIGLIO` | | Aggiorna scala, data e formato nei cartigli. |
| `MSPACE` / `PSPACE` | `MS` / `PS` | Lavora nel modello attraverso la finestra / torna allo spazio carta. |
| `SCRIPT` | `SCR` | Esegue uno script `.scr`. |
| `SETVAR`, `-LAYER`, `COLOR` | | Variabili di sistema e layer da riga di comando (utili negli script); `-LAYER` ha anche `TIpolinea` e `SPessore` e accetta i caratteri jolly `*` e `?` nei nomi. |
| `STYLESMANAGER` | `STILISTAMPA`, `CTB` | Gestione tabelle stili di stampa (.ctb): nuova, modifica, copia, rinomina, elimina, importa da un file scelto, apri cartella. Anche da Opzioni > Stampa e pubblicazione e dall'elenco delle tabelle nel dialogo Stampa. |
| `PEDIT` | `PE`, `MODPL` | Modifica polilinea (linee e archi si convertono): Chiudi/Apri, Unisci, Larghezza, Inverti, Spline, Rettifica, Annulla. |
| `DIMANGULAR` | `DAN`, `QUOTANG` | Quota angolare: due linee (lato scelto con il clic), un arco, oppure Invio e tre punti. |
| `DIMORDINATE` | `DOR`, `QUOTACOORD` | Quota a coordinata X o Y (dalla direzione della direttrice, o Xdato/Ydato). |
| `DIMCONTINUE`, `DIMBASELINE` | `DCO`, `DBA` | Quote in serie dall'ultima quota lineare (o da una scelta con Seleziona): continue sulla stessa linea, o dalla stessa base a distanza 3,75. |
| `QDIM` | `QUOTARAPIDA` | Quotatura rapida degli estremi degli oggetti selezionati, continua o da linea di base, orizzontale o verticale secondo la posizione. |
| `DRAWORDER` | `DR`, `ORDINE` | Ordine di visualizzazione degli oggetti selezionati: in primo piano, dietro, sopra o sotto un oggetto (anche dal menu del tasto destro). L'ordine salvato nei file (SORTENTSTABLE) si legge, anche dentro i blocchi; nel file si salva come ordine degli oggetti. |
| `TEXTTOFRONT`, `HATCHTOBACK` | | Testi, quote e direttrici in primo piano; tratteggi dietro. |
| `EATTEDIT` | `ATTEDIT`, `ATE` | Valori degli attributi di un blocco (cartigli): anche con doppio clic sul blocco o dal menu del tasto destro. |
| `ATTSYNC` | | Gli inserimenti di un blocco seguono le definizioni di attributo (posizione, stile, ordine); i valori restano. |
| `BATTMAN` | `GESTATT` | Definizioni di attributo di un blocco: etichetta, richiesta, valore predefinito, altezza, invisibile, ordine; sincronizzazione. |
| `QSELECT` | `SELRAPIDA` | Selezione rapida per tipo e proprietà: colore, layer, tipo e spessore di linea, nome del blocco (anche dei blocchi dinamici), contenuto e altezza dei testi, valore di un attributo; =, <>, >, < e corrispondenza con * e ?; includi o escludi, aggiungi alla selezione. |
| `FIND` | `TROVA` | Trova e sostituisci in testi, testi multilinea, attributi, quote e direttrici (maiuscole, parole intere, dove cercare); zoom sul risultato; sostituzioni annullabili. |
| `XRIF` | `XREF`, `ER`, `RIFERIMENTI` | Riferimenti esterni: disegni collegati e immagini, con stato e percorso; cambia percorso, ricarica, apri, stacca, associa (BIND: il riferimento diventa un blocco del disegno, layer `NOME$0$x` o uniti a quelli del disegno). Negli script `-XRIF` scrive l'elenco nella riga di comando. |
| `IMAGEATTACH` | `IAT`, `IMMAGINE` | Collega un'immagine raster: punto di inserimento e larghezza (negli script `IMAGEATTACH file.png`). |
| `XATTACH` | `XA`, `RIFX` | Collega un DWG/DXF come riferimento esterno e lo posiziona come un blocco (negli script: `XATTACH file.dwg` seguito da punto, scala e rotazione). |
| `-LINETYPE` | `-LT` | Carica i tipi di linea standard (`C`) e imposta quello corrente (`I`). |
| `UNDO` / `REDO` | `U` / Ctrl+Y | Annulla / ripristina. |
| `ZOOM` | `Z` | Finestra (due angoli), `E` estensioni, `P` vista precedente. |
| `WIPEOUT` | | Copertura: area che nasconde gli oggetti sotto (punti o polilinea chiusa); `Cornici` = WIPEOUTFRAME. |
| `MLINE` | `ML` | Multilinea (stile STANDARD) con giustificazione Superiore/Zero/Inferiore e scala. |
| `TABLE` / `TABLEDIT` | `TB` | Tabella con righe e colonne; doppio clic su una cella per scriverci (Tab passa alla cella successiva). |
| `GROUP` / `UNGROUP` | `G` | Gruppi con nome o senza: selezionando un oggetto si seleziona il gruppo (`PICKSTYLE`). |
| `POLYGON` | `POL` | Poligono regolare inscritto, circoscritto o dato un lato. |
| `REVCLOUD` | | Nuvola di revisione rettangolare o da un oggetto chiuso (lunghezza dell'arco a scelta). |
| `XLINE` / `RAY` | `XL` | Linee di costruzione infinite e semirette (Orizz/Vert/Ang); non contano nelle estensioni. |
| `DIVIDE` / `MEASURE` | `DIV` / `ME` | Punti a intervalli uguali o a distanza fissa lungo linee, archi, cerchi, polilinee, ellissi, spline. |
| `LENGTHEN` | `LEN` | Allunga o accorcia linee e archi (DElta, Percentuale, Totale). |
| `ALIGN` | `AL` | Allinea con una o due coppie di punti, anche scalando. |
| `OVERKILL` | | Elimina oggetti doppi e linee contenute in altre. |
| `PURGE` | `PU` | Elimina blocchi, layer, tipi di linea e stili di quota inutilizzati. |
| `AUDIT` | `CONTROLLA` | Controlla il disegno (geometrie non valide, layer, tipi di linea e stili inesistenti, blocchi mancanti, gruppi, identificativi doppi) e, con `Sì`, corregge (si annulla con `U`). |
| `RECOVER` | `RECUPERA` | Apre un file danneggiato e lo corregge con AUDIT. |
| `AGGIORNAMENTI` | `AGGIORNA` | Cerca su GitHub una versione più recente e la installa sul posto, solo se il pacchetto è firmato dall'autore (Ed25519) e ha l'impronta SHA-256 del manifesto; conferma esplicita per le versioni major; la versione attuale resta in `versione-precedente`. Controllo all'avvio facoltativo nelle Opzioni, spento di default. |
| `ASSISTENTE` | | Assistente IA, connettore per Claude Desktop (l'app di Anthropic, Windows) e Claude Code (Windows e Linux): Claude legge il disegno aperto (di default la selezione) e propone modifiche che si applicano solo con OK; il salvataggio crea una nuova versione (`-v2`, `-v3`...) senza toccare l'originale. Con gli strumenti `canalo_*` e `impianto_tratte` usa anche CanaloCAD e l'Impianto: cerca i cavi nel database, calcola canaline (anche dai circuiti di una tratta), legge quelle del disegno e le tratte, ne propone di nuove (con OK, anche collegate al percorso di una tratta) e lavora con la scheda CanaloCAD. Spento di default; canale solo locale. |
| `VERSIONEPRECEDENTE` | | Torna alla versione installata prima dell'ultimo aggiornamento. |
| `ETRANSMIT` | `ARCHIVIA` | Archivio ZIP da mandare a terzi: il disegno (DXF 2000) con riferimenti esterni e immagini in sottocartelle (percorsi riscritti), tabelle stili e un rapporto di quello che manca; i font non si includono, il rapporto li elenca. |
| `SELECTSIMILAR` | | Seleziona gli oggetti dello stesso tipo e layer. |
| `PAN` | `P` | Manina: trascinare con il tasto sinistro (anche dal pulsante nella barra di navigazione); Esc, Invio o tasto destro per uscire. |
| `LAYOFF` / `LAYFRZ` / `LAYLCK` / `LAYULK` | | Spegne, congela, blocca, sblocca il layer degli oggetti indicati. |
| `LAYISO` / `LAYUNISO` | | Isola i layer degli oggetti selezionati / ripristina. |
| `LAYTHW` / `LAYMATCH` | | Scongela tutti i layer / porta gli oggetti sul layer di un altro oggetto. |
| `INSERTFILE` | | Inserisce un altro DWG/DXF come blocco (anche dal menu del pulsante Inserisci). |
| `LAYER`, `PROPERTIES`, `NEW`, `OPEN`, `QSAVE`, `SAVEAS`, `CLOSE` | `LA`, `PR` | Comandi dell'interfaccia. |

Coordinate: `10,20` assolute, `@5,0` relative, `@50<30` polari, oppure un numero come distanza nella direzione del cursore.
Invio o spazio ripetono l'ultimo comando, Esc annulla. Il tasto destro apre il menu di scelta rapida (con o senza oggetti selezionati); durante un comando con opzioni mostra Invio, Annulla e le opzioni, altrimenti vale Invio.

## File

- **DXF**: apertura di DXF dalla versione R12 alla 2018 e successive (linee, archi, cerchi, polilinee, ellissi, spline, testi, testi multilinea, quote, tratteggi, solidi, blocchi con attributi, layer). Salvataggio in:
  - **DXF 2000**: conserva tutti gli oggetti (tratteggi, ellissi, spline, testi multilinea);
  - **DXF R12**: il formato più compatibile con i programmi vecchi; ellissi e spline vengono approssimate, i testi multilinea diventano testi, i tratteggi non vengono salvati.
- **DWG**: apertura di DWG dalla versione R14 alla 2018 e successive e salvataggio in DWG 2000 tramite [LibreDWG](https://www.gnu.org/software/libredwg/) (GNU, GPL-3.0), passando da un DXF 2000. Provato con i DWG di esempio di LibreDWG (R14, 2000, 2004, 2007, 2013, 2018): tutti gli oggetti supportati vengono letti e salvati. La scrittura DWG di LibreDWG è ancora **sperimentale**: CartesioCAD propone sempre di salvare una copia invece di sovrascrivere un DWG originale.
  Se è installato l'[ODA File Converter](https://www.opendesign.com/guestfiles/oda_file_converter) (gratuito, non open source), CartesioCAD lo usa al posto di LibreDWG (scelta nelle Opzioni, scheda Apri e salva): legge meglio i DWG recenti, compresi layout che LibreDWG a volte restituisce vuoti, e salva DWG fino alla versione 2018. Viene cercato in `Programmi\ODA`, oppure indicato nelle Opzioni o con la variabile `CARTESIO_ODA_PATH`.
- **Layout**: letti e salvati (anche più layout, con le loro finestre) in DXF 2000 e DWG. Il DXF R12 salva solo lo spazio modello.
- **Blocchi con attributi** (ATTDEF/ATTRIB) letti e salvati.
- Verifiche: i DXF 2000 di CartesioCAD passano l'auditor di [ezdxf](https://ezdxf.mozman.at/) senza errori. Su disegni di progetto veri (da 300 a 49.000 oggetti) il salvataggio **DXF 2000 e la rilettura non perdono niente** (oggetti per tipo, spazio e layer, testi e attributi, layer, blocchi, layout).
- **Salvataggio DWG con LibreDWG: limiti misurati.** Va bene sui disegni piccoli (4.885 oggetti in 9 s, identico), ma:
  - sui disegni grandi la conversione è lentissima (33.000 oggetti: 23 minuti) e con loghi OLE pesanti richiede circa un minuto anche su disegni piccoli; una finestra di avanzamento con *Annulla* lascia l'interfaccia viva;
  - non conserva il flag "non stampabile" dei layer;
  - in qualche caso perde un blocco annidato (visto: il cartiglio con molti attributi dentro un riferimento esterno associato);
  - nelle etichette degli attributi ammette solo lettere, cifre, `-`, `_` e `$`: gli altri caratteri (es. `NOME_AUT.`) diventano `_`, con un avviso (senza, il blocco con l'attributo resterebbe vuoto).
  Dopo ogni salvataggio DWG il file viene **riletto e confrontato** con il disegno: se manca qualcosa compare un avviso con l'elenco. Per un DWG completo conviene l'ODA File Converter (sotto); in alternativa il DXF 2000, che i CAD più diffusi aprono direttamente.
- Direttrici multiple (MULTILEADER) lette come direttrici; XLINE e RAY letti e salvati.
- WIPEOUT, MLINE e gruppi letti e salvati. Le tabelle vengono salvate come blocco (gli altri programmi le mostrano uguali ma non come tabelle modificabili) con i dati per riaprirle in CartesioCAD; le tabelle create da altri programmi si leggono come blocco.
- Tipi di linea e spessori letti e salvati (DXF 2000, R12 e DWG; R12 non ha spessori).
- **Immagini raster** (IMAGE/IMAGEDEF: PNG, JPG, BMP, TIF...) mostrate, con ritaglio, luminosità/contrasto/dissolvenza letti e salvati in DXF 2000. Se il file non è al percorso salvato (es. un'unità di rete) si cerca nella cartella del disegno, nelle sue sottocartelle e nelle cartelle indicate nelle Opzioni; nelle Proprietà si vede dove è stato trovato. Un'immagine mancante appare come cornice con le diagonali.
- **Riferimenti esterni** (XREF): all'apertura ogni DWG/DXF collegato viene caricato (stessa ricerca delle immagini, anche annidati; le sovrapposizioni dei disegni riferiti no), con i layer e i blocchi chiamati `NOME|layer`; al salvataggio resta solo il percorso. `XATTACH` (`XA`, `RIFX`, menu Inserisci) collega un nuovo riferimento.
- **Oggetti OLE** (OLE2FRAME: loghi e immagini incollati nel disegno da Paint, Word, Excel...) mostrati come immagini: si usa l'immagine nativa (BMP, PNG, JPG...) o, se manca, la rappresentazione salvata (DIB, WMF, EMF); al salvataggio in DXF 2000 l'oggetto si riscrive uguale.
- **Riferimenti esterni** (`XRIF`, menu Inserisci): elenco di disegni collegati e immagini con stato, percorso salvato e file trovato; cambia percorso, ricarica, apri, stacca; collega un disegno (`XATTACH`) o un'immagine (`IMAGEATTACH`). All'apertura le immagini non trovate si segnalano nella riga di comando.
- Tratteggi senza contorno (vuoti in qualsiasi programma) si segnalano all'apertura invece di risultare "non supportati".
- Mouse: muovere il cursore ridisegna solo mirino, snap ed evidenziazione (non tutta la scena sulla GPU); vicino ai blocchi grandi si esaminano solo gli oggetti vicini; i clic rapidi non si perdono; oltre 100 oggetti selezionati le maniglie non si mostrano (`GRIPOBJLIMIT`).
- **Interfaccia tutta in QML**: una sola scena (`MainView.qml`) con barra multifunzione, schede dei file, tavolozze ai lati (ridimensionabili), disegno su GPU, riga di comando sovrapposta e barra di stato: un solo fotogramma invece di tanti componenti separati. Nelle Opzioni > Visualizza > Interfaccia (o con la variabile d'ambiente `CARTESIO_UI=widgets`) si torna alla finestra a widget con i dock. `DIAGGPU` scrive scheda grafica, driver, sistema grafico e scala dello schermo.
- Testi: disegnati fino a 1 pixel di altezza (prima, sotto i 3 pixel diventavano un trattino e zoomando indietro sembravano sparire).
- **Percorsi dei file collegati** (XREF e immagini): relativi al disegno, completi o solo il nome, scelti quando si collega il file (predefinito nelle Opzioni > Apri e salva) e modificabili in `XRIF`; salvando il disegno in un'altra cartella i percorsi relativi si aggiornano.
- `TESTINASCOSTI`: per i testi nella vista dice perché non si vedono (troppo piccoli, layer spento o congelato, colore uguale allo sfondo, font non trovato...). Sullo schermo un colore uguale allo sfondo (es. RGB nero nel modello) si mostra come il colore 7; in stampa restano i colori veri.
- **Puntamento snap a oggetto (OTRACK, F11)**: fermando il cursore su un punto di snap per mezzo secondo lo si aggancia (fino a 7; di nuovo per sganciarlo); dai punti agganciati partono allineamenti orizzontali, verticali e polari, con l'incrocio di due allineamenti.
- **Blocchi dinamici**: lo stato salvato nel file si disegna così com'è e il blocco mostra il nome del blocco dinamico (Proprietà, QSELECT); il collegamento si conserva al salvataggio. Cambiare stato (visibilità, ribaltamento, stiramento) non è possibile: la conversione DWG di LibreDWG non conserva i parametri dei blocchi dinamici.
- Testi TrueType sulla GPU disegnati come curve (nitidi e stabili a ogni zoom); nelle Opzioni > Visualizza si può tornare ai distance field.
- Non ancora supportati: oggetti 3D.

## Script

Gli script `.scr` usano il formato comune ai programmi CAD: il testo viene passato alla riga di comando, spazi e a capo valgono come Invio, i testi (`TEXT`, valori degli attributi) arrivano fino a fine riga, le righe che iniziano con `;` sono commenti. Si avviano con `SCRIPT`, trascinando il file sulla finestra o passandolo come argomento (`CartesioCAD.exe disegno.scr`). Esempio:

```
-LAYER N Muri C 1 Muri S Muri

RECTANG 0,0 10000,6000
CIRCLE 5000,3000 1500
TEXT 500,6500 250 0
Pianta piano terra

TILEMODE 0
MVIEW A
SAVEAS 2000 pianta.dwg
EXPORTPDF pianta.pdf
```

Per stampare con tutte le opzioni c'è `-PLOT`, seguito su una sola riga da opzioni `chiave=valore`
(i nomi dei file sono relativi alla cartella dello script):

```
-PLOT AREA=FINESTRA FINESTRA=0,0,5000,3000 SCALA=1:50 FOGLIO=A3 CENTRA=SI STILI=Nero.ctb FILE=dettaglio.pdf
-PLOT SPAZIO=Layout1 FILE=tavola.pdf
```

Opzioni: `SPAZIO` (MODELLO o nome del layout), `IMPOSTAZIONE` (impostazione di pagina con nome),
`AREA` (SCHERMO, ESTENSIONI, LIMITI, FINESTRA, LAYOUT), `FINESTRA=x1,y1,x2,y2`, `FOGLIO` (A3, ANSI B...),
`ORIENTAMENTO` (O/V), `CAPOVOLTA`, `SCALA` (ADATTA, 1:100, 2:1), `CENTRA`, `OFFSET=x,y`, `MARGINI`,
`STILI` (nome o percorso di un `.ctb`, oppure NESSUNO), `SPESSORI`, `SCALASPESSORI`, `CARTAULTIMA`,
`TRASPARENZA`, `TIMBRO`, `DPI`, `COPIE`, `STAMPANTE`, `FILE`, `SALVA` (salva le impostazioni nel disegno).

Nei file si possono usare `OPEN`, `NEW`, `QSAVE`, `SAVEAS`, `CLOSE`, `EXPORTPDF`, `SCRIPT`, `CANALOINVIA`, `CANALOINS`, `-CANALOVERIFICA` (senza finestra) e `LOGFILEON` (copia dei messaggi in un file) seguiti dal nome del file, e le variabili `CLAYER`, `CECOLOR`, `TEXTSIZE`, `OSMODE`, `ORTHOMODE`, `GRIDMODE`, `SNAPMODE`, `DYNMODE`, `TILEMODE`, `ATTREQ`, `FILEDIA`, `CMDECHO`, `LTSCALE`, `INSUNITS`, `CELTYPE`, `CELWEIGHT`, `LWDISPLAY`, `CVPORT`.

### Riga di comando del sistema

Senza aprire la finestra, da un terminale o da un file `.bat` (su Windows con `cartesio-cli.exe`, nella cartella del programma; su Linux con `flatpak run io.github.samudipi.CartesioCAD`):

```
cartesio-cli --converti pianta.dwg pianta.pdf
cartesio-cli --converti pianta.dwg pianta.dxf --verifica
cartesio-cli --converti C:\Progetti\Tavole C:\Progetti\PDF --formato pdf --sottocartelle
cartesio-cli --csv pianta.dwg prese.csv --modo conteggio
```

Opzioni: `--r12`, `--verifica` (rilegge e confronta), `--sovrascrivi`, `--formato dxf|dxf12|dwg|pdf` e `--sottocartelle` per le cartelle, `--modo blocchi|conteggio|oggetti`, `--solo-modello` e `--separatore ,` per il CSV, `--versione`, `--aiuto`. L'originale non viene mai sovrascritto. Codice di uscita: 0 riuscito, 1 errore, 2 argomenti non validi, 3 convertito con differenze alla verifica.

## Pubblicare una versione

1. Aggiornare `VERSION` in `CMakeLists.txt`, fare il commit, compilare e `cmake --install build --prefix dist/CartesioCAD`.
2. Creare `CartesioCAD-X.Y.Z-win64.zip` con la cartella `CartesioCAD` dentro, e l'archivio dei sorgenti di terze parti (`scripts/collect-licenses.sh dist/CartesioCAD --sorgenti <cartella>`).
3. Firmare: `scripts/sign-release.sh CartesioCAD-X.Y.Z-win64.zip <chiave privata .pem> [--major]`. Scrive `CartesioCAD-X.Y.Z-aggiornamento.json` e la sua firma `.json.sig`.
4. Installatore: `scripts/build-installer.sh dist/CartesioCAD <cartella di uscita>` (Inno Setup 7) crea `CartesioCAD-X.Y.Z-installazione.exe`: installazione in Programmi con i diritti di amministratore, collegamento nel menu Start, nessuna associazione di file; la disinstallazione toglie la cartella del programma e il connettore dell'Assistente IA da Claude Desktop e chiede se cancellare le impostazioni.
5. Pubblicare la versione su GitHub (tag `vX.Y.Z`) allegando i cinque file: installatore, archivio, sorgenti di terze parti, manifesto e firma. Senza manifesto e firma la versione non si installa dal programma.
6. Linux: il flusso `.github/workflows/flatpak.yml` costruisce `CartesioCAD-X.Y.Z.flatpak` a ogni tag `v*` (e a ogni push sul ramo `flatpak`), esegue i test e una prova senza schermo; il file esce come artefatto dell'esecuzione e si allega alla versione, insieme all'archivio dei sorgenti di Poppler usato dal pacchetto se non è già fra i sorgenti di terze parti.

La chiave privata non va mai nel repository (`.gitignore` esclude `*.pem`): senza di lei non si possono pubblicare aggiornamenti installabili dal programma, quindi va conservata con una copia di sicurezza. Se andasse persa o fosse compromessa, serve una versione con una chiave pubblica nuova, da scaricare a mano.

## Compilazione (Windows)

1. Installa [MSYS2](https://www.msys2.org/) (oppure `winget install MSYS2.MSYS2`) e, nel terminale **MSYS2 UCRT64**:
   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                      mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base \
                      mingw-w64-ucrt-x86_64-qt6-declarative \
                      mingw-w64-ucrt-x86_64-poppler   # facoltativo: schemi unifilari in PDF
   ```
2. Per il supporto DWG compila i convertitori di LibreDWG dai sorgenti ufficiali (impronta SHA-256 verificata) con le
   correzioni di sicurezza di `patches/libredwg/`; escono eseguibili autonomi, senza DLL (qualche minuto):
   ```sh
   scripts/build-libredwg.sh
   ```
   In alternativa `scripts/fetch-libredwg.sh` scarica i binari ufficiali, senza quelle correzioni.
3. Compila e prova:
   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

Dopo ogni compilazione le DLL di Qt e LibreDWG vengono copiate accanto a `build/CartesioCAD.exe`,
che si avvia con un doppio clic. Per una cartella da copiare su un altro PC:

```sh
cmake --install build --prefix dist/CartesioCAD
```

Il disegno usa la scheda video tramite Qt Quick (Direct3D 11 su Windows). Senza una GPU utilizzabile
CartesioCAD torna da solo al disegno con QPainter; lo si può forzare con la variabile
`CARTESIO_RENDER=software`.

Su Linux: `sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-private-dev qt6-declarative-dev`,
poi gli stessi comandi
(per il DWG servono `dwg2dxf` e `dxf2dwg` di LibreDWG, indicati con la variabile `CARTESIO_LIBREDWG_DIR`).

## Linux (Flatpak)

Dalla pagina Releases si scarica `CartesioCAD-X.Y.Z.flatpak` e si installa, con Flatpak e il deposito Flathub configurati:

```
flatpak install --user CartesioCAD-X.Y.Z.flatpak
flatpak run io.github.samudipi.CartesioCAD
```

Alla prima installazione Flatpak scarica da Flathub il runtime `org.kde.Platform` 6.11 (Qt). Il pacchetto contiene CartesioCAD, Poppler e i convertitori di GNU LibreDWG, compilati dai sorgenti indicati in `packaging/flatpak/io.github.samudipi.CartesioCAD.yml`. Vede le cartelle dell'utente e i dischi montati (i disegni e i loro riferimenti esterni stanno ovunque) e le stampanti; non ha accesso alla rete.

Differenze dalla versione per Windows: non c'è l'aggiornamento dal programma (la versione nuova si installa dal file nuovo), l'Assistente IA funziona con Claude Code (non c'è Claude Desktop per Linux), e mancano le parti legate a Windows (correttore ortografico di sistema, anteprima degli oggetti OLE). Si disinstalla con `flatpak uninstall io.github.samudipi.CartesioCAD`.

Per costruirlo: `flatpak-builder --user --install-deps-from=flathub --repo=repo --force-clean build packaging/flatpak/io.github.samudipi.CartesioCAD.yml`.

## Architettura

```
src/
├── core/                 Nucleo in C++ puro, senza Qt (testato con ctest)
│   ├── Prim.*            Segmenti/archi: distanze, intersezioni, trasformazioni
│   ├── Entity.*          Oggetti del disegno (linee, polilinee, testi, blocchi, quote...)
│   ├── Geometry.*        Intersezioni, offset, taglio, archi per tre punti
│   ├── Document.*        Oggetti, layer, blocchi, annulla/ripristina
│   ├── InputParser.*     Sintassi della riga di comando
│   ├── Plot.*            Impostazioni di stampa, scale, formati, tabelle stili .ctb, timbro
│   ├── commands/         Comandi (disegno, modifica, annotazione, blocchi) e selezione
│   └── io/               Lettura e scrittura DXF
└── app/                  Interfaccia Qt
    ├── MainWindow.*      Finestra principale: azioni, comandi, file, tavolozze
    ├── DrawingWidget.*   Un disegno aperto
    ├── CadView.*         Vista: input, snap, input dinamico, pulsanti di navigazione; disegno QPainter (stampa, senza GPU)
    ├── TextRender.*      Stili di testo: font SHX e TrueType, impaginazione dei testi
    ├── TextEditInPlace.*  Modifica dei testi nel disegno (doppio clic, TEXTEDIT)
    ├── GpuView.*         Disegno su GPU con Qt Quick (scene graph, Direct3D/Vulkan/OpenGL tramite RHI)
    ├── CommandLine.*     Riga di comando mobile (QML)
    ├── QmlUi.*           Ponte verso il QML: azioni, elenchi a discesa, schede, barra di stato, icone
    ├── qml/              Interfaccia in QML: barra multifunzione, schede, barra di stato, riga di comando, tavolozze
    ├── LayerManager.*    Gestore proprietà layer (QML)
    ├── PropertiesPalette.*  Tavolozza Proprietà (QML)
    ├── CircuitsPanel.*   Tavolozza Circuiti: schema unifilare, verifica, legenda (QML)
    ├── DwgConverter.*    DWG tramite LibreDWG
    ├── Plot.cpp, PlotEngine.h  Stampa su PDF, stampanti e anteprima
    ├── PlotDialog.*      Dialoghi Stampa e Imposta pagina (QML), gestione impostazioni di pagina
    ├── PlotTools.*       Anteprima di stampa, editor delle tabelle .ctb e timbro di stampa (QML)
    ├── UiControls.*      Controlli per i dialoghi QML con l'interfaccia dei widget Qt (spunte, elenchi, numeri...)
    ├── FormDialog.*      Dialoghi in QML descritti dal C++: Opzioni, Seleziona colore, Stili di quota, Gestione impostazioni di pagina
    ├── ImageLibrary.*    Immagini raster: ricerca dei file e cache
    ├── OleImage.*        Immagine degli oggetti OLE incorporati (file compound OLE, BMP, WMF/EMF)
    ├── ExternalRefs.*, MainWindowXref.cpp  Riferimenti esterni (XRIF) e IMAGEATTACH
    ├── PlotStyleManager.cpp  Gestione tabelle stili di stampa (.ctb)
    ├── MainView.*, CanvasItem.*  Finestra principale tutta in QML e disegno nella scena (CadCanvas)
    └── Ribbon.* (QML), Icons.*, Theme.*, StartPage.* (QML), ColorDialog.*
```

## Roadmap

Fatto (dalle prime versioni a oggi): disegno e modifica 2D completi, testi, quote, direttrici, tabelle, stili di testo e di quota, blocchi con attributi, layout e stampa, tipi e spessori di linea, riferimenti esterni e immagini, DWG/DXF, conversione in serie e da riga di comando, dati in CSV, confronto di revisioni, Impianto e CanaloCAD, fronte quadro con IFC, Assistente IA, aggiornamenti firmati, italiano e inglese, pacchetto Flatpak per Linux.

Prossimi passi:
- [ ] Qt 6.12.1 e OpenSSL 4.1.1 (correzioni di sicurezza) appena disponibili in MSYS2
- [ ] Traduzione inglese completa (richieste dei comandi, manuale, CanaloCAD)
- [ ] Salvataggio DWG più veloce e senza perdite (nuova versione stabile di LibreDWG)
- [ ] Importazione SVG come blocco
- [ ] Cambio di stato dei blocchi dinamici
- [ ] Pacchetti winget (Windows) e AppImage (Linux)
- [ ] Portate dei cavi confrontate con il testo della norma
- [ ] Oggetti 3D (solo lettura)

## Privacy e sicurezza

- **Nessun dato raccolto.** CartesioCAD lavora solo sul computer: niente account, statistiche o telemetria. Disegni, schemi e progetti non vengono inviati a nessuno.
- **Collegamenti a internet solo su richiesta.** Il programma si collega a GitHub solo quando si cercano gli aggiornamenti (menu dell'applicazione > *Controlla aggiornamenti*, comando `AGGIORNAMENTI`) o se si accende nelle Opzioni il controllo all'avvio (spento di default; al massimo una volta al giorno). GitHub vede l'indirizzo IP del computer; non viene mandato nient'altro. Apre un sito nel browser solo se lo si chiede (per esempio *Sorgenti* nella finestra Informazioni).
- **Connettore per Claude Desktop e Claude Code (spento di default).** Con l'Assistente IA (comando `ASSISTENTE`) si può collegare il disegno aperto all'app Claude Desktop o a Claude Code, dove l'utente accede con il proprio account: CartesioCAD apre solo un canale locale (named pipe dell'utente di Windows, o file nella cartella di sessione dell'utente di Linux; nessuna porta di rete) usato dal connettore `cartesio-mcp`. Quello che Claude legge (di default gli oggetti selezionati) passa a Claude Desktop o Claude Code e ad Anthropic secondo le condizioni dell'account dell'utente; le modifiche si applicano solo con OK nel programma e il salvataggio crea sempre una nuova versione accanto al file originale. Dettagli nel manuale (Guida > Manuale > Assistente IA).
- **Scheda CanaloCAD.** La pagina di CanaloCAD gira in Microsoft Edge WebView2 (Windows) o in Qt WebEngine (versione Linux in Flatpak) senza collegarsi a siti: non apre indirizzi web (i collegamenti vanno al browser), il controllo SmartScreen degli indirizzi e il salvataggio di moduli e password sono spenti. Il progetto della scheda si salva nel disegno da cui la si apre, con i dati del cartiglio di CanaloCAD (committente, progettista, codici): chi riceve il disegno li vede aprendo la scheda; preferenze e progetti aperti senza disegno restano nella cartella dei dati locali di CartesioCAD (`%LOCALAPPDATA%\CartesioCAD\CartesioCAD\CanaloCAD-pagina`). Il motore WebView2 è un componente di Windows: i suoi aggiornamenti e i dati di diagnostica seguono le impostazioni di Windows.
- **Cosa resta sul computer:** le opzioni e l'elenco dei file recenti (nel registro di Windows, chiave `HKEY_CURRENT_USER\Software\CartesioCAD`), le copie del salvataggio automatico e i file che si salvano. I dati nei disegni (per esempio i nomi nei cartigli) li tratta chi usa il programma.
- **File da fonti affidabili.** DWG, DXF, PDF e script `.scr` vanno aperti solo se vengono da fonti di cui ci si fida. I DWG si leggono con GNU LibreDWG 0.14.8413 in un programma separato (`libredwg/dwg2dxf.exe`): è l'istantanea successiva alla 0.14 stabile che corregge le vulnerabilità note della 0.14 (CVE-2026-15520, CVE-2026-62250), e contiene anche la correzione di read_2004_compressed_section (CVE-2026-9530, stesso punto di CVE-2026-9500); è compilata con in più la correzione di CVE-2026-111167, non ancora pubblicata da LibreDWG; LibreDWG resta il componente più esposto ai file costruiti apposta. Il plugin CanaloCAD legge i PDF con pdf.js 6.5.438 e i DWG con LibreDWG-web 0.9.3, dentro la pagina (WebView2 o browser).
- **Segnalare un problema:** menu dell'applicazione > *Segnala un problema* (comando `SEGNALA`). Si scrive in due righe cosa è successo; il programma apre nel browser la pagina "nuova segnalazione" di GitHub già compilata, con versione, sistema, grafica e (a scelta) le ultime righe della riga di comando, da cui toglie percorsi e nomi dei file. Si rilegge e si invia con il proprio account GitHub (gratuito). CartesioCAD non invia niente da sé. Le segnalazioni sono pubbliche: niente nomi di clienti o dati personali, e disegni o schermate si allegano solo se si possono condividere. Dopo una chiusura imprevista il programma lo ricorda con una riga nella riga di comando.
- **Segnalare una vulnerabilità:** non in una segnalazione pubblica: vedi [SECURITY.md](SECURITY.md).

## Licenza

Copyright (C) 2026 SamuDiPi. CartesioCAD è software libero distribuito secondo la [GNU General Public License v3.0](LICENSE), **senza alcuna garanzia**. I risultati dei calcoli (per esempio quelli di CanaloCAD) sono indicativi e vanno verificati dal progettista.

Componenti di terze parti, ciascuno con la sua licenza: Qt (LGPL-3.0), Poppler per i PDF (GPL-2.0-or-later), LibreDWG per i DWG (GPL-3.0-or-later, programmi separati), CanaloCAD (GPL-3.0-or-later) e le librerie da cui dipendono (OpenSSL, FreeType, HarfBuzz, ICU, ...). `cmake --install` mette nella cartella da distribuire:

- `LICENSE.txt`, la licenza di CartesioCAD;
- `LICENZE-TERZE-PARTI.txt`, con ogni componente copiato: versione, licenza, sito e indirizzo esatto dei sorgenti di quella versione;
- la cartella `licenze`, con gli avvisi di copyright dei pacchetti e i testi standard delle licenze.

Lo fa `scripts/collect-licenses.sh` (serve MSYS2); con `--sorgenti <cartella>` scarica anche i pacchetti sorgente da allegare a una release. La finestra *Informazioni su CartesioCAD* (menu dell'applicazione, comando `INFORMAZIONI`) mostra versione, commit, licenza e componenti.

**Per distribuire una versione:** fare il commit, poi `cmake --build build` e `cmake --install build --prefix dist/CartesioCAD` (la versione mostra il commit da cui è compilata), e pubblicare la cartella insieme al tag del commit; per sicurezza allegare alla release anche i sorgenti scaricati con `scripts/collect-licenses.sh dist/CartesioCAD --sorgenti sorgenti`.
