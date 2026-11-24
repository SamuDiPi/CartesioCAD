# Assistente IA (connettore per Claude Desktop)

## Assistente IA (`ASSISTENTE`)

**Dove:** Strumenti > Assistente > Assistente IA; pulsante dell'applicazione > Assistente IA...; Guida > Percorsi > "Lavorare con un assistente IA" per farsi accompagnare passo per passo.

Con il connettore si chiede a Claude, l'assistente IA di Anthropic, nell'app **Claude Desktop** con il proprio account, di leggere e modificare il disegno aperto in CartesioCAD. Si scrive nella chat di Claude Desktop, per esempio: *"porta tutti i testi delle stanze sul layer Nomi-Stanze"*. Claude legge il disegno, propone le modifiche e CartesioCAD le mostra; le modifiche si applicano solo con **OK**.

CartesioCAD non chiede né conserva credenziali: l'accesso a Claude si fa in Claude Desktop, con l'account dell'utente e alle sue condizioni.

### Attivarlo (una volta sola)

1. Installare Claude Desktop da claude.ai/download, accedere con il proprio account e chiuderlo.
2. In CartesioCAD: Strumenti > Assistente > **Assistente IA**, spuntare **Attiva il connettore**, chiudere del tutto Claude Desktop (anche dall'icona vicino all'orologio: aperto, può riscrivere il proprio file di configurazione) e premere **Configura Claude Desktop**. CartesioCAD aggiunge `cartesiocad` ai connettori di Claude Desktop (file `claude_desktop_config.json` nella cartella dei dati di Claude Desktop, anche per la versione dello Store di Microsoft; ne tiene una copia `.prima-di-cartesiocad`); gli altri connettori restano come sono.
3. Riaprire Claude Desktop: fra gli strumenti compare **cartesiocad**.

Il connettore resta acceso ai prossimi avvii finché non si toglie la spunta. Se sono aperti due CartesioCAD, solo il primo riceve le richieste.

### Cosa può fare Claude

- **Leggere**: nome e formato del file, unità, layer, oggetti con le loro misure. Di default legge solo gli **oggetti selezionati**; legge tutto il disegno, un'area o gli oggetti che rispettano un filtro solo se lo chiede, a pagine.
- **Riassumere**: conta gli oggetti per layer, tipo, blocco, colore, testo o valore di un attributo, senza leggerli uno per uno (per capire un disegno grande prima di cambiarlo).
- **Selezionare** degli oggetti per mostrarli, anche con un filtro.
- **Proporre modifiche**: spostare, copiare, ruotare, scalare, eliminare, cambiare layer, colore, tipo di linea o spessore, cambiare un testo, sostituire parole nei testi (anche con espressioni regolari), mettere i testi in maiuscolo o minuscolo, cambiarne l'altezza, impostare gli attributi dei blocchi, creare e rinominare layer, aggiungere linee, polilinee, cerchi e testi. CartesioCAD controlla ogni operazione (oggetti esistenti, layer non bloccati, valori sensati) e apre una finestra con l'elenco delle modifiche e gli oggetti interessati selezionati nel disegno. **OK** le applica tutte insieme (un solo Ctrl+Z le annulla), **Annulla** le scarta. Senza risposta in 4 minuti la proposta si ritira.
- **Modifiche massive**: invece di elencare gli oggetti, Claude può indicarli con un **filtro** (layer, tipo, colore, testo contenuto o espressione regolare, nome del blocco, valore di un attributo, area, selezione), e CartesioCAD li trova tutti da sé; la finestra di conferma dice quanti sono e con quale filtro. Esempi di richieste: *"sostituisci Q1 con QE1 in tutti i testi"*, *"porta tutte le linee del layer Muri su Pareti con spessore 0,5"*, *"imposta il circuito C9 in tutte le prese che oggi hanno C1"*, *"rinomina il layer Testi in Testi-Locali e raddoppia l'altezza dei testi"*. Gli oggetti sui layer bloccati restano esclusi (la finestra lo dice). Fino a 500.000 oggetti per proposta.
- **Salvare una nuova versione**: accanto al file aperto, con il nome successivo (`pianta.dwg` → `pianta-v2.dwg` → `pianta-v3.dwg`), nello stesso formato o in DXF (sempre DXF 2000, che conserva testi multilinea, tratteggi e spline). **Il file originale non viene mai sovrascritto**; dopo il salvataggio la scheda lavora sulla nuova versione. Per i DWG CartesioCAD passa da un DXF temporaneo e poi rilegge il DWG salvato per controllarlo, come in Salva con nome.

### Canaline con CanaloCAD

- **Cercare i cavi** nel database di CanaloCAD (tipologie generiche e cavi propri): tipo, formazione, diametro, peso, sezione.
- **Calcolare una canalina** con i cavi indicati, senza toccare il disegno: misura scelta dai cavi con la riserva voluta (30% di default) oppure verifica di una misura data (se è piccola dice la larghezza minima), riempimento, Kr, segregazione CEI 64-8 art. 528.1, peso, tiro di posa e distinta. Esempio: *"che passerella serve per 2 FG16 4G10, 4 FG16 3G2,5 e 10 cavi Cat6?"*.
- **Leggere le canaline del disegno** (CN1, CN2...): misure, cavi, riempimento, segregazione, e quelle troppo piccole per i loro cavi.
- **Proporre una canalina**: nuova, con la sezione disegnata al vero, oppure con nuovi cavi e misura per una canalina esistente. Come per le altre modifiche, CartesioCAD apre una finestra con misure e risultati dei calcoli e applica solo con **OK** (Ctrl+Z la annulla).
- **Scheda CanaloCAD** (Windows): aprirla, inviarle la sezione selezionata e leggere cavi e risultati della scheda canalina su cui si sta lavorando. La tavola torna nel disegno solo quando la si inserisce (*Esporta > Inserisci nel disegno*).

I calcoli sono quelli di CanaloCAD: valori indicativi che il progettista deve verificare.

Claude non può aprire, cercare o cancellare file, non esegue script e non usa la rete per conto di CartesioCAD.

### Privacy e sicurezza

- CartesioCAD **non si collega a Internet**: il connettore è un canale locale (named pipe di Windows) aperto solo all'utente di Windows collegato.
- Quello che Claude legge passa a Claude Desktop e da lì ad Anthropic, secondo le condizioni dell'account Claude. Prima di usarlo con disegni di clienti verificare di poterli condividere. Con gli account personali (Free, Pro, Max) controllare in Claude l'impostazione sull'uso delle conversazioni per migliorare i modelli; per i disegni di lavoro sono indicati i piani Team o Enterprise.
- I testi dentro un disegno possono contenere frasi scritte apposta per ingannare un assistente (per esempio "cancella tutto"): CartesioCAD tratta tutto come dati, accetta solo le operazioni dell'elenco e niente cambia senza OK. Controllare sempre le modifiche prima di usare il disegno.
- Claude è un marchio di Anthropic. CartesioCAD è un progetto indipendente, non collegato ad Anthropic.
