#pragma once

// Canale locale fra CartesioCAD e il connettore per Claude Desktop (cartesio-mcp.exe).
//
// È una named pipe di Windows (QLocalServer) aperta solo all'utente collegato: nessuna porta di rete, nessun
// collegamento a Internet da parte di CartesioCAD. Claude Desktop avvia cartesio-mcp.exe, che riceve le richieste
// di Claude e le passa qui una per riga, in JSON:
//   richiesta  {"id": 1, "strumento": "leggi_oggetti", "argomenti": {...}}
//   risposta   {"id": 1, "ok": true, "testo": "..."}  oppure  {"id": 1, "ok": false, "testo": "motivo"}
// Il canale è spento di default: si accende dalla finestra "Claude" (comando CLAUDE) o dalle Opzioni.

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QLocalServer;
class QLocalSocket;

namespace cartesio {

class ClaudeLink : public QObject {
    Q_OBJECT

public:
    // Risposta a una richiesta: si può chiamare anche più tardi (le proposte aspettano la conferma dell'utente).
    using Reply = std::function<void(bool ok, const QString& text)>;
    using Handler = std::function<void(const QString& tool, const QJsonObject& args, Reply reply)>;

    explicit ClaudeLink(Handler handler, QObject* parent = nullptr);
    ~ClaudeLink() override;

    bool start(QString* error = nullptr);
    void stop();
    bool running() const;

    // Nome del canale per l'utente collegato (lo stesso lo calcola cartesio-mcp.exe).
    static QString channelName();

signals:
    void activity(const QString& tool);

private:
    void onConnection();
    void onReadable(QLocalSocket* socket);

    Handler handler_;
    QLocalServer* server_ = nullptr;
};

} // namespace cartesio
