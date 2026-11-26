#include "app/CanaloEngine.h"

#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QJSEngine>
#include <QJsonDocument>
#include <QObject>
#include <QRawFont>
#include <QRegularExpression>

#include <hb.h>

#include <algorithm>
#include <map>

namespace cartesio {

namespace {

// Tabelle del font per HarfBuzz, lette dal font che Qt ha scelto (Arial, o il suo sostituto).
hb_blob_t* fontTable(hb_face_t*, hb_tag_t tag, void* user)
{
    const char name[5] = {char(tag >> 24), char(tag >> 16), char(tag >> 8), char(tag), 0};   // stringa C
    auto* data = new QByteArray(static_cast<QRawFont*>(user)->fontTable(name));
    if (data->isEmpty()) {
        delete data;
        return nullptr;
    }
    return hb_blob_create(data->constData(), static_cast<unsigned>(data->size()), HB_MEMORY_MODE_READONLY, data,
                          [](void* p) { delete static_cast<QByteArray*>(p); });
}

// Larghezza dei testi come canvas.measureText del browser (la pagina misura con "100px Arial, ..." e scala):
// avanzamenti di progetto del font in unità esatte e crenatura di HarfBuzz, come Chrome, che compone ogni parola a
// sé (niente crenatura attorno agli spazi). QFontMetrics arrotonda a 1/64 di pixel e usa un'altra tabella di
// crenatura: le colonne delle tabelle della tavola verrebbero diverse.
class TextMeasure : public QObject {
    Q_OBJECT
public:
    ~TextMeasure() override
    {
        for (auto& [spec, f] : faces_) {
            hb_font_destroy(f.font);
            hb_face_destroy(f.face);
        }
    }

    Q_INVOKABLE double measure(const QString& text, const QString& font)
    {
        Face& f = face(font);
        if (!f.font)
            return 0.0;
        const QByteArray utf8 = text.toUtf8();
        long units = 0;
        qsizetype i = 0;
        while (i < utf8.size()) {   // parole e spazi, composti separatamente
            const bool space = utf8[i] == ' ';
            qsizetype j = i;
            while (j < utf8.size() && (utf8[j] == ' ') == space)
                ++j;
            hb_buffer_t* buffer = hb_buffer_create();
            hb_buffer_add_utf8(buffer, utf8.constData() + i, static_cast<int>(j - i), 0, -1);
            hb_buffer_guess_segment_properties(buffer);
            hb_shape(f.font, buffer, nullptr, 0);
            unsigned count = 0;
            const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, &count);
            for (unsigned k = 0; k < count; ++k)
                units += pos[k].x_advance;
            hb_buffer_destroy(buffer);
            i = j;
        }
        return static_cast<double>(units) * f.pixels / f.upem;
    }

private:
    struct Face {
        std::unique_ptr<QRawFont> raw;
        hb_face_t* face = nullptr;
        hb_font_t* font = nullptr;
        double pixels = 10.0, upem = 1000.0;
    };

    Face& face(const QString& spec)
    {
        auto it = faces_.find(spec);
        if (it != faces_.end())
            return it->second;
        Face& f = faces_[spec];
        // "100px Arial, 'Liberation Sans', ...": dimensione e primo font (stringa normale: moc non legge R"()").
        static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)px\\s+['\"]?([^,'\"]+)"));
        const QRegularExpressionMatch m = re.match(spec);
        QFont qf(m.hasMatch() ? m.captured(2).trimmed() : QStringLiteral("Arial"));
        f.pixels = m.hasMatch() ? m.captured(1).toDouble() : 10.0;
        qf.setPixelSize(std::max(1, qRound(f.pixels)));
        f.raw = std::make_unique<QRawFont>(QRawFont::fromFont(qf));
        if (!f.raw->isValid())
            return f;
        f.face = hb_face_create_for_tables(fontTable, f.raw.get(), nullptr);
        f.upem = hb_face_get_upem(f.face);
        f.font = hb_font_create(f.face);
        return f;
    }

    std::map<QString, Face> faces_;
};

QString readResource(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("CanaloCAD: manca %1").arg(path);
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

namespace {
CanaloEngine* gEngine = nullptr;
}

CanaloEngine& CanaloEngine::instance()
{
    // Il motore JavaScript (QJSEngine) va distrutto mentre l'applicazione Qt esiste ancora: un oggetto statico lo
    // sarebbe dopo, a programma già chiuso (memoria corrotta alla chiusura, visibile su Linux). qAddPostRoutine lo
    // distrugge dentro la chiusura di QCoreApplication; senza applicazione (strumenti di prova) resta fino alla fine.
    if (!gEngine) {
        gEngine = new CanaloEngine;
        if (QCoreApplication::instance())
            qAddPostRoutine([] {
                delete gEngine;
                gEngine = nullptr;
            });
    }
    return *gEngine;
}

CanaloEngine::CanaloEngine()
    : js_(std::make_unique<QJSEngine>())
{
    auto* measure = new TextMeasure;
    measure->setParent(js_.get());
    js_->globalObject().setProperty(QStringLiteral("__host"), js_->newQObject(measure));
    js_->evaluate(QStringLiteral("var __hostMeasureText = function (s, f) { return __host.measure(s, f); };"));
    // Un solo script, come la pagina: le variabili let/const dei moduli (state, CABLE_CATALOG...) si vedono fra
    // i file solo se sono valutati insieme.
    QString code;
    for (const char* name : {"host.js", "canalo-engine.js", "cartesio-api.js"}) {
        code += readResource(QStringLiteral(":/canalo/") + QLatin1String(name), &loadError_) + QLatin1Char('\n');
        if (!loadError_.isEmpty())
            return;
    }
    const QJSValue result = js_->evaluate(code, QStringLiteral("canalo"));
    if (result.isError())
        loadError_ = QStringLiteral("CanaloCAD (riga %1): %2").arg(result.property(QStringLiteral("lineNumber")).toString(), result.toString());
}

CanaloEngine::~CanaloEngine() = default;

QString CanaloEngine::error() const
{
    return loadError_.isEmpty() ? lastError_ : loadError_;
}

namespace {
QString compact(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
} // namespace

QString CanaloEngine::call(const QString& function, const QJSValueList& args)
{
    lastError_.clear();
    if (!ok())
        return {};
    const QJSValue api = js_->globalObject().property(QStringLiteral("__cartesio"));
    const QJSValue result = api.property(function).callWithInstance(api, args);
    if (result.isError()) {
        lastError_ = QStringLiteral("CanaloCAD (%1): %2").arg(function, result.toString());
        return {};
    }
    return result.toString();
}

QJsonObject CanaloEngine::stateCall(const QString& function, const QJSValueList& args, QJsonObject* metrics)
{
    const QJsonObject out = QJsonDocument::fromJson(call(function, args).toUtf8()).object();
    if (metrics)
        *metrics = out.value(QStringLiteral("metrics")).toObject();
    return out.value(QStringLiteral("state")).toObject();
}

QJsonObject CanaloEngine::defaultState()
{
    return QJsonDocument::fromJson(call(QStringLiteral("defaultState"), {}).toUtf8()).object();
}

QJsonObject CanaloEngine::database()
{
    return QJsonDocument::fromJson(call(QStringLiteral("database"), {}).toUtf8()).object();
}

void CanaloEngine::setOwnCables(const QJsonArray& cables)
{
    call(QStringLiteral("setOwnCables"), {QString::fromUtf8(QJsonDocument(cables).toJson(QJsonDocument::Compact))});
}

QJsonObject CanaloEngine::calculate(const QJsonObject& state, QJsonObject* metrics)
{
    return stateCall(QStringLiteral("calc"), {compact(state)}, metrics);
}

QJsonObject CanaloEngine::addCable(const QJsonObject& state, const QString& id, int quantity, bool trefoil, QJsonObject* metrics)
{
    return stateCall(QStringLiteral("addCable"), {compact(state), id, quantity, trefoil}, metrics);
}

QJsonObject CanaloEngine::autoSize(const QJsonObject& state, double reservePct, QJsonObject* metrics)
{
    return stateCall(QStringLiteral("autoSize"), {compact(state), reservePct}, metrics);
}

QJsonObject CanaloEngine::proposeConduit(const QJsonObject& state, bool keepCount, bool* found, QJsonObject* metrics)
{
    const QJsonObject out = QJsonDocument::fromJson(call(QStringLiteral("proposeConduit"), {compact(state), keepCount}).toUtf8()).object();
    if (found)
        *found = out.value(QStringLiteral("conduitProposal")).toBool(false);
    if (metrics)
        *metrics = out.value(QStringLiteral("metrics")).toObject();
    return out.value(QStringLiteral("state")).toObject();
}

QJsonObject CanaloEngine::importDxf(const QJsonObject& state, const QString& dxfText, const QString& fileName, QJsonObject* metrics)
{
    return stateCall(QStringLiteral("importDxf"), {compact(state), dxfText, fileName}, metrics);
}

QJsonObject CanaloEngine::setScales(const QJsonObject& state, const QString& scale, double textScale, QJsonObject* metrics)
{
    return stateCall(QStringLiteral("setScales"), {compact(state), scale, textScale}, metrics);
}

QJsonObject CanaloEngine::importFileBytes(const QJsonObject& state, const QByteArray& bytes, const QString& fileName, QJsonObject* metrics)
{
    return stateCall(QStringLiteral("importFileBytes"), {compact(state), QString::fromLatin1(bytes), fileName}, metrics);
}

QJsonObject CanaloEngine::sectionEntities(const QJsonObject& state, const QStringList& layers)
{
    return QJsonDocument::fromJson(call(QStringLiteral("sectionEntities"), {compact(state), QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(layers)).toJson(QJsonDocument::Compact))}).toUtf8()).object();
}

QString CanaloEngine::dxf(const QJsonObject& state, const QString& title)
{
    return call(QStringLiteral("dxf"), {compact(state), title});
}

QString CanaloEngine::evaluate(const QString& expression)
{
    return ok() ? js_->evaluate(expression).toString() : loadError_;
}

QByteArray CanaloEngine::dxfBytes(const QJsonObject& state, const QString& title)
{
    // Come dxfToBlob della pagina: un byte per carattere, '?' oltre U+00FF.
    const QString text = dxf(state, title);
    QByteArray out(text.size(), '\0');
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t c = text.at(i).unicode();
        out[i] = c <= 0xff ? static_cast<char>(c) : '?';
    }
    return out;
}

} // namespace cartesio

#include "CanaloEngine.moc"
