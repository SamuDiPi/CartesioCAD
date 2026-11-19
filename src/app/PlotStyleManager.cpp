// Gestione tabelle stili di stampa (STYLESMANAGER, Opzioni > Stampa e pubblicazione): le .ctb della cartella degli
// stili e quelle predefinite; nuova, modifica, copia, rinomina, elimina, importa da un file scelto, apri cartella.
#include "app/PlotDialog.h"

#include "app/FormDialog.h"
#include "core/Plot.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>

#include <vector>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>

namespace cartesio {


namespace {

bool isBuiltin(const QString& name)
{
    for (const std::string& n : builtinPlotStyleTableNames())
        if (QString::fromStdString(n).compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString withCtb(QString name)
{
    name = name.trimmed();
    if (!name.endsWith(QLatin1String(".ctb"), Qt::CaseInsensitive))
        name += QStringLiteral(".ctb");
    return name;
}

// Nome nuovo (FormDialog con un campo); vuoto se annullato.
QString askName(QWidget* parent, const QString& title, const QString& initial)
{
    QString name = initial;
    FormDialog ask(title, FormDialog::Ok | FormDialog::Cancel, parent);
    ask.text(QObject::tr("Nome della tabella (.ctb):"), name);
    ask.setSize(440, 150);
    if (ask.exec() != QDialog::Accepted || name.trimmed().isEmpty())
        return {};
    return withCtb(name);
}

QString& grabPath()
{
    static QString path;
    return path;
}

} // namespace

void grabNextPlotStyleManager(const QString& path)
{
    grabPath() = path;
}

void showPlotStyleManager(QWidget* parent)
{
    FormDialog dialog(QObject::tr("Tabelle stili di stampa"), FormDialog::Close, parent);
    QStringList names;
    int selected = 0;
    int listField = -1, detailsField = -1;
    const QString folder = plotStylesFolder();
    const auto selectedName = [&] { return selected >= 0 && selected < names.size() ? names[selected] : QString(); };
    const auto fileOf = [&](const QString& name) { return folder + QLatin1Char('/') + name; };
    const auto showDetails = [&] {
        const QString name = selectedName();
        if (name.isEmpty()) {
            dialog.setFieldText(detailsField, QString());
            return;
        }
        const bool file = QFileInfo::exists(fileOf(name));
        QString text = QObject::tr("<b>%1</b><br>").arg(name.toHtmlEscaped());
        text += file ? QObject::tr("File: %1").arg(QDir::toNativeSeparators(fileOf(name)).toHtmlEscaped())
                     : QObject::tr("Predefinita di CartesioCAD (si modifica salvandone una copia nella cartella)");
        dialog.setFieldText(detailsField, text);
    };
    const auto refill = [&](const QString& select) {
        names = plotStyleTableNames();
        QStringList items;
        for (const QString& n : names)
            items << (QFileInfo::exists(fileOf(n)) ? n : QObject::tr("%1   (predefinita)").arg(n));
        selected = std::max(0, static_cast<int>(names.indexOf(select)));
        dialog.setItems(listField, items, selected);
        showDetails();
    };
    const auto edit = [&] {
        const QString name = selectedName();
        if (name.isEmpty())
            return;
        const QString saved = editPlotStyleTable(&dialog, name);
        if (!saved.isEmpty())
            refill(saved);
    };

    dialog.group(QObject::tr("Tabelle dipendenti dal colore (.ctb)"));
    listField = dialog.list({}, 0, [&](int index) {
        selected = index;
        showDetails();
    }, [&](int index) {
        selected = index;
        edit();
    });
    dialog.buttonsRow({
        {QObject::tr("Nuova..."), [&] {
             const QString saved = editPlotStyleTable(&dialog, QString());
             if (!saved.isEmpty())
                 refill(saved);
         }},
        {QObject::tr("Modifica..."), edit},
        {QObject::tr("Copia..."), [&] {
             const QString name = selectedName();
             if (name.isEmpty())
                 return;
             const QString target = askName(&dialog, QObject::tr("Copia di %1").arg(name),
                                            QObject::tr("Copia di %1").arg(QFileInfo(name).completeBaseName()));
             if (target.isEmpty())
                 return;
             if (QFileInfo::exists(fileOf(target))) {
                 QMessageBox::information(&dialog, dialog.windowTitle(), QObject::tr("Esiste già una tabella con questo nome."));
                 return;
             }
             QString error;
             const auto table = loadPlotStyleTable(name, &error);
             if (!table || !savePlotStyleTable(*table, fileOf(target), &error)) {
                 QMessageBox::critical(&dialog, dialog.windowTitle(), error);
                 return;
             }
             refill(target);
         }},
        {QObject::tr("Rinomina..."), [&] {
             const QString name = selectedName();
             if (name.isEmpty() || !QFileInfo::exists(fileOf(name))) {
                 QMessageBox::information(&dialog, dialog.windowTitle(),
                                          QObject::tr("Le tabelle predefinite non si rinominano: farne una copia."));
                 return;
             }
             const QString target = askName(&dialog, QObject::tr("Rinomina %1").arg(name), QFileInfo(name).completeBaseName());
             if (target.isEmpty() || target == name)
                 return;
             if (QFileInfo::exists(fileOf(target)) || !QFile::rename(fileOf(name), fileOf(target))) {
                 QMessageBox::critical(&dialog, dialog.windowTitle(), QObject::tr("Impossibile rinominare in %1.").arg(target));
                 return;
             }
             refill(target);
         }},
        {QObject::tr("Elimina"), [&] {
             const QString name = selectedName();
             if (name.isEmpty() || !QFileInfo::exists(fileOf(name))) {
                 QMessageBox::information(&dialog, dialog.windowTitle(), QObject::tr("Le tabelle predefinite non si eliminano."));
                 return;
             }
             if (QMessageBox::question(&dialog, dialog.windowTitle(),
                                       isBuiltin(name)
                                           ? QObject::tr("Eliminare la copia modificata di %1? Torna quella predefinita.").arg(name)
                                           : QObject::tr("Eliminare la tabella %1?").arg(name))
                 != QMessageBox::Yes)
                 return;
             QFile::remove(fileOf(name));
             refill(QString());
         }},
    });
    // L'importazione parte dalla cartella Documenti: i file da importare li sceglie l'utente.
    const QString startFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    dialog.buttonsRow({
        {QObject::tr("Importa .ctb..."), [&] {
             const QStringList files = QFileDialog::getOpenFileNames(&dialog, QObject::tr("Importa tabelle stili"),
                                                                     startFolder,
                                                                     QObject::tr("Tabelle stili di stampa (*.ctb)"));
             QString last, errors;
             for (const QString& f : files) {
                 QString error;
                 const auto table = readCtbFile(f, &error);
                 const QString target = fileOf(QFileInfo(f).fileName());
                 if (!table || !savePlotStyleTable(*table, target, &error))
                     errors += error + QLatin1Char('\n');
                 else
                     last = QFileInfo(f).fileName();
             }
             if (!errors.isEmpty())
                 QMessageBox::warning(&dialog, dialog.windowTitle(), errors);
             refill(last);
         }},
        {QObject::tr("Apri cartella"), [&] {
             QDir().mkpath(folder);
             QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
         }},
    });
    dialog.group(QObject::tr("Dettagli"));
    detailsField = dialog.note(QString());
    dialog.note(QObject::tr("Cartella delle tabelle: %1").arg(QDir::toNativeSeparators(folder).toHtmlEscaped()));
    refill(QString());
    dialog.setSize(620, 600);
    if (!grabPath().isEmpty()) {
        dialog.show();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        dialog.grab().save(grabPath());
        grabPath().clear();
        return;
    }
    dialog.exec();
}

} // namespace cartesio
