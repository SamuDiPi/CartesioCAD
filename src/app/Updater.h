#pragma once

// Aggiornamenti di CartesioCAD dalle versioni pubblicate su GitHub (SamuDiPi/CartesioCAD).
//
// Ogni versione pubblicata porta tre file: l'archivio del programma (CartesioCAD-<v>-win64.zip), un manifesto
// (CartesioCAD-<v>-aggiornamento.json: versione, nome, dimensione e SHA-256 dell'archivio, "major") e la firma
// Ed25519 del manifesto (.json.sig), fatta con la chiave privata dell'autore (scripts/sign-release.sh). Il programma
// contiene solo la chiave pubblica: installa un archivio solo se la firma del manifesto è valida e l'archivio ha
// dimensione e impronta scritte nel manifesto. Connessioni solo HTTPS verso GitHub, e solo quando l'utente lo chiede
// o ha acceso l'opzione del controllo all'avvio.
//
// L'installazione è sul posto: i file nuovi prendono il posto di quelli vecchi nella cartella del programma; i
// vecchi vanno in "versione-precedente" (una sola), da cui si può tornare indietro. Se qualcosa va storto durante la
// sostituzione, si rimette tutto com'era. I file che l'archivio non contiene (per esempio la cartella "fonts" con i
// font aggiunti dall'utente) non si toccano.

#include "core/Version.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace cartesio {

struct UpdateManifest {
    Version version;
    bool major = false;
    QString archive;   // nome del file dell'archivio
    qint64 size = 0;   // byte
    QByteArray sha256; // esadecimale minuscolo
    QString date;
};

struct UpdateOffer {
    Version version;
    QString tag;
    QString title;
    QString notes;        // testo della pagina della versione (accorciato)
    QString pageUrl;
    QString manifestUrl;
    QString signatureUrl;
    QString archiveUrl;
    qint64 archiveSize = 0;
    bool prerelease = false;
};

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    // Versione in esecuzione.
    static Version currentVersion();

    // Firma Ed25519 (64 byte) di `data` fatta con la chiave dell'autore.
    static bool verifySignature(const QByteArray& data, const QByteArray& signature);
    static std::optional<UpdateManifest> parseManifest(const QByteArray& json, QString* error);
    // Dimensione e SHA-256 dell'archivio come nel manifesto.
    static bool verifyArchive(const QString& archive, const UpdateManifest& manifest, QString* error);

    // Cerca la versione più recente pubblicata, più nuova di quella in esecuzione e con manifesto firmato
    // (anteprime comprese). done(offerta o niente, errore).
    void check(std::function<void(std::optional<UpdateOffer>, QString)> done);
    // Scarica manifesto, firma e archivio, e li verifica. progress(byte, totale); done(archivio, manifesto, errore).
    void download(const UpdateOffer& offer, std::function<void(qint64, qint64)> progress,
                  std::function<void(QString, std::optional<UpdateManifest>, QString)> done);
    void cancel();

    // Installa l'archivio (già verificato) nella cartella del programma.
    static bool install(const QString& archive, const QString& appDir, QString* error);
    // Torna alla versione precedente (quella in "versione-precedente").
    static bool rollback(const QString& appDir, QString* error);
    // Versione salvata in "versione-precedente" (vuota se non c'è).
    static QString previousVersion(const QString& appDir);
    // All'avvio: toglie i resti di un aggiornamento (cartelle di lavoro e file scartati).
    static void cleanup(const QString& appDir);
    // Cartella in cui si scaricano gli aggiornamenti.
    static QString downloadFolder();

private:
    void get(const QString& url, qint64 limit, std::function<void(QByteArray, QString)> done);
    QNetworkAccessManager* net_ = nullptr;
    QPointer<QNetworkReply> active_;
    bool cancelled_ = false;
};

} // namespace cartesio
