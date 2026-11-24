#include "app/OptionsDialog.h"

#include "app/CanaloPlugin.h"
#include "app/DwgConverter.h"
#include "app/ExternalRefs.h"
#include "app/PlotDialog.h"
#include "app/PlotEngine.h"
#include "app/StartPage.h"
#include "app/TextRender.h"

#include <QDir>
#include <QPrinterInfo>

namespace cartesio {

namespace {

QList<QPair<QString, QVariant>> unitsItems()
{
    QList<QPair<QString, QVariant>> items;
    for (int code : {0, 4, 5, 14, 6, 7, 1, 2, 10})
        items.append({unitsName(code), code});
    return items;
}

} // namespace

OptionsDialog::OptionsDialog(QWidget* parent)
    : FormDialog(tr("Opzioni"), Ok | Cancel | Apply, parent), options_(appOptions()), stampOn_(PlotPreferences::load().stamp)
{
    displayTab();
    openSaveTab();
    plotTab();
    userTab();
    draftingTab();
    selectionTab();
    pluginTab();
    onApply = [this] { apply(); };
    setSize(720, 620);
}

void OptionsDialog::apply()
{
    // Gli aiuti al disegno li gestisce la barra di stato: non si toccano.
    options_.draft = appOptions().draft;
    appOptions() = options_;
    appOptions().save();
    PlotPreferences prefs = PlotPreferences::load();
    prefs.stamp = stampOn_;
    prefs.save();
    FontLibrary::instance().reset();   // le cartelle dei font possono essere cambiate
    emit applied();
}

void OptionsDialog::displayTab()
{
    page(tr("Visualizza"));
    group(tr("Elementi del layout"));
    check(tr("Visualizza schede Layout e Modello"), options_.showLayoutTabs);
    check(tr("Visualizza area stampabile"), options_.showPrintableArea);
    const int background = check(tr("Visualizza sfondo carta"), options_.showPaperBackground);
    enableWhen(check(tr("Visualizza ombreggiatura carta"), options_.showPaperShadow, 1), background);
    check(tr("Mostra Gestione impostazioni pagina per i nuovi layout"), options_.pageSetupForNewLayouts);
    check(tr("Crea finestra nei nuovi layout"), options_.viewportInNewLayouts);

    group(tr("Foglio dei nuovi layout"));
    QList<QPair<QString, QVariant>> papers;
    for (const MediaFormat& m : plotMediaFormats())
        papers.append({QStringLiteral("%1 (%2 x %3 mm)").arg(QString::fromLatin1(m.name)).arg(m.width).arg(m.height),
                       QString::fromLatin1(m.name)});
    choice(tr("Formato carta:"), papers, options_.newLayoutPaper,
           [this](const QVariant& v) { options_.newLayoutPaper = v.toString(); });
    choice(tr("Orientamento:"), {{tr("Orizzontale"), true}, {tr("Verticale"), false}}, options_.newLayoutLandscape,
           [this](const QVariant& v) { options_.newLayoutLandscape = v.toBool(); });
    number(tr("Margine non stampabile:"), 0.0, 50.0, 1, options_.newLayoutMargin, QStringLiteral(" mm"));

    group(tr("Interfaccia"));
    choice(tr("Finestra principale:"),
           {{tr("Finestra unica in QML"), 0}, {tr("Classica a widget, con le tavolozze nei dock"), 1}},
           options_.uiMode, [this](const QVariant& v) { options_.uiMode = v.toInt(); });
    note(tr("Il cambio vale al prossimo avvio. Se il disegno si vede male (testi fuori posto, linee spezzate), "
            "provare l'altra interfaccia e l'altro metodo dei testi qui sotto; il comando DIAGGPU dice scheda grafica "
            "e driver."));

    group(tr("Testi"));
    choice(tr("Disegno dei testi TrueType:"),
           {{tr("Curve (nitidi e stabili a ogni zoom)"), 0}, {tr("Distance field (metodo precedente)"), 1}},
           options_.textRendering, [this](const QVariant& v) { options_.textRendering = v.toInt(); });

    group(tr("Colori"));
    color(tr("Sfondo dello spazio modello:"), options_.modelBackground);
    color(tr("Sfondo dei layout (intorno al foglio):"), options_.layoutBackground);
    color(tr("Foglio dei layout:"), options_.paperColor);
    color(tr("Griglia:"), options_.gridColor);
    note(tr("Con uno sfondo chiaro gli oggetti bianchi (colore 7) si disegnano neri, come sul foglio. "
            "Predefiniti: modello #212830, layout #3a3e46, foglio bianco, griglia bianca."));
    note(tr("Foglio, dispositivo e opzioni di stampa di ogni layout si cambiano poi con Imposta pagina; "
            "le opzioni di stampa dei layout nuovi sono nella scheda Stampa e pubblicazione."));
}

void OptionsDialog::openSaveTab()
{
    page(tr("Apri e salva"));
    group(tr("Salvataggio file"));
    choice(tr("Formato dei disegni nuovi:"),
           {{tr("DXF R12 - massima compatibilità"), AppOptions::SaveDxfR12},
            {tr("DXF 2000"), AppOptions::SaveDxf2000},
            {tr("DWG"), AppOptions::SaveDwg}},
           options_.saveFormat, [this](const QVariant& v) { options_.saveFormat = v.toInt(); });

    group(tr("Misure di sicurezza dei file"));
    const int autosave = check(tr("Salvataggio automatico"), options_.autosave);
    enableWhen(integer(tr("Ogni:"), 1, 240, options_.autosaveMinutes, tr(" minuti")), autosave);
    check(tr("Crea copia di backup (.bak) a ogni salvataggio"), options_.backup);
    note(tr("Le copie automatiche dei disegni modificati vanno in %1")
             .arg(QDir::toNativeSeparators(QDir::tempPath() + QStringLiteral("/CartesioCAD"))));

    group(tr("Aggiornamenti"));
    check(tr("Controlla gli aggiornamenti all'avvio (al massimo una volta al giorno)"), options_.checkUpdatesAtStart);
    note(tr("Spento, CartesioCAD non si collega mai a internet da solo: gli aggiornamenti si cercano dal menu "
            "dell'applicazione (Controlla aggiornamenti) o con il comando AGGIORNAMENTI. Il controllo legge l'elenco delle "
            "versioni su GitHub, che vede l'indirizzo IP del computer. Si installano solo pacchetti firmati dall'autore; "
            "la versione precedente resta disponibile (Torna alla versione precedente)."));

    group(tr("Apertura file"));
    integer(tr("Numero di file usati di recente:"), 0, 50, options_.recentFiles);
    check(tr("All'avvio mostra la pagina Inizia (altrimenti un disegno nuovo vuoto)"), options_.startPageAtLaunch);

    group(tr("Modelli dei disegni"));
    path(tr("Cartella dei modelli:"), options_.templatesFolder, Folder, QDir::toNativeSeparators(defaultTemplatesFolder()));
    note(tr("I file .dwt, .dwg e .dxf di questa cartella compaiono nell'elenco Modelli della pagina Inizia: "
            "il disegno nuovo parte da una copia del modello (layer, stili, cartiglio...)."));

    group(tr("Convertitore DWG"));
    choice(tr("Convertitore:"),
           {{tr("Automatico: ODA File Converter se installato, altrimenti LibreDWG"), AppOptions::DwgAuto},
            {tr("LibreDWG (incluso, open source)"), AppOptions::DwgLibre},
            {tr("ODA File Converter"), AppOptions::DwgOda}},
           options_.dwgEngine, [this](const QVariant& v) { options_.dwgEngine = v.toInt(); });
    path(tr("ODA File Converter:"), options_.odaPath, OpenFile, tr("ricerca automatica in Programmi\\ODA"),
         tr("Programmi (*.exe)"));
    QList<QPair<QString, QVariant>> versions;
    for (const char* v : {"ACAD2018", "ACAD2013", "ACAD2010", "ACAD2007", "ACAD2004", "ACAD2000"})
        versions.append({QString::fromLatin1(v).replace(QLatin1String("ACAD"), QStringLiteral("DWG ")), QString::fromLatin1(v)});
    choice(tr("Versione dei DWG salvati con ODA:"), versions, options_.odaDwgVersion,
           [this](const QVariant& v) { options_.odaDwgVersion = v.toString(); });
    const QString found = DwgConverter::odaPath();
    note(found.isEmpty() ? tr("ODA File Converter non trovato. È gratuito (non open source): "
                              "<a href=\"https://www.opendesign.com/guestfiles/oda_file_converter\">opendesign.com</a>. "
                              "Legge meglio dei DWG recenti, compresi i layout.")
                         : tr("Trovato: %1").arg(QDir::toNativeSeparators(found)));

    group(tr("Riferimenti esterni e immagini"));
    choice(tr("Tipo di percorso dei file collegati:"),
           {{refPathTypeName(RefPathRelative), RefPathRelative},
            {refPathTypeName(RefPathFull), RefPathFull},
            {refPathTypeName(RefPathNone), RefPathNone}},
           options_.refPathType, [this](const QVariant& v) { options_.refPathType = v.toInt(); });
    note(tr("Proposto quando si collega un disegno (XATTACH) o un'immagine (IMAGEATTACH); si può sempre scegliere "
            "l'altro e cambiarlo dopo in XRIF. Salvando il disegno in un'altra cartella i percorsi relativi si aggiornano."));

    // Font dei testi: SHX e TrueType; anche nell'installazione e in Windows.
    // Le stesse cartelle servono a trovare immagini raster e riferimenti esterni spostati.
    group(tr("Font dei testi (SHX e TrueType) e file di supporto"));
    path(tr("Cartelle:"), options_.fontFolders, AddFolder, tr("cartelle aggiuntive, separate da ;"));
    check(tr("Usa anche i font SHX degli altri programmi installati"), options_.fontsOfInstalledPrograms);
    note(tr("I font SHX (romans.shx, simplex.shx...) si cercano qui, nella cartella \"fonts\" accanto al programma e "
            "in Windows; un font mancante si sostituisce con simplex.shx o, se non c'è, con il font a tratti di CartesioCAD. CartesioCAD non "
            "distribuisce font SHX. I font installati da altri programmi appartengono a chi li ha prodotti: prima "
            "di usarli qui controllare che la loro licenza lo permetta. Immagini raster e riferimenti esterni "
            "non trovati al percorso salvato si cercano nella cartella del disegno, nelle sue sottocartelle e qui."));
}

void OptionsDialog::plotTab()
{
    page(tr("Stampa e pubblicazione"));
    group(tr("Opzioni di stampa predefinite per i disegni e i layout nuovi"));
    QList<QPair<QString, QVariant>> devices{{tr("PDF (CartesioCAD)"), QString()}};
    for (const QString& name : QPrinterInfo::availablePrinterNames())
        devices.append({name, name});
    choice(tr("Stampante/plotter:"), devices, options_.defaultDevice,
           [this](const QVariant& v) { options_.defaultDevice = v.toString(); });
    const auto styleItems = [this] {
        QList<QPair<QString, QVariant>> styles{{tr("Nessuna"), QString()}};
        for (const QString& name : plotStyleTableNames())
            styles.append({name, name});
        return styles;
    };
    const int styleField = choice(tr("Tabella stili di stampa:"), styleItems(), options_.defaultStyleSheet,
                                  [this](const QVariant& v) { options_.defaultStyleSheet = v.toString(); });
    buttonsRow({{tr("Gestione tabelle stili (.ctb)..."), [this, styleField, styleItems] {
                     showPlotStyleManager(this);
                     setChoices(styleField, styleItems(), options_.defaultStyleSheet);
                 }},
                {tr("Modifica la tabella scelta..."), [this, styleField, styleItems] {
                     const QString saved = options_.defaultStyleSheet.isEmpty() ? QString()
                                                                                 : editPlotStyleTable(this, options_.defaultStyleSheet);
                     if (options_.defaultStyleSheet.isEmpty())
                         showPlotStyleManager(this);
                     if (!saved.isEmpty())
                         options_.defaultStyleSheet = saved;
                     setChoices(styleField, styleItems(), options_.defaultStyleSheet);
                 }}});
    check(tr("Stampa spessori di linea"), options_.plotLineweights);
    check(tr("Stampa con stili di stampa"), options_.plotWithStyles);
    check(tr("Stampa lo spazio carta per ultimo"), options_.plotPaperSpaceLast);
    check(tr("Nascondi oggetti dello spazio carta"), options_.plotHidePaperObjects);

    group(tr("Opzioni generali di stampa"));
    check(tr("Nascondi le stampanti di sistema (solo PDF negli elenchi)"), options_.hideSystemPrinters);
    check(tr("Timbro di stampa attivo (i campi si scelgono nel dialogo Stampa)"), stampOn_);

    group(tr("Stampa su file (PDF)"));
    path(tr("Cartella proposta:"), options_.plotFolder, Folder, tr("cartella del disegno"));
    check(tr("Aggiungi il nome del layout al nome del file"), options_.plotFileNameWithLayout);
    check(tr("Apri il PDF dopo la stampa"), options_.openAfterPlot);

    group(tr("Registro di stampa"));
    const int log = check(tr("Salva automaticamente il registro delle stampe (una riga per foglio)"), options_.plotLog);
    enableWhen(path(tr("File:"), options_.plotLogPath, SaveFile, QDir::toNativeSeparators(plotLogFile()), tr("CSV (*.csv)")), log);

    group(tr("Tabelle stili di stampa (.ctb)"));
    path(tr("Cartella:"), options_.plotStylesFolder, Folder, QDir::toNativeSeparators(plotStylesFolder()));
    buttonsRow({{tr("Gestione tabelle stili di stampa..."), [this] { showPlotStyleManager(this); }}});
    note(tr("Le tabelle sono file .ctb: si creano e si modificano qui o dal dialogo Stampa, e si possono "
            "importare da un file scelto da te."));
}

void OptionsDialog::userTab()
{
    page(tr("Preferenze utente"));
    group(tr("Funzionamento standard di Windows"));
    check(tr("Modifica mediante doppio clic (apre le Proprietà dell'oggetto)"), options_.doubleClickEdit);
    check(tr("Menu di scelta rapida nell'area di disegno (senza comando attivo; durante un comando il tasto destro è Invio)"),
          options_.rightClickMenu);

    group(tr("Scala di inserimento"));
    note(tr("Impostazioni di default per le unità impostate su Senza unità:"));
    choice(tr("Unità disegno di origine:"), unitsItems(), options_.insertSourceUnits,
           [this](const QVariant& v) { options_.insertSourceUnits = v.toInt(); });
    choice(tr("Unità disegno di destinazione:"), unitsItems(), options_.insertTargetUnits,
           [this](const QVariant& v) { options_.insertTargetUnits = v.toInt(); });
    note(tr("Inserendo un file (INSERTFILE, CanaloCAD) con unità diverse da quelle del disegno, il blocco viene scalato. "
            "Le unità dei disegni nuovi sono quelle di destinazione. I riferimenti esterni si scalano solo se entrambi "
            "i disegni dichiarano le unità."));
}

void OptionsDialog::draftingTab()
{
    page(tr("Disegno"));
    group(tr("Impostazioni dello snap"));
    check(tr("Marcatore"), options_.snapMarker);
    check(tr("Mostra il mirino dello snap"), options_.showAperture);
    color(tr("Colore marcatore:"), options_.snapMarkerColor);
    slider(tr("Dimensione marcatore:"), 3, 15, options_.snapMarkerSize);
    slider(tr("Dimensione mirino:"), 3, 30, options_.snapAperture);
    note(tr("Modi di snap, puntamento polare e incremento angolare si cambiano dalla barra di stato (tasto destro sui "
            "pulsanti) e vengono ricordati alla chiusura."));
}

void OptionsDialog::selectionTab()
{
    page(tr("Selezione"));
    group(tr("Selezione"));
    slider(tr("Dimensione mirino di selezione:"), 2, 15, options_.pickboxSize);
    check(tr("Mirino come cursore di Windows (segue il mouse senza ritardo, come il puntatore normale)"),
          options_.systemCursor);
    check(tr("Selezione nome/verbo (i comandi usano gli oggetti già selezionati)"), options_.pickFirst);
    check(tr("Selezione mediante trascinamento (lazo; diritto = finestra)"), options_.pressAndDrag);
    check(tr("Proprietà rapide a ogni selezione (altrimenti dal tasto destro)"), options_.quickProperties);
    group(tr("Maniglie"));
    slider(tr("Dimensione maniglie:"), 2, 10, options_.gripSize);
    color(tr("Colore maniglie:"), options_.gripColor);
}

void OptionsDialog::pluginTab()
{
    page(tr("Plugin"));
    group(tr("CanaloCAD - canaline portacavi"));
    path(tr("Canalo.html:"), options_.canaloPath, OpenFile, tr("quello incluso in CartesioCAD"), tr("Pagine HTML (*.html)"));
    const QString found = canaloHtmlPath();
    note(found.isEmpty() ? tr("CanaloCAD non trovato.") : tr("In uso: %1").arg(QDir::toNativeSeparators(found)));
    path(tr("Cartella di scambio:"), options_.canaloFolder, Folder, tr("Download"));
    check(tr("Avvisa quando CanaloCAD esporta una tavola nella cartella"), options_.canaloWatch);
    note(tr("Su Windows CanaloCAD si apre in una scheda accanto ai disegni: \"Invia sezione\" vi apre direttamente gli "
            "oggetti selezionati e in CanaloCAD Esporta > \"Inserisci nel disegno\" riporta la tavola (una copia va nella "
            "cartella di scambio). Se la scheda non è disponibile CanaloCAD si apre nel browser e lo scambio passa dai "
            "file: \"Invia sezione\" salva un DXF da importare, \"Inserisci tavola\" e \"Ultima tavola\" inseriscono le "
            "tavole DXF esportate, scalate con le unità del disegno."));
}

} // namespace cartesio
