// Aggiornamenti dalla finestra principale: controllo (a richiesta, o all'avvio se acceso nelle Opzioni, al massimo una
// volta al giorno), download con avanzamento, conferma esplicita per le versioni major, installazione sul posto e
// riavvio; ritorno alla versione precedente. La logica (rete, firma, file) è in Updater.

#include "app/AppOptions.h"
#include "app/MainWindow.h"
#include "app/Updater.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace cartesio {

namespace {

QString appDirectory() { return QCoreApplication::applicationDirPath(); }

QSettings updateStore() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

QString megabytes(qint64 bytes) { return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1); }

} // namespace

Updater* MainWindow::updater()
{
    if (!updater_)
        updater_ = new Updater(this);
    return updater_;
}

void MainWindow::startupUpdates()
{
    Updater::cleanup(appDirectory());
    // Solo se l'utente l'ha chiesto nelle Opzioni, mai negli script di prova, al massimo una volta al giorno.
    if (!appOptions().checkUpdatesAtStart || QApplication::arguments().size() > 1)
        return;
    QSettings store = updateStore();
    const QDateTime last = store.value(QStringLiteral("updates/lastCheck")).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600)
        return;
    QTimer::singleShot(4000, this, [this] { checkForUpdates(false); });
}

void MainWindow::checkForUpdates(bool interactive)
{
    if (updateBusy_)
        return;
    updateBusy_ = true;
    if (interactive)
        print(tr("Controllo degli aggiornamenti su GitHub..."));
    updater()->check([this, interactive](std::optional<UpdateOffer> offer, const QString& error) {
        updateBusy_ = false;
        QSettings store = updateStore();
        store.setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentDateTimeUtc());
        if (!error.isEmpty()) {
            print(tr("Aggiornamenti: controllo non riuscito (%1).").arg(error));
            if (interactive)
                QMessageBox::warning(this, tr("Aggiornamenti"), tr("Controllo non riuscito: %1").arg(error));
            return;
        }
        if (!offer) {
            print(tr("Aggiornamenti: CartesioCAD %1 è la versione più recente.").arg(QApplication::applicationVersion()));
            if (interactive)
                QMessageBox::information(this, tr("Aggiornamenti"),
                                         tr("CartesioCAD %1 è la versione più recente.").arg(QApplication::applicationVersion()));
            return;
        }
        pendingUpdate_ = offer;
        const QString version = QString::fromStdString(offer->version.text());
        if (updateAct_)
            updateAct_->setText(tr("Aggiornamento disponibile: %1...").arg(version));
        if (interactive)
            showUpdateOffer(*offer);
        else   // controllo all'avvio: solo un messaggio, niente finestre
            print(tr("È disponibile CartesioCAD %1: menu dell'applicazione > Aggiornamento disponibile.").arg(version));
    });
}

void MainWindow::showUpdateOffer(const UpdateOffer& offer)
{
    const Version current = Updater::currentVersion();
    const bool major = isMajorUpdate(current, offer.version, false);   // il flag firmato si rilegge dopo il download
    QMessageBox box(this);
    box.setWindowTitle(tr("Aggiornamento disponibile"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("È disponibile CartesioCAD %1%2 (installata: %3).")
                    .arg(QString::fromStdString(offer.version.text()), offer.prerelease ? tr(" (anteprima)") : QString(),
                         QString::fromStdString(current.text())));
    box.setInformativeText(tr("Download: %1 MB. Il pacchetto è firmato dall'autore e viene verificato prima di installarlo; "
                              "la versione attuale resta disponibile per tornare indietro.%2")
                               .arg(megabytes(offer.archiveSize),
                                    major ? tr("\n\nÈ una versione MAJOR: cambiamenti importanti, leggere le note.") : QString()));
    box.setDetailedText(offer.notes);
    QPushButton* install = box.addButton(tr("Scarica e installa"), QMessageBox::AcceptRole);
    QPushButton* page = box.addButton(tr("Pagina della versione"), QMessageBox::ActionRole);
    box.addButton(tr("Più tardi"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == page)
        QDesktopServices::openUrl(QUrl(offer.pageUrl));
    else if (box.clickedButton() == install)
        downloadAndInstall(offer);
}

void MainWindow::downloadAndInstall(const UpdateOffer& offer)
{
    auto* progress = new QProgressDialog(tr("Download di CartesioCAD %1...").arg(QString::fromStdString(offer.version.text())),
                                         tr("Annulla"), 0, 1000, this);
    progress->setWindowTitle(tr("Aggiornamento"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    connect(progress, &QProgressDialog::canceled, this, [this] { updater()->cancel(); });
    updateBusy_ = true;
    updater()->download(
        offer,
        [progress](qint64 received, qint64 total) {
            if (total > 0)
                progress->setValue(static_cast<int>(1000.0 * static_cast<double>(received) / static_cast<double>(total)));
        },
        [this, progress, offer](const QString& archive, std::optional<UpdateManifest> manifest, const QString& error) {
            updateBusy_ = false;
            progress->deleteLater();
            if (!error.isEmpty() || !manifest) {
                print(tr("Aggiornamento non riuscito: %1").arg(error));
                QMessageBox::warning(this, tr("Aggiornamento"), tr("Aggiornamento non riuscito: %1").arg(error));
                return;
            }
            const Version current = Updater::currentVersion();
            if (isMajorUpdate(current, manifest->version, manifest->major)
                && QMessageBox::warning(this, tr("Versione major"),
                                        tr("CartesioCAD %1 è una versione MAJOR, con cambiamenti importanti rispetto alla %2.\n\n"
                                           "Leggere le note della versione prima di continuare. Si potrà comunque tornare alla "
                                           "versione attuale dal menu dell'applicazione.\n\nInstallare ora?")
                                            .arg(QString::fromStdString(manifest->version.text()),
                                                 QString::fromStdString(current.text())),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                       != QMessageBox::Yes) {
                print(tr("Aggiornamento rimandato: l'archivio scaricato e verificato resta in %1.")
                          .arg(QDir::toNativeSeparators(Updater::downloadFolder())));
                return;
            }
            QString installError;
            // Installato in Programmi: l'installazione la fa una copia del programma con i diritti di amministratore
            // (richiesta UAC), che verifica di nuovo firma e archivio prima di toccare la cartella.
            const bool elevated = !Updater::folderWritable(appDirectory());
            const bool installed = elevated
                ? Updater::runElevated({QStringLiteral("--installa-aggiornamento"),
                                        QDir::toNativeSeparators(Updater::manifestPathFor(archive)),
                                        QDir::toNativeSeparators(archive)},
                                       &installError)
                : Updater::install(archive, *manifest, appDirectory(), &installError);
            if (!installed) {
                print(tr("Aggiornamento non installato: %1").arg(installError));
                QMessageBox::warning(this, tr("Aggiornamento"), tr("Aggiornamento non installato: %1").arg(installError));
                return;
            }
            QFile::remove(archive);
            QFile::remove(Updater::manifestPathFor(archive));
            QFile::remove(Updater::manifestPathFor(archive) + QStringLiteral(".sig"));
            pendingUpdate_.reset();
            if (updateAct_)
                updateAct_->setText(tr("Controlla aggiornamenti..."));
            print(tr("CartesioCAD %1 installato: parte al prossimo avvio.").arg(QString::fromStdString(manifest->version.text())));
            askRestart(tr("CartesioCAD %1 è installato. Riavviare ora? (Prima si chiudono i disegni, con la richiesta di "
                          "salvare quelli modificati.)")
                           .arg(QString::fromStdString(manifest->version.text())));
        });
}

void MainWindow::rollbackUpdate()
{
    const QString previous = Updater::previousVersion(appDirectory());
    if (previous.isEmpty()) {
        QMessageBox::information(this, tr("Versione precedente"), tr("Non c'è una versione precedente da ripristinare."));
        return;
    }
    if (QMessageBox::question(this, tr("Versione precedente"),
                              tr("Tornare a CartesioCAD %1 (la versione installata prima dell'ultimo aggiornamento)?").arg(previous))
        != QMessageBox::Yes)
        return;
    QString error;
    const bool restored = Updater::folderWritable(appDirectory())
                              ? Updater::rollback(appDirectory(), &error)
                              : Updater::runElevated({QStringLiteral("--ripristina-versione")}, &error);
    if (!restored) {
        QMessageBox::warning(this, tr("Versione precedente"), error);
        return;
    }
    print(tr("Ripristinata la versione %1: parte al prossimo avvio.").arg(previous));
    askRestart(tr("Ripristinata la versione %1. Riavviare ora?").arg(previous));
}

void MainWindow::askRestart(const QString& question)
{
    if (QMessageBox::question(this, tr("Riavvio"), question) != QMessageBox::Yes)
        return;
    if (close())   // chiede di salvare i disegni modificati; se l'utente annulla, niente riavvio
        QProcess::startDetached(QDir(appDirectory()).filePath(QStringLiteral("CartesioCAD.exe")), {});
}

// Diagnosi negli script (senza finestre).
void MainWindow::scriptUpdateCheck()
{
    QEventLoop loop;
    updater()->check([this, &loop](std::optional<UpdateOffer> offer, const QString& error) {
        if (!error.isEmpty())
            print(tr("-AGGIORNAMENTICONTROLLA: errore %1").arg(error));
        else if (!offer)
            print(tr("-AGGIORNAMENTICONTROLLA: nessun aggiornamento firmato più recente di %1").arg(QApplication::applicationVersion()));
        else
            print(tr("-AGGIORNAMENTICONTROLLA: %1 %2 (%3 byte) %4")
                      .arg(QString::fromStdString(offer->version.text()), offer->prerelease ? tr("anteprima") : tr("stabile"))
                      .arg(offer->archiveSize)
                      .arg(offer->pageUrl));
        loop.quit();
    });
    loop.exec();
}

void MainWindow::scriptUpdateLocal(const QString& manifestPath, const QString& archive, const QString& folder)
{
    QFile manifestFile(manifestPath), signatureFile(manifestPath + QStringLiteral(".sig"));
    if (!manifestFile.open(QIODevice::ReadOnly) || !signatureFile.open(QIODevice::ReadOnly)) {
        print(tr("-AGGIORNAMENTOLOCALE: manifesto o firma non leggibili"));
        return;
    }
    const QByteArray data = manifestFile.readAll();
    if (!Updater::verifySignature(data, signatureFile.readAll())) {
        print(tr("-AGGIORNAMENTOLOCALE: firma NON valida, rifiutato"));
        return;
    }
    QString error;
    const auto manifest = Updater::parseManifest(data, &error);
    if (!manifest || !Updater::verifyArchive(archive, *manifest, &error)) {
        print(tr("-AGGIORNAMENTOLOCALE: %1").arg(error));
        return;
    }
    if (!Updater::install(archive, *manifest, folder, &error)) {
        print(tr("-AGGIORNAMENTOLOCALE: %1").arg(error));
        return;
    }
    print(tr("-AGGIORNAMENTOLOCALE: installata la %1 in %2 (major: %3)")
              .arg(QString::fromStdString(manifest->version.text()), QDir::toNativeSeparators(folder),
                   isMajorUpdate(Updater::currentVersion(), manifest->version, manifest->major) ? tr("sì") : tr("no")));
}

void MainWindow::scriptUpdateDownload(const QString& folder)
{
    QEventLoop loop;
    std::optional<UpdateOffer> found;
    QString error;
    updater()->check([&](std::optional<UpdateOffer> offer, const QString& e) {
        found = offer;
        error = e;
        loop.quit();
    });
    loop.exec();
    if (!error.isEmpty() || !found) {
        print(tr("-AGGIORNAMENTOSCARICA: %1").arg(error.isEmpty() ? tr("nessun aggiornamento") : error));
        return;
    }
    updater()->download(*found, {}, [&](const QString& archive, std::optional<UpdateManifest> manifest, const QString& e) {
        if (!e.isEmpty() || !manifest)
            print(tr("-AGGIORNAMENTOSCARICA: %1").arg(e));
        else if (QString installError; !Updater::install(archive, *manifest, folder, &installError))
            print(tr("-AGGIORNAMENTOSCARICA: %1").arg(installError));
        else
            print(tr("-AGGIORNAMENTOSCARICA: scaricata, verificata e installata la %1 in %2")
                      .arg(QString::fromStdString(manifest->version.text()), QDir::toNativeSeparators(folder)));
        loop.quit();
    });
    loop.exec();
}

void MainWindow::scriptUpdateRollback(const QString& folder)
{
    const QString previous = Updater::previousVersion(folder);
    QString error;
    if (Updater::rollback(folder, &error))
        print(tr("-VERSIONEPRECEDENTE: ripristinata la %1 in %2").arg(previous, QDir::toNativeSeparators(folder)));
    else
        print(tr("-VERSIONEPRECEDENTE: %1").arg(error));
}

} // namespace cartesio
