// Conversione in serie (DWGCONVERT / CONVERTI): tutti i disegni di una cartella (anche nelle sottocartelle) in
// DXF 2000, DXF R12, DWG o PDF, con verifica facoltativa (il file scritto si rilegge e si confronta) e un rapporto
// nella cartella di destinazione. Ogni disegno si legge in un altro thread: la finestra resta viva.
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/ConvertFile.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlotEngine.h"
#include "core/DataExport.h"
#include "core/DocCompare.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QSettings>
#include <QTextStream>
#include <QThread>

#include <memory>
#include <set>

namespace cartesio {

namespace {

QSettings convertSettings()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

QString formatExtension(int format)
{
    return format == MainWindow::ConvertPdf ? QStringLiteral("pdf")
           : format == MainWindow::ConvertDwg ? QStringLiteral("dwg")
                                              : QStringLiteral("dxf");
}

QString formatName(int format)
{
    switch (format) {
    case MainWindow::ConvertDxfR12: return QObject::tr("DXF R12");
    case MainWindow::ConvertDwg: return QObject::tr("DWG");
    case MainWindow::ConvertPdf: return QObject::tr("PDF");
    default: return QObject::tr("DXF 2000");
    }
}

// Legge un disegno in un altro thread aspettando con il ciclo degli eventi (la finestra si ridisegna).
bool readInBackground(const QString& path, Document& doc, QString* error, DxfReadResult* result)
{
    bool ok = false;
    QEventLoop loop;
    QThread* thread = QThread::create([&] { ok = DrawingWidget::readFile(path, doc, error, result); });
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    return ok;
}

} // namespace

int convertFormatFromPath(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("dxf") ? ConvertFileDxf2000
           : ext == QLatin1String("dwg") ? ConvertFileDwg
           : ext == QLatin1String("pdf") ? ConvertFilePdf
                                         : -1;
}

ConvertFileResult convertDrawingFile(const QString& file, const QString& destination, int format, bool verify,
                                     const DraftSettings& settings, bool inThread)
{
    ConvertFileResult r;
    Document doc;
    DxfReadResult result;
    const bool read = inThread ? readInBackground(file, doc, &r.error, &result)
                               : DrawingWidget::readFile(file, doc, &r.error, &result);
    if (!read)
        return r;
    DrawingWidget::loadXrefs(file, doc, result);
    // Un disegno nascosto fa da tramite: stessa scrittura (DXF/DWG) e stessa stampa (PDF) dei comandi normali.
    DrawingWidget drawing(settings, QFileInfo(file).fileName());
    drawing.adopt(std::move(doc), file, result, nullptr);
    const DocSummary before = summarizeDocument(drawing.document());
    r.objects = before.total;
    r.stage = ConvertFileResult::Write;
    bool ok = false;
    if (format == ConvertFilePdf) {
        // I layout con qualcosa sopra (i Layout1/Layout2 vuoti dei modelli no); nessuno: il modello.
        std::set<int> used;
        for (const auto& e : drawing.document().entities())
            if (e->space() > 0)
                used.insert(e->space());
        std::vector<PlotJob> jobs;
        for (const Layout& l : drawing.document().layouts())
            if (used.count(l.id))
                jobs.push_back(plotJobFor(drawing, l.id));
        if (jobs.empty()) {
            PlotJob model = plotJobFor(drawing, 0);
            // Senza schermo "Schermo" e "Vista" non hanno senso: estensioni adattate al foglio, centrate, con
            // l'orientamento del disegno.
            if (model.settings.plotType == PlotType::Display || model.settings.plotType == PlotType::View
                || model.settings.plotType == PlotType::Layout) {
                model.settings.plotType = PlotType::Extents;
                model.settings.setFlag(kPlotUseStandardScale, true);
                model.settings.standardScaleType = 0;
                model.settings.setFlag(kPlotCentered, true);
                const BBox ext = drawing.document().extents(0);
                if (ext.isValid())
                    setOrientation(model.settings, ext.width() >= ext.height(), false);
            }
            jobs.push_back(model);
        }
        ok = plotJobsToPdf(drawing, destination, jobs, &r.error);
    } else {
        const DxfVersion version = format == ConvertFileDxfR12 ? DxfVersion::R12 : DxfVersion::R2000;
        ok = drawing.saveTo(destination, &r.error, version);
    }
    if (!ok)
        return r;
    r.stage = ConvertFileResult::Done;
    if (verify && format != ConvertFilePdf) {
        Document again;
        QString readError;
        const bool reread = inThread ? readInBackground(destination, again, &readError, nullptr)
                                     : DrawingWidget::readFile(destination, again, &readError);
        if (!reread) {
            r.verifyProblem = readError;
        } else {
            const DocComparison c = compareSummaries(before, summarizeDocument(again));
            if (c.identical()) {
                r.verified = true;
            } else {
                r.verifyProblem = QString::number(c.differences.size());
                const QString detail = QString::fromStdString(c.report(8));
                for (const QString& d : detail.split(QLatin1Char('\n')).mid(1))
                    if (!d.trimmed().isEmpty())
                        r.differences << d.trimmed();
                if (r.differences.isEmpty())
                    r.differences << QObject::tr("%1 differenze").arg(c.differences.size());
            }
        }
    }
    return r;
}

void MainWindow::showBatchConvert()
{
    QSettings s = convertSettings();
    BatchConvertOptions o;
    o.source = s.value(QStringLiteral("BatchConvert/source")).toString();
    o.output = s.value(QStringLiteral("BatchConvert/output")).toString();
    o.recursive = s.value(QStringLiteral("BatchConvert/recursive"), false).toBool();
    o.format = s.value(QStringLiteral("BatchConvert/format"), ConvertDxf2000).toInt();
    o.overwrite = s.value(QStringLiteral("BatchConvert/overwrite"), false).toBool();
    o.verify = s.value(QStringLiteral("BatchConvert/verify"), true).toBool();

    FormDialog dialog(tr("Conversione in serie"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.group(tr("Disegni da convertire"));
    dialog.path(tr("Cartella:"), o.source, FormDialog::Folder, tr("cartella con i DWG e DXF"));
    dialog.check(tr("Anche le sottocartelle"), o.recursive);
    dialog.group(tr("Risultato"));
    dialog.choice(tr("Formato:"),
                  {{tr("DXF 2000 - conserva tutto, gli altri CAD lo aprono direttamente"), ConvertDxf2000},
                   {tr("DXF R12 - per programmi vecchi"), ConvertDxfR12},
                   {tr("DWG - con il convertitore delle Opzioni (LibreDWG è lento sui disegni grandi)"), ConvertDwg},
                   {tr("PDF - tutti i layout (o il modello se non ce ne sono)"), ConvertPdf}},
                  o.format, [&o](const QVariant& v) { o.format = v.toInt(); });
    dialog.path(tr("Cartella di destinazione:"), o.output, FormDialog::Folder,
                tr("vuoto = sottocartella \"Convertiti\" della cartella dei disegni"));
    dialog.check(tr("Sovrascrivi i file già convertiti"), o.overwrite);
    dialog.check(tr("Verifica: rileggi ogni file scritto e confrontalo con il disegno (DXF e DWG)"), o.verify);
    dialog.note(tr("I disegni originali non vengono toccati. Le sottocartelle si ricreano nella destinazione e alla fine "
                   "vi si scrive il <b>Rapporto conversione.txt</b> con l'esito di ogni file."));
    dialog.setSize(640, 520);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (o.source.isEmpty() || !QFileInfo(o.source).isDir()) {
        print(tr("Conversione in serie: cartella dei disegni non valida."));
        return;
    }
    s.setValue(QStringLiteral("BatchConvert/source"), o.source);
    s.setValue(QStringLiteral("BatchConvert/output"), o.output);
    s.setValue(QStringLiteral("BatchConvert/recursive"), o.recursive);
    s.setValue(QStringLiteral("BatchConvert/format"), o.format);
    s.setValue(QStringLiteral("BatchConvert/overwrite"), o.overwrite);
    s.setValue(QStringLiteral("BatchConvert/verify"), o.verify);
    runBatchConvert(o, true);
}

QString MainWindow::runBatchConvert(const BatchConvertOptions& options, bool interactive)
{
    const QDir source(options.source);
    const QString output = options.output.isEmpty() ? source.filePath(QStringLiteral("Convertiti")) : options.output;
    const QDir target(output);
    QDir().mkpath(output);
    const QString extension = formatExtension(options.format);

    // I file da convertire (non quelli già nella cartella di destinazione, se è dentro la sorgente).
    QStringList files;
    QDirIterator it(options.source, {QStringLiteral("*.dwg"), QStringLiteral("*.dxf")}, QDir::Files,
                    options.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    const QString outputAbsolute = QFileInfo(output).absoluteFilePath();
    while (it.hasNext()) {
        const QString f = it.next();
        if (QFileInfo(f).absoluteFilePath().startsWith(outputAbsolute + QLatin1Char('/'), Qt::CaseInsensitive))
            continue;
        files << f;
    }
    files.sort(Qt::CaseInsensitive);
    print(tr("Conversione in serie: %1 disegni in %2 verso %3.")
              .arg(files.size())
              .arg(formatName(options.format), QDir::toNativeSeparators(output)));

    std::unique_ptr<QProgressDialog> progress;
    if (interactive) {
        progress = std::make_unique<QProgressDialog>(tr("Conversione in serie..."), tr("Annulla"), 0,
                                                     static_cast<int>(files.size()), this);
        progress->setWindowTitle(tr("Conversione in serie"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
    }

    QStringList lines;
    int converted = 0, skipped = 0, failed = 0, withDifferences = 0;
    QElapsedTimer total;
    total.start();
    for (int i = 0; i < files.size(); ++i) {
        const QString file = files.at(i);
        const QString relative = source.relativeFilePath(file);
        if (progress) {
            progress->setValue(i);
            progress->setLabelText(tr("%1 di %2: %3").arg(i + 1).arg(files.size()).arg(relative));
            QCoreApplication::processEvents();
            if (progress->wasCanceled()) {
                lines << tr("ANNULLATA dall'utente prima di %1.").arg(relative);
                break;
            }
        }
        const QString destination =
            target.filePath(QFileInfo(relative).path() + QLatin1Char('/') + QFileInfo(relative).completeBaseName()
                            + QLatin1Char('.') + extension);
        if (QFileInfo(destination) == QFileInfo(file)) {   // stessa cartella e stesso formato: mai sovrascrivere l'originale
            lines << tr("SALTATO  %1: il risultato coinciderebbe con l'originale.").arg(relative);
            ++skipped;
            continue;
        }
        if (QFileInfo::exists(destination) && !options.overwrite) {
            lines << tr("SALTATO  %1: esiste già.").arg(relative);
            ++skipped;
            continue;
        }
        QDir().mkpath(QFileInfo(destination).path());
        QElapsedTimer timer;
        timer.start();

        const ConvertFileResult r = convertDrawingFile(file, destination, options.format, options.verify, settings_, interactive);
        if (r.stage == ConvertFileResult::Read) {
            lines << tr("ERRORE   %1: lettura non riuscita (%2).").arg(relative, r.error.simplified().left(300));
            ++failed;
            continue;
        }
        if (r.stage == ConvertFileResult::Write) {
            lines << tr("ERRORE   %1: scrittura non riuscita (%2).").arg(relative, r.error.simplified().left(300));
            ++failed;
            continue;
        }
        ++converted;
        QString line = tr("OK       %1 -> %2 (%3 oggetti, %4 s)")
                           .arg(relative, target.relativeFilePath(destination))
                           .arg(r.objects)
                           .arg(timer.elapsed() / 1000.0, 0, 'f', 1);
        if (options.verify && options.format != ConvertPdf) {
            if (r.verified) {
                line += tr("  verificato");
            } else if (r.differences.isEmpty()) {
                line += tr("  VERIFICA: il file scritto non si rilegge (%1).").arg(r.verifyProblem.simplified().left(200));
                ++withDifferences;
            } else {
                ++withDifferences;
                line += tr("  VERIFICA: %1 differenze").arg(r.verifyProblem);
                for (const QString& d : r.differences)
                    line += QLatin1Char('\n') + QStringLiteral("           ") + d;
            }
        }
        lines << line;
    }
    if (progress)
        progress->setValue(static_cast<int>(files.size()));

    const QString summary = tr("%1 convertiti, %2 saltati, %3 errori%4 in %5 s.")
                                .arg(converted)
                                .arg(skipped)
                                .arg(failed)
                                .arg(options.verify && options.format != ConvertPdf
                                         ? tr(", %1 con differenze alla verifica").arg(withDifferences)
                                         : QString())
                                .arg(total.elapsed() / 1000.0, 0, 'f', 1);
    const QString reportPath = target.filePath(QStringLiteral("Rapporto conversione.txt"));
    QFile report(reportPath);
    if (report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&report);
        out << tr("Conversione in serie - %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("dd/MM/yyyy HH:mm"))) << '\n';
        out << tr("Da: %1%2").arg(QDir::toNativeSeparators(options.source), options.recursive ? tr(" (con sottocartelle)") : QString()) << '\n';
        out << tr("A:  %1 (%2)").arg(QDir::toNativeSeparators(output), formatName(options.format)) << '\n' << '\n';
        for (const QString& l : std::as_const(lines))
            out << l << '\n';
        out << '\n' << summary << '\n';
    }
    print(tr("Conversione in serie terminata: %1 Rapporto: %2").arg(summary, QDir::toNativeSeparators(reportPath)));
    return reportPath;
}

// ------------------------------------------------------------------ ESTRAIDATI

bool MainWindow::writeDataExport(DrawingWidget* d, const QString& path, const DataExportOptions& options)
{
    const DataExportResult r = exportData(d->document(), options);
    QSaveFile file(path);
    // Con il BOM UTF-8 Excel riconosce le lettere accentate.
    if (!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF") < 0
        || file.write(r.csv.data(), static_cast<qint64>(r.csv.size())) < 0 || !file.commit()) {
        print(tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    print(tr("Dati estratti (%1): %2 righe in %3.")
              .arg(options.mode == DataExportOptions::Mode::Count     ? tr("distinta dei blocchi")
                   : options.mode == DataExportOptions::Mode::Objects ? tr("oggetti")
                                                                      : tr("blocchi con attributi"))
              .arg(r.rows)
              .arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::showDataExport()
{
    DrawingWidget* d = current();
    if (!d) {
        print(tr("ESTRAIDATI: nessun disegno aperto."));
        return;
    }
    QSettings s = convertSettings();
    DataExportOptions o;
    int mode = s.value(QStringLiteral("DataExport/mode"), 0).toInt();
    bool onlySelection = !d->selection().empty();
    bool layouts = s.value(QStringLiteral("DataExport/layouts"), true).toBool();
    int separator = s.value(QStringLiteral("DataExport/separator"), 0).toInt();
    const QFileInfo drawing(d->filePath());
    QString path = drawing.fileName().isEmpty()
                       ? QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(tr("dati.csv"))
                       : drawing.dir().filePath(drawing.completeBaseName() + tr("-dati.csv"));

    FormDialog dialog(tr("Estrai dati in CSV"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.group(tr("Cosa"));
    dialog.choice(tr("Tabella:"),
                  {{tr("Blocchi con attributi (una riga per blocco)"), 0},
                   {tr("Distinta dei blocchi (con le quantità)"), 1},
                   {tr("Tutti gli oggetti (lunghezze, aree, testi)"), 2}},
                  mode, [&mode](const QVariant& v) { mode = v.toInt(); });
    const int only = dialog.check(tr("Solo gli oggetti selezionati (%1)").arg(d->selection().size()), onlySelection);
    if (d->selection().empty())
        dialog.setFieldValue(only, false);
    dialog.check(tr("Anche gli oggetti dei layout (oltre al modello)"), layouts);
    dialog.group(tr("File"));
    dialog.choice(tr("Formato:"),
                  {{tr("Excel in italiano (\";\" e virgola decimale)"), 0},
                   {tr("CSV standard (\",\" e punto decimale)"), 1}},
                  separator, [&separator](const QVariant& v) { separator = v.toInt(); });
    dialog.path(tr("Salva in:"), path, FormDialog::SaveFile, QString(), tr("Tabella CSV (*.csv)"));
    dialog.note(tr("I testi che iniziano con = + - @ prendono un apostrofo davanti, così il foglio di calcolo non li "
                   "esegue come formule."));
    dialog.setSize(640, 420);
    if (dialog.exec() != QDialog::Accepted || path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, tr("Estrai dati in CSV"), tr("%1 esiste già. Sostituirlo?").arg(QDir::toNativeSeparators(path)))
               != QMessageBox::Yes)
        return;
    s.setValue(QStringLiteral("DataExport/mode"), mode);
    s.setValue(QStringLiteral("DataExport/layouts"), layouts);
    s.setValue(QStringLiteral("DataExport/separator"), separator);
    o.mode = mode == 1 ? DataExportOptions::Mode::Count : mode == 2 ? DataExportOptions::Mode::Objects : DataExportOptions::Mode::Blocks;
    if (onlySelection && !d->selection().empty())
        o.only = d->selection();
    o.layouts = layouts;
    o.separator = separator == 1 ? ',' : ';';
    writeDataExport(d, path, o);
}

} // namespace cartesio
