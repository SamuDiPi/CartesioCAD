#pragma once

// Pagina web dentro una finestra di CartesioCAD con Microsoft Edge WebView2 (solo Windows): il motore è quello di
// Edge già installato in Windows 10/11 (WebView2 Runtime), caricato con WebView2Loader.dll accanto al programma.
// Lo usa la scheda CanaloCAD. Funziona anche su una finestra mai mostrata (prove senza schermo).

#include <QObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace cartesio {

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
