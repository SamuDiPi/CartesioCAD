# Stampa e PDF

## Stampa (`PLOT`, `STAMPA`, `PRINT`)

**Dove:** Output > Stampa > Stampa; Ctrl+P.

La finestra di stampa ha:
- **Impostazione di pagina** con nome (scelta, nuova, salvataggio nel disegno);
- **Stampante**: PDF o le stampanti di Windows (proprietà, stampa su file, copie);
- **Formato**: ISO, ANSI, ARCH, quelli della stampante o personalizzati;
- **Area**: Layout, Estensioni, Limiti, Schermo, Finestra (scelta nel disegno con due angoli);
- **Offset** e centratura;
- **Scala**: adattata al foglio, standard (1:100, 1:50...) o personalizzata, anche in pollici; scala degli spessori;
- **Tabella stili** (`.ctb`: monochrome, grayscale, acad, retinature, anche quelle già presenti sul computer) con il pulsante per modificarla;
- **Opzioni**: qualità (DPI), spessori di linea, trasparenza, stili di stampa, spazio carta per ultimo, oggetti dello spazio carta nascosti, timbro di stampa;
- **Orientamento** e stampa capovolta;
- **Anteprima** parziale (nella finestra) e completa.

Le impostazioni si salvano nel disegno (DXF); trasparenza e timbro restano nelle preferenze del programma. Non hanno senso in 2D e sono disattivate: stampa ombreggiata, nascondi oggetti, stampa in background.

## Anteprima (`PREVIEW`, `ANTEPRIMA`, `PRE`)

**Dove:** Output > Stampa > Anteprima.

Mostra il foglio come verrà stampato, con zoom e pan; da lì si stampa o si chiude.

## Esporta PDF (`EXPORTPDF`, `ESPORTAPDF`)

**Dove:** Output > Esporta in PDF > Esporta PDF.

Esporta in PDF il layout attivo (o il modello): apre la finestra di stampa con la stampante PDF. Per tutti i layout in un solo file c'è *Pubblica*. La cartella proposta, il nome del layout nel nome del file e l'apertura del PDF dopo la stampa si scelgono nelle Opzioni > Stampa e pubblicazione.

## Pubblica (`PUBLISH`, `PUBBLICA`)

**Dove:** Output > Stampa > Pubblica.

Mette tutti i layout del disegno in un solo PDF, un foglio per layout.

## Tabelle stili di stampa (`STYLESMANAGER`, `STILISTAMPA`, `CTB`)

**Dove:** Opzioni > Stampa e pubblicazione (anche da Output > Stampa > Gestione plotter); elenco delle tabelle nel dialogo Stampa.

Gestione delle tabelle `.ctb` (come stampare ogni colore: colore di stampa, spessore, retino): nuova, modifica, copia, rinomina, elimina, importa (anche quelle già presenti sul computer), apri cartella.

## Stampa da script (`-PLOT`, `-STAMPA`)

`-PLOT` seguito su una sola riga da opzioni `chiave=valore`, per esempio:

```
-PLOT AREA=FINESTRA FINESTRA=0,0,5000,3000 SCALA=1:50 FOGLIO=A3 CENTRA=SI STILI=Nero.ctb FILE=dettaglio.pdf
-PLOT SPAZIO=Layout1 FILE=tavola.pdf
```

Opzioni: `SPAZIO` (MODELLO o nome del layout), `IMPOSTAZIONE`, `AREA` (SCHERMO, ESTENSIONI, LIMITI, FINESTRA, LAYOUT), `FINESTRA=x1,y1,x2,y2`, `FOGLIO`, `ORIENTAMENTO` (O/V), `CAPOVOLTA`, `SCALA` (ADATTA, 1:100, 2:1), `CENTRA`, `OFFSET=x,y`, `MARGINI`, `STILI`, `SPESSORI`, `SCALASPESSORI`, `CARTAULTIMA`, `TRASPARENZA`, `TIMBRO`, `DPI`, `COPIE`, `STAMPANTE`, `FILE`, `SALVA`.

## Registro delle stampe

Nelle Opzioni > Stampa e pubblicazione si può tenere un registro delle stampe in CSV, una riga per foglio; Output > Stampa > *Visualizza dettagli* lo apre.
