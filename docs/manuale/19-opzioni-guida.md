# Opzioni, guida e informazioni

## Opzioni (`OPTIONS`, `OPZIONI`, `OP`, `PREFERENCES`)

**Dove:** pulsante dell'applicazione > Opzioni; Strumenti > Personalizzazione > Opzioni; menu del tasto destro.

- **Visualizza**: elementi del layout (schede Layout e Modello, area stampabile, sfondo e ombreggiatura della carta, Gestione impostazioni pagina e finestra nei layout nuovi); colori di sfondo del modello, dei layout e del foglio, colore della griglia (con uno sfondo chiaro gli oggetti bianchi si disegnano neri); formato, orientamento e margine del foglio dei layout nuovi; interfaccia moderna o classica; disegno dei testi TrueType.
- **Apri e salva**: formato predefinito, salvataggio automatico (ogni quanti minuti), copie .bak, numero di file recenti, pagina Inizia all'avvio, convertitore DWG (LibreDWG o ODA File Converter), cartelle dei font SHX e TrueType (quelli degli altri programmi installati solo spuntando la casella apposita), cartelle dove cercare immagini e riferimenti, tipo di percorso dei file collegati, cartella dei modelli.
- **Stampa e pubblicazione**: stampante e tabella stili predefinite, spessori di linea, stili di stampa, spazio carta per ultimo, oggetti dello spazio carta nascosti; stampanti di sistema nascoste; timbro di stampa; cartella proposta per i PDF, nome del layout nel nome del file, apertura del PDF dopo la stampa; registro delle stampe in CSV; cartella delle tabelle stili.
- **Preferenze utente**: doppio clic per modificare, menu del tasto destro, scala di inserimento fra unità diverse.
- **Disegno**: marcatore dello snap (dimensione, colore) e mirino.
- **Selezione**: dimensione del mirino di selezione, selezione prima del comando, selezione a trascinamento, maniglie (dimensione, colore).
- **Plugin**: percorso di CanaloCAD e cartella di scambio, sorveglianza della cartella.

## Guida (`GUIDA`, `GUIDE`)

**Dove:** F1; pulsante dell'applicazione > Guida.

Tavolozza con due schede:
- **Percorsi**: percorsi guidati passo per passo. CartesioCAD propone un passo alla volta, evidenzia in arancione il pulsante da usare nella barra multifunzione e si accorge da solo quando il passo è fatto. *Indietro*, *Salta* ed *Esci* in qualsiasi momento.
- **Manuale**: questo manuale, con la ricerca. Durante un comando, F1 apre direttamente la sua pagina.

La guida si apre solo quando la si chiede.

## Elenco dei comandi (`HELP`, `AIUTO`)

Scrive nella riga di comando l'elenco dei comandi principali con gli alias e i tasti funzione.

## Aggiornamenti (`AGGIORNAMENTI`, `AGGIORNA`)

**Dove:** pulsante dell'applicazione > Controlla aggiornamenti (diventa *Aggiornamento disponibile* quando ce n'è uno).

Cerca su GitHub la versione più recente di CartesioCAD (anteprime comprese) e, se è più nuova di quella in uso, la propone con le sue note: *Scarica e installa*, *Pagina della versione* o *Più tardi*.

- **Sicurezza:** ogni versione pubblicata ha un manifesto firmato dall'autore (firma Ed25519) con dimensione e impronta SHA-256 dell'archivio. CartesioCAD contiene la chiave pubblica e installa l'archivio solo se firma, dimensione e impronta corrispondono; i collegamenti sono solo HTTPS verso GitHub.
- **Versioni major** (cambia il primo numero, o la versione è dichiarata tale): prima di installare chiede una conferma esplicita.
- **Installazione sul posto:** i file nuovi prendono il posto di quelli della cartella del programma, i vecchi vanno in `versione-precedente`. Niente cartelle con tante versioni. I file che l'aggiornamento non contiene (per esempio la cartella `fonts` con i propri font) restano. Se qualcosa va storto a metà, tutto torna com'era.
- **Riavvio:** a installazione finita propone di riavviare (prima chiede di salvare i disegni modificati). Altrimenti la versione nuova parte al prossimo avvio.
- Se la cartella del programma non è scrivibile (per esempio in *Programmi* senza permessi), lo dice: si scarica l'archivio dalla pagina della versione e si scompatta a mano.
- **Controllo all'avvio:** Opzioni > Apri e salva > *Controlla gli aggiornamenti all'avvio*, spento di default. Acceso, controlla al massimo una volta al giorno e, se c'è una versione nuova, lo scrive nella riga di comando, senza aprire finestre.

## Torna alla versione precedente (`VERSIONEPRECEDENTE`)

**Dove:** pulsante dell'applicazione > Torna alla versione precedente.

Rimette la versione installata prima dell'ultimo aggiornamento (quella in `versione-precedente`) e propone il riavvio.

## Informazioni su CartesioCAD (`ABOUT`, `INFORMAZIONI`)

**Dove:** pulsante dell'applicazione > Informazioni su CartesioCAD.

Versione e commit da cui è compilato, licenza (GNU GPL v3), assenza di garanzia, componenti di terze parti con le loro licenze e collegamento ai sorgenti.
