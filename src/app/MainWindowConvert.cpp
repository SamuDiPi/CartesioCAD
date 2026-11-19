// Conversione in serie (DWGCONVERT / CONVERTI): tutti i disegni di una cartella (anche nelle sottocartelle) in
// DXF 2000, DXF R12, DWG o PDF, con verifica facoltativa (il file scritto si rilegge e si confronta) e un rapporto
// nella cartella di destinazione. Ogni disegno si legge in un altro thread: la finestra resta viva.
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlotEngine.h"
#include "core/DocCompare.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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

        Document doc;
        QString error;
        DxfReadResult result;
        const bool read = interactive ? readInBackground(file, doc, &error, &result)
                                      : DrawingWidget::readFile(file, doc, &error, &result);
        if (!read) {
            lines << tr("ERRORE   %1: lettura non riuscita (%2).").arg(relative, error.simplified().left(300));
            ++failed;
            continue;
        }
        DrawingWidget::loadXrefs(file, doc, result);
        // Un disegno nascosto fa da tramite: stessa scrittura (DXF/DWG) e stessa stampa (PDF) dei comandi normali.
        DrawingWidget drawing(settings_, QFileInfo(file).fileName());
        drawing.adopt(std::move(doc), file, result, nullptr);
        const DocSummary before = summarizeDocument(drawing.document());
        bool ok = false;
        if (options.format == ConvertPdf) {
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
            ok = plotJobsToPdf(drawing, destination, jobs, &error);
        } else {
            const DxfVersion version = options.format == ConvertDxfR12 ? DxfVersion::R12 : DxfVersion::R2000;
            ok = drawing.saveTo(destination, &error, version);
        }
        if (!ok) {
            lines << tr("ERRORE   %1: scrittura non riuscita (%2).").arg(relative, error.simplified().left(300));
            ++failed;
            continue;
        }
        ++converted;
        QString line = tr("OK       %1 -> %2 (%3 oggetti, %4 s)")
                           .arg(relative, target.relativeFilePath(destination))
                           .arg(before.total)
                           .arg(timer.elapsed() / 1000.0, 0, 'f', 1);
        if (options.verify && options.format != ConvertPdf) {
            Document again;
            QString readError;
            const bool reread = interactive ? readInBackground(destination, again, &readError, nullptr)
                                            : DrawingWidget::readFile(destination, again, &readError);
            if (!reread) {
                line += tr("  VERIFICA: il file scritto non si rilegge (%1).").arg(readError.simplified().left(200));
                ++withDifferences;
            } else {
                const DocComparison c = compareSummaries(before, summarizeDocument(again));
                if (c.identical()) {
                    line += tr("  verificato");
                } else {
                    ++withDifferences;
                    line += tr("  VERIFICA: %1 differenze").arg(c.differences.size());
                    const QString detail = QString::fromStdString(c.report(8));
                    for (const QString& d : detail.split(QLatin1Char('\n')).mid(1))
                        line += QLatin1Char('\n') + QStringLiteral("           ") + d.trimmed();
                }
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

} // namespace cartesio
