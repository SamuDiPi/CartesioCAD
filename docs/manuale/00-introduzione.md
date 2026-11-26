# Come usare questo manuale

Il manuale descrive tutte le funzioni di CartesioCAD. Si apre dalla tavolozza **Guida** (F1, comando `GUIDA`), scheda *Manuale*, e si legge anche su GitHub nella cartella `docs/manuale`.

## Convenzioni

- I **comandi** sono scritti in maiuscolo, per esempio `LINE`. Si scrivono nella riga di comando e si confermano con Invio; tra parentesi ci sono gli **alias**, cioè i nomi brevi o italiani che fanno la stessa cosa (`L`, `LINEA`).
- **Dove** indica il pulsante nella barra multifunzione: *Scheda > Pannello > Pulsante*. Un pulsante con la freccetta ▾ ha altre varianti nel suo menu.
- Le **opzioni** di un comando compaiono tra parentesi quadre nel messaggio, per esempio `[Arco/LArghezza/Annulla]`: si scrivono le lettere maiuscole dell'opzione (`A`, `LA`, `U`), si cliccano nel messaggio o si scelgono con il tasto destro.
- Il valore tra `< >` è quello proposto: Invio lo accetta.
- **Esc** annulla il comando in corso; **Invio** o **Spazio** a vuoto ripetono l'ultimo comando.

## Cercare nel manuale

Nella scheda *Manuale* della Guida si scrive una parola (un comando come `TRIM`, un alias come `TR`, o un argomento come "tratteggio") e compaiono le sezioni che la contengono. Durante un comando, **F1** apre il manuale direttamente su quel comando.

## Percorsi guidati

Per imparare facendo ci sono i **percorsi guidati** (scheda *Percorsi* della Guida): CartesioCAD propone un passo alla volta, evidenzia il pulsante da usare e si accorge da solo quando il passo è fatto. Si possono interrompere e riprendere in qualsiasi momento; non partono mai da soli.

## Privacy e sicurezza

- **Nessun dato raccolto.** CartesioCAD lavora solo sul computer: niente account, statistiche o telemetria. Disegni, schemi e progetti non vengono inviati a nessuno.
- **Collegamenti a internet solo su richiesta.** Il programma si collega a GitHub solo quando si cercano gli aggiornamenti (menu dell'applicazione > *Controlla aggiornamenti*, comando `AGGIORNAMENTI`) o se si accende nelle Opzioni il controllo all'avvio (spento di default; al massimo una volta al giorno). GitHub vede l'indirizzo IP del computer; non viene mandato nient'altro. Apre un sito nel browser solo se lo si chiede (per esempio *Sorgenti* nella finestra Informazioni).
- **Scheda CanaloCAD.** La pagina di CanaloCAD gira in Microsoft Edge WebView2 (Windows) o in Qt WebEngine (versione Linux in Flatpak) senza collegarsi a siti: non apre indirizzi web (i collegamenti vanno al browser), il controllo SmartScreen degli indirizzi e il salvataggio di moduli e password sono spenti. Il progetto della scheda si salva nel disegno da cui la si apre, con i dati del cartiglio di CanaloCAD (committente, progettista, codici): chi riceve il disegno li vede aprendo la scheda; preferenze e progetti aperti senza disegno restano nella cartella dei dati locali di CartesioCAD (`%LOCALAPPDATA%\CartesioCAD\CartesioCAD\CanaloCAD-pagina`). Il motore WebView2 è un componente di Windows: i suoi aggiornamenti e i dati di diagnostica seguono le impostazioni di Windows.
- **Cosa resta sul computer:** le opzioni e l'elenco dei file recenti (nel registro di Windows, chiave `HKEY_CURRENT_USER\Software\CartesioCAD`), le copie del salvataggio automatico e i file che si salvano. I dati nei disegni (per esempio i nomi nei cartigli) li tratta chi usa il programma.
- **File da fonti affidabili.** DWG, DXF, PDF e script `.scr` vanno aperti solo se vengono da fonti di cui ci si fida. I DWG si leggono con GNU LibreDWG 0.14.8413 in un programma separato (`libredwg/dwg2dxf.exe`): è l'istantanea successiva alla 0.14 stabile che corregge le vulnerabilità note della 0.14 (CVE-2026-15520, CVE-2026-62250), e contiene anche la correzione di read_2004_compressed_section (CVE-2026-9530, stesso punto di CVE-2026-9500); è compilata con in più la correzione di CVE-2026-111167, non ancora pubblicata da LibreDWG; LibreDWG resta il componente più esposto ai file costruiti apposta. Il plugin CanaloCAD legge i PDF con pdf.js 6.5.438, nel browser.
- **Segnalare un problema:** menu dell'applicazione > *Segnala un problema* (comando `SEGNALA`). Si scrive in due righe cosa è successo; il programma apre nel browser la pagina "nuova segnalazione" di GitHub già compilata, con versione, sistema, grafica e (a scelta) le ultime righe della riga di comando, da cui toglie percorsi e nomi dei file. Si rilegge e si invia con il proprio account GitHub (gratuito). CartesioCAD non invia niente da sé. Le segnalazioni sono pubbliche: niente nomi di clienti o dati personali, e disegni o schermate si allegano solo se si possono condividere. Dopo una chiusura imprevista il programma lo ricorda con una riga nella riga di comando.
- **Segnalare una vulnerabilità:** non in una segnalazione pubblica: vedi il file SECURITY.md nel repository.
