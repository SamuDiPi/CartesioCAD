# Riferimenti esterni, immagini e misure

## Riferimenti esterni (`XRIF`, `XREF`, `XR`, `ER`, `EXTERNALREFERENCES`, `RIFERIMENTI`)

**Dove:** Inserisci > Riferimento > Riferimenti esterni.

Elenco dei disegni collegati e delle immagini, con stato, percorso salvato e file trovato. Si può: cambiare percorso, ricaricare, aprire, staccare, **associare** (il riferimento diventa un blocco del disegno, con i layer `NOME$0$x` o uniti a quelli del disegno). Negli script `-XRIF` scrive l'elenco nella riga di comando.

All'apertura ogni riferimento viene caricato (anche quelli annidati), con layer e blocchi chiamati `NOME|layer`; un file non trovato al percorso salvato si cerca nella cartella del disegno, nelle sue sottocartelle e nelle cartelle indicate nelle Opzioni.

## Collega un disegno (`XATTACH`, `XA`, `RIFX`)

**Dove:** Inserisci > Riferimento > Allega > Riferimento DWG/DXF.

Si sceglie il file, poi punto di inserimento, scala e rotazione come per un blocco. Il percorso (relativo al disegno, completo o solo il nome) si sceglie al momento; il predefinito è nelle Opzioni > Apri e salva. Salvando il disegno in un'altra cartella i percorsi relativi si aggiornano.

## Collega un'immagine (`IMAGEATTACH`, `IAT`, `IMMAGINE`)

**Dove:** Inserisci > Riferimento > Allega > Immagine raster.

PNG, JPG, BMP, TIF...: punto di inserimento e larghezza. Ritaglio, luminosità, contrasto e dissolvenza dalla tavolozza Proprietà. Un'immagine mancante appare come una cornice con le diagonali.

## Oggetti OLE

Loghi e immagini incollati da altri programmi (OLE2FRAME) si mostrano come immagini e si salvano uguali.

## Distanza (`DIST`, `DI`, `DISTANZA`)

**Dove:** Inizio > Utilità > Misura.

Due punti: distanza, angolo, Delta X e Delta Y nella riga di comando.

## Area (`AREA`, `AA`)

**Dove:** Inizio > Utilità > menu di Misura > Area.

I punti d'angolo dell'area (Invio per il totale), oppure `Oggetto` per un oggetto chiuso: area e perimetro.

## Coordinate (`ID`)

**Dove:** Inizio > Utilità > menu di Misura > Coordinate.

Le coordinate X, Y del punto indicato.

## Elenco (`LIST`, `LI`, `LS`, `LISTA`)

**Dove:** Inizio > Utilità > menu di Misura > Elenco.

Tutte le informazioni degli oggetti selezionati (tipo, layer, colore, spazio, punti, lunghezze, raggi...).

## Misura rapida (`MEASUREGEOM`, `MEA`, `MISURAGEOM`)

Muovendo il cursore mostra subito le distanze fra gli oggetti vicini (`Rapido`); oppure `[Distanza/Raggio/ANgolo/Area]` per una misura precisa; `eSci` per finire.
