// Motore di stampa: area, tabelle stili, pagine su PDF, stampanti e anteprima.
#include "app/PlotEngine.h"

#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/AppOptions.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace cartesio {

namespace {

QSettings store()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

// Impaginazione del dispositivo: formato standard quando le misure corrispondono, con l'orientamento giusto.
QPageLayout pageLayoutFor(double w, double h, bool exact)
{
    const QPageSize size(QSizeF(std::min(w, h), std::max(w, h)), QPageSize::Millimeter, QString(),
                         exact ? QPageSize::ExactMatch : QPageSize::FuzzyMatch);
    return QPageLayout(size, w > h ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                       QPageLayout::Millimeter);
}

QString scaleLabel(double mmPerUnit)
{
    if (mmPerUnit <= 0.0)
        return {};
    const auto fmt = [](double v) { return QString::number(v, 'g', 6); };
    return mmPerUnit <= 1.0 ? QStringLiteral("1:%1").arg(fmt(1.0 / mmPerUnit))
                            : QStringLiteral("%1:1").arg(fmt(mmPerUnit));
}

// Una pagina: il foglio di w x h mm su un dispositivo con `px` punti per mm.
bool paintJob(QPainter& painter, DrawingWidget& d, const PlotJob& job, double px, QString* error)
{
    const auto area = plotArea(d, job.settings, job.space, error);
    if (!area)
        return false;
    const PlotFrame frame = computePlotFrame(job.settings, *area);
    if (!frame.valid) {
        if (error)
            *error = QObject::tr("Impossibile disporre il disegno sul foglio (area o foglio vuoti).");
        return false;
    }
    const double H = frame.sheet.height;
    const QTransform world(px * frame.a, -px * frame.b, px * frame.c, -px * frame.d, px * frame.e,
                           px * (H - frame.f));

    PrintOptions options;
    options.dotsPerMm = px;
    const PlotSettings& s = job.settings;
    const bool styles = s.flag(kPlotPlotStyles) && !s.styleSheet.empty();
    options.lineweights = s.flag(kPlotPrintLineweights) || styles;
    options.lineweightScale = s.flag(kPlotScaleLineweights) ? frame.scale : 1.0;
    options.transparency = job.transparency;
    options.paperSpaceLast = s.flag(kPlotDrawViewportsFirst);
    if (styles) {
        // Tabella mancante: si stampa con i colori degli oggetti; l'avviso lo dà plotWarnings().
        options.styles = loadPlotStyleTable(QString::fromStdString(s.styleSheet));
    }

    // Si stampa solo nell'area stampabile e, per finestra e schermo, solo dentro l'area scelta.
    BBox clip = frame.printable;
    if (s.plotType == PlotType::Window || s.plotType == PlotType::Display) {
        clip.min.x = std::max(clip.min.x, frame.placed.min.x);
        clip.min.y = std::max(clip.min.y, frame.placed.min.y);
        clip.max.x = std::min(clip.max.x, frame.placed.max.x);
        clip.max.y = std::min(clip.max.y, frame.placed.max.y);
    }
    painter.save();
    if (clip.isValid())
        painter.setClipRect(QRectF(clip.min.x * px, (H - clip.max.y) * px, clip.width() * px, clip.height() * px));
    d.view()->renderForPrint(painter, world, *area, job.space, options);
    painter.restore();

    if (job.stamp) {
        PlotStampValues v;
        v.drawingName = (d.filePath().isEmpty() ? d.displayName() : QDir::toNativeSeparators(d.filePath())).toStdString();
        if (job.space == 0)
            v.layoutName = "Modello";
        else if (const Layout* l = d.document().findLayout(job.space))
            v.layoutName = l->name;
        v.dateTime = QDateTime::currentDateTime().toString(QStringLiteral("dd/MM/yyyy HH:mm")).toStdString();
        v.loginName = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")).toStdString();
        v.deviceName = deviceDisplayName(QString::fromStdString(s.device)).toStdString();
        v.paperSize = mediaDisplayName(frame.sheet.width, frame.sheet.height);
        v.plotScale = (isFitToPaper(s) ? scaleLabel(frame.scale) : QString::fromStdString(plotScaleText(s))).toStdString();
        const QString text = QString::fromStdString(plotStampText(job.stampSettings, v));
        if (!text.isEmpty()) {
            QFont font(QStringLiteral("Arial"));
            font.setPixelSize(std::max(1, qRound(job.stampSettings.textHeight * px / 0.72)));
            painter.save();
            painter.setFont(font);
            painter.setPen(Qt::black);
            const QFontMetricsF fm(font, painter.device());
            const double w = fm.horizontalAdvance(text);
            const double W = frame.sheet.width * px, Hpx = H * px;
            const double ox = job.stampSettings.offsetX * px, oy = job.stampSettings.offsetY * px;
            const int corner = job.stampSettings.corner;
            const double x = corner == 2 || corner == 3 ? W - ox - w : ox;
            const double y = corner == 1 || corner == 2 ? oy + fm.ascent() : Hpx - oy - fm.descent();
            painter.drawText(QPointF(x, y), text);
            painter.restore();
        }
    }
    return true;
}

int pdfResolution(const PlotSettings& s)
{
    switch (s.shadeResolution) {
    case 0: return 150;
    case 1: return 300;
    case 3: return 1200;
    case 4: return 2400;
    case 5: return std::clamp(s.shadeDpi, 72, 4800);
    default: return 600;
    }
}

} // namespace

bool isPdfDevice(const QString& device)
{
    return device.isEmpty() || device.compare(QLatin1String(kPdfDevice), Qt::CaseInsensitive) == 0
           || (device.contains(QLatin1String("PDF"), Qt::CaseInsensitive) && device.endsWith(QLatin1String(".pc3"), Qt::CaseInsensitive));
}

bool isNoDevice(const QString& device)
{
    return device.compare(QLatin1String(kNoDevice), Qt::CaseInsensitive) == 0
           || device.compare(QLatin1String("Nessuno"), Qt::CaseInsensitive) == 0
           || device.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0;
}

QString deviceDisplayName(const QString& device)
{
    if (isNoDevice(device))
        return QObject::tr("Nessuno");
    if (isPdfDevice(device))
        return QObject::tr("PDF (CartesioCAD)");
    return device;
}

PlotPreferences PlotPreferences::load()
{
    QSettings s = store();
    s.beginGroup(QStringLiteral("Plot"));
    PlotPreferences p;
    p.transparency = s.value(QStringLiteral("transparency"), false).toBool();
    p.stamp = s.value(QStringLiteral("stamp"), false).toBool();
    PlotStampSettings& t = p.stampSettings;
    t.drawingName = s.value(QStringLiteral("stampDrawing"), t.drawingName).toBool();
    t.layoutName = s.value(QStringLiteral("stampLayout"), t.layoutName).toBool();
    t.dateTime = s.value(QStringLiteral("stampDate"), t.dateTime).toBool();
    t.loginName = s.value(QStringLiteral("stampLogin"), t.loginName).toBool();
    t.deviceName = s.value(QStringLiteral("stampDevice"), t.deviceName).toBool();
    t.paperSize = s.value(QStringLiteral("stampPaper"), t.paperSize).toBool();
    t.plotScale = s.value(QStringLiteral("stampScale"), t.plotScale).toBool();
    t.user1 = s.value(QStringLiteral("stampUser1")).toString().toStdString();
    t.user2 = s.value(QStringLiteral("stampUser2")).toString().toStdString();
    t.corner = s.value(QStringLiteral("stampCorner"), t.corner).toInt();
    t.textHeight = s.value(QStringLiteral("stampHeight"), t.textHeight).toDouble();
    t.offsetX = s.value(QStringLiteral("stampOffsetX"), t.offsetX).toDouble();
    t.offsetY = s.value(QStringLiteral("stampOffsetY"), t.offsetY).toDouble();
    return p;
}

void PlotPreferences::save() const
{
    QSettings s = store();
    s.beginGroup(QStringLiteral("Plot"));
    s.setValue(QStringLiteral("transparency"), transparency);
    s.setValue(QStringLiteral("stamp"), stamp);
    const PlotStampSettings& t = stampSettings;
    s.setValue(QStringLiteral("stampDrawing"), t.drawingName);
    s.setValue(QStringLiteral("stampLayout"), t.layoutName);
    s.setValue(QStringLiteral("stampDate"), t.dateTime);
    s.setValue(QStringLiteral("stampLogin"), t.loginName);
    s.setValue(QStringLiteral("stampDevice"), t.deviceName);
    s.setValue(QStringLiteral("stampPaper"), t.paperSize);
    s.setValue(QStringLiteral("stampScale"), t.plotScale);
    s.setValue(QStringLiteral("stampUser1"), QString::fromStdString(t.user1));
    s.setValue(QStringLiteral("stampUser2"), QString::fromStdString(t.user2));
    s.setValue(QStringLiteral("stampCorner"), t.corner);
    s.setValue(QStringLiteral("stampHeight"), t.textHeight);
    s.setValue(QStringLiteral("stampOffsetX"), t.offsetX);
    s.setValue(QStringLiteral("stampOffsetY"), t.offsetY);
}

PlotJob plotJobFor(const DrawingWidget& d, int space)
{
    PlotJob job;
    job.space = space;
    if (space == 0)
        job.settings = d.document().modelPlot();
    else if (const Layout* l = d.document().findLayout(space))
        job.settings = layoutPlotSettings(*l);
    const PlotPreferences prefs = PlotPreferences::load();
    job.transparency = prefs.transparency;
    job.stamp = prefs.stamp;
    job.stampSettings = prefs.stampSettings;
    return job;
}

std::optional<BBox> plotArea(const DrawingWidget& d, const PlotSettings& s, int space, QString* error)
{
    const Document& doc = d.document();
    const Layout* layout = space != 0 ? doc.findLayout(space) : nullptr;
    const auto fail = [error](const QString& message) -> std::optional<BBox> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (space != 0 && !layout)
        return fail(QObject::tr("Layout inesistente."));
    const auto sheet = [layout] {
        BBox b;
        b.expand(layout->origin);
        b.expand(layout->origin + Vec2{layout->paperWidth, layout->paperHeight});
        return b;
    };
    BBox box;
    switch (s.plotType) {
    case PlotType::Display:
        if (d.shownSpace() == space)
            box = d.view()->visibleArea();
        else
            box = doc.extents(space);
        break;
    case PlotType::Extents:
        box = doc.extents(space);
        if (!box.isValid())
            return fail(QObject::tr("Non c'è niente da stampare: lo spazio è vuoto."));
        break;
    case PlotType::Limits:
        if (layout)
            return sheet();
        box.expand(doc.limitsMin());
        box.expand(doc.limitsMax());
        break;
    case PlotType::View:
        return fail(QObject::tr("Il disegno non ha viste con nome: scegliere un'altra area di stampa."));
    case PlotType::Window:
        box.expand(s.windowMin);
        box.expand(s.windowMax);
        if (box.width() <= 0.0 || box.height() <= 0.0)
            return fail(QObject::tr("La finestra di stampa non è stata scelta (o ha area nulla)."));
        break;
    case PlotType::Layout:
        if (!layout)
            return fail(QObject::tr("L'area \"Layout\" vale solo nei layout: nel modello scegliere Estensioni, Limiti, "
                                    "Schermo o Finestra."));
        return sheet();
    }
    if (!box.isValid() || (box.width() <= 0.0 && box.height() <= 0.0))
        return fail(QObject::tr("L'area di stampa è vuota."));
    return box;
}

// ---------------------------------------------------------------------- tabelle stili

QString plotStylesFolder()
{
    if (!appOptions().plotStylesFolder.isEmpty())
        return appOptions().plotStylesFolder;
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/CartesioCAD");
    return base + QStringLiteral("/PlotStyles");
}

QStringList plotStyleTableNames()
{
    QStringList names;
    for (const std::string& n : builtinPlotStyleTableNames())
        names << QString::fromStdString(n);
    const QDir dir(plotStylesFolder());
    for (const QString& file : dir.entryList({QStringLiteral("*.ctb")}, QDir::Files, QDir::Name))
        if (!names.contains(file, Qt::CaseInsensitive))
            names << file;
    return names;
}

std::optional<PlotStyleTable> readCtbFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(path));
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    std::string message;
    const auto text = ctbText(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), &message);
    if (!text) {
        if (error)
            *error = QString::fromStdString(message);
        return std::nullopt;
    }
    auto table = PlotStyleTable::fromText(*text, &message);
    if (!table && error)
        *error = QString::fromStdString(message);
    return table;
}

bool savePlotStyleTable(const PlotStyleTable& table, const QString& path, QString* error)
{
    QByteArray text = QByteArray::fromStdString(table.toText());
    text.append('\0');
    const QByteArray zlib = qCompress(text, 9).mid(4);   // senza la lunghezza di Qt
    const std::string bytes = ctbFileBytes(std::string_view(zlib.constData(), static_cast<std::size_t>(zlib.size())),
                                           static_cast<std::size_t>(text.size()));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())) {
        if (error)
            *error = QObject::tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

std::shared_ptr<const PlotStyleTable> loadPlotStyleTable(const QString& name, QString* error)
{
    // Prima un file nella cartella degli stili (anche una copia modificata di una predefinita).
    // Un percorso di un altro PC (es. quello salvato da un altro programma): si cerca il file per nome.
    QString path = name;
    if (QFileInfo(path).isRelative() || !QFileInfo::exists(path))
        path = plotStylesFolder() + QLatin1Char('/') + QFileInfo(name).fileName();
    if (QFileInfo::exists(path)) {
        auto table = readCtbFile(path, error);
        return table ? std::make_shared<const PlotStyleTable>(std::move(*table)) : nullptr;
    }
    if (auto builtin = builtinPlotStyleTable(QFileInfo(name).fileName().toStdString()))
        return std::make_shared<const PlotStyleTable>(std::move(*builtin));
    if (error)
        *error = QObject::tr("Tabella stili di stampa non trovata: %1 (cartella %2).")
                     .arg(name, QDir::toNativeSeparators(plotStylesFolder()));
    return nullptr;
}

// ---------------------------------------------------------------------- uscita

bool checkPlotJobs(const DrawingWidget& d, const std::vector<PlotJob>& jobs, QString* error)
{
    if (jobs.empty()) {
        if (error)
            *error = QObject::tr("Niente da stampare.");
        return false;
    }
    for (const PlotJob& job : jobs) {
        if (!plotArea(d, job.settings, job.space, error))
            return false;
    }
    return true;
}

QStringList plotWarnings(const std::vector<PlotJob>& jobs)
{
    QStringList out;
    for (const PlotJob& job : jobs) {
        const PlotSettings& s = job.settings;
        QString error;
        if (s.flag(kPlotPlotStyles) && !s.styleSheet.empty()
            && !loadPlotStyleTable(QString::fromStdString(s.styleSheet), &error)) {
            const QString message = error + QObject::tr(" Si stampa con i colori degli oggetti; per il bianco e nero "
                                                        "scegliere Nero.ctb (o Grigi.ctb) nel dialogo Stampa.");
            if (!out.contains(message))
                out << message;
        }
    }
    return out;
}

bool renderPlotJobs(DrawingWidget& d, QPagedPaintDevice& device, const std::vector<PlotJob>& jobs, QString* error)
{
    if (jobs.empty()) {
        if (error)
            *error = QObject::tr("Niente da stampare.");
        return false;
    }
    const bool exact = dynamic_cast<QPdfWriter*>(&device) != nullptr;
    const auto setPage = [&](const PlotJob& job) {
        const SheetSize sheet = sheetSize(job.settings);
        device.setPageLayout(pageLayoutFor(sheet.width, sheet.height, exact));
        return sheet;
    };
    SheetSize sheet = setPage(jobs.front());
    QPainter painter;
    if (!painter.begin(&device)) {
        if (error)
            *error = QObject::tr("Impossibile avviare la stampa.");
        return false;
    }
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        if (i > 0) {
            sheet = setPage(jobs[i]);
            device.newPage();
        }
        const double px = device.width() / sheet.width;   // punti del dispositivo per millimetro
        if (!paintJob(painter, d, jobs[i], px, error)) {
            painter.end();
            return false;
        }
    }
    painter.end();
    return true;
}

// Registro delle stampe (Opzioni > Stampa): una riga CSV per foglio, con separatore ";" per Excel.
static void logPlot(const DrawingWidget& d, const std::vector<PlotJob>& jobs, const QString& device, const QString& output)
{
    if (!appOptions().plotLog)
        return;
    const QString path = plotLogFile();
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    const bool fresh = !file.exists() || file.size() == 0;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const auto field = [](QString v) {
        v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + v + QLatin1Char('"');
    };
    QByteArray out;
    if (fresh)
        out += "\xEF\xBB\xBF" "Data;Disegno;Spazio;Dispositivo;Foglio;Scala;Tabella stili;File\n";
    const QString when = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString drawing = d.filePath().isEmpty() ? d.displayName() : QDir::toNativeSeparators(d.filePath());
    for (const PlotJob& job : jobs) {
        const Layout* layout = d.document().findLayout(job.space);
        const SheetSize sheet = sheetSize(job.settings);
        const QStringList row = {
            when, drawing, layout ? QString::fromStdString(layout->name) : QObject::tr("Modello"), deviceDisplayName(device),
            QString::fromStdString(mediaDisplayName(sheet.width, sheet.height)),
            QString::fromStdString(plotScaleText(job.settings)), QString::fromStdString(job.settings.styleSheet),
            output.isEmpty() ? QString() : QDir::toNativeSeparators(output)};
        QStringList quoted;
        for (const QString& v : row)
            quoted << field(v);
        out += quoted.join(QLatin1Char(';')).toUtf8() + '\n';
    }
    file.write(out);
}

bool plotJobsToPdf(DrawingWidget& d, const QString& path, const std::vector<PlotJob>& jobs, QString* error)
{
    QString message;
    // Prima si controlla che tutto si possa stampare: QPdfWriter crea il file appena si inizia.
    if (!checkPlotJobs(d, jobs, error))
        return false;
    QPdfWriter pdf(path);
    pdf.setCreator(QStringLiteral("CartesioCAD"));
    pdf.setTitle(d.displayName());
    pdf.setResolution(jobs.empty() ? 600 : pdfResolution(jobs.front().settings));
    if (!renderPlotJobs(d, pdf, jobs, &message)) {
        if (error)
            *error = message.isEmpty() ? QObject::tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(path)) : message;
        return false;
    }
    logPlot(d, jobs, QString::fromLatin1(kPdfDevice), path);
    return true;
}

bool plotJobsToPrinter(DrawingWidget& d, const QString& printerName, const QString& outputFile,
                       const std::vector<PlotJob>& jobs, QString* error)
{
    if (!checkPlotJobs(d, jobs, error))
        return false;
    QPrinter printer(QPrinter::HighResolution);
    printer.setPrinterName(printerName);
    if (!printer.isValid()) {
        if (error)
            *error = QObject::tr("La stampante \"%1\" non è disponibile.").arg(printerName);
        return false;
    }
    printer.setFullPage(true);   // coordinate dall'angolo del foglio, come nel PDF
    printer.setDocName(d.displayName());
    printer.setCopyCount(jobs.empty() ? 1 : std::max(1, jobs.front().copies));
    if (!outputFile.isEmpty())
        printer.setOutputFileName(outputFile);
    if (!renderPlotJobs(d, printer, jobs, error))
        return false;
    logPlot(d, jobs, printerName, outputFile);
    return true;
}

QImage renderPlotPreview(DrawingWidget& d, const PlotJob& job, double dotsPerMm, QString* error)
{
    const SheetSize sheet = sheetSize(job.settings);
    const QSize size(std::max(1, qRound(sheet.width * dotsPerMm)), std::max(1, qRound(sheet.height * dotsPerMm)));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    if (!paintJob(painter, d, job, dotsPerMm, error)) {
        painter.end();
        return {};
    }
    painter.end();
    return image;
}

std::vector<PrinterPaper> printerPapers(const QString& printerName)
{
    std::vector<PrinterPaper> out;
    const QPrinterInfo info = QPrinterInfo::printerInfo(printerName);
    if (info.isNull())
        return out;
    QPrinter printer(info, QPrinter::HighResolution);
    for (const QPageSize& size : info.supportedPageSizes()) {
        const QSizeF mm = size.size(QPageSize::Millimeter);
        if (mm.width() <= 0 || mm.height() <= 0)
            continue;
        PrinterPaper p;
        p.key = size.key();
        p.name = size.name();
        p.width = std::min(mm.width(), mm.height());
        p.height = std::max(mm.width(), mm.height());
        if (printer.setPageSize(size)) {
            QPageLayout layout = printer.pageLayout();
            layout.setUnits(QPageLayout::Millimeter);   // converte anche i margini
            const QMarginsF m = layout.minimumMargins();
            const double k = 1.0;
            p.marginLeft = m.left() * k;
            p.marginTop = m.top() * k;
            p.marginRight = m.right() * k;
            p.marginBottom = m.bottom() * k;
        }
        out.push_back(p);
    }
    return out;
}

} // namespace cartesio
