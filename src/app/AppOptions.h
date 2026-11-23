#pragma once

// Opzioni del programma (comando OPZIONI), salvate nelle impostazioni dell'utente e valide per
// tutti i disegni. Le schede sono quelle: Apri e salva, Stampa, Preferenze utente,
// Disegno, Selezione.

#include "app/DraftSettings.h"
#include "core/Plot.h"

#include <QColor>
#include <QString>

namespace cartesio {

struct AppOptions {
    // --- Apri e salva
    enum SaveFormat { SaveDxfR12 = 0, SaveDxf2000 = 1, SaveDwg = 2 };
    int saveFormat = SaveDwg;          // formato dei disegni nuovi (nome e "Salva con nome")
    bool autosave = true;
    int autosaveMinutes = 10;
    bool backup = true;                // copia .bak del file precedente a ogni salvataggio
    int recentFiles = 10;
    bool startPageAtLaunch = true;     // all'avvio la pagina Inizia (STARTMODE); no = disegno nuovo
    enum DwgEngine { DwgAuto = 0, DwgLibre = 1, DwgOda = 2 };
    int dwgEngine = DwgAuto;           // automatico: ODA se installato, altrimenti LibreDWG
    QString odaPath;                   // vuoto = cerca in C:\Program Files\ODA
    QString odaDwgVersion = QStringLiteral("ACAD2018");   // versione dei DWG scritti con ODA

    // --- Visualizza: elementi del layout
    bool showLayoutTabs = true;        // schede Modello e Layout nella barra di stato
    bool showFileTabs = true;          // schede dei disegni aperti sotto la barra multifunzione
    bool groupLayoutTabs = false;      // schede dei layout raggruppate (una per gruppo)
    bool showUcsIcon = true;           // icona degli assi nell'angolo della vista (UCSICON)
    bool showNavigationBar = true;     // pulsanti di navigazione sul disegno (pan e zoom)
    bool showPrintableArea = true;     // area stampabile tratteggiata
    bool showPaperBackground = true;   // foglio bianco su fondo scuro (altrimenti tutto bianco)
    bool showPaperShadow = true;
    QColor layoutBackground = QColor(58, 62, 70);   // fondo intorno al foglio
    QColor modelBackground = QColor(33, 40, 48);    // sfondo dello spazio modello
    QColor paperColor = QColor(255, 255, 255);      // foglio dei layout
    QColor gridColor = QColor(255, 255, 255);       // griglia (le linee secondarie più tenui)
    bool pageSetupForNewLayouts = false;  // apre Imposta pagina quando si crea un layout
    bool viewportInNewLayouts = true;     // una finestra su tutto il modello nei layout nuovi
    // Foglio dei layout nuovi.
    QString newLayoutPaper = QStringLiteral("ISO A3");   // nome dei formati di stampa
    bool newLayoutLandscape = true;
    double newLayoutMargin = 10.0;     // mm (area non stampabile)

    // --- Stampa
    QString defaultDevice;             // vuoto = PDF
    QString defaultStyleSheet;         // tabella stili dei disegni nuovi (vuoto = nessuna)
    QString plotStylesFolder;          // vuoto = cartella del programma
    QString templatesFolder;           // modelli dei disegni nuovi (.dwt/.dwg/.dxf); vuoto = cartella Modelli nei dati del programma
    // Percorso dei riferimenti esterni e delle immagini collegati: 0 relativo al disegno, 1 completo, 2 solo il nome.
    int refPathType = 0;
    // Testi TrueType sulla GPU: 0 curve (nitidi e stabili a ogni zoom), 1 distance field (il vecchio metodo).
    int textRendering = 0;
    // Interfaccia: 0 finestra unica in QML, 1 classica a widget con i dock (vale al riavvio).
    int uiMode = 0;
    // Controllo degli aggiornamenti all'avvio (al massimo una volta al giorno): spento di default, perché collega il
    // programma a GitHub senza un'azione dell'utente.
    bool checkUpdatesAtStart = false;
    // Assistente IA, connettore per Claude Desktop (canale locale, comando ASSISTENTE): spento di default.
    bool claudeConnector = false;
    QString fontFolders;               // cartelle dei font SHX e TTF (separate da ;), oltre a "fonts" accanto al programma e a Windows
    // Cerca i font SHX anche nelle cartelle Fonts degli altri programmi installati: spento di default, perché
    // quei font appartengono a chi li ha prodotti e la loro licenza può non permetterne l'uso altrove.
    bool fontsOfInstalledPrograms = false;
    bool hideSystemPrinters = false;   // solo PDF negli elenchi dei dispositivi
    // Opzioni di stampa dei disegni e dei layout nuovi.
    bool plotLineweights = true;
    bool plotWithStyles = true;
    bool plotPaperSpaceLast = true;
    bool plotHidePaperObjects = false;
    // Stampa su file.
    QString plotFolder;                // cartella proposta per i PDF; vuoto = quella del disegno
    bool plotFileNameWithLayout = true;   // "Disegno - Layout1.pdf"
    bool openAfterPlot = false;        // apre il PDF appena creato
    bool plotLog = false;              // registro delle stampe (CSV)
    QString plotLogPath;               // vuoto = "Registro stampe.csv" nei dati del programma

    // --- Preferenze utente
    bool doubleClickEdit = true;       // doppio clic su un oggetto: Proprietà
    bool rightClickMenu = true;        // tasto destro senza comando attivo: menu (altrimenti Invio)
    int insertSourceUnits = 4;         // unità di un file inserito senza unità (codici $INSUNITS, 4 = mm)
    int insertTargetUnits = 4;         // unità del disegno corrente se non ne ha

    // --- Plugin CanaloCAD (scambio di file con l'app web delle canaline)
    QString canaloPath;                // Canalo.html; vuoto = quello incluso (plugins/CanaloCAD)
    QString canaloFolder;              // cartella di scambio; vuoto = Download
    bool canaloWatch = true;           // avvisa quando CanaloCAD esporta una tavola nella cartella

    // --- Disegno
    bool snapMarker = true;
    int snapMarkerSize = 6;            // pixel
    QColor snapMarkerColor = QColor(255, 196, 0);
    int snapAperture = 10;             // pixel: raggio entro cui scatta lo snap
    bool showAperture = false;

    // --- Selezione
    int pickboxSize = 6;               // pixel
    bool systemCursor = true;          // mirino come cursore di Windows: segue il mouse senza ritardo
    int gripSize = 4;                  // pixel (metà lato)
    QColor gripColor = QColor(40, 120, 255);
    bool pickFirst = true;             // oggetti selezionati prima del comando (nome/verbo)
    bool pressAndDrag = true;          // finestra di selezione trascinando
    bool quickProperties = false;      // Proprietà rapide a ogni selezione (QPMODE; spente di default)

    // Aiuti al disegno della barra di stato (anche loro ricordati fra una sessione e l'altra).
    DraftSettings draft;

    static AppOptions load();
    void save() const;
};

// Opzioni correnti (caricate all'avvio, cambiate dalla finestra Opzioni).
AppOptions& appOptions();

// Impostazioni di stampa con le opzioni dei disegni nuovi (dispositivo, tabella stili, spessori...).
void applyPlotDefaults(PlotSettings& s);
// Impostazioni di un layout nuovo: foglio, orientamento e margine dalle opzioni.
PlotSettings newLayoutPlotSettings();
// File del registro delle stampe.
QString plotLogFile();

// Fattore da un'unità di disegno a un'altra (codici di $INSUNITS); 1 se una è "senza unità".
double unitsFactor(int fromUnits, int toUnits);
// Metri per unità del disegno per l'impianto: quelle impostate nel disegno, altrimenti $INSUNITS, altrimenti le
// unità predefinite delle Opzioni.
double plantMetersPerUnit(const class Document& doc);
// Nome delle unità ($INSUNITS) per gli elenchi.
QString unitsName(int code);

} // namespace cartesio
