// Affidabilità sui disegni veri: recupero dei salvataggi automatici dopo una chiusura anomala, RECOVER (apertura
// con correzione AUDIT) ed ETRANSMIT (archivio ZIP con il disegno e tutti i file collegati).
#include "app/MainWindow.h"

#include "app/DrawingWidget.h"
#include "app/ImageLibrary.h"
#include "app/PlotEngine.h"
#include "app/TextRender.h"
#include "core/Document.h"
#include "core/io/Dxf.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <private/qzipwriter_p.h>

#include <filesystem>
#include <vector>

namespace cartesio {

namespace {

QString autosaveFolder()
{
    return QDir::tempPath() + QStringLiteral("/CartesioCAD");
}

QString key(const QString& path)
{
    return QFileInfo(path).absoluteFilePath().toLower();
}

} // namespace

QString MainWindow::autosavePath(const DrawingWidget* drawing) const
{
    const QString name = QFileInfo(drawing->displayName()).completeBaseName();
    return autosaveFolder() + QLatin1Char('/') + name + QStringLiteral("_salvataggio_automatico.dxf");
}

// All'avvio: salvataggi automatici rimasti da una sessione che non si è chiusa bene (quelli delle sessioni chiuse
// regolarmente vengono cancellati). Si propone di riaprirli come disegni da salvare.
void MainWindow::checkAutosaveRecovery()
{
    const QDir dir(autosaveFolder());
    QFileInfoList left;
    const QDateTime limit = QDateTime::currentDateTime().addDays(-14);
    for (const QFileInfo& f : dir.entryInfoList({QStringLiteral("*_salvataggio_automatico.dxf")}, QDir::Files, QDir::Time))
        if (!autosaved_.contains(f.absoluteFilePath()) && f.lastModified() >= limit)
            left << f;
    if (left.isEmpty())
        return;
    QStringList lines;
    for (const QFileInfo& f : left)
        lines << QStringLiteral("%1  (%2)").arg(f.fileName().remove(QStringLiteral("_salvataggio_automatico.dxf")),
                                               QLocale().toString(f.lastModified(), QLocale::ShortFormat));
    QMessageBox box(QMessageBox::Warning, tr("Recupero disegni"),
                    tr("CartesioCAD non si è chiuso correttamente l'ultima volta. Sono rimasti questi salvataggi "
                       "automatici:\n\n%1\n\nSi aprono come disegni senza nome, da salvare con Salva con nome.")
                        .arg(lines.join(QLatin1Char('\n'))),
                    QMessageBox::NoButton, this);
    QPushButton* open = box.addButton(tr("Apri"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(tr("Elimina"), QMessageBox::DestructiveRole);
    box.addButton(tr("Più tardi"), QMessageBox::RejectRole);
    box.setDefaultButton(open);
    box.exec();
    if (box.clickedButton() == discard) {
        for (const QFileInfo& f : left)
            QFile::remove(f.absoluteFilePath());
        print(tr("Salvataggi automatici eliminati: %1.").arg(left.size()));
        return;
    }
    if (box.clickedButton() != open)
        return;
    for (const QFileInfo& f : left) {
        recoveredAutosaves_.insert(key(f.absoluteFilePath()));
        openFileAs(f.absoluteFilePath(), true);
    }
}

// RECOVER: il file si apre (anche se danneggiato, la lettura salta le parti illeggibili) e si corregge con AUDIT.
void MainWindow::recoverFile(const QString& path)
{
    QString file = path;
    if (file.isEmpty()) {
        file = QFileDialog::getOpenFileName(this, tr("Recupera un file danneggiato"), QString(), tr("Disegni (*.dwg *.dxf)"));
        if (file.isEmpty())
            return;
    }
    if (!QFileInfo::exists(file)) {
        print(tr("RECOVER: file non trovato: %1").arg(QDir::toNativeSeparators(file)));
        return;
    }
    print(tr("RECOVER: %1").arg(QDir::toNativeSeparators(file)));
    recoverPaths_.insert(key(file));
    if (!openFile(file)) {
        recoverPaths_.remove(key(file));
        return;
    }
    // Già aperto (si è solo passati alla sua scheda) o letto subito: il controllo si fa adesso.
    if (recoverPaths_.contains(key(file)))
        if (DrawingWidget* d = current(); d && key(d->filePath()) == key(file))
            auditAfterOpen(file);
}

// Fine della lettura di un file: RECOVER corregge il disegno; un salvataggio automatico recuperato diventa un
// disegno da salvare (e il file di recupero si cancella).
void MainWindow::auditAfterOpen(const QString& path)
{
    const QString k = key(path);
    if (recoveredAutosaves_.remove(k)) {
        if (DrawingWidget* d = current()) {
            d->document().touch();   // da salvare: chiudendolo senza salvare il programma chiede
            d->notifyChanged();
        }
        QFile::remove(path);
        print(tr("Disegno recuperato da %1: salvarlo con Salva con nome.").arg(QFileInfo(path).fileName()));
        return;
    }
    if (!recoverPaths_.remove(k))
        return;
    DrawingWidget* d = nullptr;
    for (int i = 1; i < stack_->count(); ++i)
        if (auto* w = qobject_cast<DrawingWidget*>(stack_->widget(i)); w && key(w->filePath()) == k)
            d = w;
    if (!d)
        return;
    const auto report = d->document().audit(true);
    const std::size_t shown = std::min<std::size_t>(report.lines.size(), 200);
    for (std::size_t i = 0; i < shown; ++i)
        print(QString::fromStdString(report.lines[i]));
    if (report.lines.size() > shown)
        print(tr("... e altri %1.").arg(report.lines.size() - shown));
    print(report.found == 0 ? tr("RECOVER: nessun errore trovato.")
                            : tr("RECOVER: %1 errori trovati e corretti. Salvare il disegno per conservare le correzioni.")
                                  .arg(report.found));
    d->notifyChanged();
}

// ETRANSMIT: nello ZIP il disegno (DXF 2000) con i riferimenti esterni e le immagini in sottocartelle (i percorsi
// nel disegno vengono riscritti), le tabelle stili e un rapporto con quello che manca. I font non si copiano
// (sono di chi li ha prodotti e la licenza di solito non permette di passarli ad altri): il rapporto li elenca.
bool MainWindow::etransmit(const QString& target)
{
    DrawingWidget* d = current();
    if (!d) {
        print(tr("ETRANSMIT: nessun disegno aperto."));
        return false;
    }
    Document& doc = d->document();
    const QString drawingPath = d->filePath();
    const QString base = QFileInfo(d->displayName()).completeBaseName();
    QString zipPath = target;
    if (zipPath.isEmpty()) {
        const QString folder = drawingPath.isEmpty()
                                   ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                   : QFileInfo(drawingPath).path();
        zipPath = QFileDialog::getSaveFileName(this, tr("Archivia per l'invio"),
                                               folder + QLatin1Char('/') + base + tr("_trasmissione.zip"),
                                               tr("Archivio ZIP (*.zip)"));
        if (zipPath.isEmpty())
            return false;
    }

    struct Item {
        QString source;
        QString name;
    };
    std::vector<Item> files;
    QStringList missing, notes;
    QSet<QString> used{(base + QStringLiteral(".dxf")).toLower()};
    const auto place = [&used](const QString& folder, const QString& source) {
        const QFileInfo info(source);
        const QString prefix = folder.isEmpty() ? QString() : folder + QLatin1Char('/');
        QString name = prefix + info.fileName();
        for (int k = 2; used.contains(name.toLower()); ++k)
            name = prefix + QStringLiteral("%1_%2.%3").arg(info.completeBaseName()).arg(k).arg(info.suffix());
        used.insert(name.toLower());
        return name;
    };

    // Riferimenti esterni e immagini: i percorsi nel disegno diventano quelli dentro l'archivio (ripristinati dopo).
    std::vector<std::pair<BlockDef*, std::string>> xrefPaths;
    for (const auto& b : doc.blocks()) {
        if (!b->isXref())
            continue;
        QString found = QString::fromStdString(b->xrefFound);
        if (found.isEmpty() || !QFileInfo::exists(found))
            found = ImageLibrary::locate(drawingPath, b->xrefPath);
        if (found.isEmpty() || !QFileInfo::exists(found)) {
            missing << tr("Riferimento esterno: %1").arg(QString::fromStdString(b->xrefPath));
            continue;
        }
        const QString name = place(QStringLiteral("riferimenti"), found);
        files.push_back({found, name});
        xrefPaths.emplace_back(b.get(), b->xrefPath);
        b->xrefPath = name.toStdString();
    }
    std::vector<std::pair<ImageEntity*, std::string>> imagePaths;
    std::map<QString, QString> imageNames;   // stesso file in più immagini: una copia sola
    for (const auto& e : doc.entities()) {
        if (e->type() != EntityType::Image)
            continue;
        auto& image = const_cast<ImageEntity&>(static_cast<const ImageEntity&>(*e));
        if (image.isOle() || image.path.empty())
            continue;
        const QString found = ImageLibrary::locate(drawingPath, image.path);
        if (found.isEmpty() || !QFileInfo::exists(found)) {
            missing << tr("Immagine: %1").arg(QString::fromStdString(image.path));
            continue;
        }
        QString& name = imageNames[key(found)];
        if (name.isEmpty()) {
            name = place(QStringLiteral("immagini"), found);
            files.push_back({found, name});
        }
        imagePaths.emplace_back(&image, image.path);
        image.path = name.toStdString();
    }

    // Disegno: DXF 2000 (conserva tutto e si apre in tutti i CAD) con i percorsi dell'archivio.
    QTemporaryDir temp;
    const QString dxf = temp.path() + QLatin1Char('/') + base + QStringLiteral(".dxf");
    std::string failure;
    const bool written = temp.isValid()
                         && writeDxf(doc, std::filesystem::path(dxf.toStdWString()), &failure, nullptr, DxfVersion::R2000);
    for (auto& [block, path] : xrefPaths)
        block->xrefPath = path;
    for (auto& [image, path] : imagePaths)
        image->path = path;
    if (!written) {
        print(tr("ETRANSMIT: impossibile scrivere il disegno: %1").arg(QString::fromStdString(failure)));
        return false;
    }

    // Font SHX degli stili di testo: solo l'elenco nel rapporto. Poi le tabelle stili dei layout.
    QStringList shxNames;
    for (const TextStyle& s : doc.textStyles()) {
        std::vector<std::string> shx;
        if (s.isShx() && !s.font.empty())
            shx.push_back(s.font);
        if (!s.bigFont.empty())
            shx.push_back(s.bigFont);
        for (const std::string& f : shx) {
            QString file = QString::fromStdString(f).toLower();
            if (!file.contains(QLatin1Char('.')))
                file += QStringLiteral(".shx");
            if (!shxNames.contains(file))
                shxNames << file;
        }
    }
    if (!shxNames.isEmpty())
        notes << tr("Font SHX usati dal disegno, non inclusi (chi riceve deve averli per conto suo; quelli che "
                    "mancano si sostituiscono): %1").arg(shxNames.join(QStringLiteral(", ")));
    QSet<QString> tables;
    for (const Layout& layout : doc.layouts()) {
        const QString name = QString::fromStdString(layout.plot.styleSheet);
        if (name.isEmpty() || tables.contains(name.toLower()))
            continue;
        tables.insert(name.toLower());
        QString path = name;
        if (QFileInfo(path).isRelative() || !QFileInfo::exists(path))
            path = plotStylesFolder() + QLatin1Char('/') + QFileInfo(name).fileName();
        if (QFileInfo::exists(path))
            files.push_back({path, place(QStringLiteral("stili di stampa"), path)});
        else
            notes << tr("Tabella stili %1: predefinita di CartesioCAD, non serve un file.").arg(QFileInfo(name).fileName());
    }
    bool trueType = false;
    for (const TextStyle& s : doc.textStyles())
        trueType = trueType || !s.isShx();
    if (trueType)
        notes << tr("I font TrueType non si includono: sono installati con Windows o vanno installati a parte.");

    // Archivio.
    QZipWriter zip(zipPath);
    if (zip.status() != QZipWriter::NoError) {
        print(tr("ETRANSMIT: impossibile creare %1.").arg(QDir::toNativeSeparators(zipPath)));
        return false;
    }
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    QFile main(dxf);
    if (main.open(QIODevice::ReadOnly))
        zip.addFile(base + QStringLiteral(".dxf"), main.readAll());
    QStringList included{base + QStringLiteral(".dxf")};
    for (const Item& item : files) {
        QFile f(item.source);
        if (!f.open(QIODevice::ReadOnly)) {
            missing << tr("Non leggibile: %1").arg(QDir::toNativeSeparators(item.source));
            continue;
        }
        zip.addFile(item.name, f.readAll());
        included << QStringLiteral("%1   (da %2)").arg(item.name, QDir::toNativeSeparators(item.source));
    }
    QString report = tr("Trasmissione di %1 - %2\r\n\r\n")
                         .arg(drawingPath.isEmpty() ? d->displayName() : QDir::toNativeSeparators(drawingPath),
                              QLocale().toString(QDateTime::currentDateTime(), QLocale::LongFormat));
    report += tr("File inclusi:\r\n  ") + included.join(QStringLiteral("\r\n  ")) + QStringLiteral("\r\n");
    if (!missing.isEmpty())
        report += tr("\r\nNon trovati (non inclusi):\r\n  ") + missing.join(QStringLiteral("\r\n  ")) + QStringLiteral("\r\n");
    if (!notes.isEmpty())
        report += tr("\r\nNote:\r\n  ") + notes.join(QStringLiteral("\r\n  ")) + QStringLiteral("\r\n");
    report += tr("\r\nIl disegno è in DXF 2000: i percorsi dei riferimenti esterni e delle immagini puntano alle "
                 "sottocartelle dell'archivio.\r\n");
    zip.addFile(tr("Rapporto trasmissione.txt"), QByteArray("\xEF\xBB\xBF") + report.toUtf8());   // UTF-8 con BOM (Blocco note)
    zip.close();
    print(tr("ETRANSMIT: %1 (%2 file%3).")
              .arg(QDir::toNativeSeparators(zipPath))
              .arg(included.size() + 1)
              .arg(missing.isEmpty() ? QString() : tr(", %1 non trovati: vedi il rapporto").arg(missing.size())));
    return true;
}

} // namespace cartesio
