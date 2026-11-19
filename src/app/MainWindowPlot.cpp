// Parte della finestra principale dedicata alla stampa: PLOT, PREVIEW, PUBLISH, EXPORTPDF e -PLOT.
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/DrawingWidget.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace cartesio {

namespace {

QString suggestedPath(const DrawingWidget& d, const QString& suffix, const QString& extension)
{
    const QString base = d.filePath().isEmpty() ? d.displayName() : d.filePath();
    // Cartella dalle Opzioni (Stampa su file), altrimenti quella del disegno.
    const QString folder = appOptions().plotFolder.trimmed();
    QString path = (!folder.isEmpty() && QFileInfo(folder).isDir() ? QDir::fromNativeSeparators(folder) : QFileInfo(base).path())
                   + QStringLiteral("/") + QFileInfo(base).completeBaseName();
    if (!suffix.isEmpty() && appOptions().plotFileNameWithLayout)
        path += QStringLiteral(" - ") + suffix;
    return path + extension;
}

// Dopo una stampa su PDF: lo si apre, se richiesto nelle Opzioni.
void openPlotted(const QString& path)
{
    if (appOptions().openAfterPlot)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString spaceName(const DrawingWidget& d, int space)
{
    if (const Layout* l = d.document().findLayout(space))
        return QString::fromStdString(l->name);
    return QObject::tr("Modello");
}

// "1:100", "2:1", "ADATTA".
bool parseScale(const QString& text, PlotSettings& s)
{
    const QString t = text.trimmed().toUpper();
    if (t == QLatin1String("ADATTA") || t == QLatin1String("FIT")) {
        s.setFlag(kPlotUseStandardScale, true);
        s.standardScaleType = 0;
        return true;
    }
    const QStringList parts = t.split(QLatin1Char(':'));
    bool ok1 = false, ok2 = false;
    const double paper = parts.size() == 2 ? parts[0].toDouble(&ok1) : 0.0;
    const double drawing = parts.size() == 2 ? parts[1].toDouble(&ok2) : 0.0;
    if (!ok1 || !ok2 || paper <= 0 || drawing <= 0)
        return false;
    s.paperUnits = 1;
    s.customPaper = paper;
    s.customDrawing = drawing;
    const int type = standardScaleTypeFor(paper, drawing);
    s.setFlag(kPlotUseStandardScale, type > 0);
    if (type > 0)
        s.standardScaleType = type;
    return true;
}

bool parseYes(const QString& v)
{
    const QString t = v.toUpper();
    return t == QLatin1String("SI") || t == QLatin1String("SÌ") || t == QLatin1String("S") || t == QLatin1String("Y")
           || t == QLatin1String("YES") || t == QLatin1String("1") || t == QLatin1String("ON");
}

// Divide la riga in chiave=valore, rispettando le virgolette.
QList<QPair<QString, QString>> splitOptions(const QString& line)
{
    QList<QPair<QString, QString>> out;
    QString current;
    bool quoted = false;
    const auto flush = [&] {
        const QString token = current.trimmed();
        current.clear();
        if (token.isEmpty())
            return;
        const qsizetype eq = token.indexOf(QLatin1Char('='));
        if (eq < 0)
            out.append({token.toUpper(), QString()});
        else
            out.append({token.left(eq).trimmed().toUpper(), token.mid(eq + 1).trimmed()});
    };
    for (const QChar c : line) {
        if (c == QLatin1Char('"'))
            quoted = !quoted;
        else if (c.isSpace() && !quoted)
            flush();
        else
            current += c;
    }
    flush();
    return out;
}

} // namespace

void MainWindow::runPlot(DrawingWidget* d, const PlotDialogState& state)
{
    // Opzioni che restano nelle impostazioni del programma.
    PlotPreferences prefs = PlotPreferences::load();
    prefs.transparency = state.job.transparency;
    prefs.stamp = state.job.stamp;
    prefs.stampSettings = state.job.stampSettings;
    prefs.save();

    const PlotJob& job = state.job;
    if (state.saveChanges) {
        Document& doc = d->document();
        if (job.space == 0)
            doc.setModelPlot(job.settings);
        else if (Layout* l = doc.findLayout(job.space)) {
            applyPlotSettings(*l, job.settings);
            doc.touch();
        }
        d->notifyChanged();
    }

    for (const QString& warning : plotWarnings({job}))
        print(warning);
    const QString device = QString::fromStdString(job.settings.device);
    QString error;
    if (isPdfDevice(device)) {
        const QString suffix = job.space != 0 ? spaceName(*d, job.space) : QString();
        const QString path = QFileDialog::getSaveFileName(this, tr("Stampa su PDF"), suggestedPath(*d, suffix, QStringLiteral(".pdf")),
                                                          tr("Documento PDF (*.pdf)"));
        if (path.isEmpty())
            return;
        if (!plotJobsToPdf(*d, path, {job}, &error)) {
            QMessageBox::critical(this, tr("Stampa"), error);
            return;
        }
        print(tr("PDF salvato: %1").arg(QDir::toNativeSeparators(path)));
        openPlotted(path);
        return;
    }
    QString file;
    if (state.plotToFile) {
        file = QFileDialog::getSaveFileName(this, tr("Stampa su file"), suggestedPath(*d, QString(), QStringLiteral(".prn")),
                                            tr("File di stampa (*.prn);;Tutti i file (*)"));
        if (file.isEmpty())
            return;
    }
    if (!plotJobsToPrinter(*d, device, file, {job}, &error)) {
        QMessageBox::critical(this, tr("Stampa"), error.isEmpty() ? tr("Stampa non riuscita.") : error);
        return;
    }
    print(file.isEmpty() ? tr("Inviato alla stampante %1.").arg(device)
                         : tr("Stampato su file: %1").arg(QDir::toNativeSeparators(file)));
}

void MainWindow::plotDialog(std::optional<PlotDialogState> state)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    PlotDialog dialog(*d, d->shownSpace(), false, this);
    if (state)
        dialog.setState(*state);
    if (dialog.exec() != QDialog::Accepted)
        return;
    PlotDialogState result = dialog.state();
    if (dialog.outcome() == PlotDialog::Outcome::PickWindow) {
        print(tr("Specificare la finestra di stampa (Invio o Esc per tornare al dialogo)."));
        QPointer<DrawingWidget> guard(d);
        d->startCommand(makeWindowPickCommand([this, result, guard](std::optional<BBox> box) mutable {
                            if (box) {
                                result.job.settings.plotType = PlotType::Window;
                                result.job.settings.windowMin = box->min;
                                result.job.settings.windowMax = box->max;
                            }
                            QTimer::singleShot(0, this, [this, result, guard] {
                                if (guard && current() == guard)
                                    plotDialog(result);
                            });
                        }),
                        QStringLiteral("STAMPA"));
        return;
    }
    if (dialog.outcome() == PlotDialog::Outcome::Plot)
        runPlot(d, result);
}

void MainWindow::plotPreview()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    PlotDialogState state;
    state.job = plotJobFor(*d, d->shownSpace());
    if (showPlotPreview(this, *d, state.job))
        runPlot(d, state);
}

void MainWindow::publish()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    std::vector<PlotJob> jobs;
    for (const Layout& l : d->document().layouts())
        jobs.push_back(plotJobFor(*d, l.id));
    if (jobs.empty()) {
        QMessageBox::information(this, tr("Pubblica"), tr("Il disegno non ha layout."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Pubblica tutti i layout in PDF"),
                                                      suggestedPath(*d, QString(), QStringLiteral(".pdf")),
                                                      tr("Documento PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    for (const QString& warning : plotWarnings(jobs))
        print(warning);
    QString error;
    if (!plotJobsToPdf(*d, path, jobs, &error)) {
        QMessageBox::critical(this, tr("Pubblica"), error);
        return;
    }
    print(tr("Pubblicati %1 layout in %2").arg(jobs.size()).arg(QDir::toNativeSeparators(path)));
    openPlotted(path);
}

bool MainWindow::exportPdfTo(DrawingWidget* d, const QString& path, QString* error)
{
    const PlotJob job = plotJobFor(*d, d->shownSpace());
    for (const QString& warning : plotWarnings({job}))
        print(warning);
    if (!plotJobsToPdf(*d, path, {job}, error))
        return false;
    print(tr("PDF esportato: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool MainWindow::plotFromOptions(DrawingWidget* d, const QString& line, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const auto options = splitOptions(line);
    // Prima lo spazio e l'impostazione di pagina: le altre opzioni li modificano.
    int space = d->shownSpace();
    for (const auto& [key, value] : options)
        if (key == QLatin1String("SPAZIO") || key == QLatin1String("SPACE") || key == QLatin1String("LAYOUT")) {
            if (value.compare(QLatin1String("MODELLO"), Qt::CaseInsensitive) == 0
                || value.compare(QLatin1String("MODEL"), Qt::CaseInsensitive) == 0) {
                space = 0;
                continue;
            }
            space = -1;
            for (const Layout& l : d->document().layouts())
                if (QString::fromStdString(l.name).compare(value, Qt::CaseInsensitive) == 0)
                    space = l.id;
            if (space < 0)
                return fail(tr("layout \"%1\" inesistente.").arg(value));
        }
    PlotJob job = plotJobFor(*d, space);
    for (const auto& [key, value] : options)
        if (key == QLatin1String("IMPOSTAZIONE") || key == QLatin1String("PAGESETUP")) {
            const PlotSettings* s = d->document().findPageSetup(value.toStdString());
            if (!s)
                return fail(tr("impostazione di pagina \"%1\" inesistente.").arg(value));
            job.settings = *s;
            job.settings.setFlag(kPlotModelType, space == 0);
        }

    PlotSettings& s = job.settings;
    QString file, printer;
    bool save = false;
    for (const auto& [key, value] : options) {
        const QString v = value.toUpper();
        if (key == QLatin1String("SPAZIO") || key == QLatin1String("SPACE") || key == QLatin1String("LAYOUT")
            || key == QLatin1String("IMPOSTAZIONE") || key == QLatin1String("PAGESETUP")) {
            continue;
        } else if (key == QLatin1String("AREA") || key == QLatin1String("WHAT")) {
            static const QList<QPair<QStringList, PlotType>> types = {
                {{QStringLiteral("SCHERMO"), QStringLiteral("DISPLAY")}, PlotType::Display},
                {{QStringLiteral("ESTENSIONI"), QStringLiteral("EXTENTS")}, PlotType::Extents},
                {{QStringLiteral("LIMITI"), QStringLiteral("LIMITS")}, PlotType::Limits},
                {{QStringLiteral("VISTA"), QStringLiteral("VIEW")}, PlotType::View},
                {{QStringLiteral("FINESTRA"), QStringLiteral("WINDOW")}, PlotType::Window},
                {{QStringLiteral("LAYOUT")}, PlotType::Layout},
            };
            bool found = false;
            for (const auto& [names, type] : types)
                if (names.contains(v)) {
                    s.plotType = type;
                    found = true;
                }
            if (!found)
                return fail(tr("area \"%1\" sconosciuta (SCHERMO, ESTENSIONI, LIMITI, FINESTRA, LAYOUT).").arg(value));
        } else if (key == QLatin1String("FINESTRA") || key == QLatin1String("WINDOW")) {
            const QStringList n = value.split(QLatin1Char(','));
            bool ok[4] = {false, false, false, false};
            if (n.size() != 4)
                return fail(tr("FINESTRA vuole x1,y1,x2,y2."));
            const double x1 = n[0].toDouble(&ok[0]), y1 = n[1].toDouble(&ok[1]);
            const double x2 = n[2].toDouble(&ok[2]), y2 = n[3].toDouble(&ok[3]);
            if (!ok[0] || !ok[1] || !ok[2] || !ok[3])
                return fail(tr("FINESTRA vuole x1,y1,x2,y2."));
            s.windowMin = {std::min(x1, x2), std::min(y1, y2)};
            s.windowMax = {std::max(x1, x2), std::max(y1, y2)};
            s.plotType = PlotType::Window;
        } else if (key == QLatin1String("FOGLIO") || key == QLatin1String("PAPER")) {
            const MediaFormat* m = findMediaByName(value.toStdString());
            if (!m)
                return fail(tr("formato \"%1\" sconosciuto.").arg(value));
            const bool landscape = isLandscape(s), upside = isUpsideDown(s);
            s.paperWidth = m->width;
            s.paperHeight = m->height;
            s.mediaName = canonicalMediaName(*m);
            setOrientation(s, landscape, upside);
        } else if (key == QLatin1String("ORIENTAMENTO") || key == QLatin1String("ORIENTATION")) {
            const bool landscape = v.startsWith(QLatin1Char('O')) || v.startsWith(QLatin1Char('L'));
            setOrientation(s, landscape, isUpsideDown(s));
        } else if (key == QLatin1String("CAPOVOLTA") || key == QLatin1String("UPSIDEDOWN")) {
            setOrientation(s, isLandscape(s), parseYes(value));
        } else if (key == QLatin1String("SCALA") || key == QLatin1String("SCALE")) {
            if (!parseScale(value, s))
                return fail(tr("scala \"%1\" non valida (ADATTA, 1:100, 2:1).").arg(value));
        } else if (key == QLatin1String("CENTRA") || key == QLatin1String("CENTER")) {
            s.setFlag(kPlotCentered, parseYes(value));
        } else if (key == QLatin1String("OFFSET")) {
            const QStringList n = value.split(QLatin1Char(','));
            if (n.size() != 2)
                return fail(tr("OFFSET vuole x,y in mm."));
            s.plotOrigin = {n[0].toDouble(), n[1].toDouble()};
            s.setFlag(kPlotCentered, false);
        } else if (key == QLatin1String("MARGINI") || key == QLatin1String("MARGINS")) {
            const double m = value.toDouble();
            s.marginLeft = s.marginBottom = s.marginRight = s.marginTop = std::max(0.0, m);
        } else if (key == QLatin1String("STILI") || key == QLatin1String("STYLES") || key == QLatin1String("CTB")) {
            if (v == QLatin1String("NESSUNO") || v == QLatin1String("NONE")) {
                s.styleSheet.clear();
                s.setFlag(kPlotPlotStyles, false);
            } else {
                s.styleSheet = value.toStdString();
                s.setFlag(kPlotPlotStyles, true);
            }
        } else if (key == QLatin1String("SPESSORI") || key == QLatin1String("LINEWEIGHTS")) {
            s.setFlag(kPlotPrintLineweights, parseYes(value));
        } else if (key == QLatin1String("SCALASPESSORI") || key == QLatin1String("SCALELINEWEIGHTS")) {
            s.setFlag(kPlotScaleLineweights, parseYes(value));
        } else if (key == QLatin1String("CARTAULTIMA") || key == QLatin1String("PAPERLAST")) {
            s.setFlag(kPlotDrawViewportsFirst, parseYes(value));
        } else if (key == QLatin1String("TRASPARENZA") || key == QLatin1String("TRANSPARENCY")) {
            job.transparency = parseYes(value);
        } else if (key == QLatin1String("TIMBRO") || key == QLatin1String("STAMP")) {
            job.stamp = parseYes(value);
        } else if (key == QLatin1String("QUALITA") || key == QLatin1String("DPI")) {
            s.shadeResolution = 5;
            s.shadeDpi = value.toInt();
        } else if (key == QLatin1String("COPIE") || key == QLatin1String("COPIES")) {
            job.copies = std::max(1, value.toInt());
        } else if (key == QLatin1String("STAMPANTE") || key == QLatin1String("PRINTER")) {
            printer = value;
        } else if (key == QLatin1String("FILE")) {
            file = value;
        } else if (key == QLatin1String("SALVA") || key == QLatin1String("SAVE")) {
            save = parseYes(value);
        } else {
            return fail(tr("opzione \"%1\" sconosciuta.").arg(key));
        }
    }
    if (printer.isEmpty() && file.isEmpty())
        return fail(tr("manca FILE=uscita.pdf (oppure STAMPANTE=nome)."));
    s.device = printer.isEmpty() ? std::string(kPdfDevice) : printer.toStdString();
    if (save) {
        if (space == 0)
            d->document().setModelPlot(s);
        else if (Layout* l = d->document().findLayout(space)) {
            applyPlotSettings(*l, s);
            d->document().touch();
        }
        d->notifyChanged();
    }
    for (const QString& warning : plotWarnings({job}))
        print(warning);
    QString message;
    const bool ok = printer.isEmpty() ? plotJobsToPdf(*d, file, {job}, &message)
                                      : plotJobsToPrinter(*d, printer, file, {job}, &message);
    if (!ok)
        return fail(message);
    print(printer.isEmpty() ? tr("PDF salvato: %1").arg(QDir::toNativeSeparators(file))
                            : tr("Inviato alla stampante %1.").arg(printer));
    return true;
}

} // namespace cartesio
