# Layer e proprietà

## Gestore proprietà layer (`LAYER`, `LA`)

**Dove:** Inizio > Layer > Proprietà layer; Vista > Pannelli > Proprietà layer.

Tavolozza con tutti i layer: acceso/spento, congelato, bloccato, colore, tipo e spessore di linea, stampabile, *Congela nella finestra* (nei layout), nuovo, rinomina, elimina, rendi corrente. Le modifiche si annullano con Ctrl+Z (non l'eliminazione). Nell'elenco dei layer della barra multifunzione (Inizio > Layer) le stesse icone si cliccano direttamente e si sceglie il layer corrente.

## Layer da riga di comando (`-LAYER`, `-LA`)

`[?/Crea/Imposta/Nuovo/ON/OFF/Colore/TIpolinea/SPessore/Congela/Scongela/Blocca/Sblocca]`: crea, rende corrente, accende, spegne, colora... I nomi accettano i caratteri jolly `*` e `?`. Utile negli script, per esempio `-LAYER N Muri C 1 Muri S Muri` (nuovo layer Muri, colore 1, corrente).

## Stati dei layer (`LAYERSTATE`, `LAS`, `STATILAYER`)

Salva con un nome la situazione di tutti i layer (accensione, congelamento, blocco, colore, tipo e spessore di linea, stampa) e la ripristina quando serve (si annulla con Ctrl+Z). Gli stati si salvano nel DXF 2000.

## Comandi rapidi sui layer degli oggetti

**Dove:** Inizio > Layer.

| Comando | Alias | Cosa fa |
|---|---|---|
| `LAYOFF` | `LAYSPEGNI` | Spegne il layer degli oggetti indicati |
| `LAYFRZ` | `LAYCONGELA` | Congela il layer degli oggetti indicati (il layer corrente no) |
| `LAYLCK` | `LAYBLOCCA` | Blocca il layer degli oggetti indicati (si vedono ma non si modificano) |
| `LAYULK` | `LAYSBLOCCA` | Sblocca il layer dell'oggetto indicato |
| `LAYISO` | `LAYISOLA` | Lascia accesi solo i layer degli oggetti selezionati |
| `LAYUNISO` | `LAYDISISOLA` | Annulla l'isolamento |
| `LAYTHW` | `LAYSCONGELA` | Scongela tutti i layer |
| `LAYON` | | Accende tutti i layer |
| `LAYMCUR` | | Rende corrente il layer dell'oggetto selezionato |
| `LAYMATCH` | `LAYCORRISP` | Porta gli oggetti selezionati sul layer di un altro oggetto |

## Tavolozza Proprietà (`PROPERTIES`, `PR`, `PROPRIETA`)

**Dove:** Inizio > Proprietà; Vista > Pannelli > Proprietà; Ctrl+1; doppio clic su un oggetto che non è un testo.

Mostra e modifica le proprietà degli oggetti selezionati:
- la tendina in alto divide la selezione per tipo: scegliendo un tipo si modificano solo quelli;
- sezioni comprimibili *Generale* (colore, layer, tipo di linea, scala del tipo di linea, spessore, trasparenza), *Stile di stampa*, *Geometria* / *Testo* / *Quota* / *Attributi*, *Vista*, *Varie*;
- con più oggetti, i valori diversi compaiono come `*VARIA*`;
- i pulsanti accanto alla tendina: ≈ seleziona simili (`SELECTSIMILAR`), ▣ seleziona tutto nello spazio corrente, ✕ deseleziona tutto;
- senza selezione mostra le proprietà correnti (per i nuovi oggetti), la tabella stili dello spazio, la vista e le unità del disegno.

## Colore, tipo e spessore di linea correnti (`COLOR`, `COL`, `COLORE`; `CECOLOR`; `CELTYPE`; `CELWEIGHT`; `CLAYER`)

`COLOR` sceglie il colore dei nuovi oggetti (numero 1–255, nome come ROSSO, o DALAYER). Le variabili `CECOLOR`, `CELTYPE`, `CELWEIGHT` e `CLAYER` impostano colore, tipo di linea, spessore e layer correnti dalla riga di comando o dagli script.

## Tipi di linea (`-LINETYPE`, `-LT`, `-TIPOLINEA`; `LTSCALE`)

Tipi disponibili: DASHED, CENTER, HIDDEN, DASHDOT, PHANTOM, DOT, DIVIDE, BORDER e quelli dei disegni aperti. `-LINETYPE`: `Carica` i tipi standard, `Imposta` quello corrente. `LTSCALE` è la scala globale dei tipi di linea (più grande = trattini più lunghi); nelle finestre dei layout vale anche `PSLTSCALE`.

## Spessori di linea (`LWDISPLAY`)

Ogni layer e oggetto può avere uno spessore. Sullo schermo si vedono solo con il pulsante *Spessori di linea* della barra di stato (variabile `LWDISPLAY`); in stampa si usano sempre, se nelle impostazioni di stampa è attivo *Spessori*.

## Trasparenza

Dalla tavolozza Proprietà, per oggetto o per layer; in stampa si usa se è attiva l'opzione *Trasparenza*.
