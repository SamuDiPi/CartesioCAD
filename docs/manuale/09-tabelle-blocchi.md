# Tabelle e blocchi

## Tabella (`TABLE`, `TB`, `TABELLA`)

**Dove:** Annota > Tabelle > Tabella; Inizio > Annotazione > Tabella.

1. Numero di colonne, numero di righe, larghezza delle colonne.
2. Punto di inserimento (angolo in alto a sinistra).
3. Doppio clic su una cella (o `TABLEDIT`, `MODIFICATABELLA`) per scriverci: Tab passa alla cella successiva, Maiusc+Tab alla precedente.

Le tabelle si salvano come blocco (gli altri programmi le mostrano uguali) con i dati per riaprirle modificabili in CartesioCAD; le tabelle create da altri programmi si leggono come blocco.

## Creare un blocco (`BLOCK`, `B`, `BLOCCO`)

**Dove:** Inizio > Blocco > Crea; Inserisci > Definizione di un blocco > Crea.

Un blocco è un gruppo di oggetti con un nome, da inserire più volte. Nome del blocco, punto base (il punto con cui si inserirà), oggetti.

## Inserire un blocco (`INSERT`, `I`, `INSERISCI`)

**Dove:** Inizio > Blocco > Inserisci; Inserisci > Blocco > Inserisci.

Nome del blocco (`?` per l'elenco), punto di inserimento, scala e rotazione. Se il blocco ha attributi, ne chiede i valori (con `ATTREQ` = 0 usa quelli predefiniti senza chiedere; `ATTDIA` si conserva per compatibilità con gli script).

## Libreria simboli (`BLOCKSPALETTE`, `BLOCCHI`, `LIBRERIA`, `SIMBOLI`, `ADC`, `ADCENTER`)

**Dove:** Inizio > Blocco > Libreria simboli; Vista > Pannelli.

Tavolozza con le anteprime: blocchi del disegno, di un file libreria DWG/DXF o di una cartella (ogni disegno è un simbolo), recenti. **Clic** per inserire o **trascinamento** sul disegno (si inserisce dove si rilascia). Tasto destro per modificarlo (`BEDIT`) o aprire il file. I simboli di un file in altre unità si scalano (per esempio libreria in cm, disegno in mm).

## Editor blocchi (`BEDIT`, `BE`, `EDITBLOCCO`; `BSAVE`, `BS`; `BCLOSE`, `BC`)

**Dove:** Inizio > Blocco > Modifica; Inserisci > Definizione di un blocco > Editor blocchi. Anche il doppio clic su un blocco senza attributi.

Il blocco si apre in una scheda e si modifica con i comandi normali. `BSAVE` (`SALVABLOCCO`) aggiorna la definizione e tutti gli inserimenti; `BCLOSE` (`CHIUDIBLOCCO`) chiude.

## Attributi (`ATTDEF`, `ATT`, `DEFATT`)

**Dove:** Inserisci > Definizione di un blocco > Definisci attributi.

Un attributo è un testo variabile dentro un blocco (per esempio il titolo di un cartiglio). Etichetta (il nome), richiesta (la domanda che si fa all'inserimento), valore predefinito, posizione e altezza. Poi si crea il blocco includendo l'attributo.

## Modificare gli attributi (`EATTEDIT`, `ATTEDIT`, `ATE`, `MODATT`)

**Dove:** Inizio > Blocco > Edita attributi; Inserisci > Blocco > Edita attributo. Anche il doppio clic su un blocco con attributi o il menu del tasto destro.

Mostra i valori degli attributi del blocco scelto e li modifica.

## Sincronizza attributi (`ATTSYNC`, `SINCATT`)

**Dove:** Inserisci > Blocco > menu di Edita attributo > Sincronizza attributi.

Dopo aver cambiato le definizioni di attributo di un blocco, gli inserimenti esistenti le seguono (posizione, stile, ordine); i valori restano.

## Gestione attributi (`BATTMAN`, `GESTATT`)

**Dove:** Inserisci > Definizione di un blocco > Gestisci attributi.

Elenco delle definizioni di attributo di un blocco: etichetta, richiesta, valore predefinito, altezza, invisibile, ordine; poi sincronizzazione degli inserimenti.

## Ritaglio dei blocchi (`XCLIP`, `XC`, `RITAGLIO`)

**Dove:** Inserisci > Riferimento > Ritaglia.

Mostra solo una parte di un blocco o di un riferimento esterno: contorno rettangolare o poligonale; `ON`/`OFF` per accendere e spegnere il ritaglio, `Elimina` per toglierlo. Il ritaglio segue il blocco se si sposta o ruota.

## Blocchi dinamici

Lo stato salvato nel file si disegna così com'è, e il blocco mostra il nome del blocco dinamico (Proprietà, `QSELECT`). Non si può cambiare stato (visibilità, ribaltamento, stiramento).
