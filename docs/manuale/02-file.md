# File e disegni

## Formati

- **DXF**: si aprono i DXF dalla versione R12 alla 2018 e successive. Si salva in **DXF 2000** (conserva tutto: tratteggi, ellissi, spline, testi multilinea, layout, stili, annotativi, impostazioni di stampa) o in **DXF R12** (il più compatibile con i programmi vecchi: ellissi e spline approssimate, testi multilinea come testi, tratteggi non salvati, solo lo spazio modello).
- **DWG**: si aprono i DWG dalla versione R14 alla 2018 e successive e si salvano in DWG 2000, tramite LibreDWG (incluso). La scrittura DWG di LibreDWG è sperimentale: dopo ogni salvataggio il file viene riletto e confrontato con il disegno, e se manca qualcosa compare un avviso. Su un DWG esistente CartesioCAD propone di salvare una copia. Se è installato l'ODA File Converter (gratuito) si può usare al suo posto (Opzioni > Apri e salva): legge meglio i DWG recenti e salva fino alla versione 2018. Per lo scambio sicuro con altri CAD il **DXF 2000** è la scelta più affidabile.
- **Modelli**: file .dwt, .dwg o .dxf usati come partenza per i disegni nuovi.

## Nuovo (`NEW`, `NUOVO`)

**Dove:** barra di accesso rapido; pulsante dell'applicazione > Nuovo; pagina Inizia > Inizia disegno. Ctrl+N.

Crea un disegno vuoto in una nuova scheda (dal modello predefinito, se indicato nelle Opzioni).

## Apri (`OPEN`, `APRI`)

**Dove:** barra di accesso rapido; pulsante dell'applicazione > Apri; Ctrl+O. Anche trascinando i file sulla finestra o dai Documenti recenti.

Apre uno o più DWG o DXF. Durante la lettura compare l'avanzamento. All'apertura:
- i riferimenti esterni e le immagini vengono cercati anche nella cartella del disegno, nelle sue sottocartelle e nelle cartelle delle Opzioni; quelli non trovati si segnalano nella riga di comando;
- i font SHX mancanti diventano simplex o, se non c'è, il font a tratti di CartesioCAD;
- i tratteggi senza contorno si segnalano.

## Salva (`QSAVE`, `SAVE`, `SALVA`) e Salva con nome (`SAVEAS`, `SALVACOME`)

**Dove:** barra di accesso rapido; pulsante dell'applicazione; Ctrl+S e Ctrl+Maiusc+S.

*Salva* scrive nel file del disegno (per un disegno nuovo chiede dove). *Salva con nome* chiede nome, cartella e formato (DWG 2000, DXF 2000, DXF R12). Il formato proposto per i disegni nuovi si sceglie nelle Opzioni (Apri e salva), dove si attivano anche le copie .bak e il salvataggio automatico.

## Chiudi (`CLOSE`, `CHIUDI`) ed Esci (`QUIT`, `EXIT`, `ESCI`)

Chiudi chiude il disegno corrente (Ctrl+F4, o la × della scheda), Esci chiude CartesioCAD (Ctrl+Q). Per i disegni modificati chiede se salvare.

## Inserisci da file (`INSERTFILE`)

**Dove:** Inserisci > Riferimento > Inserisci da file.

Inserisce un altro DWG o DXF come blocco nel disegno corrente: si sceglie il file, poi punto di inserimento, scala e rotazione. I blocchi e i layer del file arrivano nel disegno.

## Conversione in serie (`DWGCONVERT`, `CONVERTI`, `CONVERSIONE`)

Converte tutti i DWG e DXF di una cartella (anche le sottocartelle, ricreate nella destinazione) in **DXF 2000, DXF R12, DWG o PDF**. Per il PDF si stampano i layout che hanno qualcosa sopra, altrimenti il modello alle estensioni adattato al foglio.
- *Verifica* (facoltativa): ogni file scritto si rilegge e si confronta con l'originale.
- Nella cartella di destinazione si crea *Rapporto conversione.txt* con l'esito di ogni file; gli originali non si toccano.
- Negli script: `-DWGCONVERT sorgente destinazione DXF|DXF12|DWG|PDF [SUB] [SOVRASCRIVI]`.

## Controlla (`AUDIT`, `CONTROLLA`, `VERIFICA`)

**Dove:** Gestisci > Ripulitura > Verifica; pulsante dell'applicazione > Utilità disegno.

Controlla il disegno: geometrie non valide, oggetti su layer, tipi di linea o stili inesistenti, blocchi mancanti, gruppi rotti, identificativi doppi. Chiede `Correggere gli errori rilevati? [Sì/No]`: con *Sì* corregge (si annulla con `U`).

## Recupera (`RECOVER`, `RECUPERA`)

**Dove:** Gestisci > Ripulitura > Recupera; pulsante dell'applicazione > Utilità disegno.

Apre un file danneggiato e lo corregge con il controllo di `AUDIT`.

## Archivia per l'invio (`ETRANSMIT`, `ARCHIVIA`)

**Dove:** pulsante dell'applicazione > Utilità disegno.

Crea un archivio ZIP da mandare a terzi: il disegno in DXF 2000, i riferimenti esterni e le immagini in sottocartelle (con i percorsi riscritti), le tabelle stili di stampa e un rapporto di quello che manca. I font non si includono (appartengono a chi li ha prodotti): il rapporto elenca quelli usati dal disegno.

## Elimina inutilizzati (`PURGE`, `PU`, `ELIMINAINUTILIZZATI`)

**Dove:** Gestisci > Ripulitura > Elimina.

Elimina dal disegno gli elementi non usati: `[Blocchi/LAyer/TIpi di linea/Stili di quota/Tutto]`. L'eliminazione dei layer non si annulla.

## Elimina duplicati (`OVERKILL`, `DOPPIONI`)

**Dove:** Gestisci > Ripulitura > Elimina duplicati.

Selezionati gli oggetti, elimina quelli doppi e le linee contenute in altre linee.

## Disegni lunghi da leggere

Sui disegni molto grandi CartesioCAD disegna sulla scheda video. `DIAGGPU` scrive nella riga di comando la scheda grafica, il driver, il sistema grafico usato e la scala dello schermo (utile per segnalare un problema di visualizzazione).
