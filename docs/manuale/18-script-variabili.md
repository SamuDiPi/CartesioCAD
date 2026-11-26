# Script e variabili

## Script (`SCRIPT`, `SCR`)

**Dove:** Strumenti > Personalizzazione > Esegui script. Anche trascinando un file `.scr` sulla finestra o passandolo all'avvio (`CartesioCAD.exe disegno.scr`).

Uno script è un file di testo con i comandi da eseguire in fila, come se si scrivessero nella riga di comando:
- spazi e a capo valgono come Invio;
- i testi (`TEXT`, valori degli attributi) arrivano fino a fine riga;
- le righe che iniziano con `;` sono commenti.

```
-LAYER N Muri C 1 Muri S Muri

RECTANG 0,0 10000,6000
CIRCLE 5000,3000 1500
TEXT 500,6500 250 0
Pianta piano terra

TILEMODE 0
MVIEW A
SAVEAS 2000 pianta.dwg
EXPORTPDF pianta.pdf
```

Nei file si possono usare `OPEN`, `NEW`, `QSAVE`, `SAVEAS`, `CLOSE`, `EXPORTPDF`, `SCRIPT`, `CANALOINVIA`, `CANALOINS`, `-CANALOVERIFICA`, `-CANALOTAVOLA`, `-DWGCONVERT`, `-ESTRAIDATI`, `-FRONTEQUADRO` (vedi *Impianto*), `-PLOT` (vedi *Stampa*) e `LOGFILEON` (copia dei messaggi in un file), seguiti dal nome del file (relativo alla cartella dello script).

## Variabili di sistema (`SETVAR`, `SET`, `IMPOSTAVAR`)

`SETVAR` chiede il nome di una variabile e il nuovo valore; molte variabili si scrivono anche direttamente come comandi (per esempio `ORTHOMODE 1`).

| Variabile | Significato |
|---|---|
| `CLAYER` | Layer corrente |
| `CECOLOR` | Colore corrente (numero, nome o BYLAYER) |
| `CELTYPE` | Tipo di linea corrente |
| `CELWEIGHT` | Spessore di linea corrente (−1 = DaLayer) |
| `TEXTSIZE` | Altezza proposta per i testi |
| `TEXTSTYLE` | Stile di testo corrente |
| `OSMODE` | Tipi di snap ad oggetto (somma dei valori) |
| `ORTHOMODE` | Orto acceso (1) o spento (0) |
| `GRIDMODE` | Griglia |
| `SNAPMODE` | Snap alla griglia |
| `DYNMODE` | Input dinamico |
| `TILEMODE` | 1 = modello, 0 = layout |
| `CVPORT` | Finestra corrente |
| `LTSCALE` | Scala dei tipi di linea |
| `LWDISPLAY` | Spessori di linea a schermo |
| `INSUNITS` | Unità del disegno (4 = mm, 5 = cm, 6 = m...) |
| `ATTREQ` | 1 = chiede i valori degli attributi all'inserimento, 0 = usa quelli predefiniti |
| `ATTDIA` | Conservata per compatibilità |
| `FILEDIA` | Conservata per compatibilità: negli script i file si danno sempre per nome |
| `CMDECHO` | Conservata per compatibilità con gli script |
| `EXPERT` | Conservata per compatibilità |
| `PICKSTYLE` | Selezione per gruppo |
| `WIPEOUTFRAME` | Cornici delle coperture |
| `CANNOSCALE` | Scala di annotazione |
| `ANNOALLVISIBLE` | Solo memorizzata (gli annotativi si vedono sempre) |
| `DIMSTYLE`, `DIMSCALE`, `DIMTXT`, `DIMASZ`, `DIMDEC` | Stile di quota corrente e sue misure |

## Cronologia (`TEXTSCR`)

Apre la cronologia completa della riga di comando (anche F2).

## Nuovo layout (`LAYOUT`)

Crea un layout nuovo (come il **+** accanto alle schede dei layout).

## Comandi di diagnosi

`DIAGGPU` (scheda grafica e driver) e `-BENCH` (misura la velocità di disegno) servono a segnalare problemi; non modificano il disegno.
