# Sicurezza

## Versioni seguite

Solo l'ultima versione pubblicata nella pagina Releases e il ramo `main`.

## Segnalare una vulnerabilità

Non aprire una issue pubblica. Usa la segnalazione privata di GitHub: scheda **Security** del repository, **Report a vulnerability**. Indica come riprodurre il problema (per esempio il file DWG, DXF, PDF o lo script che lo provoca), la versione di CartesioCAD (finestra Informazioni) e il sistema operativo.

CartesioCAD è un progetto personale e gratuito: le segnalazioni si esaminano appena possibile, di solito entro due settimane. Le correzioni escono in una nuova versione, con la vulnerabilità indicata nelle note.

## Componenti di terze parti

CartesioCAD usa librerie di altri progetti (elenco completo in `LICENZE-TERZE-PARTI.txt` della versione distribuita). Le loro vulnerabilità vanno segnalate anche ai rispettivi progetti; a ogni versione nuova CartesioCAD si ricompila con le librerie aggiornate.

- **GNU LibreDWG 0.14.8413** (lettura e scrittura DWG, programma separato `libredwg/dwg2dxf.exe`): istantanea pubblicata dal progetto dopo la 0.14 stabile, scelta perché corregge le vulnerabilità della 0.14 (CVE-2026-15520 nella decompressione R2004, CVE-2026-62250 ciclo infinito con DWG pre-R13); le correzioni di CVE-2026-9502, CVE-2026-15182 e CVE-2026-9530 erano già nella 0.14. Verificata sugli stessi DWG della 0.14 (lettura identica). Si passerà alla prossima versione stabile appena esce. Aprire DWG solo da fonti affidabili.
- **Poppler** (lettura degli schemi in PDF) e **Qt**: versioni di MSYS2 aggiornate al momento della compilazione. Poppler fino alla 26.11 ha una vulnerabilità nella verifica delle firme digitali con NSS (CVE-2026-131847): CartesioCAD legge solo il testo dei PDF e non verifica firme, quindi quella parte non viene usata.
- **OpenSSL 4.1** (solo la verifica della firma Ed25519 degli aggiornamenti; l'HTTPS usa il TLS di Windows). La serie 3.6 non è più supportata dal 1° novembre 2026 e non si distribuisce più. La 4.1.0 ha vulnerabilità corrette dal progetto OpenSSL nel ramo 4.1 (avvisi del 10 e 17 novembre 2026: CVE-2026-35190, 88822, 88823, 95691, 95692, 107493, 107496, 124709) ma la 4.1.1 non è ancora uscita: CartesioCAD distribuisce la 4.1.0 ricompilata con quelle otto correzioni ufficiali (pacchetto `4.1.0-1.1`: ricetta MSYS2 più i commit del ramo 4.1; ricetta, correzioni e sorgenti sono nell'archivio dei sorgenti di terze parti della versione). Appena MSYS2 pubblica la 4.1.1 si torna al pacchetto ufficiale.
- **pdf.js 6.5.438** nel plugin CanaloCAD, aperto con `isEvalSupported: false`.

## Cose da sapere

- Gli script `.scr` eseguono comandi di CartesioCAD (non comandi di sistema), ma possono salvare e sovrascrivere file: eseguire solo script di cui si conosce il contenuto.
- CartesioCAD si collega a internet solo per cercare gli aggiornamenti: a richiesta, o all'avvio se l'opzione è accesa (spenta di default). Il connettore per Claude Desktop è solo locale (vedi sotto).

## Connettore per Claude Desktop

- Spento di default; si accende dall'Assistente IA (comando `ASSISTENTE`). CartesioCAD apre una named pipe locale (`QLocalServer` con accesso limitato all'utente di Windows collegato), nessuna porta di rete. `cartesio-mcp.exe`, avviato da Claude Desktop, la usa per inoltrare le richieste; non legge né scrive file e non si collega a Internet. CartesioCAD accetta solo collegamenti da `cartesio-mcp.exe` della propria cartella sullo stesso computer (verifica il programma che si collega) e rifiuta gli altri, scrivendolo nella riga di comando.
- Ogni proposta o ricerca con filtro ha un tempo massimo di calcolo (15 secondi): un filtro o un'espressione regolare costruiti male non bloccano il programma.
- Le richieste sono un elenco chiuso di strumenti e di operazioni, controllate una per una (oggetti esistenti, layer non bloccati, numeri finiti, limiti sul numero di operazioni e sulla dimensione dei messaggi). Nessuno script, nessun comando di sistema, nessun accesso ai file oltre al salvataggio di una nuova versione accanto al disegno aperto, con un nome nuovo (mai sovrascritto).
- Le modifiche si applicano solo dopo la conferma dell'utente nella finestra di CartesioCAD. I testi presenti nei disegni sono dati: se contengono istruzioni rivolte all'assistente (prompt injection), non hanno effetto senza quella conferma.

## Aggiornamenti dal programma

- Ogni versione pubblicata ha un manifesto con dimensione e impronta SHA-256 dell'archivio, firmato con Ed25519 dalla chiave privata dell'autore, che non è nel repository. CartesioCAD contiene la chiave pubblica e installa solo archivi che corrispondono a un manifesto con firma valida.
- Collegamenti solo HTTPS (HTTP/1.1) verso GitHub, anche dopo i reindirizzamenti, con il TLS di Windows (Schannel); dimensioni massime per ogni risposta; percorsi dell'archivio controllati (niente file fuori dalla cartella del programma).
- L'archivio si verifica e si scompatta dalla stessa copia letta in memoria: non si può sostituire il file fra la verifica e l'installazione.
- La versione precedente resta in `versione-precedente` e si ripristina dal menu dell'applicazione.
- Installato in Programmi (installatore), CartesioCAD scarica e verifica l'aggiornamento come utente normale, poi avvia una copia di sé con i diritti di amministratore (richiesta UAC di Windows) solo per installarlo. Quella copia non si fida di chi l'ha avviata: rilegge manifesto e firma, verifica la firma con la chiave incorporata, rifiuta versioni più vecchie di quella installata, rilegge l'archivio in memoria e ne verifica dimensione e SHA-256 prima di scompattarlo, e scrive solo nella propria cartella. Lo stesso per il ripristino della versione precedente.
- L'installatore (Inno Setup) non è firmato digitalmente: Windows può mostrare l'avviso "autore sconosciuto". Scaricarlo solo dalla pagina Releases del repository.
- Una vulnerabilità nell'aggiornatore (per esempio un modo per far accettare un pacchetto non firmato) va segnalata in privato come le altre.
