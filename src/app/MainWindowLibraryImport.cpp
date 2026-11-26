// Importa libreria del costruttore (IMPORTALIBRERIA): dalla libreria di blocchi installata sul PC dell'utente ai
// blocchi puliti (senza loghi, sigle e codici), in mm, nella sua cartella personale, con l'indice, il registro di
// ciò che è stato tolto e le righe dei cataloghi del fronte quadro per i blocchi riconosciuti. CartesioCAD non
// contiene né distribuisce nulla delle librerie. La logica è in core/LibraryImport.
#include "app/MainWindow.h"

#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "core/LibraryImport.h"
#include "core/io/Dxf.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <map>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace cartesio {

namespace {

QSettings importSettings() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

QString documentsFolder(const QString& relative)
{
    // Documenti può essere spostata (sincronizzazione): si preferisce quella dove la cartella esiste già.
    const QStringList roots{QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                            QDir::home().filePath(QStringLiteral("Documents"))};
    for (const QString& r : roots)
        if (QFileInfo::exists(QDir(r).filePath(relative)))
            return QDir::toNativeSeparators(QDir(r).filePath(relative));
    return QDir::toNativeSeparators(QDir(roots.front()).filePath(relative));
}

// Librerie installate: cartelle in cima ai dischi locali (mai quelli di rete) con i menu testuali
// (cartella/*/Menu/*/*.mns).
QStringList findInstalledLibraries()
{
    QStringList out;
    for (const QFileInfo& drive : QDir::drives()) {
#ifdef Q_OS_WIN
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(drive.absoluteFilePath().utf16())) != DRIVE_FIXED)
            continue;
#endif
        const QDir root(drive.absoluteFilePath());
        for (const QFileInfo& top : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir lib(top.absoluteFilePath());
            bool found = false;
            for (const QFileInfo& sub : lib.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QDir menu(QDir(sub.absoluteFilePath()).filePath(QStringLiteral("Menu")));
                if (!menu.exists())
                    continue;
                for (const QFileInfo& m : menu.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                    if (!QDir(m.absoluteFilePath()).entryList({QStringLiteral("*.mns")}, QDir::Files).isEmpty()) {
                        found = true;
                        break;
                    }
                if (found)
                    break;
            }
            if (found)
                out << QDir::toNativeSeparators(top.absoluteFilePath());
        }
    }
    return out;
}

QString csvCell(QString v)
{
    if (v.startsWith(QLatin1Char('=')) || v.startsWith(QLatin1Char('+')) || v.startsWith(QLatin1Char('-'))
        || v.startsWith(QLatin1Char('@')))
        v.prepend(QLatin1Char('\''));
    if (v.contains(QLatin1Char(';')) || v.contains(QLatin1Char('"')) || v.contains(QLatin1Char('\n')))
        return QLatin1Char('"') + v.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
    return v;
}

bool writeUtf8(const QString& path, const QString& text, bool bom)
{
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    const QByteArray data = text.toUtf8();
    return file.open(QIODevice::WriteOnly) && (!bom || file.write("\xEF\xBB\xBF") >= 0) && file.write(data) >= 0
           && file.commit();
}

// Unisce le righe nel catalogo `path` (copia di sicurezza accanto prima di cambiarlo).
QString updateCatalog(const QString& path, const char* header, const std::string& rows, const std::string& family,
                      std::size_t familyColumn, QString* backup)
{
    std::string existing;
    if (QFile f(path); f.exists()) {
        if (!f.open(QIODevice::ReadOnly))
            return QObject::tr("impossibile leggere %1").arg(QDir::toNativeSeparators(path));
        existing = f.readAll().toStdString();
        if (existing.rfind("\xEF\xBB\xBF", 0) == 0)
            existing.erase(0, 3);
        *backup = path + QStringLiteral(".") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                  + QStringLiteral(".bak");
        if (!QFile::copy(path, *backup))
            return QObject::tr("impossibile salvare la copia di %1").arg(QDir::toNativeSeparators(path));
    }
    const std::string merged = mergeLibraryCatalog(existing, header, rows, family, familyColumn);
    if (!writeUtf8(path, QString::fromStdString(merged), false))
        return QObject::tr("impossibile scrivere %1").arg(QDir::toNativeSeparators(path));
    return {};
}

} // namespace

QString MainWindow::runLibraryImport(const LibraryImportRequest& r, bool interactive)
{
    const QDir source(r.source);
    if (r.source.isEmpty() || !source.exists())
        return tr("Cartella della libreria non trovata: %1.").arg(QDir::toNativeSeparators(r.source));
    const QString family = (r.family.trimmed().isEmpty() ? source.dirName() : r.family.trimmed())
                               .replace(QLatin1Char(';'), QLatin1Char(' '));
    QString safeFamily = family;
    for (QChar& c : safeFamily)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    const QDir out(QDir(r.destination).filePath(QStringLiteral("libreria/") + safeFamily));
    if (QDir(r.destination).absolutePath().startsWith(source.absolutePath(), Qt::CaseInsensitive))
        return tr("La cartella di destinazione non può stare dentro la libreria installata.");

    // Indice dai menu testuali (Windows-1252: le lettere accentate restano leggibili come Latin-1).
    std::map<QString, LibraryMenuEntry> menu;
    QDirIterator menus(r.source, {QStringLiteral("*.mns")}, QDir::Files, QDirIterator::Subdirectories);
    while (menus.hasNext()) {
        QFile f(menus.next());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        for (LibraryMenuEntry& e : parseLibraryMenu(QString::fromLatin1(f.readAll()).toStdString()))
            menu.emplace(QString::fromStdString(e.file).toLower(), std::move(e));
    }

    // Disegni della libreria e loro impronte.
    QStringList files;
    QDirIterator it(r.source, {QStringLiteral("*.dwg"), QStringLiteral("*.dxf")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort(Qt::CaseInsensitive);
    if (files.isEmpty())
        return tr("Nella cartella %1 non ci sono disegni DWG o DXF.").arg(QDir::toNativeSeparators(r.source));
    std::vector<std::string> hashes;
    int known = 0;
    for (const QString& file : files) {
        QFile f(file);
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (f.open(QIODevice::ReadOnly))
            h.addData(&f);
        hashes.push_back(h.result().toHex().toStdString());
        known += knownLibraryBlock(hashes.back()) ? 1 : 0;
    }
    const double scale = r.unit == 1 ? 10.0 : r.unit == 2 ? 1.0 : known > 0 ? knownLibraryScale() : 1.0;

    std::unique_ptr<QProgressDialog> progress;
    if (interactive) {
        progress = std::make_unique<QProgressDialog>(tr("Importazione della libreria..."), tr("Annulla"), 0,
                                                     static_cast<int>(files.size()), this);
        progress->setWindowTitle(tr("Importa libreria del costruttore"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
    }
    QElapsedTimer timer;
    timer.start();
    LibraryCleanReport report;
    QString index = QStringLiteral("file;categoria;voce;origine\r\n");
    QString log = QStringLiteral("file;cosa;testo tolto\r\n");
    std::vector<std::pair<QString, const KnownLibraryBlock*>> recognised;   // percorso assoluto, voce
    int written = 0, failed = 0, other = 0;
    bool cancelled = false;
    for (int i = 0; i < files.size(); ++i) {
        const QString relative = source.relativeFilePath(files.at(i));
        if (progress) {
            progress->setValue(i);
            progress->setLabelText(tr("%1 di %2: %3").arg(i + 1).arg(files.size()).arg(relative));
            QCoreApplication::processEvents();
            if (progress->wasCanceled()) {
                cancelled = true;
                break;
            }
        } else if (i % 500 == 0) {
            print(tr("Importa libreria: %1 di %2...").arg(i).arg(files.size()));
        }
        Document doc;
        QString error;
        if (!DrawingWidget::readFile(files.at(i), doc, &error)) {
            log += csvCell(relative) + QStringLiteral(";errore;") + csvCell(error.simplified().left(200)) + QStringLiteral("\r\n");
            ++failed;
            continue;
        }
        const KnownLibraryBlock* k = knownLibraryBlock(hashes[static_cast<std::size_t>(i)]);
        LibraryCleanReport one;
        cleanLibraryDrawing(doc, QStringLiteral("B%1").arg(i + 1, 5, 10, QLatin1Char('0')).toStdString(), scale, one);
        report.removed += one.removed;
        for (const auto& [w, c] : one.words)
            report.words[w] += c;
        for (const auto& [what, text] : one.log)
            log += csvCell(relative) + QLatin1Char(';') + QString::fromStdString(what) + QLatin1Char(';')
                   + csvCell(QString::fromStdString(text)) + QStringLiteral("\r\n");
        const QString name = k ? QStringLiteral("fronte-quadro/%1.dxf").arg(QString::fromLatin1(k->code))
                               : QStringLiteral("altri/%1.dxf").arg(++other, 5, 10, QLatin1Char('0'));
        const QString target = out.filePath(name);
        QDir().mkpath(QFileInfo(target).path());
        std::string writeError;
        if (!writeDxf(doc, target.toStdWString(), &writeError, nullptr, DxfVersion::R2000)) {
            log += csvCell(relative) + QStringLiteral(";errore;") + csvCell(QString::fromStdString(writeError)) + QStringLiteral("\r\n");
            ++failed;
            continue;
        }
        ++written;
        if (k)
            recognised.push_back({target, k});
        // Indice: per i riconosciuti la descrizione generica, per gli altri la voce del menu della libreria.
        QString category, label;
        if (k) {
            category = tr("fronte quadro");
            label = QString::fromUtf8(k->text);
        } else if (const auto m = menu.find(QFileInfo(relative).path().toLower() + QLatin1Char('/')
                                            + QFileInfo(relative).completeBaseName().toLower());
                   m != menu.end()) {
            for (const std::string& p : m->second.path)
                category += (category.isEmpty() ? QString() : QStringLiteral(" > ")) + QString::fromStdString(p);
            label = QString::fromStdString(m->second.label);
        }
        index += csvCell(name) + QLatin1Char(';') + csvCell(category) + QLatin1Char(';') + csvCell(label) + QLatin1Char(';')
                 + csvCell(relative) + QStringLiteral("\r\n");
    }
    if (progress)
        progress->setValue(static_cast<int>(files.size()));
    writeUtf8(out.filePath(QStringLiteral("indice.csv")), index, true);
    writeUtf8(out.filePath(QStringLiteral("registro-pulizia.csv")), log, true);
    writeUtf8(out.filePath(QStringLiteral("LEGGIMI.txt")),
              tr("Blocchi ricavati dalla libreria installata in %1 per uso personale di chi l'ha installata: non "
                 "ridistribuirli. Origine di ogni file in indice.csv, testi e loghi tolti in registro-pulizia.csv. "
                 "Le condizioni d'uso restano quelle della libreria.\r\n")
                  .arg(QDir::toNativeSeparators(r.source)),
              true);

    QStringList lines;
    lines << tr("Importa libreria: %1 disegni scritti in %2, %3 riconosciuti per il fronte quadro, %4 non letti, %5 "
                "oggetti tolti (registro-pulizia.csv), %6 s%7.")
                 .arg(written)
                 .arg(QDir::toNativeSeparators(out.absolutePath()))
                 .arg(recognised.size())
                 .arg(failed)
                 .arg(report.removed)
                 .arg(timer.elapsed() / 1000)
                 .arg(cancelled ? tr(", annullata") : QString());
    if (r.catalogs && !recognised.empty() && !cancelled) {
        const std::vector<std::string> keywords = libraryKeywords(report.words);
        const auto relativeTo = [&recognised](const QString& catalog) {
            std::vector<ImportedLibraryBlock> blocks;
            const QDir dir = QFileInfo(catalog).dir();
            for (const auto& [path, k] : recognised)
                blocks.push_back({QDir::fromNativeSeparators(dir.relativeFilePath(path)).toStdString(), k});
            return blocks;
        };
        QString backup;
        if (!r.devices.isEmpty()) {
            const QString e = updateCatalog(
                r.devices, "codice;tipo;poli;in_min;in_max;moduli;larghezza;altezza;blocco;nome_blocco;riferimento;fonte;modello;piastra;famiglia",
                libraryDeviceRows(relativeTo(r.devices), family.toStdString(), keywords), family.toStdString(), 14, &backup);
            lines << (e.isEmpty() ? tr("   catalogo apparecchi aggiornato: %1 (famiglia %2%3)")
                                        .arg(QDir::toNativeSeparators(r.devices), family,
                                             backup.isEmpty() ? QString() : tr(", copia %1").arg(QFileInfo(backup).fileName()))
                                  : tr("   catalogo apparecchi: %1").arg(e));
        }
        backup.clear();
        if (!r.enclosures.isEmpty()) {
            const std::string rows = libraryEnclosureRows(relativeTo(r.enclosures), family.toStdString());
            if (!rows.empty()) {
                const QString e = updateCatalog(
                    r.enclosures, "codice;tipo;larghezza;altezza;profondita;ip;moduli_per_fila;riferimento;fonte;blocco;nome_blocco;famiglia",
                    rows, family.toStdString(), 11, &backup);
                lines << (e.isEmpty() ? tr("   catalogo carpenterie aggiornato: %1%2")
                                            .arg(QDir::toNativeSeparators(r.enclosures),
                                                 backup.isEmpty() ? QString() : tr(" (copia %1)").arg(QFileInfo(backup).fileName()))
                                      : tr("   catalogo carpenterie: %1").arg(e));
            }
        }
    } else if (r.catalogs && recognised.empty()) {
        lines << tr("   nessun blocco riconosciuto: cataloghi non cambiati (i blocchi si collegano a mano nel catalogo)");
    }
    for (const QString& l : lines)
        print(l);
    if (!cancelled)
        guideCommandFinished(QStringLiteral("IMPORTALIBRERIA"), false);
    return {};
}

void MainWindow::showLibraryImport()
{
    QSettings s = importSettings();
    LibraryImportRequest r;
    r.source = s.value(QStringLiteral("ImportaLibreria/source")).toString();
    if (r.source.isEmpty() || !QFileInfo::exists(r.source)) {
        const QStringList found = findInstalledLibraries();
        r.source = found.value(0);
    }
    r.family = s.value(QStringLiteral("ImportaLibreria/family")).toString();
    r.destination = s.value(QStringLiteral("ImportaLibreria/destination"),
                            documentsFolder(QStringLiteral("CartesioCAD-blocchi"))).toString();
    r.unit = s.value(QStringLiteral("ImportaLibreria/unit"), 0).toInt();
    r.catalogs = s.value(QStringLiteral("ImportaLibreria/catalogs"), true).toBool();
    r.devices = s.value(QStringLiteral("FronteQuadro/devices")).toString();
    if (r.devices.isEmpty())
        r.devices = documentsFolder(QStringLiteral("CartesioCAD-blocchi/apparecchi.csv"));
    r.enclosures = s.value(QStringLiteral("FronteQuadro/catalog")).toString();
    if (r.enclosures.isEmpty())
        r.enclosures = documentsFolder(QStringLiteral("CartesioCAD-cataloghi/carpenterie.csv"));

    FormDialog dialog(tr("Importa libreria del costruttore"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.group(tr("Libreria installata"));
    dialog.path(tr("Cartella della libreria:"), r.source, FormDialog::Folder, tr("trovata da sola se installata"));
    dialog.text(tr("Nome nei cataloghi (famiglia):"), r.family, tr("vuoto = nome della cartella"));
    dialog.choice(tr("Unità dei disegni:"),
                  {{tr("Automatica"), 0}, {tr("Centimetri"), 1}, {tr("Millimetri"), 2}}, r.unit,
                  [&r](const QVariant& v) { r.unit = v.toInt(); });
    dialog.group(tr("Risultato"));
    dialog.path(tr("Cartella dei blocchi:"), r.destination, FormDialog::Folder);
    dialog.check(tr("Aggiorna i cataloghi del fronte quadro (solo le righe di questa famiglia)"), r.catalogs);
    dialog.path(tr("Catalogo apparecchi:"), r.devices, FormDialog::SaveFile, QString(), tr("CSV (*.csv)"));
    dialog.path(tr("Catalogo carpenterie:"), r.enclosures, FormDialog::SaveFile, QString(), tr("CSV (*.csv)"));
    dialog.note(tr("Si usa solo la libreria già installata su questo PC: CartesioCAD non ne contiene nulla. I blocchi "
                   "vengono convertiti in mm, ripuliti da loghi, sigle e codici e salvati nella cartella scelta, per uso "
                   "personale: non vanno ridistribuiti. Controlla le condizioni d'uso della libreria: se vietano di "
                   "modificarla, non importarla (collega i file originali a mano nel catalogo). Dei cataloghi si "
                   "aggiornano solo le righe di questa famiglia, con una copia di sicurezza accanto. Ci vogliono "
                   "alcuni minuti per qualche migliaio di disegni."));
    if (dialog.exec() != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("ImportaLibreria/source"), r.source);
    s.setValue(QStringLiteral("ImportaLibreria/family"), r.family);
    s.setValue(QStringLiteral("ImportaLibreria/destination"), r.destination);
    s.setValue(QStringLiteral("ImportaLibreria/unit"), r.unit);
    s.setValue(QStringLiteral("ImportaLibreria/catalogs"), r.catalogs);
    const QString error = runLibraryImport(r, true);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Importa libreria del costruttore"), error);
}

} // namespace cartesio
