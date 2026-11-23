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

- **Nessun dato raccolto.** CartesioCAD lavora solo sul computer: niente account, statistiche, telemetria o controllo degli aggiornamenti, e non si collega a internet da solo (apre un sito solo se lo si chiede, per esempio *Sorgenti* nella finestra Informazioni). Disegni, schemi e progetti non vengono inviati a nessuno.
- **Cosa resta sul computer:** le opzioni e l'elenco dei file recenti (nel registro di Windows, chiave `HKEY_CURRENT_USER\Software\CartesioCAD`), le copie del salvataggio automatico e i file che si salvano. I dati nei disegni (per esempio i nomi nei cartigli) li tratta chi usa il programma.
- **File da fonti affidabili.** DWG, DXF, PDF e script `.scr` vanno aperti solo se vengono da fonti di cui ci si fida. I DWG si leggono con GNU LibreDWG 0.14 in un programma separato (`libredwg/dwg2dxf.exe`): per questa versione sono state pubblicate vulnerabilità che un DWG costruito apposta potrebbe sfruttare. Il plugin CanaloCAD legge i PDF con pdf.js 6.5.438, nel browser.
- **Segnalare una vulnerabilità:** vedi il file SECURITY.md nel repository.
