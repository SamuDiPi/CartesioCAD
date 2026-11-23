#include "app/Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSslSocket>
#include <QStandardPaths>
#include <QUrl>
#include <private/qzipreader_p.h>

#include <openssl/evp.h>

#include <memory>

namespace cartesio {

namespace {

// Chiave pubblica Ed25519 delle versioni di CartesioCAD (la privata è solo dell'autore, fuori dal repository).
constexpr unsigned char kPublicKey[32] = {
    0x02, 0xc7, 0x2b, 0x4d, 0x90, 0x40, 0x2c, 0xfa, 0x13, 0x7f, 0x6a, 0x08, 0xc9, 0x20, 0x26, 0xa0,
    0x29, 0xf5, 0xbd, 0xb7, 0x03, 0xee, 0x2a, 0x4b, 0x0c, 0x09, 0xb5, 0xf5, 0xea, 0xa0, 0x73, 0x6a};

const char* const kReleasesUrl = "https://api.github.com/repos/SamuDiPi/CartesioCAD/releases?per_page=20";
constexpr qint64 kApiLimit = 4 * 1024 * 1024;
constexpr qint64 kManifestLimit = 64 * 1024;
constexpr qint64 kArchiveLimit = 1024LL * 1024 * 1024;
constexpr qint64 kUnpackedLimit = 3LL * 1024 * 1024 * 1024;

const char* const kPrevious = "versione-precedente";
const char* const kStaging = ".aggiornamento-nuovo";
const char* const kDiscarded = ".aggiornamento-scartato";
const char* const kReplacedList = "FILE-SOSTITUITI.txt";
const char* const kAddedList = "FILE-AGGIUNTI.txt";
const char* const kPreviousVersion = "VERSIONE.txt";

// Solo HTTPS e solo GitHub (anche dopo i reindirizzamenti dei download).
bool trustedUrl(const QUrl& url)
{
    if (url.scheme() != QLatin1String("https"))
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("github.com") || host == QLatin1String("api.github.com")
           || host.endsWith(QLatin1String(".github.com")) || host.endsWith(QLatin1String(".githubusercontent.com"));
}

QString manifestName(const Version& v) { return QStringLiteral("CartesioCAD-%1-aggiornamento.json").arg(QString::fromStdString(v.text())); }
QString archiveName(const Version& v) { return QStringLiteral("CartesioCAD-%1-win64.zip").arg(QString::fromStdString(v.text())); }

// Percorso relativo sicuro dentro la cartella del programma ("" se non va bene).
QString safeRelative(QString path)
{
    path.replace(u'\\', u'/');
    if (path.isEmpty() || path.startsWith(u'/') || path.contains(u':'))
        return {};
    for (const QString& part : path.split(u'/', Qt::SkipEmptyParts))
        if (part == QLatin1String("..") || part == QLatin1String("."))
            return {};
    return path;
}

bool writeList(const QString& file, const QStringList& items)
{
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    out.write(items.join(u'\n').toUtf8());
    return out.commit();
}

QStringList readList(const QString& file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList out;
    for (const QString& line : QString::fromUtf8(in.readAll()).split(u'\n', Qt::SkipEmptyParts)) {
        const QString rel = safeRelative(line.trimmed());
        if (!rel.isEmpty())
            out << rel;
    }
    return out;
}

bool moveFile(const QString& from, const QString& to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo::exists(to) && !QFile::remove(to))
        return false;
    return QFile::rename(from, to);
}

} // namespace

Updater::Updater(QObject* parent) : QObject(parent)
{
    // TLS di Windows (Schannel): aggiornato da Windows Update.
    if (QSslSocket::availableBackends().contains(QStringLiteral("schannel")))
        QSslSocket::setActiveBackend(QStringLiteral("schannel"));
    net_ = new QNetworkAccessManager(this);
    net_->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    net_->setTransferTimeout(60000);
}

Version Updater::currentVersion()
{
    return Version::parse(QCoreApplication::applicationVersion().toStdString()).value_or(Version{});
}

bool Updater::verifySignature(const QByteArray& data, const QByteArray& signature)
{
    if (signature.size() != 64)
        return false;
    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, kPublicKey, sizeof kPublicKey);
    if (!key)
        return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1
                    && EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char*>(signature.constData()),
                                        static_cast<size_t>(signature.size()),
                                        reinterpret_cast<const unsigned char*>(data.constData()),
                                        static_cast<size_t>(data.size()))
                           == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

std::optional<UpdateManifest> Updater::parseManifest(const QByteArray& json, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return std::nullopt;
    };
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return fail(QObject::tr("manifesto non leggibile"));
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("prodotto")).toString() != QLatin1String("CartesioCAD"))
        return fail(QObject::tr("il manifesto non è di CartesioCAD"));
    UpdateManifest m;
    const auto version = Version::parse(o.value(QStringLiteral("versione")).toString().toStdString());
    if (!version)
        return fail(QObject::tr("versione del manifesto non valida"));
    m.version = *version;
    m.major = o.value(QStringLiteral("major")).toBool(false);
    m.archive = o.value(QStringLiteral("archivio")).toString();
    m.size = static_cast<qint64>(o.value(QStringLiteral("dimensione")).toDouble(0));
    m.sha256 = o.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
    m.date = o.value(QStringLiteral("data")).toString();
    if (m.archive != archiveName(m.version))
        return fail(QObject::tr("nome dell'archivio non valido nel manifesto"));
    if (m.size <= 0 || m.size > kArchiveLimit)
        return fail(QObject::tr("dimensione dell'archivio non valida nel manifesto"));
    if (m.sha256.size() != 64 || QByteArray::fromHex(m.sha256).size() != 32)
        return fail(QObject::tr("impronta SHA-256 non valida nel manifesto"));
    return m;
}

bool Updater::verifyArchive(const QString& archive, const UpdateManifest& manifest, QString* error)
{
    QFile file(archive);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("archivio non leggibile: %1").arg(QDir::toNativeSeparators(archive));
        return false;
    }
    if (file.size() != manifest.size) {
        if (error)
            *error = QObject::tr("l'archivio non ha la dimensione indicata nel manifesto firmato");
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != manifest.sha256) {
        if (error)
            *error = QObject::tr("l'impronta SHA-256 dell'archivio non corrisponde al manifesto firmato");
        return false;
    }
    return true;
}

void Updater::get(const QString& url, qint64 limit, std::function<void(QByteArray, QString)> done)
{
    const QUrl target(url);
    if (!trustedUrl(target)) {
        done({}, tr("indirizzo non ammesso: %1").arg(url));
        return;
    }
    QNetworkRequest request(target);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CartesioCAD-aggiornamenti"));
    request.setRawHeader("Accept", url.contains(QLatin1String("api.github.com")) ? "application/vnd.github+json"
                                                                                  : "application/octet-stream");
    QNetworkReply* reply = net_->get(request);
    active_ = reply;
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl& to) {
        if (!trustedUrl(to))
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 received, qint64) {
        if (received > limit)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, limit, done] {
        reply->deleteLater();
        if (cancelled_) {
            done({}, tr("annullato"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->error() == QNetworkReply::OperationCanceledError ? tr("risposta non ammessa o troppo grande")
                                                                            : reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() > limit) {
            done({}, tr("risposta troppo grande"));
            return;
        }
        done(data, {});
    });
}

void Updater::check(std::function<void(std::optional<UpdateOffer>, QString)> done)
{
    cancelled_ = false;
    get(QString::fromLatin1(kReleasesUrl), kApiLimit, [done](const QByteArray& data, const QString& error) {
        if (!error.isEmpty()) {
            done(std::nullopt, error);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) {
            done(std::nullopt, tr("risposta di GitHub non leggibile"));
            return;
        }
        const Version current = currentVersion();
        std::optional<UpdateOffer> best;
        for (const QJsonValue& value : doc.array()) {
            const QJsonObject release = value.toObject();
            if (release.value(QStringLiteral("draft")).toBool())
                continue;
            const auto version = Version::parse(release.value(QStringLiteral("tag_name")).toString().toStdString());
            if (!version || !(current < *version) || (best && !(best->version < *version)))
                continue;
            UpdateOffer offer;
            offer.version = *version;
            for (const QJsonValue& a : release.value(QStringLiteral("assets")).toArray()) {
                const QJsonObject asset = a.toObject();
                const QString name = asset.value(QStringLiteral("name")).toString();
                const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
                if (name == manifestName(*version))
                    offer.manifestUrl = url;
                else if (name == manifestName(*version) + QStringLiteral(".sig"))
                    offer.signatureUrl = url;
                else if (name == archiveName(*version)) {
                    offer.archiveUrl = url;
                    offer.archiveSize = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble());
                }
            }
            if (offer.manifestUrl.isEmpty() || offer.signatureUrl.isEmpty() || offer.archiveUrl.isEmpty())
                continue;   // versione senza aggiornamento firmato: si scarica a mano
            offer.tag = release.value(QStringLiteral("tag_name")).toString();
            offer.title = release.value(QStringLiteral("name")).toString();
            offer.notes = release.value(QStringLiteral("body")).toString().left(6000);
            offer.pageUrl = release.value(QStringLiteral("html_url")).toString();
            offer.prerelease = release.value(QStringLiteral("prerelease")).toBool();
            best = offer;
        }
        done(best, {});
    });
}

QString Updater::downloadFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/aggiornamenti");
}

void Updater::download(const UpdateOffer& offer, std::function<void(qint64, qint64)> progress,
                       std::function<void(QString, std::optional<UpdateManifest>, QString)> done)
{
    cancelled_ = false;
    get(offer.manifestUrl, kManifestLimit, [this, offer, progress, done](const QByteArray& manifestData, const QString& e1) {
        if (!e1.isEmpty()) {
            done({}, std::nullopt, e1);
            return;
        }
        get(offer.signatureUrl, 1024, [this, offer, progress, done, manifestData](const QByteArray& signature, const QString& e2) {
            if (!e2.isEmpty()) {
                done({}, std::nullopt, e2);
                return;
            }
            if (!verifySignature(manifestData, signature)) {
                done({}, std::nullopt, tr("la firma del manifesto non è valida: aggiornamento rifiutato"));
                return;
            }
            QString error;
            const auto manifest = parseManifest(manifestData, &error);
            if (!manifest || !(manifest->version == offer.version)) {
                done({}, std::nullopt, manifest ? tr("il manifesto è di un'altra versione") : error);
                return;
            }
            // Archivio: scritto mentre arriva, con la dimensione del manifesto come limite.
            QDir().mkpath(downloadFolder());
            const QString path = downloadFolder() + u'/' + manifest->archive;
            auto file = std::make_shared<QFile>(path + QStringLiteral(".parziale"));
            if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                done({}, std::nullopt, tr("impossibile scrivere in %1").arg(QDir::toNativeSeparators(downloadFolder())));
                return;
            }
            QNetworkRequest request{QUrl(offer.archiveUrl)};
            if (!trustedUrl(request.url())) {
                done({}, std::nullopt, tr("indirizzo non ammesso"));
                return;
            }
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CartesioCAD-aggiornamenti"));
            QNetworkReply* reply = net_->get(request);
            active_ = reply;
            const qint64 limit = manifest->size;
            connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl& to) {
                if (!trustedUrl(to))
                    reply->abort();
            });
            connect(reply, &QNetworkReply::readyRead, reply, [reply, file, limit] {
                file->write(reply->readAll());
                if (file->size() > limit)
                    reply->abort();
            });
            connect(reply, &QNetworkReply::downloadProgress, this, [progress, limit](qint64 received, qint64) {
                if (progress)
                    progress(received, limit);
            });
            connect(reply, &QNetworkReply::finished, this, [this, reply, file, path, manifest, done] {
                reply->deleteLater();
                file->write(reply->readAll());
                file->close();
                if (cancelled_ || reply->error() != QNetworkReply::NoError) {
                    file->remove();
                    done({}, std::nullopt, cancelled_ ? tr("annullato") : reply->errorString());
                    return;
                }
                QString error;
                if (!verifyArchive(file->fileName(), *manifest, &error)) {
                    file->remove();
                    done({}, std::nullopt, error);
                    return;
                }
                QFile::remove(path);
                if (!file->rename(path)) {
                    done({}, std::nullopt, tr("impossibile salvare l'archivio scaricato"));
                    return;
                }
                done(path, manifest, {});
            });
        });
    });
}

void Updater::cancel()
{
    cancelled_ = true;
    if (active_)
        active_->abort();
}

bool Updater::install(const QString& archive, const QString& appDir, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    const QDir app(appDir);
    // Cartella scrivibile? (per esempio non in Programmi senza permessi)
    {
        QFile probe(app.filePath(QStringLiteral(".prova-scrittura")));
        if (!probe.open(QIODevice::WriteOnly))
            return fail(QObject::tr("la cartella del programma non è scrivibile (%1): scaricare l'archivio dalla pagina "
                                    "della versione e scompattarlo a mano")
                            .arg(QDir::toNativeSeparators(appDir)));
        probe.close();
        probe.remove();
    }

    // 1. Scompattato in una cartella di lavoro accanto al programma (stesso disco: si sposta senza copiare).
    const QString staging = app.filePath(QString::fromLatin1(kStaging));
    QDir(staging).removeRecursively();
    QZipReader zip(archive);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return fail(QObject::tr("archivio non leggibile"));
    QStringList files;
    qint64 unpacked = 0;
    for (const QZipReader::FileInfo& info : zip.fileInfoList()) {
        QString path = info.filePath;
        path.replace(u'\\', u'/');
        if (!path.startsWith(QLatin1String("CartesioCAD/")))
            return fail(QObject::tr("contenuto dell'archivio non previsto: %1").arg(info.filePath));
        const QString rel = safeRelative(path.mid(12));
        if (info.isDir || path.endsWith(u'/'))
            continue;
        if (rel.isEmpty() || info.isSymLink)
            return fail(QObject::tr("percorso non ammesso nell'archivio: %1").arg(info.filePath));
        unpacked += info.size;
        if (unpacked > kUnpackedLimit)
            return fail(QObject::tr("archivio troppo grande"));
        const QString target = staging + u'/' + rel;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        const QByteArray data = zip.fileData(path);
        if (data.size() != info.size || !out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
            return fail(QObject::tr("impossibile scompattare %1").arg(rel));
        files << rel;
    }
    zip.close();
    if (!files.contains(QStringLiteral("CartesioCAD.exe")))
        return fail(QObject::tr("l'archivio non contiene CartesioCAD.exe"));

    // 2. Versione attuale in "versione-precedente" (sostituisce quella di prima), file per file, e i nuovi al loro posto.
    const QString previous = app.filePath(QString::fromLatin1(kPrevious));
    if (QFileInfo::exists(previous) && !QDir(previous).removeRecursively())
        return fail(QObject::tr("impossibile togliere la vecchia cartella %1").arg(QString::fromLatin1(kPrevious)));
    QDir().mkpath(previous);
    QStringList replaced, added;
    const auto undo = [&] {
        for (const QString& rel : added)
            QFile::remove(app.filePath(rel));
        for (const QString& rel : replaced)
            moveFile(previous + u'/' + rel, app.filePath(rel));
    };
    for (const QString& rel : files) {
        const QString target = app.filePath(rel);
        if (QFileInfo::exists(target)) {
            if (!moveFile(target, previous + u'/' + rel)) {
                undo();
                return fail(QObject::tr("impossibile spostare %1: aggiornamento annullato, nulla è cambiato").arg(rel));
            }
            replaced << rel;
        } else {
            added << rel;
        }
        if (!moveFile(staging + u'/' + rel, target)) {
            undo();   // il vecchio file torna al suo posto
            return fail(QObject::tr("impossibile installare %1: aggiornamento annullato, nulla è cambiato").arg(rel));
        }
    }
    writeList(previous + u'/' + QString::fromLatin1(kReplacedList), replaced);
    writeList(previous + u'/' + QString::fromLatin1(kAddedList), added);
    writeList(previous + u'/' + QString::fromLatin1(kPreviousVersion), {QString::fromStdString(currentVersion().text())});
    QDir(staging).removeRecursively();
    return true;
}

bool Updater::rollback(const QString& appDir, QString* error)
{
    const QDir app(appDir);
    const QString previous = app.filePath(QString::fromLatin1(kPrevious));
    const QStringList replaced = readList(previous + u'/' + QString::fromLatin1(kReplacedList));
    const QStringList added = readList(previous + u'/' + QString::fromLatin1(kAddedList));
    if (replaced.isEmpty()) {
        if (error)
            *error = QObject::tr("non c'è una versione precedente da ripristinare");
        return false;
    }
    // I file della versione attuale vanno in una cartella da cancellare al prossimo avvio (possono essere in uso).
    const QString discarded = app.filePath(QString::fromLatin1(kDiscarded));
    QDir(discarded).removeRecursively();
    QStringList moved;
    for (const QString& rel : replaced + added) {
        if (!QFileInfo::exists(app.filePath(rel)))
            continue;
        if (!moveFile(app.filePath(rel), discarded + u'/' + rel)) {
            for (const QString& back : moved)
                moveFile(discarded + u'/' + back, app.filePath(back));
            if (error)
                *error = QObject::tr("impossibile spostare %1: ripristino annullato").arg(rel);
            return false;
        }
        moved << rel;
    }
    for (const QString& rel : replaced)
        if (!moveFile(previous + u'/' + rel, app.filePath(rel))) {
            if (error)
                *error = QObject::tr("impossibile ripristinare %1").arg(rel);
            return false;
        }
    QDir(previous).removeRecursively();
    return true;
}

QString Updater::previousVersion(const QString& appDir)
{
    const QString previous = QDir(appDir).filePath(QString::fromLatin1(kPrevious));
    if (readList(previous + u'/' + QString::fromLatin1(kReplacedList)).isEmpty())
        return {};
    QFile in(previous + u'/' + QString::fromLatin1(kPreviousVersion));
    if (!in.open(QIODevice::ReadOnly))
        return QObject::tr("sconosciuta");
    return QString::fromUtf8(in.readAll()).trimmed();
}

void Updater::cleanup(const QString& appDir)
{
    const QDir app(appDir);
    QDir(app.filePath(QString::fromLatin1(kStaging))).removeRecursively();
    QDir(app.filePath(QString::fromLatin1(kDiscarded))).removeRecursively();
}

} // namespace cartesio
