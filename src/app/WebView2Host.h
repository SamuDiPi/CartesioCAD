#pragma once

// Pagina web dentro una finestra di CartesioCAD: su Windows con Microsoft Edge WebView2 (il motore di Edge già
// installato in Windows 10/11, caricato con WebView2Loader.dll accanto al programma), su Linux con Qt WebEngine
// (pacchetto Flatpak con il BaseApp di Flathub). Lo usa la scheda CanaloCAD. Funziona anche senza schermo (prove).
// La pagina parla al programma con chrome.webview.postMessage (su Linux lo stesso nome, sopra QWebChannel).

#include <QObject>
#include <QJsonValue>
#include <QWidget>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace cartesio {

// Oggetto esposto alla pagina con QWebChannel (solo Qt WebEngine): la pagina chiama post(json).
class WebBridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void post(const QString& json) { emit received(json); }
signals:
    void received(const QString& json);
};

class WebView2Host : public QObject {
    Q_OBJECT

public:
    explicit WebView2Host(QObject* parent = nullptr);
    ~WebView2Host() override;

    // Disponibile su questo PC (loader accanto al programma e WebView2 Runtime installato)? Se no, il motivo.
    static bool available(QString* why = nullptr);

    // Crea il motore nella finestra nativa "window" (HWND) con i dati della pagina (memoria locale, cache) in
    // "dataFolder", poi apre "page". Fine asincrona: segnale ready() o failed().
    void start(quintptr window, const QString& dataFolder, const QUrl& page);
    // Lo stesso dentro un widget (Qt WebEngine: la pagina riempie "container"). Su Windows si usa start().
    void startInWidget(QWidget* container, const QString& dataFolder, const QUrl& page);
    // Con Qt WebEngine la pagina è un widget figlio del contenitore; con WebView2 una finestra nativa.
    static bool usesWidget();
    bool isReady() const;

    void setBounds(int x, int y, int width, int height);   // in pixel fisici della finestra
    void setVisible(bool visible);
    void focus();

    // Esegue uno script; "done" riceve il risultato in JSON (per una Promise: l'oggetto vuoto, usare runAsync).
    void run(const QString& script, std::function<void(const QString& json)> done = {});
    // Esegue un'espressione che può essere una Promise e passa il valore risolto (o l'errore) a "done".
    void runAsync(const QString& expression, std::function<void(const QJsonValue& value, const QString& error)> done);

signals:
    void ready();
    void failed(const QString& reason);
    void message(const QJsonValue& value);   // chrome.webview.postMessage dalla pagina

private:
    // failed() sempre dal ciclo degli eventi: chi aspetta (anche le prove) è già in ascolto.
    void failLater(const QString& reason);
    struct Impl;
    std::unique_ptr<Impl> d_;
    int nextCall_ = 1;
};

} // namespace cartesio
