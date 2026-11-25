#include "app/ClaudeLink.h"
#include "app/ClaudeChannel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>

#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace cartesio {

namespace {
// Una richiesta non supera qualche centinaio di kB (proposte con migliaia di operazioni): oltre, il canale si chiude.
constexpr qsizetype kMaxRequest = 8 * 1024 * 1024;

// Il motivo del rifiuto va a Claude (e quindi fuori dal computer): la cartella del profilo, che contiene il nome
// dell'utente di Windows, diventa %USERPROFILE%.
QString withoutProfile(QString path)
{
    const QString home = QDir::toNativeSeparators(QDir::homePath());
    if (!home.isEmpty() && path.startsWith(home, Qt::CaseInsensitive))
        path.replace(0, home.size(), QStringLiteral("%USERPROFILE%"));
    return path;
}
}

ClaudeLink::ClaudeLink(Handler handler, QObject* parent) : QObject(parent), handler_(std::move(handler)) {}

ClaudeLink::~ClaudeLink() { stop(); }

QString ClaudeLink::channelName() { return claudeChannelName(); }

bool ClaudeLink::start(QString* error)
{
#ifndef Q_OS_WIN
    // Il controllo di chi si collega (trustedClient) esiste solo per Windows: altrove il canale non si apre.
    if (error)
        *error = tr("L'Assistente IA è disponibile solo nella versione per Windows.");
    return false;
#else
    if (running())
        return true;
    server_ = new QLocalServer(this);
    // Solo l'utente collegato può aprire il canale (le altre utenze del PC no).
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    server_->setMaxPendingConnections(4);
    if (!server_->listen(channelName())) {
        if (error)
            *error = server_->errorString();
        delete server_;
        server_ = nullptr;
        return false;
    }
    connect(server_, &QLocalServer::newConnection, this, &ClaudeLink::onConnection);
    return true;
#endif
}

void ClaudeLink::stop()
{
    if (!server_)
        return;
    server_->close();
    delete server_;
    server_ = nullptr;
}

bool ClaudeLink::running() const { return server_ && server_->isListening(); }

// Chi si collega deve essere cartesio-mcp.exe della cartella del programma, sullo stesso computer: il canale è già
// riservato all'utente di Windows, così non lo usa nemmeno un altro programma dell'utente.
bool ClaudeLink::trustedClient(QLocalSocket* socket, QString* who)
{
#ifdef Q_OS_WIN
    const auto pipe = reinterpret_cast<HANDLE>(socket->socketDescriptor());
    wchar_t computer[256] = {};
    if (GetNamedPipeClientComputerNameW(pipe, computer, static_cast<ULONG>(sizeof computer))) {
        *who = tr("un altro computer (%1)").arg(QString::fromWCharArray(computer));
        return false;   // per i client locali la funzione fallisce (ERROR_PIPE_LOCAL)
    }
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) {
        *who = tr("un programma sconosciuto");
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        *who = tr("il processo %1").arg(pid);
        return false;
    }
    wchar_t path[32768] = {};
    DWORD size = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) {
        *who = tr("il processo %1").arg(pid);
        return false;
    }
    const QString client = QFileInfo(QString::fromWCharArray(path, static_cast<qsizetype>(size))).canonicalFilePath();
    const QString expected = QFileInfo(QCoreApplication::applicationDirPath() + QStringLiteral("/cartesio-mcp.exe")).canonicalFilePath();
    if (expected.isEmpty() || client.compare(expected, Qt::CaseInsensitive) != 0) {
        *who = QDir::toNativeSeparators(client);
        return false;
    }
    return true;
#else
    Q_UNUSED(socket);
    Q_UNUSED(who);
    return true;
#endif
}

void ClaudeLink::onConnection()
{
    while (QLocalSocket* socket = server_->nextPendingConnection()) {
        QString who;
        if (!trustedClient(socket, &who)) {
            emit rejected(who);
            // Il motivo va anche al connettore, che lo riferisce a Claude (altrimenti sembra solo che non risponda).
            const QString reason =
                tr("CartesioCAD ha rifiutato il collegamento: il connettore avviato da Claude Desktop (%1) non è quello "
                   "della cartella del CartesioCAD aperto (%2). Aprire CartesioCAD dalla stessa cartella del connettore, "
                   "oppure ricollegare Claude Desktop dal CartesioCAD aperto (comando ASSISTENTE) e riavviare Claude Desktop.")
                    .arg(withoutProfile(who), withoutProfile(QDir::toNativeSeparators(QCoreApplication::applicationDirPath())));
            socket->write(QJsonDocument(QJsonObject{{QStringLiteral("rifiutato"), reason}}).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
            socket->disconnectFromServer();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->state() == QLocalSocket::UnconnectedState)
                socket->deleteLater();
            continue;
        }
        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadable(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void ClaudeLink::onReadable(QLocalSocket* socket)
{
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine();
        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(line, &parseError);
        const QJsonObject request = json.object();
        const QJsonValue id = request.value(QStringLiteral("id"));
        QPointer<QLocalSocket> target(socket);
        Reply reply = [target, id](bool ok, const QString& text) {
            if (!target || target->state() != QLocalSocket::ConnectedState)
                return;
            QJsonObject answer{{QStringLiteral("id"), id}, {QStringLiteral("ok"), ok}, {QStringLiteral("testo"), text}};
            target->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n');
            target->flush();
        };
        if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
            reply(false, tr("Richiesta non valida."));
            continue;
        }
        const QString tool = request.value(QStringLiteral("strumento")).toString();
        emit activity(tool);
        handler_(tool, request.value(QStringLiteral("argomenti")).toObject(), reply);
    }
    if (socket->bytesAvailable() > kMaxRequest)
        socket->abort();
}

} // namespace cartesio
