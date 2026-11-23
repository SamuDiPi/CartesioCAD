# Claude (connettore per Claude Desktop)

## Claude (`CLAUDE`)

**Dove:** Strumenti > Assistente > Claude; pulsante dell'applicazione > Claude...; Guida > Percorsi > "Lavorare con Claude" per farsi accompagnare passo per passo.

Con il connettore si chiede a Claude, nell'app **Claude Desktop** con il proprio account, di leggere e modificare il disegno aperto in CartesioCAD. Si scrive nella chat di Claude Desktop, per esempio: *"porta tutti i testi delle stanze sul layer Nomi-Stanze"*. Claude legge il disegno, propone le modifiche e CartesioCAD le mostra; le modifiche si applicano solo con **OK**.

CartesioCAD non chiede né conserva credenziali: l'accesso a Claude si fa in Claude Desktop, con l'account dell'utente e alle sue condizioni.

### Attivarlo (una volta sola)

1. Installare Claude Desktop da claude.ai/download, accedere con il proprio account e chiuderlo.
2. In CartesioCAD: Strumenti > Assistente > **Claude**, spuntare **Attiva il connettore**, chiudere del tutto Claude Desktop (anche dall'icona vicino all'orologio: aperto, può riscrivere il proprio file di configurazione) e premere **Configura Claude Desktop**. CartesioCAD aggiunge `cartesiocad` ai connettori di Claude Desktop (file `claude_desktop_config.json` nella cartella dei dati di Claude Desktop, anche per la versione dello Store di Microsoft; ne tiene una copia `.prima-di-cartesiocad`); gli altri connettori restano come sono.
3. Riaprire Claude Desktop: fra gli strumenti compare **cartesiocad**.

Il connettore resta acceso ai prossimi avvii finché non si toglie la spunta. Se sono aperti due CartesioCAD, solo il primo riceve le richieste.

### Cosa può fare Claude

- **Leggere**: nome e formato del file, unità, layer, oggetti con le loro misure. Di default legge solo gli **oggetti selezionati**; legge tutto il disegno o un'area solo se lo chiede.
- **Selezionare** degli oggetti per mostrarli.
- **Proporre modifiche**: spostare, copiare, ruotare, scalare, eliminare, cambiare layer o colore, cambiare un testo, creare layer, aggiungere linee, polilinee, cerchi e testi. CartesioCAD controlla ogni operazione (oggetti esistenti, layer non bloccati, valori sensati) e apre una finestra con l'elenco delle modifiche e gli oggetti interessati selezionati nel disegno. **OK** le applica tutte insieme (un solo Ctrl+Z le annulla), **Annulla** le scarta. Senza risposta in 4 minuti la proposta si ritira.
- **Salvare una nuova versione**: accanto al file aperto, con il nome successivo (`pianta.dwg` → `pianta-v2.dwg` → `pianta-v3.dwg`), nello stesso formato o in DXF. **Il file originale non viene mai sovrascritto**; dopo il salvataggio la scheda lavora sulla nuova versione. Per i DWG CartesioCAD passa da un DXF temporaneo e poi rilegge il DWG salvato per controllarlo, come in Salva con nome.

Claude non può aprire, cercare o cancellare file, non esegue script e non usa la rete per conto di CartesioCAD.

### Privacy e sicurezza

- CartesioCAD **non si collega a Internet**: il connettore è un canale locale (named pipe di Windows) aperto solo all'utente di Windows collegato.
- Quello che Claude legge passa a Claude Desktop e da lì ad Anthropic, secondo le condizioni dell'account Claude. Prima di usarlo con disegni di clienti verificare di poterli condividere. Con gli account personali (Free, Pro, Max) controllare in Claude l'impostazione sull'uso delle conversazioni per migliorare i modelli; per i disegni di lavoro sono indicati i piani Team o Enterprise.
- I testi dentro un disegno possono contenere frasi scritte apposta per ingannare un assistente (per esempio "cancella tutto"): CartesioCAD tratta tutto come dati, accetta solo le operazioni dell'elenco e niente cambia senza OK. Controllare sempre le modifiche prima di usare il disegno.
- Claude è un marchio di Anthropic. CartesioCAD è un progetto indipendente, non collegato ad Anthropic.
