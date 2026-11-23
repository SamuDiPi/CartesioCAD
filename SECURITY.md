# Sicurezza

## Versioni seguite

Solo l'ultima versione pubblicata nella pagina Releases e il ramo `main`.

## Segnalare una vulnerabilità

Non aprire una issue pubblica. Usa la segnalazione privata di GitHub: scheda **Security** del repository, **Report a vulnerability**. Indica come riprodurre il problema (per esempio il file DWG, DXF, PDF o lo script che lo provoca), la versione di CartesioCAD (finestra Informazioni) e il sistema operativo.

CartesioCAD è un progetto personale e gratuito: le segnalazioni si esaminano appena possibile, di solito entro due settimane. Le correzioni escono in una nuova versione, con la vulnerabilità indicata nelle note.

## Componenti di terze parti

CartesioCAD usa librerie di altri progetti (elenco completo in `LICENZE-TERZE-PARTI.txt` della versione distribuita). Le loro vulnerabilità vanno segnalate anche ai rispettivi progetti; a ogni versione nuova CartesioCAD si ricompila con le librerie aggiornate.

- **GNU LibreDWG 0.14** (lettura e scrittura DWG, programma separato `libredwg/dwg2dxf.exe`): per la 0.14 sono state pubblicate vulnerabilità nella lettura dei DWG; si aggiornerà alla prossima versione stabile. Aprire DWG solo da fonti affidabili.
- **Poppler** (lettura degli schemi in PDF) e **Qt**: versioni di MSYS2 aggiornate al momento della compilazione.
- **pdf.js 6.5.438** nel plugin CanaloCAD, aperto con `isEvalSupported: false`.

## Cose da sapere

- Gli script `.scr` eseguono comandi di CartesioCAD (non comandi di sistema), ma possono salvare e sovrascrivere file: eseguire solo script di cui si conosce il contenuto.
- CartesioCAD si collega a internet solo per cercare gli aggiornamenti: a richiesta, o all'avvio se l'opzione è accesa (spenta di default).

## Aggiornamenti dal programma

- Ogni versione pubblicata ha un manifesto con dimensione e impronta SHA-256 dell'archivio, firmato con Ed25519 dalla chiave privata dell'autore, che non è nel repository. CartesioCAD contiene la chiave pubblica e installa solo archivi che corrispondono a un manifesto con firma valida.
- Collegamenti solo HTTPS verso GitHub (anche dopo i reindirizzamenti), con il TLS di Windows (Schannel); dimensioni massime per ogni risposta; percorsi dell'archivio controllati (niente file fuori dalla cartella del programma).
- La versione precedente resta in `versione-precedente` e si ripristina dal menu dell'applicazione.
- Una vulnerabilità nell'aggiornatore (per esempio un modo per far accettare un pacchetto non firmato) va segnalata in privato come le altre.
