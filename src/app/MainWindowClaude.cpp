// Connettore per Claude Desktop: gli strumenti che Claude usa sul disegno aperto (vedi ClaudeLink.h e
// src/mcp/main.cpp), la conferma delle modifiche proposte e la finestra "Claude" per attivare il connettore.
//
// Regole:
// - Claude legge solo quello che chiede (di default la selezione) e non vede file, cartelle o rete.
// - Le modifiche sono un elenco di operazioni semplici, controllate qui una per una; si applicano solo quando
//   l'utente preme "Applica" nella finestra di CartesioCAD, in un unico passo annullabile.
// - Il salvataggio crea sempre un file nuovo accanto all'originale (mai sovrascritto).

#include "app/AppOptions.h"
#include "app/ClaudeLink.h"
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
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <cmath>
#include <map>
#include <set>

namespace cartesio {

namespace {

constexpr int kMaxOperations = 2000;
constexpr int kMaxIds = 20000;
constexpr double kMaxCoordinate = 1e12;
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

// Modifiche pronte da applicare: oggetti nuovi o copie modificate (stesso id) e id da togliere.
struct ClaudePlan {
    std::vector<std::pair<QString, int>> newLayers;   // nome, colore
    std::vector<std::unique_ptr<Entity>> added;
    std::vector<EntityId> removed;
    std::vector<EntityId> touched;   // oggetti esistenti interessati (per evidenziarli)
    QStringList summary;
};

// Controlla le operazioni sul documento e prepara le modifiche. Con `layersCreated` falso i layer di crea_layer non
// esistono ancora (anteprima): gli oggetti destinati a loro si preparano sul layer corrente, solo per il conteggio.
std::optional<ClaudePlan> buildPlan(const Document& doc, const QJsonArray& ops, bool layersCreated, QString* error)
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
    int idCount = 0;

    const auto layerIndex = [&](const QString& name, int* index) -> bool {
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
    const auto idList = [&](const QJsonObject& op, std::vector<qint64>* ids, QString* why) -> bool {
        const QJsonArray a = op.value(QStringLiteral("ids")).toArray();
        if (a.isEmpty()) {
            *why = QObject::tr("manca l'elenco \"ids\"");
            return false;
        }
        idCount += static_cast<int>(a.size());
        if (idCount > kMaxIds) {
            *why = QObject::tr("troppi oggetti (al massimo %1)").arg(kMaxIds);
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

// ------------------------------------------------------------------ canale

bool MainWindow::startClaude(QString* error)
{
    if (!claude_) {
        claude_ = new ClaudeLink([this](const QString& tool, const QJsonObject& args, ClaudeLink::Reply reply) {
            handleClaudeTool(tool, args, std::move(reply));
        }, this);
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
        print(tr("Connettore per Claude non attivo: %1 (forse è già aperto un altro CartesioCAD).").arg(error));
}

void MainWindow::handleClaudeTool(const QString& tool, const QJsonObject& args, ClaudeLink::Reply reply)
{
    ++claudeCalls_;
    DrawingWidget* d = current();
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

    if (tool == QLatin1String("leggi_oggetti")) {
        const QString scope = args.value(QStringLiteral("ambito")).toString(QStringLiteral("selezione"));
        const int limit = std::clamp(args.value(QStringLiteral("massimo")).toInt(300), 1, 2000);
        const QString onlyLayer = args.value(QStringLiteral("layer")).toString();
        const QString onlyType = args.value(QStringLiteral("tipo")).toString();
        BBox area;
        if (scope == QLatin1String("area")) {
            const QJsonArray a = args.value(QStringLiteral("area")).toArray();
            if (a.size() != 4) {
                reply(false, tr("Con ambito \"area\" serve area: [x1, y1, x2, y2]."));
                return;
            }
            area.expand(Vec2{a[0].toDouble(), a[1].toDouble()});
            area.expand(Vec2{a[2].toDouble(), a[3].toDouble()});
        } else if (scope != QLatin1String("selezione") && scope != QLatin1String("tutto")) {
            reply(false, tr("Ambito sconosciuto: usare selezione, tutto o area."));
            return;
        }
        std::set<EntityId> selected(d->selection().begin(), d->selection().end());
        if (scope == QLatin1String("selezione") && selected.empty()) {
            reply(false, tr("Nel disegno non è selezionato niente: chiedere all'utente di selezionare gli oggetti, "
                            "oppure leggere con ambito \"tutto\" o \"area\"."));
            return;
        }
        QJsonArray objects;
        int matching = 0;
        for (const auto& e : doc.entities()) {
            if (scope == QLatin1String("selezione") ? !selected.count(e->id()) : e->space() != 0)
                continue;
            if (scope == QLatin1String("area") && !e->touchesRect(area))
                continue;
            if (!onlyLayer.isEmpty() && layerName(doc, e->layer()).compare(onlyLayer, Qt::CaseInsensitive) != 0)
                continue;
            if (!onlyType.isEmpty() && typeKey(e->type()) != onlyType.toLower())
                continue;
            if (++matching <= limit)
                objects.append(describe(doc, *e));
        }
        QJsonObject answer{{QStringLiteral("oggetti"), objects}, {QStringLiteral("trovati"), matching}};
        if (matching > limit)
            answer.insert(QStringLiteral("nota"), tr("Mostrati i primi %1 di %2: restringere con layer, tipo o area.").arg(limit).arg(matching));
        reply(true, json(answer));
        return;
    }

    if (tool == QLatin1String("seleziona")) {
        std::vector<EntityId> ids;
        for (const QJsonValue& v : args.value(QStringLiteral("ids")).toArray())
            if (doc.find(static_cast<EntityId>(v.toDouble())) && ids.size() < static_cast<std::size_t>(kMaxIds))
                ids.push_back(static_cast<EntityId>(v.toDouble()));
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
        if (!d->saveTo(target, &error, dwg ? DxfVersion::R2000 : d->dxfVersion())) {
            reply(false, tr("Salvataggio non riuscito: %1").arg(error));
            return;
        }
        print(tr("Claude: salvata la nuova versione %1 (l'originale %2 non è stato toccato).")
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
    const auto preview = buildPlan(d->document(), ops, false, &error);
    if (!preview) {
        reply(false, tr("Proposta non valida, niente è stato mostrato all'utente. %1").arg(error));
        return;
    }

    QPointer<DrawingWidget> drawing(d);
    // Applica: si ricontrolla tutto sul disegno di adesso (l'utente può averlo cambiato nel frattempo).
    auto apply = [this, drawing, ops]() -> QString {
        if (!drawing)
            return tr("Il disegno è stato chiuso.");
        if (drawing->commands().isActive())
            drawing->cancel();
        Document& doc = drawing->document();
        QString error;
        const auto check = buildPlan(doc, ops, false, &error);
        if (!check)
            return error;
        std::vector<int> created;
        for (const auto& [name, color] : check->newLayers) {
            Layer layer;
            layer.name = name.toStdString();
            layer.color = color;
            created.push_back(doc.addLayer(layer));
        }
        auto plan = buildPlan(doc, ops, true, &error);
        if (!plan) {
            for (auto it = created.rbegin(); it != created.rend(); ++it)
                doc.removeLayer(*it);
            return error;
        }
        std::vector<EntityId> touched = plan->touched;
        doc.commit(std::move(plan->added), plan->removed);
        drawing->notifyChanged();
        std::vector<EntityId> alive;
        for (EntityId id : touched)
            if (doc.find(id))
                alive.push_back(id);
        drawing->setSelection(alive);
        ++claudeApplied_;
        return {};
    };

    const QString title = description.isEmpty() ? tr("Modifiche proposte da Claude") : description;
    if (claudeAutoApprove_) {   // solo negli script di prova (-CLAUDEATTIVA AUTO)
        const QString failure = apply();
        print(tr("Claude (prova, applicata senza conferma): %1").arg(title));
        reply(failure.isEmpty(), failure.isEmpty() ? tr("L'utente ha applicato le modifiche.") : tr("Non applicate: %1").arg(failure));
        return;
    }

    d->setSelection(preview->touched);   // gli oggetti interessati si vedono evidenziati
    auto* dialog = new FormDialog(tr("Claude propone delle modifiche"), FormDialog::Ok | FormDialog::Cancel, this);
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
            print(tr("Claude: applicate le modifiche \"%1\" (Ctrl+Z per annullarle).").arg(title));
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

// ------------------------------------------------------------------ finestra "Claude"

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
    FormDialog dialog(tr("Claude"), FormDialog::Close, this);
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
            QMessageBox::warning(this, tr("Claude"), tr("Impossibile attivare il connettore: %1").arg(error));
        if (!on)
            stopClaude();
        refresh();
    });
    dialog.note(tr("Prima di premere Configura chiudere Claude Desktop (anche dall'icona vicino all'orologio)."));
    dialog.buttonsRow({{tr("Configura Claude Desktop"), [this] {
                           QMessageBox::information(this, tr("Claude"), configureClaudeDesktop());
                       }}});
    dialog.note(tr("Claude è un marchio di Anthropic. CartesioCAD è un progetto indipendente, non collegato ad Anthropic."));
    refresh();
    dialog.exec();
}

} // namespace cartesio
