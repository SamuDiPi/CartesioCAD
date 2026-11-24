// Connettore per Claude Desktop: gli strumenti che Claude usa sul disegno aperto (vedi ClaudeLink.h e
// src/mcp/main.cpp), la conferma delle modifiche proposte e la finestra "Assistente IA" (comando ASSISTENTE).
//
// Regole:
// - Claude legge solo quello che chiede (di default la selezione) e non vede file, cartelle o rete.
// - Le modifiche sono un elenco di operazioni semplici, controllate qui una per una; si applicano solo quando
//   l'utente preme "Applica" nella finestra di CartesioCAD, in un unico passo annullabile.
// - Il salvataggio crea sempre un file nuovo accanto all'originale (mai sovrascritto).

#include "app/AppOptions.h"
#include "app/CanaloPanel.h"
#include "app/CanaloTab.h"
#include "app/ClaudeLink.h"
#include "app/QmlUi.h"
#include "app/WebView2Host.h"
#include "app/DrawingWidget.h"
#include "app/DwgConverter.h"
#include "app/FormDialog.h"
#include "app/MainWindow.h"
#include "core/Document.h"
#include "core/Entity.h"
#include "core/DocCompare.h"
#include "core/QuickSelect.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <functional>
#include <map>
#include <set>

namespace cartesio {

namespace {

constexpr int kMaxOperations = 2000;
constexpr std::size_t kMaxIds = 500000;   // oggetti toccati da una proposta, anche scelti con i filtri
constexpr double kMaxCoordinate = 1e12;
constexpr qint64 kMaxPlanMs = 15000;    // tempo massimo per calcolare una proposta o una ricerca con filtro
constexpr int kProposalSeconds = 240;   // poi la proposta scade (cartesio-mcp aspetta al massimo 5 minuti)

QString typeKey(EntityType t)
{
    switch (t) {
    case EntityType::Line: return QStringLiteral("linea");
    case EntityType::Circle: return QStringLiteral("cerchio");
    case EntityType::Arc: return QStringLiteral("arco");
    case EntityType::Polyline: return QStringLiteral("polilinea");
    case EntityType::Text: return QStringLiteral("testo");
    case EntityType::MText: return QStringLiteral("testo_multilinea");
    case EntityType::Insert: return QStringLiteral("blocco");
    case EntityType::Hatch: return QStringLiteral("tratteggio");
    case EntityType::Dimension: return QStringLiteral("quota");
    case EntityType::Leader: return QStringLiteral("direttrice");
    case EntityType::Ellipse: return QStringLiteral("ellisse");
    case EntityType::Spline: return QStringLiteral("spline");
    case EntityType::Point: return QStringLiteral("punto");
    case EntityType::Solid: return QStringLiteral("solido");
    case EntityType::Viewport: return QStringLiteral("finestra");
    case EntityType::Image: return QStringLiteral("immagine");
    case EntityType::Wipeout: return QStringLiteral("copertura");
    case EntityType::Table: return QStringLiteral("tabella");
    case EntityType::AttDef: return QStringLiteral("attributo");
    case EntityType::Mline: return QStringLiteral("multilinea");
    case EntityType::XLine: return QStringLiteral("linea_costruzione");
    default: return QStringLiteral("altro");
    }
}

QString insUnitsLabel(int insUnits)
{
    switch (insUnits) {
    case 1: return QStringLiteral("pollici");
    case 2: return QStringLiteral("piedi");
    case 4: return QStringLiteral("millimetri");
    case 5: return QStringLiteral("centimetri");
    case 6: return QStringLiteral("metri");
    case 14: return QStringLiteral("decimetri");
    default: return QStringLiteral("non indicate");
    }
}

double rounded(double v) { return std::abs(v) < 1e9 ? std::round(v * 1e6) / 1e6 : v; }
QJsonArray point(Vec2 p) { return {rounded(p.x), rounded(p.y)}; }
double degrees(double radians) { return rounded(radians * 180.0 / 3.14159265358979323846); }

QString layerName(const Document& doc, int index)
{
    const auto& layers = doc.layers();
    return index >= 0 && index < static_cast<int>(layers.size()) ? QString::fromStdString(layers[static_cast<std::size_t>(index)].name)
                                                                  : QString();
}

QString json(const QJsonValue& v)
{
    return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                                         : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
}

QJsonObject describe(const Document& doc, const Entity& e)
{
    QJsonObject o{{QStringLiteral("id"), static_cast<qint64>(e.id())},
                  {QStringLiteral("tipo"), typeKey(e.type())},
                  {QStringLiteral("layer"), layerName(doc, e.layer())},
                  {QStringLiteral("colore"), e.color()}};
    if (e.space() != 0)
        o.insert(QStringLiteral("spazio_carta"), true);
    switch (e.type()) {
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        o.insert(QStringLiteral("da"), point(l.start));
        o.insert(QStringLiteral("a"), point(l.end));
        o.insert(QStringLiteral("lunghezza"), rounded((l.end - l.start).length()));
        break;
    }
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        o.insert(QStringLiteral("centro"), point(c.center));
        o.insert(QStringLiteral("raggio"), rounded(c.radius));
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        o.insert(QStringLiteral("centro"), point(a.center));
        o.insert(QStringLiteral("raggio"), rounded(a.radius));
        o.insert(QStringLiteral("angolo_iniziale"), degrees(a.startAngle));
        o.insert(QStringLiteral("angolo_finale"), degrees(a.endAngle));
        break;
    }
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        QJsonArray vertices;
        bool curved = false;
        for (std::size_t i = 0; i < p.vertices.size() && i < 500; ++i) {
            vertices.append(point(p.vertices[i].point));
            curved = curved || p.vertices[i].bulge != 0.0;
        }
        o.insert(QStringLiteral("vertici"), vertices);
        if (p.vertices.size() > 500)
            o.insert(QStringLiteral("vertici_totali"), static_cast<qint64>(p.vertices.size()));
        o.insert(QStringLiteral("chiusa"), p.closed);
        if (curved)
            o.insert(QStringLiteral("con_archi"), true);
        break;
    }
    case EntityType::Text: {
        const auto& t = static_cast<const TextEntity&>(e);
        o.insert(QStringLiteral("testo"), QString::fromStdString(t.displayText()));
        o.insert(QStringLiteral("posizione"), point(t.origin()));
        o.insert(QStringLiteral("altezza"), rounded(t.height));
        o.insert(QStringLiteral("rotazione"), degrees(t.effectiveRotation()));
        break;
    }
    case EntityType::MText: {
        const auto& t = static_cast<const MTextEntity&>(e);
        QStringList lines;
        for (const std::string& l : t.lines())
            lines << QString::fromStdString(l);
        o.insert(QStringLiteral("testo"), lines.join(QLatin1Char('\n')));
        o.insert(QStringLiteral("posizione"), point(t.position));
        o.insert(QStringLiteral("altezza"), rounded(t.height));
        o.insert(QStringLiteral("rotazione"), degrees(t.rotation));
        break;
    }
    case EntityType::Insert: {
        const auto& b = static_cast<const InsertEntity&>(e);
        o.insert(QStringLiteral("blocco"), QString::fromStdString(b.effectiveName()));
        o.insert(QStringLiteral("posizione"), point(b.position));
        o.insert(QStringLiteral("scala"), QJsonArray{rounded(b.scaleX), rounded(b.scaleY)});
        o.insert(QStringLiteral("rotazione"), degrees(b.rotation));
        if (!b.attributes.empty()) {
            QJsonObject attributes;
            for (const Attribute& a : b.attributes)
                attributes.insert(QString::fromStdString(a.tag), QString::fromStdString(a.text.displayText()));
            o.insert(QStringLiteral("attributi"), attributes);
        }
        break;
    }
    default:
        break;
    }
    const BBox box = e.bounds();
    if (box.isValid() && e.type() != EntityType::Line && e.type() != EntityType::Circle)
        o.insert(QStringLiteral("ingombro"), QJsonArray{rounded(box.min.x), rounded(box.min.y), rounded(box.max.x), rounded(box.max.y)});
    return o;
}

// ---------------------------------------------------------------- proposte

QString objectCount(std::size_t n)
{
    return n == 1 ? QObject::tr("1 oggetto") : QObject::tr("%1 oggetti").arg(n);
}

// Descrizione breve di un oggetto per l'elenco delle modifiche: tipo, layer ed eventuale testo.
QString shortDescription(const Document& doc, const Entity& e)
{
    QString text = typeKey(e.type()).replace(QLatin1Char('_'), QLatin1Char(' '));
    if (e.type() == EntityType::Text)
        text += QStringLiteral(" \"%1\"").arg(QString::fromStdString(static_cast<const TextEntity&>(e).displayText()).left(40));
    else if (e.type() == EntityType::Insert)
        text += QStringLiteral(" %1").arg(QString::fromStdString(static_cast<const InsertEntity&>(e).effectiveName()));
    return text + QObject::tr(" sul layer %1").arg(layerName(doc, e.layer()));
}

bool finite(double v) { return std::isfinite(v) && std::abs(v) < kMaxCoordinate; }

std::optional<double> number(const QJsonObject& o, const QString& key)
{
    const QJsonValue v = o.value(key);
    if (!v.isDouble() || !finite(v.toDouble()))
        return std::nullopt;
    return v.toDouble();
}

std::optional<Vec2> xy(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble() || !finite(a[0].toDouble()) || !finite(a[1].toDouble()))
        return std::nullopt;
    return Vec2{a[0].toDouble(), a[1].toDouble()};
}

bool validLayerName(const QString& name)
{
    static const QRegularExpression bad(QStringLiteral(R"([<>/\\":;?*|=`\x00-\x1f])"));
    return !name.trimmed().isEmpty() && name.size() <= 255 && !bad.match(name).hasMatch();
}

// ---------------------------------------------------------------- filtri (modifiche massive)

// Testo leggibile di un oggetto (testi e testi multilinea), vuoto per gli altri.
QString entityText(const Entity& e)
{
    if (e.type() == EntityType::Text)
        return QString::fromStdString(static_cast<const TextEntity&>(e).displayText());
    if (e.type() == EntityType::MText) {
        QStringList lines;
        for (const std::string& l : static_cast<const MTextEntity&>(e).lines())
            lines << QString::fromStdString(l);
        return lines.join(QLatin1Char('\n'));
    }
    return {};
}

// Criteri per scegliere gli oggetti senza elencarli: valgono tutti quelli indicati insieme.
struct ClaudeFilter {
    std::vector<QRegularExpression> layers, blocks;
    std::set<QString> types;
    std::optional<int> color;
    QString text;
    std::optional<QRegularExpression> textRegex;
    QString attributeTag;
    std::optional<QRegularExpression> attributeValue;
    std::optional<BBox> area;
    bool inside = false;
    bool selection = false;
    bool allSpaces = false;
};

// Nomi con * e ? (uno o un elenco), senza distinzione fra maiuscole e minuscole.
std::vector<QRegularExpression> wildcards(const QJsonValue& v)
{
    QStringList names;
    if (v.isArray()) {
        for (const QJsonValue& x : v.toArray())
            names << x.toString();
    } else {
        names << v.toString();
    }
    std::vector<QRegularExpression> out;
    for (const QString& n : names)
        if (!n.isEmpty() && n.size() <= 255)
            out.push_back(QRegularExpression::fromWildcard(n, Qt::CaseInsensitive));
    return out;
}

std::optional<ClaudeFilter> parseFilter(const QJsonValue& value, QString* why)
{
    const auto bad = [why](const QString& message) -> std::optional<ClaudeFilter> {
        *why = message;
        return std::nullopt;
    };
    if (!value.isObject())
        return bad(QObject::tr("\"filtro\" deve essere un oggetto JSON"));
    const QJsonObject f = value.toObject();
    static const QStringList known = {QStringLiteral("layer"), QStringLiteral("tipo"), QStringLiteral("colore"),
                                      QStringLiteral("testo"), QStringLiteral("testo_regex"), QStringLiteral("blocco"),
                                      QStringLiteral("attributo"), QStringLiteral("area"), QStringLiteral("dentro"),
                                      QStringLiteral("selezione"), QStringLiteral("spazio")};
    if (f.isEmpty())
        return bad(QObject::tr("filtro vuoto (per tutti gli oggetti usare per esempio {\"layer\": \"*\"})"));
    for (const QString& key : f.keys())
        if (!known.contains(key))
            return bad(QObject::tr("criterio sconosciuto nel filtro: %1").arg(key));
    ClaudeFilter out;
    if (f.contains(QStringLiteral("layer")) && (out.layers = wildcards(f.value(QStringLiteral("layer")))).empty())
        return bad(QObject::tr("nome di layer non valido nel filtro"));
    if (f.contains(QStringLiteral("blocco")) && (out.blocks = wildcards(f.value(QStringLiteral("blocco")))).empty())
        return bad(QObject::tr("nome di blocco non valido nel filtro"));
    if (f.contains(QStringLiteral("tipo"))) {
        const QJsonValue t = f.value(QStringLiteral("tipo"));
        const QJsonArray list = t.isArray() ? t.toArray() : QJsonArray{t};
        for (const QJsonValue& x : list)
            out.types.insert(x.toString().toLower());
    }
    if (f.contains(QStringLiteral("colore"))) {
        if (!f.value(QStringLiteral("colore")).isDouble())
            return bad(QObject::tr("colore del filtro non numerico"));
        out.color = f.value(QStringLiteral("colore")).toInt();
    }
    out.text = f.value(QStringLiteral("testo")).toString().left(1000);
    if (f.contains(QStringLiteral("testo_regex"))) {
        const QString pattern = f.value(QStringLiteral("testo_regex")).toString();
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        if (pattern.isEmpty() || pattern.size() > 500 || !re.isValid())
            return bad(QObject::tr("espressione regolare non valida: %1").arg(re.errorString()));
        out.textRegex = re;
    }
    if (f.contains(QStringLiteral("attributo"))) {
        const QJsonObject a = f.value(QStringLiteral("attributo")).toObject();
        out.attributeTag = a.value(QStringLiteral("tag")).toString();
        if (out.attributeTag.isEmpty())
            return bad(QObject::tr("nel filtro \"attributo\" serve il tag"));
        if (a.contains(QStringLiteral("valore")))
            out.attributeValue = QRegularExpression::fromWildcard(a.value(QStringLiteral("valore")).toString(), Qt::CaseInsensitive);
    }
    if (f.contains(QStringLiteral("area"))) {
        const QJsonArray a = f.value(QStringLiteral("area")).toArray();
        if (a.size() != 4)
            return bad(QObject::tr("area del filtro: [x1, y1, x2, y2]"));
        BBox box;
        box.expand(Vec2{a[0].toDouble(), a[1].toDouble()});
        box.expand(Vec2{a[2].toDouble(), a[3].toDouble()});
        out.area = box;
    }
    out.inside = f.value(QStringLiteral("dentro")).toBool();
    out.selection = f.value(QStringLiteral("selezione")).toBool();
    out.allSpaces = f.value(QStringLiteral("spazio")).toString() == QLatin1String("tutti");
    return out;
}

bool matches(const Document& doc, const Entity& e, const ClaudeFilter& f, const std::set<EntityId>& selection)
{
    if (f.selection ? !selection.count(e.id()) : (!f.allSpaces && e.space() != 0))
        return false;
    if (!f.layers.empty()) {
        const QString name = layerName(doc, e.layer());
        if (std::none_of(f.layers.begin(), f.layers.end(), [&](const QRegularExpression& re) { return re.match(name).hasMatch(); }))
            return false;
    }
    if (!f.types.empty() && !f.types.count(typeKey(e.type())))
        return false;
    if (f.color && e.color() != *f.color)
        return false;
    if (!f.text.isEmpty() || f.textRegex) {
        const QString t = entityText(e);
        if (t.isEmpty() || (!f.text.isEmpty() && !t.contains(f.text, Qt::CaseInsensitive))
            || (f.textRegex && !f.textRegex->match(t).hasMatch()))
            return false;
    }
    if (!f.blocks.empty() || !f.attributeTag.isEmpty()) {
        if (e.type() != EntityType::Insert)
            return false;
        const auto& b = static_cast<const InsertEntity&>(e);
        if (!f.blocks.empty()) {
            const QString shown = QString::fromStdString(b.effectiveName()), name = QString::fromStdString(b.blockName);
            if (std::none_of(f.blocks.begin(), f.blocks.end(), [&](const QRegularExpression& re) {
                    return re.match(shown).hasMatch() || re.match(name).hasMatch();
                }))
                return false;
        }
        if (!f.attributeTag.isEmpty()
            && std::none_of(b.attributes.begin(), b.attributes.end(), [&](const Attribute& a) {
                   return QString::fromStdString(a.tag).compare(f.attributeTag, Qt::CaseInsensitive) == 0
                          && (!f.attributeValue || f.attributeValue->match(QString::fromStdString(a.text.displayText())).hasMatch());
               }))
            return false;
    }
    if (f.area) {
        if (f.inside) {
            const BBox box = e.bounds();
            if (!box.isValid() || box.min.x < f.area->min.x || box.min.y < f.area->min.y || box.max.x > f.area->max.x
                || box.max.y > f.area->max.y)
                return false;
        } else if (!e.touchesRect(*f.area)) {
            return false;
        }
    }
    return true;
}

QString describeFilter(const QJsonObject& f)
{
    QStringList parts;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const QJsonValue v = it.value();
        QString text = v.isString() ? QStringLiteral("\"%1\"").arg(v.toString())
                       : v.isBool() ? (v.toBool() ? QObject::tr("sì") : QObject::tr("no"))
                       : v.isDouble() ? QString::number(v.toDouble())
                                      : json(v);
        parts << it.key() + QStringLiteral(" ") + text;
    }
    return parts.join(QStringLiteral(", "));
}

QString escapeMText(const QString& s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('{'), QStringLiteral("\\{")).replace(QLatin1Char('}'), QStringLiteral("\\}"));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\P"));
    return out;
}

// Applica `change` solo alle parti di testo di un MTEXT: i codici di formattazione (\P, \fArial|b0;, \H2.5x;...)
// restano come sono.
std::string mtextPlain(const std::string& raw, const std::function<QString(const QString&)>& change)
{
    const QString s = QString::fromStdString(raw);
    QString out, plain;
    const auto flush = [&] {
        if (!plain.isEmpty()) {
            out += escapeMText(change(plain));
            plain.clear();
        }
    };
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c == QLatin1Char('{') || c == QLatin1Char('}')) {
            flush();
            out += c;
            continue;
        }
        if (c != QLatin1Char('\\') || i + 1 >= s.size()) {
            plain += c;
            continue;
        }
        const QChar code = s[i + 1];
        if (code == QLatin1Char('\\') || code == QLatin1Char('{') || code == QLatin1Char('}')) {
            plain += code;   // carattere scritto con la barra davanti
            ++i;
            continue;
        }
        flush();
        if (QStringLiteral("PpNnLlOoKkXx~").contains(code)) {   // codici di due caratteri
            out += s.mid(i, 2);
            ++i;
        } else if ((code == QLatin1Char('U') || code == QLatin1Char('u')) && i + 2 < s.size() && s[i + 2] == QLatin1Char('+')) {
            out += s.mid(i, 7);   // \U+00B0
            i += 6;
        } else {
            const qsizetype end = s.indexOf(QLatin1Char(';'), i);
            if (end < 0) {
                out += s.mid(i);
                i = s.size();
            } else {
                out += s.mid(i, end - i + 1);
                i = end;
            }
        }
    }
    flush();
    return out.toStdString();
}

// Cambia i testi di un oggetto: testo, testo multilinea, valori degli attributi di un blocco. True se è cambiato.
bool changeTexts(Entity& e, const std::function<QString(const QString&)>& change)
{
    bool changed = false;
    const auto apply = [&](std::string& text) {
        const std::string before = text;
        text = change(QString::fromStdString(text)).toStdString();
        changed = changed || text != before;
    };
    if (e.type() == EntityType::Text) {
        apply(static_cast<TextEntity&>(e).text);
    } else if (e.type() == EntityType::MText) {
        auto& m = static_cast<MTextEntity&>(e);
        const std::string before = m.text;
        m.text = mtextPlain(m.text, change);
        changed = m.text != before;
    } else if (e.type() == EntityType::Insert) {
        for (Attribute& a : static_cast<InsertEntity&>(e).attributes)
            apply(a.text.text);
    }
    return changed;
}

// Modifiche pronte da applicare: oggetti nuovi o copie modificate (stesso id) e id da togliere.
struct ClaudePlan {
    std::vector<std::pair<QString, int>> newLayers;   // nome, colore
    std::vector<std::pair<int, QString>> layerRenames;   // indice, nome nuovo
    std::vector<std::unique_ptr<Entity>> added;
    std::vector<EntityId> removed;
    std::vector<EntityId> touched;   // oggetti esistenti interessati (per evidenziarli)
    QStringList summary;
};

// Controlla le operazioni sul documento e prepara le modifiche. Con `layersCreated` falso i layer di crea_layer non
// esistono ancora (anteprima): gli oggetti destinati a loro si preparano sul layer corrente, solo per il conteggio.
// `selection` è la selezione dell'utente al momento della proposta (per i filtri con "selezione": true). I filtri
// guardano il disegno com'era prima della proposta, non le modifiche delle operazioni precedenti.
std::optional<ClaudePlan> buildPlan(const Document& doc, const QJsonArray& ops, bool layersCreated,
                                    const std::set<EntityId>& selection, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<ClaudePlan> {
        *error = message;
        return std::nullopt;
    };
    if (ops.isEmpty())
        return fail(QObject::tr("Nessuna operazione."));
    if (ops.size() > kMaxOperations)
        return fail(QObject::tr("Troppe operazioni (al massimo %1).").arg(kMaxOperations));

    ClaudePlan plan;
    std::map<EntityId, std::unique_ptr<Entity>> working;   // copie degli oggetti esistenti modificati
    std::set<EntityId> erased;
    std::set<QString> planned;
    std::map<QString, int> renamed;   // nome nuovo (minuscolo) -> indice del layer rinominato
    std::size_t idCount = 0;
    QString scope;   // oggetti scelti con un filtro: si aggiunge alla riga dell'operazione
    // Tempo massimo: un filtro o un'espressione regolare costruiti male (anche da un testo del disegno) non devono
    // bloccare il programma.
    QElapsedTimer clock;
    clock.start();
    const auto late = [&clock] { return clock.elapsed() > kMaxPlanMs; };
    const QString tooSlow = QObject::tr("il calcolo supera %1 secondi: restringere il filtro o semplificare "
                                        "l'espressione regolare").arg(kMaxPlanMs / 1000);

    const auto layerIndex = [&](const QString& name, int* index) -> bool {
        if (const auto it = renamed.find(name.toLower()); it != renamed.end()) {
            *index = it->second;
            return true;
        }
        const int found = doc.findLayer(name.toStdString());
        if (found >= 0) {
            *index = found;
            return true;
        }
        if (planned.count(name.toLower())) {
            *index = -1;
            return true;
        }
        return false;
    };
    // Oggetto esistente da modificare: la sua copia di lavoro.
    const auto target = [&](qint64 id, QString* why) -> Entity* {
        if (erased.count(static_cast<EntityId>(id))) {
            *why = QObject::tr("l'oggetto %1 è già eliminato da un'operazione precedente").arg(id);
            return nullptr;
        }
        auto it = working.find(static_cast<EntityId>(id));
        if (it != working.end())
            return it->second.get();
        const Entity* e = id > 0 ? doc.find(static_cast<EntityId>(id)) : nullptr;
        if (!e) {
            *why = QObject::tr("l'oggetto %1 non esiste").arg(id);
            return nullptr;
        }
        const int layer = e->layer();
        if (layer >= 0 && layer < static_cast<int>(doc.layers().size()) && doc.layers()[static_cast<std::size_t>(layer)].locked) {
            *why = QObject::tr("l'oggetto %1 è sul layer bloccato \"%2\"").arg(id).arg(layerName(doc, layer));
            return nullptr;
        }
        if (e->type() == EntityType::Viewport) {
            *why = QObject::tr("l'oggetto %1 è una finestra di layout (non modificabile da qui)").arg(id);
            return nullptr;
        }
        plan.touched.push_back(e->id());
        return working.emplace(e->id(), e->clone()).first->second.get();
    };
    // Oggetti dell'operazione: elenco "ids" oppure "filtro" (gli oggetti sui layer bloccati si saltano).
    const auto idList = [&](const QJsonObject& op, std::vector<qint64>* ids, QString* why) -> bool {
        if (op.contains(QStringLiteral("filtro"))) {
            if (op.contains(QStringLiteral("ids"))) {
                *why = QObject::tr("usare \"ids\" oppure \"filtro\", non tutti e due");
                return false;
            }
            const auto filter = parseFilter(op.value(QStringLiteral("filtro")), why);
            if (!filter)
                return false;
            int locked = 0;
            for (const auto& e : doc.entities()) {
                if (late()) {
                    *why = tooSlow;
                    return false;
                }
                if (erased.count(e->id()) || e->type() == EntityType::Viewport || !matches(doc, *e, *filter, selection))
                    continue;
                const int layer = e->layer();
                if (layer >= 0 && layer < static_cast<int>(doc.layers().size()) && doc.layers()[static_cast<std::size_t>(layer)].locked) {
                    ++locked;
                    continue;
                }
                ids->push_back(static_cast<qint64>(e->id()));
            }
            const QString described = describeFilter(op.value(QStringLiteral("filtro")).toObject());
            if (ids->empty()) {
                *why = locked ? QObject::tr("il filtro (%1) trova solo oggetti su layer bloccati").arg(described)
                              : QObject::tr("il filtro (%1) non trova nessun oggetto").arg(described);
                return false;
            }
            scope = QObject::tr(" [filtro: %1]").arg(described);
            if (locked)
                scope += QObject::tr(" (%1 su layer bloccati esclusi)").arg(locked);
        } else {
            const QJsonArray a = op.value(QStringLiteral("ids")).toArray();
            if (a.isEmpty()) {
                *why = QObject::tr("serve l'elenco \"ids\" oppure un \"filtro\"");
                return false;
            }
            std::set<qint64> seen;
            for (const QJsonValue& v : a) {
                if (!v.isDouble()) {
                    *why = QObject::tr("id non numerico");
                    return false;
                }
                if (seen.insert(static_cast<qint64>(v.toDouble())).second)
                    ids->push_back(static_cast<qint64>(v.toDouble()));
            }
        }
        idCount += ids->size();
        if (idCount > kMaxIds) {
            *why = QObject::tr("troppi oggetti in tutto (al massimo %1)").arg(kMaxIds);
            return false;
        }
        return true;
    };
    const auto styleNew = [&](Entity& e, const QJsonObject& op, QString* why) -> bool {
        if (op.contains(QStringLiteral("layer"))) {
            int index = -1;
            const QString name = op.value(QStringLiteral("layer")).toString();
            if (!layerIndex(name, &index)) {
                *why = QObject::tr("il layer \"%1\" non esiste (crearlo prima con crea_layer)").arg(name);
                return false;
            }
            e.setLayer(index);
        }
        return true;
    };

    for (int k = 0; k < ops.size(); ++k) {
        const QJsonObject op = ops[k].toObject();
        const QString kind = op.value(QStringLiteral("op")).toString();
        QString why;
        const auto bad = [&](const QString& message) {
            return fail(QObject::tr("Operazione %1 (%2): %3.").arg(k + 1).arg(kind.isEmpty() ? QStringLiteral("?") : kind, message));
        };
        std::vector<qint64> ids;
        scope.clear();

        if (kind == QLatin1String("sposta") || kind == QLatin1String("copia")) {
            const auto dx = number(op, QStringLiteral("dx")), dy = number(op, QStringLiteral("dy"));
            if (!dx || !dy)
                return bad(QObject::tr("servono dx e dy numerici"));
            if (!idList(op, &ids, &why))
                return bad(why);
            for (qint64 id : ids) {
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                if (kind == QLatin1String("copia")) {
                    auto copy = e->clone();
                    copy->setId(0);
                    copy->translate({*dx, *dy});
                    plan.added.push_back(std::move(copy));
                } else {
                    e->translate({*dx, *dy});
                }
            }
            plan.summary << (kind == QLatin1String("copia") ? QObject::tr("Copia %1 spostando di %2, %3")
                                                            : QObject::tr("Sposta %1 di %2, %3"))
                                .arg(objectCount(ids.size())).arg(*dx).arg(*dy);
        } else if (kind == QLatin1String("ruota") || kind == QLatin1String("scala")) {
            const auto cx = number(op, QStringLiteral("cx")), cy = number(op, QStringLiteral("cy"));
            const bool rotate = kind == QLatin1String("ruota");
            const auto value = number(op, rotate ? QStringLiteral("angolo") : QStringLiteral("fattore"));
            if (!cx || !cy || !value)
                return bad(rotate ? QObject::tr("servono cx, cy e angolo") : QObject::tr("servono cx, cy e fattore"));
            if (!rotate && (*value <= 1e-9 || *value > 1e6))
                return bad(QObject::tr("fattore di scala non valido"));
            if (!idList(op, &ids, &why))
                return bad(why);
            const Affine t = rotate ? Affine::rotation({*cx, *cy}, *value * 3.14159265358979323846 / 180.0)
                                    : Affine::scaling({*cx, *cy}, *value);
            for (qint64 id : ids) {
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                e->transform(t);
            }
            plan.summary << (rotate ? QObject::tr("Ruota %1 di %2° attorno a %3, %4")
                                    : QObject::tr("Scala %1 di %2 rispetto a %3, %4"))
                                .arg(objectCount(ids.size())).arg(*value).arg(*cx).arg(*cy);
        } else if (kind == QLatin1String("elimina")) {
            if (!idList(op, &ids, &why))
                return bad(why);
            for (qint64 id : ids) {
                if (!target(id, &why))
                    return bad(why);
                working.erase(static_cast<EntityId>(id));
                erased.insert(static_cast<EntityId>(id));
            }
            if (ids.size() <= 3) {
                for (qint64 id : ids)
                    plan.summary << QObject::tr("Elimina %1").arg(shortDescription(doc, *doc.find(static_cast<EntityId>(id))));
            } else {
                plan.summary << QObject::tr("Elimina %1").arg(objectCount(ids.size()));
            }
        } else if (kind == QLatin1String("cambia_layer")) {
            const QString name = op.value(QStringLiteral("layer")).toString();
            int index = -1;
            if (!layerIndex(name, &index))
                return bad(QObject::tr("il layer \"%1\" non esiste (crearlo prima con crea_layer)").arg(name));
            if (!idList(op, &ids, &why))
                return bad(why);
            for (qint64 id : ids) {
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                e->setLayer(index);
            }
            plan.summary << QObject::tr("Porta %1 sul layer \"%2\"").arg(objectCount(ids.size())).arg(name);
        } else if (kind == QLatin1String("cambia_colore")) {
            const auto color = number(op, QStringLiteral("colore"));
            if (!color || *color < 1 || *color > 256 || *color != std::floor(*color))
                return bad(QObject::tr("colore da 1 a 255, oppure 256 (DaLayer)"));
            if (!idList(op, &ids, &why))
                return bad(why);
            for (qint64 id : ids) {
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                e->setColor(static_cast<int>(*color));
                e->setTrueColor(-1);
            }
            plan.summary << QObject::tr("Colore %1 a %2").arg(static_cast<int>(*color)).arg(objectCount(ids.size()));
        } else if (kind == QLatin1String("modifica_testo")) {
            const QJsonValue idValue = op.value(QStringLiteral("id"));
            const QString text = op.value(QStringLiteral("testo")).toString();
            if (!idValue.isDouble() || !op.value(QStringLiteral("testo")).isString() || text.size() > 5000)
                return bad(QObject::tr("servono id e testo (al massimo 5000 caratteri)"));
            Entity* e = target(static_cast<qint64>(idValue.toDouble()), &why);
            if (!e)
                return bad(why);
            if (e->type() == EntityType::Text) {
                QString single = text;
                single.replace(QLatin1Char('\n'), QLatin1Char(' '));
                static_cast<TextEntity*>(e)->text = single.toStdString();
            } else if (e->type() == EntityType::MText) {
                QString escaped = text;
                escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('{'), QStringLiteral("\\{"))
                    .replace(QLatin1Char('}'), QStringLiteral("\\}")).replace(QLatin1Char('\n'), QStringLiteral("\\P"));
                static_cast<MTextEntity*>(e)->text = escaped.toStdString();
            } else {
                return bad(QObject::tr("l'oggetto %1 non è un testo").arg(static_cast<qint64>(idValue.toDouble())));
            }
            plan.summary << QObject::tr("Testo dell'oggetto %1: \"%2\"").arg(static_cast<qint64>(idValue.toDouble())).arg(text.left(80));
        } else if (kind == QLatin1String("crea_layer")) {
            const QString name = op.value(QStringLiteral("nome")).toString().trimmed();
            if (!validLayerName(name))
                return bad(QObject::tr("nome di layer non valido"));
            int color = 7;
            if (op.contains(QStringLiteral("colore"))) {
                const auto c = number(op, QStringLiteral("colore"));
                if (!c || *c < 1 || *c > 255)
                    return bad(QObject::tr("colore del layer da 1 a 255"));
                color = static_cast<int>(*c);
            }
            if (doc.findLayer(name.toStdString()) >= 0) {
                if (!layersCreated)
                    plan.summary << QObject::tr("Layer \"%1\" (esiste già)").arg(name);
                continue;
            }
            planned.insert(name.toLower());
            plan.newLayers.emplace_back(name, color);
            plan.summary << QObject::tr("Nuovo layer \"%1\" (colore %2)").arg(name).arg(color);
        } else if (kind == QLatin1String("sostituisci_testo") || kind == QLatin1String("maiuscole")) {
            // Testi, testi multilinea (solo le parti di testo, non i codici) e valori degli attributi dei blocchi.
            std::function<QString(const QString&)> change;
            QString line;
            if (kind == QLatin1String("maiuscole")) {
                const QString mode = op.value(QStringLiteral("modo")).toString(QStringLiteral("maiuscolo"));
                if (mode != QLatin1String("maiuscolo") && mode != QLatin1String("minuscolo"))
                    return bad(QObject::tr("modo: maiuscolo o minuscolo"));
                const bool upper = mode == QLatin1String("maiuscolo");
                change = [upper](const QString& s) { return upper ? s.toUpper() : s.toLower(); };
                line = upper ? QObject::tr("Testi in maiuscolo") : QObject::tr("Testi in minuscolo");
            } else {
                const QString find = op.value(QStringLiteral("cerca")).toString();
                const QString with = op.value(QStringLiteral("sostituisci")).toString();
                if (find.isEmpty() || find.size() > 1000 || with.size() > 5000 || !op.value(QStringLiteral("sostituisci")).isString())
                    return bad(QObject::tr("servono \"cerca\" (non vuoto) e \"sostituisci\""));
                const bool caseSensitive = op.value(QStringLiteral("maiuscole_minuscole")).toBool();
                if (op.value(QStringLiteral("regex")).toBool()) {
                    QRegularExpression re(find, caseSensitive ? QRegularExpression::NoPatternOption
                                                              : QRegularExpression::CaseInsensitiveOption);
                    if (!re.isValid() || find.size() > 500)
                        return bad(QObject::tr("espressione regolare non valida: %1").arg(re.errorString()));
                    change = [re, with](const QString& s) { return QString(s).replace(re, with).left(20000); };
                } else {
                    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                    change = [find, with, cs](const QString& s) { return QString(s).replace(find, with, cs).left(20000); };
                }
                line = QObject::tr("Sostituisci \"%1\" con \"%2\"").arg(find.left(60), with.left(60));
            }
            if (!idList(op, &ids, &why))
                return bad(why);
            std::size_t changed = 0;
            for (qint64 id : ids) {
                if (late())
                    return bad(tooSlow);
                // Prima su una copia: gli oggetti che non cambiano restano fuori dalla modifica.
                const Entity* original = doc.find(static_cast<EntityId>(id));
                auto probe = original ? (working.count(static_cast<EntityId>(id)) ? working[static_cast<EntityId>(id)]->clone() : original->clone()) : nullptr;
                if (!probe || !changeTexts(*probe, change))
                    continue;
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                changeTexts(*e, change);
                ++changed;
            }
            if (changed == 0)
                return bad(QObject::tr("nessun testo cambierebbe"));
            plan.summary << QObject::tr("%1 in %2").arg(line, objectCount(changed));
        } else if (kind == QLatin1String("altezza_testo")) {
            const auto height = number(op, QStringLiteral("altezza"));
            const auto factor = number(op, QStringLiteral("fattore"));
            if ((!height && !factor) || (height && *height <= 0) || (factor && (*factor <= 0 || *factor > 1000)))
                return bad(QObject::tr("serve \"altezza\" o \"fattore\" positivo"));
            if (!idList(op, &ids, &why))
                return bad(why);
            std::size_t changed = 0;
            for (qint64 id : ids) {
                const Entity* original = doc.find(static_cast<EntityId>(id));
                if (!original || (original->type() != EntityType::Text && original->type() != EntityType::MText
                                  && !(original->type() == EntityType::Insert && !static_cast<const InsertEntity*>(original)->attributes.empty())))
                    continue;
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                const auto resize = [&](double& h) { h = height ? *height : h * *factor; };
                if (e->type() == EntityType::Text)
                    resize(static_cast<TextEntity*>(e)->height);
                else if (e->type() == EntityType::MText)
                    resize(static_cast<MTextEntity*>(e)->height);
                else
                    for (Attribute& a : static_cast<InsertEntity*>(e)->attributes)
                        resize(a.text.height);
                ++changed;
            }
            if (changed == 0)
                return bad(QObject::tr("fra gli oggetti scelti non ci sono testi"));
            plan.summary << (height ? QObject::tr("Altezza %1 a %2").arg(*height).arg(objectCount(changed))
                                    : QObject::tr("Altezza per %1 a %2").arg(*factor).arg(objectCount(changed)));
        } else if (kind == QLatin1String("imposta_attributo")) {
            const QString tag = op.value(QStringLiteral("tag")).toString();
            const QString value = op.value(QStringLiteral("valore")).toString();
            if (tag.isEmpty() || !op.value(QStringLiteral("valore")).isString() || value.size() > 2000)
                return bad(QObject::tr("servono \"tag\" e \"valore\""));
            if (!idList(op, &ids, &why))
                return bad(why);
            std::size_t changed = 0;
            for (qint64 id : ids) {
                const Entity* original = doc.find(static_cast<EntityId>(id));
                if (!original || original->type() != EntityType::Insert)
                    continue;
                const auto& attributes = static_cast<const InsertEntity*>(original)->attributes;
                if (std::none_of(attributes.begin(), attributes.end(), [&](const Attribute& a) {
                        return QString::fromStdString(a.tag).compare(tag, Qt::CaseInsensitive) == 0;
                    }))
                    continue;
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                for (Attribute& a : static_cast<InsertEntity*>(e)->attributes)
                    if (QString::fromStdString(a.tag).compare(tag, Qt::CaseInsensitive) == 0)
                        a.text.text = value.toStdString();
                ++changed;
            }
            if (changed == 0)
                return bad(QObject::tr("nessun blocco scelto ha l'attributo %1").arg(tag));
            plan.summary << QObject::tr("Attributo %1 = \"%2\" in %3").arg(tag, value.left(60), objectCount(changed));
        } else if (kind == QLatin1String("cambia_proprieta")) {
            std::optional<int> layer, color, lineweight;
            std::optional<std::string> linetype;
            QStringList what;
            if (op.contains(QStringLiteral("layer"))) {
                int index = -1;
                const QString name = op.value(QStringLiteral("layer")).toString();
                if (!layerIndex(name, &index))
                    return bad(QObject::tr("il layer \"%1\" non esiste (crearlo prima con crea_layer)").arg(name));
                layer = index;
                what << QObject::tr("layer \"%1\"").arg(name);
            }
            if (op.contains(QStringLiteral("colore"))) {
                const auto c = number(op, QStringLiteral("colore"));
                if (!c || *c < 0 || *c > 256 || *c != std::floor(*c))
                    return bad(QObject::tr("colore da 1 a 255, 256 = DaLayer, 0 = DaBlocco"));
                color = static_cast<int>(*c);
                what << QObject::tr("colore %1").arg(*color);
            }
            if (op.contains(QStringLiteral("tipo_linea"))) {
                const QString name = op.value(QStringLiteral("tipo_linea")).toString();
                if (name.compare(QLatin1String("DaLayer"), Qt::CaseInsensitive) == 0 || name.compare(QLatin1String("ByLayer"), Qt::CaseInsensitive) == 0)
                    linetype = "ByLayer";
                else if (name.compare(QLatin1String("DaBlocco"), Qt::CaseInsensitive) == 0 || name.compare(QLatin1String("ByBlock"), Qt::CaseInsensitive) == 0)
                    linetype = "ByBlock";
                else if (const Linetype* lt = doc.findLinetype(name.toStdString()))
                    linetype = lt->name;
                else
                    return bad(QObject::tr("tipo di linea \"%1\" non caricato nel disegno").arg(name));
                what << QObject::tr("tipo di linea %1").arg(QString::fromStdString(*linetype));
            }
            if (op.contains(QStringLiteral("spessore"))) {
                const QJsonValue v = op.value(QStringLiteral("spessore"));
                const QString s = v.toString();
                if (s.compare(QLatin1String("DaLayer"), Qt::CaseInsensitive) == 0)
                    lineweight = kLineweightByLayer;
                else if (s.compare(QLatin1String("DaBlocco"), Qt::CaseInsensitive) == 0)
                    lineweight = kLineweightByBlock;
                else if (s.compare(QLatin1String("Predefinito"), Qt::CaseInsensitive) == 0)
                    lineweight = kLineweightDefault;
                else if (v.isDouble() && v.toDouble() >= 0 && v.toDouble() <= 2.11)
                    lineweight = static_cast<int>(std::lround(v.toDouble() * 100.0));
                else
                    return bad(QObject::tr("spessore in mm da 0 a 2.11, oppure DaLayer, DaBlocco, Predefinito"));
                what << QObject::tr("spessore %1").arg(v.isDouble() ? QStringLiteral("%1 mm").arg(v.toDouble()) : s);
            }
            if (what.isEmpty())
                return bad(QObject::tr("indicare almeno una fra layer, colore, tipo_linea, spessore"));
            if (!idList(op, &ids, &why))
                return bad(why);
            for (qint64 id : ids) {
                Entity* e = target(id, &why);
                if (!e)
                    return bad(why);
                if (layer)
                    e->setLayer(*layer);
                if (color) {
                    e->setColor(*color);
                    e->setTrueColor(-1);
                }
                if (linetype)
                    e->setLinetype(*linetype);
                if (lineweight)
                    e->setLineweight(*lineweight);
            }
            plan.summary << QObject::tr("%1: %2").arg(objectCount(ids.size()), what.join(QStringLiteral(", ")));
        } else if (kind == QLatin1String("rinomina_layer")) {
            const QString from = op.value(QStringLiteral("da")).toString();
            const QString to = op.value(QStringLiteral("a")).toString().trimmed();
            if (!validLayerName(to))
                return bad(QObject::tr("nome nuovo non valido"));
            const int index = doc.findLayer(from.toStdString());
            if (index < 0) {
                if (layersCreated && doc.findLayer(to.toStdString()) >= 0)
                    continue;   // già rinominato prima di applicare
                return bad(QObject::tr("il layer \"%1\" non esiste").arg(from));
            }
            if (index == 0 || from == QLatin1String("0"))
                return bad(QObject::tr("il layer 0 non si rinomina"));
            if (doc.findLayer(to.toStdString()) >= 0 && to.compare(from, Qt::CaseInsensitive) != 0)
                return bad(QObject::tr("esiste già un layer \"%1\"").arg(to));
            renamed[to.toLower()] = index;
            plan.layerRenames.emplace_back(index, to);
            plan.summary << QObject::tr("Rinomina il layer \"%1\" in \"%2\"").arg(from, to);
        } else if (kind.startsWith(QLatin1String("aggiungi_"))) {
            std::unique_ptr<Entity> e;
            if (kind == QLatin1String("aggiungi_linea")) {
                const auto a = xy(op.value(QStringLiteral("da"))), b = xy(op.value(QStringLiteral("a")));
                if (!a || !b)
                    return bad(QObject::tr("servono da: [x, y] e a: [x, y]"));
                e = std::make_unique<LineEntity>(*a, *b);
                plan.summary << QObject::tr("Linea da %1, %2 a %3, %4").arg(a->x).arg(a->y).arg(b->x).arg(b->y);
            } else if (kind == QLatin1String("aggiungi_polilinea")) {
                const QJsonArray points = op.value(QStringLiteral("punti")).toArray();
                if (points.size() < 2 || points.size() > 10000)
                    return bad(QObject::tr("servono da 2 a 10000 punti"));
                std::vector<PolyVertex> vertices;
                for (const QJsonValue& v : points) {
                    const auto p = xy(v);
                    if (!p)
                        return bad(QObject::tr("punto non valido"));
                    vertices.push_back({*p});
                }
                e = std::make_unique<PolylineEntity>(std::move(vertices), op.value(QStringLiteral("chiusa")).toBool());
                plan.summary << QObject::tr("Polilinea di %1 punti").arg(points.size());
            } else if (kind == QLatin1String("aggiungi_cerchio")) {
                const auto c = xy(op.value(QStringLiteral("centro")));
                const auto r = number(op, QStringLiteral("raggio"));
                if (!c || !r || *r <= 0)
                    return bad(QObject::tr("servono centro: [x, y] e raggio positivo"));
                e = std::make_unique<CircleEntity>(*c, *r);
                plan.summary << QObject::tr("Cerchio di raggio %1 in %2, %3").arg(*r).arg(c->x).arg(c->y);
            } else if (kind == QLatin1String("aggiungi_testo")) {
                const auto p = xy(op.value(QStringLiteral("posizione")));
                const auto h = number(op, QStringLiteral("altezza"));
                const QString text = op.value(QStringLiteral("testo")).toString();
                const double rotation = number(op, QStringLiteral("rotazione")).value_or(0.0);
                if (!p || !h || *h <= 0 || text.trimmed().isEmpty() || text.size() > 5000)
                    return bad(QObject::tr("servono posizione, altezza positiva e testo"));
                QString single = text;
                single.replace(QLatin1Char('\n'), QLatin1Char(' '));
                e = std::make_unique<TextEntity>(*p, *h, single.toStdString(), rotation * 3.14159265358979323846 / 180.0);
                plan.summary << QObject::tr("Testo \"%1\" in %2, %3").arg(text.left(60)).arg(p->x).arg(p->y);
            } else {
                return bad(QObject::tr("operazione sconosciuta"));
            }
            if (!styleNew(*e, op, &why))
                return bad(why);
            plan.added.push_back(std::move(e));
        } else {
            return bad(QObject::tr("operazione sconosciuta"));
        }
        if (!scope.isEmpty() && !plan.summary.isEmpty())
            plan.summary.last() += scope;
    }
    for (auto& [id, e] : working) {
        plan.removed.push_back(id);
        plan.added.push_back(std::move(e));
    }
    for (EntityId id : erased)
        plan.removed.push_back(id);
    return plan;
}

// Prossimo nome libero: pianta.dwg -> pianta-v2.dwg; pianta-v2.dwg -> pianta-v3.dwg.
QString nextVersionPath(const QString& path, const QString& suffix)
{
    const QFileInfo info(path);
    static const QRegularExpression versioned(QStringLiteral(R"(^(.*)-v(\d+)$)"));
    QString stem = info.completeBaseName();
    int n = 2;
    if (const auto m = versioned.match(stem); m.hasMatch()) {
        stem = m.captured(1);
        n = m.captured(2).toInt() + 1;
    }
    for (;; ++n) {
        const QString candidate = info.dir().filePath(QStringLiteral("%1-v%2.%3").arg(stem).arg(n).arg(suffix));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

// Cartelle della configurazione di Claude Desktop (installazione normale e quella dello Store di Microsoft).
QStringList claudeDesktopFolders()
{
    QStringList folders;
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!roaming.isEmpty() && QFileInfo(roaming + QStringLiteral("/Claude")).isDir())
        folders << roaming + QStringLiteral("/Claude");
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    // Versione dello Store: le versioni recenti tengono i dati in LOCALAPPDATA/Claude-Data/claude_<id>, quelle
    // precedenti nella cartella virtualizzata del pacchetto.
    QDirIterator data(local + QStringLiteral("/Claude-Data"), {QStringLiteral("claude_*")}, QDir::Dirs);
    while (data.hasNext())
        folders << data.next();
    QDirIterator it(local + QStringLiteral("/Packages"), {QStringLiteral("Claude_*"), QStringLiteral("AnthropicPBC.Claude*")}, QDir::Dirs);
    while (it.hasNext()) {
        const QString folder = it.next() + QStringLiteral("/LocalCache/Roaming/Claude");
        if (QFileInfo(folder).isDir())
            folders << folder;
    }
    return folders;
}

} // namespace

QString removeClaudeDesktopEntry(const QString& appDir)
{
    const QString ours = QFileInfo(appDir + QStringLiteral("/cartesio-mcp.exe")).absoluteFilePath();
    QStringList removed;
    for (const QString& folder : claudeDesktopFolders()) {
        const QString path = folder + QStringLiteral("/claude_desktop_config.json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject config = doc.object();
        QJsonObject servers = config.value(QStringLiteral("mcpServers")).toObject();
        const QString command = servers.value(QStringLiteral("cartesiocad")).toObject().value(QStringLiteral("command")).toString();
        if (command.isEmpty() || QFileInfo(command).absoluteFilePath().compare(ours, Qt::CaseInsensitive) != 0)
            continue;   // nessuna voce, o un'altra installazione
        servers.remove(QStringLiteral("cartesiocad"));
        config.insert(QStringLiteral("mcpServers"), servers);
        QSaveFile out(path);
        if (out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(config).toJson(QJsonDocument::Indented)) >= 0 && out.commit())
            removed << QDir::toNativeSeparators(path);
    }
    return removed.isEmpty() ? QObject::tr("Nessun connettore di questa installazione in Claude Desktop.")
                             : QObject::tr("Connettore tolto da: %1").arg(removed.join(QStringLiteral(", ")));
}

// ------------------------------------------------------------------ canale

bool MainWindow::startClaude(QString* error)
{
    if (!claude_) {
        claude_ = new ClaudeLink([this](const QString& tool, const QJsonObject& args, ClaudeLink::Reply reply) {
            handleClaudeTool(tool, args, std::move(reply));
        }, this);
        connect(claude_, &ClaudeLink::rejected, this, [this](const QString& who) {
            print(tr("Assistente IA: collegamento rifiutato da %1 (è ammesso solo cartesio-mcp.exe della cartella "
                     "di CartesioCAD).").arg(who));
        });
    }
    return claude_->start(error);
}

void MainWindow::stopClaude()
{
    if (claude_)
        claude_->stop();
}

void MainWindow::startupClaude()
{
    if (!appOptions().claudeConnector)
        return;
    QString error;
    if (!startClaude(&error))
        print(tr("Assistente IA: connettore non attivo: %1 (forse è già aperto un altro CartesioCAD).").arg(error));
}

void MainWindow::handleClaudeTool(const QString& tool, const QJsonObject& args, ClaudeLink::Reply reply)
{
    ++claudeCalls_;
    DrawingWidget* d = current();
    if (!d && isCanaloTab(fileTabs_->currentIndex()))
        d = canaloFrom_;   // dalla scheda CanaloCAD: il disegno da cui ci si è arrivati
    if (tool.startsWith(QLatin1String("canalo_")) && handleCanaloTool(tool, args, d, reply))
        return;
    if (!d) {
        reply(false, tr("In CartesioCAD non c'è nessun disegno aperto."));
        return;
    }
    const Document& doc = d->document();

    if (tool == QLatin1String("disegno_info")) {
        BBox extents;
        int model = 0, paper = 0;
        for (const auto& e : doc.entities()) {
            if (e->space() == 0) {
                ++model;
                extents.expand(e->bounds());
            } else {
                ++paper;
            }
        }
        QJsonObject info{{QStringLiteral("file"), d->filePath().isEmpty() ? d->displayName() : QFileInfo(d->filePath()).fileName()},
                         {QStringLiteral("formato"), d->filePath().isEmpty() ? QStringLiteral("non ancora salvato")
                                                     : d->isDwg() ? QStringLiteral("DWG") : QStringLiteral("DXF")},
                         {QStringLiteral("modificato"), d->isModified()},
                         {QStringLiteral("unita"), insUnitsLabel(doc.insUnits())},
                         {QStringLiteral("oggetti_modello"), model},
                         {QStringLiteral("oggetti_layout"), paper},
                         {QStringLiteral("layer"), static_cast<int>(doc.layers().size())},
                         {QStringLiteral("layer_corrente"), layerName(doc, doc.currentLayer())},
                         {QStringLiteral("selezionati"), static_cast<int>(d->selection().size())}};
        if (extents.isValid())
            info.insert(QStringLiteral("ingombro"), QJsonArray{rounded(extents.min.x), rounded(extents.min.y),
                                                               rounded(extents.max.x), rounded(extents.max.y)});
        if (d->isBlockEditor())
            info.insert(QStringLiteral("editor_blocco"), d->blockName());
        reply(true, json(info));
        return;
    }

    if (tool == QLatin1String("elenca_layer")) {
        std::vector<int> counts(doc.layers().size(), 0);
        for (const auto& e : doc.entities())
            if (e->layer() >= 0 && e->layer() < static_cast<int>(counts.size()))
                ++counts[static_cast<std::size_t>(e->layer())];
        QJsonArray layers;
        for (std::size_t i = 0; i < doc.layers().size(); ++i) {
            const Layer& l = doc.layers()[i];
            layers.append(QJsonObject{{QStringLiteral("nome"), QString::fromStdString(l.name)},
                                      {QStringLiteral("colore"), l.color},
                                      {QStringLiteral("acceso"), l.visible && !l.frozen},
                                      {QStringLiteral("bloccato"), l.locked},
                                      {QStringLiteral("oggetti"), counts[i]}});
        }
        reply(true, json(layers));
        return;
    }

    // Oggetti scelti da ambito (selezione, tutto, area) ed eventuale filtro, per leggi_oggetti, riepilogo e seleziona.
    const auto chosen = [&](const QJsonObject& q, std::vector<const Entity*>* out, QString* why) -> bool {
        const bool hasFilter = q.contains(QStringLiteral("filtro"));
        const QString scope = q.value(QStringLiteral("ambito")).toString(hasFilter ? QStringLiteral("tutto") : QStringLiteral("selezione"));
        std::optional<ClaudeFilter> filter;
        if (hasFilter && !(filter = parseFilter(q.value(QStringLiteral("filtro")), why)))
            return false;
        BBox area;
        if (scope == QLatin1String("area")) {
            const QJsonArray a = q.value(QStringLiteral("area")).toArray();
            if (a.size() != 4) {
                *why = tr("Con ambito \"area\" serve area: [x1, y1, x2, y2].");
                return false;
            }
            area.expand(Vec2{a[0].toDouble(), a[1].toDouble()});
            area.expand(Vec2{a[2].toDouble(), a[3].toDouble()});
        } else if (scope != QLatin1String("selezione") && scope != QLatin1String("tutto")) {
            *why = tr("Ambito sconosciuto: usare selezione, tutto o area.");
            return false;
        }
        const std::set<EntityId> selected(d->selection().begin(), d->selection().end());
        if (scope == QLatin1String("selezione") && selected.empty()) {
            *why = tr("Nel disegno non è selezionato niente: chiedere all'utente di selezionare gli oggetti, oppure usare "
                      "ambito \"tutto\", \"area\" o un filtro.");
            return false;
        }
        // Parametri semplici di leggi_oggetti, ancora accettati.
        const QString onlyLayer = q.value(QStringLiteral("layer")).toString();
        const QString onlyType = q.value(QStringLiteral("tipo")).toString();
        QElapsedTimer clock;
        clock.start();
        for (const auto& e : doc.entities()) {
            if (clock.elapsed() > kMaxPlanMs) {
                *why = tr("La ricerca supera %1 secondi: restringere il filtro o semplificare l'espressione regolare.")
                           .arg(kMaxPlanMs / 1000);
                return false;
            }
            if (scope == QLatin1String("selezione") ? !selected.count(e->id())
                                                    : (e->space() != 0 && !(filter && filter->allSpaces)))
                continue;
            if (scope == QLatin1String("area") && !e->touchesRect(area))
                continue;
            if (!onlyLayer.isEmpty() && layerName(doc, e->layer()).compare(onlyLayer, Qt::CaseInsensitive) != 0)
                continue;
            if (!onlyType.isEmpty() && typeKey(e->type()) != onlyType.toLower())
                continue;
            if (filter && !matches(doc, *e, *filter, selected))
                continue;
            out->push_back(e.get());
        }
        return true;
    };

    if (tool == QLatin1String("leggi_oggetti")) {
        const int limit = std::clamp(args.value(QStringLiteral("massimo")).toInt(300), 1, 2000);
        const int from = std::max(0, args.value(QStringLiteral("da")).toInt(0));
        std::vector<const Entity*> list;
        QString why;
        if (!chosen(args, &list, &why)) {
            reply(false, why);
            return;
        }
        QJsonArray objects;
        for (int i = from; i < static_cast<int>(list.size()) && i < from + limit; ++i)
            objects.append(describe(doc, *list[static_cast<std::size_t>(i)]));
        QJsonObject answer{{QStringLiteral("oggetti"), objects}, {QStringLiteral("trovati"), static_cast<qint64>(list.size())}};
        if (from + limit < static_cast<int>(list.size()))
            answer.insert(QStringLiteral("nota"), tr("Mostrati %1-%2 di %3: per i successivi \"da\": %4. Per le modifiche "
                                                     "massive non serve leggerli tutti: usare un filtro in proponi_modifiche.")
                                                      .arg(from + 1).arg(from + objects.size()).arg(list.size()).arg(from + limit));
        reply(true, json(answer));
        return;
    }

    if (tool == QLatin1String("riepilogo")) {
        const QString by = args.value(QStringLiteral("per")).toString(QStringLiteral("layer"));
        const QString tag = args.value(QStringLiteral("tag")).toString();
        if (by != QLatin1String("layer") && by != QLatin1String("tipo") && by != QLatin1String("blocco")
            && by != QLatin1String("colore") && by != QLatin1String("testo") && by != QLatin1String("attributo")) {
            reply(false, tr("\"per\": layer, tipo, blocco, colore, testo oppure attributo (con \"tag\")."));
            return;
        }
        if (by == QLatin1String("attributo") && tag.isEmpty()) {
            reply(false, tr("Con \"per\": \"attributo\" serve il \"tag\"."));
            return;
        }
        QJsonObject scopeArgs = args;   // senza ambito né filtro: tutto il disegno
        if (!args.contains(QStringLiteral("ambito")) && !args.contains(QStringLiteral("filtro")))
            scopeArgs.insert(QStringLiteral("ambito"), QStringLiteral("tutto"));
        std::vector<const Entity*> list;
        QString why;
        if (!chosen(scopeArgs, &list, &why)) {
            reply(false, why);
            return;
        }
        std::map<QString, int> counts;
        for (const Entity* e : list) {
            if (by == QLatin1String("layer")) {
                ++counts[layerName(doc, e->layer())];
            } else if (by == QLatin1String("tipo")) {
                ++counts[typeKey(e->type())];
            } else if (by == QLatin1String("colore")) {
                ++counts[QString::number(e->color())];
            } else if (by == QLatin1String("testo")) {
                const QString t = entityText(*e).simplified().left(200);
                if (!t.isEmpty())
                    ++counts[t];
            } else if (e->type() == EntityType::Insert) {
                const auto& b = static_cast<const InsertEntity&>(*e);
                if (by == QLatin1String("blocco")) {
                    ++counts[QString::fromStdString(b.effectiveName())];
                } else {
                    for (const Attribute& a : b.attributes)
                        if (QString::fromStdString(a.tag).compare(tag, Qt::CaseInsensitive) == 0)
                            ++counts[QString::fromStdString(a.text.displayText()).left(200)];
                }
            }
        }
        std::vector<std::pair<QString, int>> sorted(counts.begin(), counts.end());
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        const int limit = std::clamp(args.value(QStringLiteral("massimo")).toInt(200), 1, 2000);
        QJsonArray groups;
        for (std::size_t i = 0; i < sorted.size() && static_cast<int>(i) < limit; ++i)
            groups.append(QJsonObject{{QStringLiteral("valore"), sorted[i].first}, {QStringLiteral("oggetti"), sorted[i].second}});
        reply(true, json(QJsonObject{{QStringLiteral("per"), by}, {QStringLiteral("oggetti"), static_cast<qint64>(list.size())},
                                     {QStringLiteral("gruppi"), groups}, {QStringLiteral("gruppi_totali"), static_cast<qint64>(sorted.size())}}));
        return;
    }

    if (tool == QLatin1String("seleziona")) {
        std::vector<EntityId> ids;
        if (args.contains(QStringLiteral("filtro"))) {
            std::vector<const Entity*> list;
            QString why;
            if (!chosen(args, &list, &why)) {
                reply(false, why);
                return;
            }
            for (const Entity* e : list)
                ids.push_back(e->id());
        } else {
            for (const QJsonValue& v : args.value(QStringLiteral("ids")).toArray())
                if (doc.find(static_cast<EntityId>(v.toDouble())) && ids.size() < kMaxIds)
                    ids.push_back(static_cast<EntityId>(v.toDouble()));
        }
        d->setSelection(ids);
        reply(true, tr("Selezionati: %1.").arg(objectCount(ids.size())));
        return;
    }

    if (tool == QLatin1String("proponi_modifiche")) {
        proposeClaudeChanges(d, args, std::move(reply));
        return;
    }

    if (tool == QLatin1String("salva_nuova_versione")) {
        if (d->isBlockEditor()) {
            reply(false, tr("La scheda attiva è l'editor di un blocco: chiuderlo prima di salvare."));
            return;
        }
        if (claudeProposal_) {
            reply(false, tr("C'è una proposta in attesa della scelta dell'utente: salvare dopo."));
            return;
        }
        const QString original = d->filePath();
        if (original.isEmpty()) {
            reply(false, tr("Il disegno non è ancora stato salvato: l'utente deve salvarlo la prima volta con un nome."));
            return;
        }
        const QString format = args.value(QStringLiteral("formato")).toString(QStringLiteral("uguale"));
        const bool dwg = format == QLatin1String("dwg") || (format != QLatin1String("dxf") && d->isDwg());
        if (dwg && !DwgConverter::available()) {
            reply(false, tr("Per scrivere DWG manca il convertitore: salvare in formato dxf."));
            return;
        }
        const QString target = nextVersionPath(original, dwg ? QStringLiteral("dwg") : QStringLiteral("dxf"));
        if (QFileInfo(target).absoluteFilePath().compare(QFileInfo(original).absoluteFilePath(), Qt::CaseInsensitive) == 0
            || QFileInfo::exists(target)) {
            reply(false, tr("Nome della nuova versione non disponibile."));
            return;
        }
        QString error;
        // DXF sempre in versione 2000: conserva testi multilinea, tratteggi, ellissi e spline (l'R12 no).
        if (!d->saveTo(target, &error, DxfVersion::R2000)) {
            reply(false, tr("Salvataggio non riuscito: %1").arg(error));
            return;
        }
        print(tr("Assistente IA: salvata la nuova versione %1 (l'originale %2 non è stato toccato).")
                  .arg(QDir::toNativeSeparators(target), QFileInfo(original).fileName()));
        ++claudeSaved_;
        saveThumbnail(d, target);
        addRecentFile(target);
        if (dwg) {
            pendingSummaries_[target] = std::make_shared<DocSummary>(summarizeDocument(doc));
            verifySavedDwg(target, target);
        }
        reply(true, tr("Salvato come %1. Il file originale %2 non è stato modificato; la scheda ora lavora sulla nuova versione.")
                        .arg(QFileInfo(target).fileName(), QFileInfo(original).fileName()));
        return;
    }

    reply(false, tr("Strumento sconosciuto: %1").arg(tool));
}

void MainWindow::proposeClaudeChanges(DrawingWidget* d, const QJsonObject& args, ClaudeLink::Reply reply)
{
    if (claudeProposal_) {
        reply(false, tr("C'è già una proposta in attesa della scelta dell'utente."));
        return;
    }
    if (d->isBlockEditor()) {
        reply(false, tr("La scheda attiva è l'editor di un blocco: le modifiche si propongono sul disegno."));
        return;
    }
    const QJsonArray ops = args.value(QStringLiteral("operazioni")).toArray();
    const QString description = args.value(QStringLiteral("descrizione")).toString().left(300);
    QString error;
    // La selezione dell'utente di adesso: i filtri con "selezione" la usano anche al momento di applicare (intanto
    // nel disegno si selezionano gli oggetti interessati, per mostrarli).
    const std::set<EntityId> selection(d->selection().begin(), d->selection().end());
    const auto preview = buildPlan(d->document(), ops, false, selection, &error);
    if (!preview) {
        reply(false, tr("Proposta non valida, niente è stato mostrato all'utente. %1").arg(error));
        return;
    }

    QPointer<DrawingWidget> drawing(d);
    // Applica: si ricontrolla tutto sul disegno di adesso (l'utente può averlo cambiato nel frattempo).
    auto apply = [this, drawing, ops, selection]() -> QString {
        if (!drawing)
            return tr("Il disegno è stato chiuso.");
        if (drawing->commands().isActive())
            drawing->cancel();
        Document& doc = drawing->document();
        QString error;
        const auto check = buildPlan(doc, ops, false, selection, &error);
        if (!check)
            return error;
        std::vector<int> created;
        for (const auto& [name, color] : check->newLayers) {
            Layer layer;
            layer.name = name.toStdString();
            layer.color = color;
            created.push_back(doc.addLayer(layer));
        }
        auto plan = buildPlan(doc, ops, true, selection, &error);
        if (!plan) {
            for (auto it = created.rbegin(); it != created.rend(); ++it)
                doc.removeLayer(*it);
            return error;
        }
        std::vector<EntityId> touched = plan->touched;
        doc.commit(std::move(plan->added), plan->removed);
        // Nomi nuovi dei layer dopo gli oggetti (i filtri hanno usato i nomi di prima); stesso passo da annullare.
        for (const auto& [index, name] : plan->layerRenames) {
            std::string why;
            if (!doc.renameLayer(index, name.toStdString(), &why))
                print(tr("Assistente IA: layer non rinominato in %1: %2").arg(name, QString::fromStdString(why)));
        }
        drawing->notifyChanged();
        std::vector<EntityId> alive;
        for (EntityId id : touched)
            if (doc.find(id))
                alive.push_back(id);
        drawing->setSelection(alive);
        ++claudeApplied_;
        return {};
    };

    const QString title = description.isEmpty() ? tr("Modifiche proposte dall'assistente IA") : description;
    if (claudeAutoApprove_) {   // solo negli script di prova (-ASSISTENTEATTIVA AUTO)
        const QString failure = apply();
        print(tr("Assistente IA (prova, applicata senza conferma): %1").arg(title));
        reply(failure.isEmpty(), failure.isEmpty() ? tr("L'utente ha applicato le modifiche.") : tr("Non applicate: %1").arg(failure));
        return;
    }

    d->setSelection(preview->touched);   // gli oggetti interessati si vedono evidenziati
    auto* dialog = new FormDialog(tr("Modifiche proposte dall'assistente IA"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->group(title);
    dialog->note(tr("Disegno: %1. Gli oggetti esistenti interessati sono selezionati. Le modifiche si applicano solo "
                    "con OK e si annullano tutte con un solo Ctrl+Z. Controllarle prima di usare il disegno.")
                     .arg(d->displayName()));
    dialog->list(preview->summary, 0, [](int) {});
    dialog->setSize(560, 420);
    claudeProposal_ = dialog;
    auto replied = std::make_shared<bool>(false);
    auto answer = [reply, replied](bool ok, const QString& text) {
        if (!*replied) {
            *replied = true;
            reply(ok, text);
        }
    };
    connect(dialog, &QDialog::accepted, this, [this, apply, answer, title] {
        const QString failure = apply();
        if (failure.isEmpty())
            print(tr("Assistente IA: applicate le modifiche \"%1\" (Ctrl+Z per annullarle).").arg(title));
        answer(failure.isEmpty(), failure.isEmpty() ? tr("L'utente ha applicato le modifiche.")
                                                    : tr("L'utente ha confermato, ma non è stato possibile applicarle: %1").arg(failure));
    });
    connect(dialog, &QDialog::rejected, this, [answer] { answer(false, tr("L'utente ha rifiutato le modifiche: il disegno non è cambiato.")); });
    QTimer::singleShot(kProposalSeconds * 1000, dialog, [dialog, answer] {
        answer(false, tr("Nessuna risposta dall'utente entro %1 minuti: proposta ritirata, il disegno non è cambiato.").arg(kProposalSeconds / 60));
        dialog->close();
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    QApplication::alert(this);
}

// ------------------------------------------------------------------ strumenti di CanaloCAD

bool MainWindow::handleCanaloTool(const QString& tool, const QJsonObject& args, DrawingWidget* d, ClaudeLink::Reply& reply)
{
    canalo_->setDrawing(d);

    if (tool == QLatin1String("canalo_cerca_cavi")) {
        const int limit = std::clamp(args.value(QStringLiteral("massimo")).toInt(30), 1, 200);
        const QJsonArray found = canalo_->searchCables(args.value(QStringLiteral("testo")).toString().left(100),
                                                       args.value(QStringLiteral("categoria")).toString(), limit);
        reply(true, json(QJsonObject{{QStringLiteral("cavi"), found},
                                     {QStringLiteral("nota"), found.isEmpty()
                                          ? tr("Nessun cavo trovato: provare con meno parole o un'altra categoria.")
                                          : tr("Usare il campo id nei cavi di canalo_calcola e canalo_proponi_canalina.")}}));
        return true;
    }

    if (tool == QLatin1String("canalo_calcola")) {
        QString error;
        const QJsonObject st = canalo_->stateFor(args.value(QStringLiteral("cavi")).toArray(), args, &error);
        if (st.isEmpty())
            reply(false, error);
        else
            reply(true, json(canalo_->analyze(st)));
        return true;
    }

    if (tool == QLatin1String("canalo_canaline")) {
        if (!d) {
            reply(false, tr("In CartesioCAD non c'è nessun disegno aperto."));
            return true;
        }
        const QString id = args.value(QStringLiteral("codice")).toString().trimmed().toUpper();
        if (id.isEmpty()) {
            const QJsonArray list = canalo_->trayList();
            reply(true, json(QJsonObject{{QStringLiteral("canaline"), list},
                                         {QStringLiteral("nota"), list.isEmpty()
                                              ? tr("Nel disegno non ci sono canaline di CanaloCAD (si creano con canalo_proponi_canalina "
                                                   "o dalla finestra Canalina).")
                                              : tr("Con codice si ottengono cavi e riepilogo completo di una canalina.")}}));
            return true;
        }
        const QJsonObject st = canalo_->trayState(id);
        if (st.isEmpty()) {
            reply(false, tr("Nessuna canalina %1 nel disegno (elenco con canalo_canaline senza codice).").arg(id));
            return true;
        }
        QJsonObject a = canalo_->analyze(st);
        a.insert(QStringLiteral("codice"), id);
        a.insert(QStringLiteral("titolo"), st.value(QStringLiteral("title")).toString());
        reply(true, json(a));
        return true;
    }

    if (tool == QLatin1String("canalo_proponi_canalina")) {
        if (!d) {
            reply(false, tr("In CartesioCAD non c'è nessun disegno aperto."));
            return true;
        }
        if (claudeProposal_) {
            reply(false, tr("C'è già una proposta in attesa della scelta dell'utente."));
            return true;
        }
        const QString existing = args.value(QStringLiteral("canalina")).toString().trimmed().toUpper();
        if (!existing.isEmpty() && canalo_->trayState(existing).isEmpty()) {
            reply(false, tr("Nessuna canalina %1 nel disegno.").arg(existing));
            return true;
        }
        QString error;
        const QJsonObject st = canalo_->stateFor(args.value(QStringLiteral("cavi")).toArray(), args, &error);
        if (st.isEmpty()) {
            reply(false, tr("Proposta non valida, niente è stato mostrato all'utente. %1").arg(error));
            return true;
        }
        const QJsonObject analysis = canalo_->analyze(st);
        const QString description = args.value(QStringLiteral("descrizione")).toString().left(300);
        // Cosa mettere nel disegno: la sezione al vero (default), la tavola completa di CanaloCAD, o entrambe; dove.
        const QString insert = args.value(QStringLiteral("inserisci")).toString(QStringLiteral("sezione"));
        if (insert != QLatin1String("sezione") && insert != QLatin1String("tavola") && insert != QLatin1String("sezione_e_tavola")) {
            reply(false, tr("inserisci: sezione, tavola o sezione_e_tavola."));
            return true;
        }
        const bool withSection = insert != QLatin1String("tavola"), withSheet = insert != QLatin1String("sezione");
        std::optional<Vec2> at;
        if (const QJsonArray p = args.value(QStringLiteral("posizione")).toArray(); p.size() == 2 && p[0].isDouble() && p[1].isDouble())
            at = Vec2{p[0].toDouble(), p[1].toDouble()};
        QPointer<DrawingWidget> drawing(d);
        auto created = std::make_shared<QString>();
        auto apply = [this, drawing, st, existing, withSection, withSheet, at, created]() -> QString {
            if (!drawing)
                return tr("Il disegno è stato chiuso.");
            if (drawing->commands().isActive())
                drawing->cancel();
            canalo_->setDrawing(drawing);
            QString id = existing;
            if (existing.isEmpty())
                id = canalo_->addTray(st, withSection, at);
            else if (!canalo_->replaceTray(existing, st))
                return tr("La canalina %1 non c'è più.").arg(existing);
            if (id.isEmpty())
                return tr("Canalina non creata.");
            *created = id;
            if (withSheet) {   // la tavola: dove indicato, oppure accanto alla sezione
                canalo_->selectTray(id);
                canalo_->insertSheetAt(canalo_->sheetPosition(withSection ? std::nullopt : at));
            }
            ++claudeApplied_;
            return {};
        };
        const QString title = description.isEmpty() ? (existing.isEmpty() ? tr("Canalina nuova proposta dall'assistente IA")
                                                                          : tr("Canalina %1: cavi e misura proposti dall'assistente IA").arg(existing))
                                                     : description;
        auto done = [existing, analysis, withSection, withSheet, created](const QString& failure) {
            if (!failure.isEmpty())
                return tr("Non applicata: %1").arg(failure);
            const QJsonObject c = analysis.value(QStringLiteral("canalina")).toObject();
            const QString size = tr("%1 × %2 mm").arg(c.value(QStringLiteral("larghezza_mm")).toDouble()).arg(c.value(QStringLiteral("altezza_mm")).toDouble());
            const QString what = withSection && withSheet ? tr("con la sezione disegnata e la tavola") : withSheet ? tr("con la tavola") : tr("con la sezione disegnata");
            return existing.isEmpty()
                ? tr("L'utente ha applicato la proposta: canalina %1 (%2) creata %3 (Ctrl+Z annulla).")
                      .arg(*created, size, what)
                : tr("L'utente ha applicato la proposta: canalina %1 aggiornata (%2)%3.")
                      .arg(existing, size, withSheet ? tr(", tavola inserita") : QString());
        };
        if (claudeAutoApprove_) {   // solo negli script di prova (-ASSISTENTEATTIVA AUTO)
            const QString failure = apply();
            print(tr("Assistente IA (prova, applicata senza conferma): %1").arg(title));
            reply(failure.isEmpty(), done(failure));
            return true;
        }
        auto* dialog = new FormDialog(tr("Canalina proposta dall'assistente IA"), FormDialog::Ok | FormDialog::Cancel, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->group(title);
        dialog->note(tr("Disegno: %1. %2 La canalina si applica solo con OK (con la sezione disegnata) e si annulla con Ctrl+Z. "
                        "Calcoli di CanaloCAD: valori indicativi da verificare.")
                         .arg(d->displayName(), existing.isEmpty() ? tr("Viene creata una canalina nuova.")
                                                                   : tr("Cambiano cavi e misura della canalina %1.").arg(existing)));
        QStringList lines;
        for (const QJsonValue& v : analysis.value(QStringLiteral("riepilogo")).toArray())
            lines << v.toString();
        dialog->list(lines, 0, [](int) {});
        dialog->setSize(620, 460);
        claudeProposal_ = dialog;
        auto replied = std::make_shared<bool>(false);
        auto shared = std::make_shared<ClaudeLink::Reply>(std::move(reply));
        auto answer = [shared, replied](bool ok, const QString& text) {
            if (!*replied) {
                *replied = true;
                (*shared)(ok, text);
            }
        };
        connect(dialog, &QDialog::accepted, this, [this, apply, answer, done, title] {
            const QString failure = apply();
            if (failure.isEmpty())
                print(tr("Assistente IA: applicata \"%1\" (Ctrl+Z per annullarla).").arg(title));
            answer(failure.isEmpty(), done(failure));
        });
        connect(dialog, &QDialog::rejected, this, [answer] { answer(false, tr("L'utente ha rifiutato la proposta: il disegno non è cambiato.")); });
        QTimer::singleShot(kProposalSeconds * 1000, dialog, [dialog, answer] {
            answer(false, tr("Nessuna risposta dall'utente entro %1 minuti: proposta ritirata, il disegno non è cambiato.").arg(kProposalSeconds / 60));
            dialog->close();
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        QApplication::alert(this);
        return true;
    }

    if (tool == QLatin1String("canalo_scheda")) {
        const QString action = args.value(QStringLiteral("azione")).toString();
        QString why;
        if (!canaloEmbedded()) {
            reply(false, tr("La scheda CanaloCAD non è disponibile su questo computer (serve Windows con Microsoft Edge "
                            "WebView2): usare gli altri strumenti canalo_*."));
            return true;
        }
        if (action == QLatin1String("apri")) {
            canaloShowTab();
            reply(true, tr("Scheda CanaloCAD aperta."));
            return true;
        }
        if (action == QLatin1String("invia_selezione")) {
            if (!d || d->selection().empty()) {
                reply(false, tr("Nessun oggetto selezionato nel disegno: selezionare la sezione (anche con lo strumento seleziona)."));
                return true;
            }
            fileTabs_->setCurrentIndex(stack_->indexOf(d));
            if (!canaloSend()) {
                reply(false, tr("Sezione non inviata a CanaloCAD (vedi la riga di comando)."));
                return true;
            }
            reply(true, tr("Sezione inviata alla scheda CanaloCAD: si apre in una scheda canalina nuova della pagina. "
                           "Con azione leggi si ottengono cavi e risultati."));
            return true;
        }
        if (action == QLatin1String("leggi")) {
            if (!canaloTab_ || !canaloReady_) {
                reply(false, tr("La scheda CanaloCAD non è aperta (azione apri)."));
                return true;
            }
            auto shared = std::make_shared<ClaudeLink::Reply>(std::move(reply));
            canaloTab_->host()->runAsync(QStringLiteral("window.CanaloHost.state()"),
                                         [this, shared](const QJsonValue& value, const QString& error) {
                if (!error.isEmpty() || !value.isObject()) {
                    (*shared)(false, tr("Scheda CanaloCAD non letta: %1").arg(error));
                    return;
                }
                const QJsonObject st = value.toObject();
                QJsonObject a = canalo_->analyze(st);
                a.insert(QStringLiteral("titolo"), st.value(QStringLiteral("title")).toString());
                a.insert(QStringLiteral("nota"), tr("Scheda canalina attiva nella pagina di CanaloCAD (non ancora nel disegno: per "
                                                    "riportarla l'utente usa Esporta > Inserisci nel disegno)."));
                (*shared)(true, json(a));
            });
            return true;
        }
        reply(false, tr("Azione sconosciuta: %1 (apri, invia_selezione, leggi).").arg(action));
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ finestra "Assistente IA"

QString MainWindow::configureClaudeDesktop()
{
    const QString bridge = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/cartesio-mcp.exe"));
    if (!QFileInfo::exists(bridge))
        return tr("Manca %1 accanto al programma: reinstallare CartesioCAD.").arg(bridge);
    const QStringList folders = claudeDesktopFolders();
    if (folders.isEmpty())
        return tr("Claude Desktop non risulta installato. Installarlo da claude.ai/download, aprirlo una volta, "
                  "poi ripetere questa configurazione.");
    QStringList done;
    for (const QString& folder : folders) {
        const QString path = folder + QStringLiteral("/claude_desktop_config.json");
        QJsonObject config;
        if (QFile file(path); file.exists()) {
            if (!file.open(QIODevice::ReadOnly))
                return tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(path));
            const QByteArray data = file.readAll();
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (!data.trimmed().isEmpty() && (parseError.error != QJsonParseError::NoError || !doc.isObject()))
                return tr("%1 non è un JSON valido: non lo modifico. Correggerlo o cancellarlo e ripetere.")
                    .arg(QDir::toNativeSeparators(path));
            config = doc.object();
            const QString backup = path + QStringLiteral(".prima-di-cartesiocad");
            if (!QFileInfo::exists(backup))
                QFile::copy(path, backup);
        }
        QJsonObject servers = config.value(QStringLiteral("mcpServers")).toObject();
        servers.insert(QStringLiteral("cartesiocad"), QJsonObject{{QStringLiteral("command"), bridge},
                                                                  {QStringLiteral("args"), QJsonArray{}}});
        config.insert(QStringLiteral("mcpServers"), servers);
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(config).toJson(QJsonDocument::Indented)) < 0 || !out.commit())
            return tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(path));
        done << QDir::toNativeSeparators(path);
    }
    return tr("Fatto: CartesioCAD aggiunto ai connettori di Claude Desktop (%1).\n\nOra aprire Claude Desktop "
              "(se era aperto, chiuderlo del tutto, anche dall'icona vicino all'orologio, e riaprirlo): fra i "
              "connettori compare \"cartesiocad\". Se non compare, chiudere Claude Desktop e premere di nuovo "
              "Configura: Claude Desktop aperto può riscrivere il proprio file di configurazione.")
        .arg(done.join(QStringLiteral(", ")));
}

void MainWindow::showClaudeDialog()
{
#ifndef Q_OS_WIN
    QMessageBox::information(this, tr("Assistente IA"), tr("L'Assistente IA è disponibile solo nella versione per Windows."));
    return;
#endif
    FormDialog dialog(tr("Assistente IA"), FormDialog::Close, this);
    dialog.group(tr("Connettore per Claude Desktop"));
    dialog.note(tr("Con il connettore si chiede a Claude, nell'app Claude Desktop con il proprio account, di leggere e "
                   "modificare il disegno aperto. Claude propone le modifiche, CartesioCAD le mostra e le applica solo "
                   "dopo la conferma; il salvataggio crea sempre una nuova versione accanto al file originale."));
    dialog.note(tr("Privacy: CartesioCAD non si collega a Internet. Il connettore è un canale locale, solo per "
                   "questo utente di Windows; quello che Claude legge (di default la selezione) passa a Claude Desktop "
                   "e da lì ad Anthropic, secondo le condizioni dell'account Claude. Con i disegni di clienti verificare "
                   "di poterli condividere; con gli account personali controllare l'impostazione sull'uso delle "
                   "conversazioni per migliorare i modelli."));
    const int status = dialog.note(QString());
    const auto refresh = [this, &dialog, status] {
        dialog.setFieldText(status, claude_ && claude_->running() ? tr("Stato: connettore attivo.")
                                                                  : tr("Stato: connettore spento."));
    };
    dialog.check(tr("Attiva il connettore (anche ai prossimi avvii)"), appOptions().claudeConnector, [this, refresh](bool on) {
        appOptions().claudeConnector = on;
        appOptions().save();
        QString error;
        if (on && !startClaude(&error))
            QMessageBox::warning(this, tr("Assistente IA"), tr("Impossibile attivare il connettore: %1").arg(error));
        if (!on)
            stopClaude();
        refresh();
    });
    dialog.note(tr("Prima di premere Configura chiudere Claude Desktop (anche dall'icona vicino all'orologio)."));
    dialog.buttonsRow({{tr("Configura Claude Desktop"), [this] {
                           QMessageBox::information(this, tr("Assistente IA"), configureClaudeDesktop());
                       }}});
    dialog.note(tr("Claude è un marchio di Anthropic. CartesioCAD è un progetto indipendente, non collegato ad Anthropic."));
    dialog.setSize(580, 460);
    refresh();
    dialog.exec();
}

} // namespace cartesio
