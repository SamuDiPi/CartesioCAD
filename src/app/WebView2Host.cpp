#include "app/WebView2Host.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <map>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <WebView2.h>
#include <wrl/client.h>
#endif

namespace cartesio {

void WebView2Host::failLater(const QString& reason)
{
    QMetaObject::invokeMethod(this, [this, reason] { emit failed(reason); }, Qt::QueuedConnection);
}

#ifdef Q_OS_WIN

using Microsoft::WRL::ComPtr;

namespace {

// Gestore COM di un evento o di un completamento di WebView2 (interfacce con un solo metodo Invoke).
template <typename I, typename... A>
class Handler : public I {
public:
    explicit Handler(std::function<HRESULT(A...)> f) : f_(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = this;   // WebView2 chiede solo IUnknown o l'interfaccia del gestore
        (void)riid;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --ref_;
        if (!r)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return f_(a...); }

private:
    std::function<HRESULT(A...)> f_;
    ULONG ref_ = 1;
};

template <typename I, typename... A, typename F>
ComPtr<I> handler(F f)
{
    ComPtr<I> p;
    p.Attach(new Handler<I, A...>(std::function<HRESULT(A...)>(std::move(f))));
    return p;
}

using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                     ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
using VersionFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);

// WebView2Loader.dll accanto al programma (mai dal PATH: niente DLL da altre cartelle).
HMODULE loaderModule()
{
    static HMODULE module = [] {
        const QString path = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/WebView2Loader.dll"));
        return LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }();
    return module;
}

QString fromWide(LPCWSTR s)
{
    return s ? QString::fromWCharArray(s) : QString();
}

} // namespace

struct WebView2Host::Impl {
    HWND window = nullptr;
    QUrl page;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    std::map<int, std::function<void(const QJsonValue&, const QString&)>> pending;
};

WebView2Host::WebView2Host(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {}

WebView2Host::~WebView2Host()
{
    if (d_->controller)
        d_->controller->Close();
}

bool WebView2Host::available(QString* why)
{
    HMODULE module = loaderModule();
    if (!module) {
        if (why)
            *why = QObject::tr("manca WebView2Loader.dll accanto a CartesioCAD.exe");
        return false;
    }
    auto version = reinterpret_cast<VersionFn>(GetProcAddress(module, "GetAvailableCoreWebView2BrowserVersionString"));
    LPWSTR text = nullptr;
    if (!version || FAILED(version(nullptr, &text)) || !text) {
        if (why)
            *why = QObject::tr("Microsoft Edge WebView2 Runtime non è installato");
        return false;
    }
    CoTaskMemFree(text);
    return true;
}

bool WebView2Host::isReady() const
{
    return d_->web != nullptr;
}

void WebView2Host::start(quintptr window, const QString& dataFolder, const QUrl& page)
{
    QString why;
    if (!available(&why)) {
        failLater(why);
        return;
    }
    // WebView2 vuole COM a thread singolo (con la finestra normale lo inizializza già Qt; senza schermo no).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    d_->window = reinterpret_cast<HWND>(window);
    d_->page = page;
    QDir().mkpath(dataFolder);
    auto create = reinterpret_cast<CreateEnvironmentFn>(GetProcAddress(loaderModule(), "CreateCoreWebView2EnvironmentWithOptions"));
    const std::wstring data = QDir::toNativeSeparators(dataFolder).toStdWString();
    QPointer<WebView2Host> self(this);
    auto onController = handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
        [self](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (!self)
                return S_OK;
            if (FAILED(result) || !controller) {
                self->failLater(QObject::tr("WebView2 non avviato (errore 0x%1)").arg(quint32(result), 8, 16, QLatin1Char('0')));
                return S_OK;
            }
            self->d_->controller = controller;
            controller->get_CoreWebView2(&self->d_->web);
            ICoreWebView2* web = self->d_->web.Get();
            RECT rc{};
            GetClientRect(self->d_->window, &rc);
            controller->put_Bounds(rc);

            ComPtr<ICoreWebView2Settings> settings;
            if (SUCCEEDED(web->get_Settings(&settings))) {
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_AreHostObjectsAllowed(FALSE);
                settings->put_IsZoomControlEnabled(TRUE);
                // Niente dati verso servizi di Microsoft dalla pagina: senza controllo SmartScreen degli indirizzi
                // (la pagina è un file locale e non apre siti) e senza salvataggio di moduli e password.
                ComPtr<ICoreWebView2Settings8> settings8;
                if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings8, reinterpret_cast<void**>(settings8.GetAddressOf()))))
                    settings8->put_IsReputationCheckingRequired(FALSE);
                ComPtr<ICoreWebView2Settings4> settings4;
                if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings4, reinterpret_cast<void**>(settings4.GetAddressOf())))) {
                    settings4->put_IsPasswordAutosaveEnabled(FALSE);
                    settings4->put_IsGeneralAutofillEnabled(FALSE);
                }
            }
            EventRegistrationToken token;
            // Solo la pagina di CanaloCAD: i collegamenti verso siti web si aprono nel browser predefinito.
            web->add_NavigationStarting(handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*,
                                                ICoreWebView2NavigationStartingEventArgs*>(
                [self](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    const QUrl url(fromWide(uri));
                    CoTaskMemFree(uri);
                    if (self && url.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery) != self->d_->page.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery)) {
                        args->put_Cancel(TRUE);
                        if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
                            QDesktopServices::openUrl(url);
                    }
                    return S_OK;
                }).Get(), &token);
            web->add_NewWindowRequested(handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*,
                                                ICoreWebView2NewWindowRequestedEventArgs*>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    const QUrl url(fromWide(uri));
                    CoTaskMemFree(uri);
                    args->put_Handled(TRUE);
                    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
                        QDesktopServices::openUrl(url);
                    return S_OK;
                }).Get(), &token);
            web->add_WebMessageReceived(handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*,
                                                ICoreWebView2WebMessageReceivedEventArgs*>(
                [self](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    if (!self)
                        return S_OK;
                    // Solo dalla pagina aperta da CartesioCAD (non da altri indirizzi o riquadri).
                    LPWSTR source = nullptr;
                    args->get_Source(&source);
                    const QUrl from(fromWide(source));
                    CoTaskMemFree(source);
                    if (from.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery) != self->d_->page.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery))
                        return S_OK;
                    LPWSTR json = nullptr;
                    args->get_WebMessageAsJson(&json);
                    const QByteArray text = fromWide(json).toUtf8();
                    CoTaskMemFree(json);
                    if (text.size() > 96 * 1024 * 1024)   // una tavola DXF in base64 sta molto sotto
                        return S_OK;
                    const QJsonDocument doc = QJsonDocument::fromJson(text);
                    const QJsonObject obj = doc.object();
                    if (obj.contains(QStringLiteral("__risposta"))) {
                        const int id = obj.value(QStringLiteral("__risposta")).toInt();
                        if (auto it = self->d_->pending.find(id); it != self->d_->pending.end()) {
                            auto done = std::move(it->second);
                            self->d_->pending.erase(it);
                            if (done)
                                done(obj.value(QStringLiteral("valore")), obj.value(QStringLiteral("errore")).toString());
                        }
                        return S_OK;
                    }
                    emit self->message(doc.isObject() ? QJsonValue(obj) : QJsonValue(QString::fromUtf8(text)));
                    return S_OK;
                }).Get(), &token);
            bool first = true;
            web->add_NavigationCompleted(handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*,
                                                 ICoreWebView2NavigationCompletedEventArgs*>(
                [self, first](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) mutable -> HRESULT {
                    BOOL ok = FALSE;
                    args->get_IsSuccess(&ok);
                    if (!self || !first)
                        return S_OK;
                    first = false;
                    if (ok)
                        emit self->ready();
                    else
                        self->failLater(QObject::tr("pagina non aperta: %1").arg(self->d_->page.toLocalFile()));
                    return S_OK;
                }).Get(), &token);
            const std::wstring url = self->d_->page.toString(QUrl::FullyEncoded).toStdWString();
            web->Navigate(url.c_str());
            return S_OK;
        });
    auto onEnvironment = handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [self, onController](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (!self)
                return S_OK;
            if (FAILED(result) || !env) {
                self->failLater(QObject::tr("WebView2 non avviato (errore 0x%1)").arg(quint32(result), 8, 16, QLatin1Char('0')));
                return S_OK;
            }
            return env->CreateCoreWebView2Controller(self->d_->window, onController.Get());
        });
    const HRESULT hr = create(nullptr, data.c_str(), nullptr, onEnvironment.Get());
    if (FAILED(hr))
        failLater(tr("WebView2 non avviato (errore 0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0')));
}

void WebView2Host::setBounds(int x, int y, int width, int height)
{
    if (d_->controller)
        d_->controller->put_Bounds(RECT{x, y, x + width, y + height});
}

void WebView2Host::setVisible(bool visible)
{
    if (d_->controller)
        d_->controller->put_IsVisible(visible ? TRUE : FALSE);
}

void WebView2Host::focus()
{
    if (d_->controller)
        d_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void WebView2Host::run(const QString& script, std::function<void(const QString& json)> done)
{
    if (!d_->web) {
        if (done)
            done(QString());
        return;
    }
    const std::wstring code = script.toStdWString();
    d_->web->ExecuteScript(code.c_str(), handler<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
        [done](HRESULT, LPCWSTR json) -> HRESULT {
            if (done)
                done(fromWide(json));
            return S_OK;
        }).Get());
}

void WebView2Host::runAsync(const QString& expression, std::function<void(const QJsonValue&, const QString&)> done)
{
    const int id = nextCall_++;
    d_->pending[id] = std::move(done);
    run(QStringLiteral("(async () => { try { const valore = await (%1); chrome.webview.postMessage({ __risposta: %2, valore }); }"
                       " catch (e) { chrome.webview.postMessage({ __risposta: %2, errore: String((e && e.message) || e) }); } })(); 0")
            .arg(expression).arg(id));
}

#else   // senza WebView2: la scheda CanaloCAD non c'è, CanaloCAD si apre nel browser.

struct WebView2Host::Impl {};
WebView2Host::WebView2Host(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {}
WebView2Host::~WebView2Host() = default;
bool WebView2Host::available(QString* why)
{
    if (why)
        *why = QObject::tr("disponibile solo su Windows");
    return false;
}
bool WebView2Host::isReady() const { return false; }
void WebView2Host::start(quintptr, const QString&, const QUrl&) { failLater(tr("disponibile solo su Windows")); }
void WebView2Host::setBounds(int, int, int, int) {}
void WebView2Host::setVisible(bool) {}
void WebView2Host::focus() {}
void WebView2Host::run(const QString&, std::function<void(const QString&)> done)
{
    if (done)
        done(QString());
}
void WebView2Host::runAsync(const QString&, std::function<void(const QJsonValue&, const QString&)> done)
{
    if (done)
        done(QJsonValue(), tr("disponibile solo su Windows"));
}

#endif

} // namespace cartesio
