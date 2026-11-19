#pragma once

// Motore di stampa: dalle impostazioni di stampa (core/Plot.h) al PDF, alla stampante o
// all'anteprima. Usato dal dialogo Stampa, da -PLOT, da PUBLISH e dagli script.

#include "core/Plot.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QPagedPaintDevice;

namespace cartesio {

class DrawingWidget;

// Nome del dispositivo PDF interno (lo stesso, così i DXF restano compatibili).
inline constexpr const char* kPdfDevice = "DWG To PDF.pc3";
inline constexpr const char* kNoDevice = "none_device";
bool isPdfDevice(const QString& device);
bool isNoDevice(const QString& device);
// Nome mostrato per un dispositivo.
QString deviceDisplayName(const QString& device);

// Preferenze di stampa che il DXF R2000 non sa salvare: restano nelle impostazioni del programma.
struct PlotPreferences {
    bool transparency = false;
    bool stamp = false;
    PlotStampSettings stampSettings;

    static PlotPreferences load();
    void save() const;
};

// Una pagina da stampare.
struct PlotJob {
    PlotSettings settings;   // per i layout: layoutPlotSettings()
    int space = 0;           // 0 = modello, altrimenti l'id del layout
    int copies = 1;
    bool transparency = false;
    bool stamp = false;
    PlotStampSettings stampSettings;
};
// Lavoro con le impostazioni salvate nel disegno per quello spazio e le preferenze del programma.
PlotJob plotJobFor(const DrawingWidget& d, int space);

// Parte dello spazio da stampare secondo il tipo di area (Schermo, Estensioni, Limiti, Finestra, Layout).
std::optional<BBox> plotArea(const DrawingWidget& d, const PlotSettings& s, int space, QString* error = nullptr);

// Tabelle stili: quelle predefinite e i file .ctb nella cartella degli stili.
QString plotStylesFolder();
QStringList plotStyleTableNames();
std::shared_ptr<const PlotStyleTable> loadPlotStyleTable(const QString& name, QString* error = nullptr);
bool savePlotStyleTable(const PlotStyleTable& table, const QString& path, QString* error = nullptr);

// Legge un file .ctb (anche).
std::optional<PlotStyleTable> readCtbFile(const QString& path, QString* error = nullptr);

// Controlla prima di stampare che ogni lavoro abbia un'area valida.
bool checkPlotJobs(const DrawingWidget& d, const std::vector<PlotJob>& jobs, QString* error);
// Avvisi che non impediscono la stampa (es. tabella stili non trovata: si usano i colori degli oggetti).
QStringList plotWarnings(const std::vector<PlotJob>& jobs);
// Disegna i lavori sul dispositivo, uno per pagina.
bool renderPlotJobs(DrawingWidget& d, QPagedPaintDevice& device, const std::vector<PlotJob>& jobs, QString* error);
bool plotJobsToPdf(DrawingWidget& d, const QString& path, const std::vector<PlotJob>& jobs, QString* error);
// `outputFile` non vuoto: stampa su file con il driver della stampante.
bool plotJobsToPrinter(DrawingWidget& d, const QString& printer, const QString& outputFile,
                       const std::vector<PlotJob>& jobs, QString* error);
// Immagine del foglio intero; `dotsPerMm` dà la risoluzione.
QImage renderPlotPreview(DrawingWidget& d, const PlotJob& job, double dotsPerMm, QString* error = nullptr);

// Formati carta di una stampante (misure in mm, in verticale) con i margini minimi.
struct PrinterPaper {
    QString key;       // nome di QPageSize
    QString name;      // nome mostrato
    double width = 0.0;
    double height = 0.0;
    double marginLeft = 0.0, marginBottom = 0.0, marginRight = 0.0, marginTop = 0.0;
};
std::vector<PrinterPaper> printerPapers(const QString& printer);

} // namespace cartesio
