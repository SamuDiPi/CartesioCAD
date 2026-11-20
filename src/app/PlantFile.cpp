#include "app/PlantFile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>

namespace cartesio {

namespace {

using namespace plant;

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QJsonValue& v) { return v.toString().toStdString(); }

const char* kFormat = "CartesioCAD impianto";

QJsonObject toJson(const plant::Circuit& c)
{
    QJsonObject o{{"sigla", q(c.code)}, {"descrizione", q(c.description)}, {"tipo", q(c.cableType)}, {"formazione", q(c.formation)},
                  {"sistema", QString::fromLatin1(systemName(c.system))}};
    if (c.ib > 0.0)
        o["ib"] = c.ib;
    if (c.lengthM > 0.0)
        o["lunghezza"] = c.lengthM;
    if (!c.breaker.empty())
        o["interruttore"] = q(c.breaker);
    if (c.diameter > 0.0)
        o["diametro"] = c.diameter;
    if (!c.notes.empty())
        o["note"] = q(c.notes);
    if (c.fromPdf)
        o["daSchema"] = true;
    if (c.breakerIn > 0.0)
        o["In"] = c.breakerIn;
    if (c.izRef > 0.0)
        o["izRiferimento"] = c.izRef;
    return o;
}

plant::Circuit circuitFrom(const QJsonObject& o)
{
    plant::Circuit c;
    c.code = s(o["sigla"]);
    c.description = s(o["descrizione"]);
    c.cableType = s(o["tipo"]);
    c.formation = s(o["formazione"]);
    c.system = systemFromName(s(o["sistema"]));
    c.ib = o["ib"].toDouble();
    c.lengthM = o["lunghezza"].toDouble();
    c.breaker = s(o["interruttore"]);
    c.diameter = o["diametro"].toDouble();
    c.notes = s(o["note"]);
    c.fromPdf = o["daSchema"].toBool();
    c.breakerIn = o["In"].toDouble();
    c.izRef = o["izRiferimento"].toDouble();
    return c;
}

QJsonObject toJson(const canalo::CableType& t)
{
    return {{"codice", q(t.code)}, {"descrizione", q(t.desc)}, {"tipo", q(t.type)}, {"formazione", q(t.form)}, {"diametro", t.d},
            {"peso", t.w}, {"sezione", t.sec}, {"conduttori", t.cores}, {"iz0", t.iz0}, {"raggioCurvatura", t.bendK},
            {"sistema", q(t.sys)}};
}

canalo::CableType cableFrom(const QJsonObject& o)
{
    canalo::CableType t;
    t.id = "CST_" + s(o["codice"]);
    t.cat = "custom";
    t.code = s(o["codice"]);
    t.desc = s(o["descrizione"]);
    t.type = s(o["tipo"]);
    t.form = s(o["formazione"]);
    t.d = o["diametro"].toDouble(10.0);
    t.w = o["peso"].toDouble(0.0);
    if (t.w <= 0.0)
        t.w = canalo::fixed(std::pow(t.d / 10.0, 2) * 0.145, 2);
    t.sec = o["sezione"].toDouble(2.5);
    t.cores = o["conduttori"].toInt(2);
    t.iz0 = o["iz0"].toDouble(20.0);
    t.bendK = o["raggioCurvatura"].toDouble(10.0);
    t.sys = o["sistema"].toString(QStringLiteral("BT_POWER")).toStdString();
    t.standard = "Cavo del progetto";
    return t;
}

QJsonObject toJson(const Run& r)
{
    QJsonArray circuits;
    for (const std::string& c : r.circuits)
        circuits.append(q(c));
    QJsonObject o{{"codice", q(r.id)}, {"nome", q(r.name)}, {"tipo", QString::fromLatin1(runKindName(r.kind))}, {"circuiti", circuits}};
    if (r.kind == RunKind::Tube) {
        o["tubo"] = q(r.tubeType);
        o["diametroEsterno"] = r.tubeOuter;
        if (r.tubeInner > 0.0)
            o["diametroInterno"] = r.tubeInner;
    } else {
        o["larghezza"] = r.width;
        o["altezza"] = r.height;
        o["setti"] = r.septums;
        o["coperchio"] = q(r.cover);
        if (r.kind == RunKind::Tray) {
            o["staffaggio"] = q(r.support);
            o["passo"] = r.spanM;
            o["marca"] = q(r.brand);
            o["serie"] = q(r.seriesId);
        }
    }
    if (!r.notes.empty())
        o["note"] = q(r.notes);
    if (r.kind == RunKind::Tube)
        o["posa"] = q(r.laying);
    if (r.ambientC != 30.0)
        o["temperatura"] = r.ambientC;
    return o;
}

Run runFrom(const QJsonObject& o)
{
    Run r;
    r.id = s(o["codice"]);
    r.name = s(o["nome"]);
    const QString kind = o["tipo"].toString();
    r.kind = kind == QLatin1String("canale") ? RunKind::Duct : kind == QLatin1String("passerella") ? RunKind::Tray : RunKind::Tube;
    for (const QJsonValue& v : o["circuiti"].toArray())
        r.circuits.push_back(s(v));
    r.tubeType = o["tubo"].toString(QStringLiteral("rigido")).toStdString();
    r.tubeOuter = o["diametroEsterno"].toDouble(25.0);
    r.tubeInner = o["diametroInterno"].toDouble(0.0);
    r.width = o["larghezza"].toDouble(100.0);
    r.height = o["altezza"].toDouble(60.0);
    r.septums = o["setti"].toInt(0);
    r.cover = o["coperchio"].toString(QStringLiteral("snap")).toStdString();
    r.support = o["staffaggio"].toString(QStringLiteral("none")).toStdString();
    r.spanM = o["passo"].toDouble(1.5);
    r.brand = o["marca"].toString(QStringLiteral("MARCA_A")).toStdString();
    r.seriesId = o["serie"].toString(QStringLiteral("A1_ASO")).toStdString();
    r.notes = s(o["note"]);
    r.laying = o["posa"].toString(QStringLiteral("vista")).toStdString();
    r.ambientC = o["temperatura"].toDouble(30.0);
    return r;
}

double firstNumber(const std::string& text)
{
    static const QRegularExpression re(QStringLiteral("(\\d+(?:[.,]\\d+)?)"));
    const auto m = re.match(q(text));
    return m.hasMatch() ? m.captured(1).replace(u',', u'.').toDouble() : 0.0;
}

} // namespace

bool loadPlant(const QString& path, plant::Project& project, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Impossibile aprire %1").arg(path);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (!doc.isObject() || doc.object()["formato"].toString() != QLatin1String(kFormat)) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError
                         ? QObject::tr("%1 non è valido: %2").arg(path, parseError.errorString())
                         : QObject::tr("%1 non è un file di progetto dell'impianto").arg(path);
        return false;
    }
    const QJsonObject root = doc.object();
    plant::Project p;
    p.name = s(root["nome"]);
    for (const QJsonValue& v : root["circuiti"].toArray())
        p.circuits.push_back(circuitFrom(v.toObject()));
    for (const QJsonValue& v : root["cavi"].toArray())
        p.cables.push_back(cableFrom(v.toObject()));
    for (const QJsonValue& v : root["tratte"].toArray())
        p.runs.push_back(runFrom(v.toObject()));
    project = std::move(p);
    return true;
}

QJsonObject cableTypeToJson(const canalo::CableType& type) { return toJson(type); }

canalo::CableType cableTypeFromJson(const QJsonObject& object) { return cableFrom(object); }

bool savePlant(const QString& path, const plant::Project& project, QString* error)
{
    QJsonArray circuits, cables, runs;
    for (const plant::Circuit& c : project.circuits)
        circuits.append(toJson(c));
    for (const canalo::CableType& t : project.cables)
        cables.append(toJson(t));
    for (const plant::Run& r : project.runs)
        runs.append(toJson(r));
    const QJsonObject root{{"formato", QString::fromLatin1(kFormat)}, {"versione", 1}, {"nome", q(project.name)},
                           {"circuiti", circuits}, {"cavi", cables}, {"tratte", runs}};
    QSaveFile f(path);   // scrittura sicura: o il file nuovo intero, o resta quello di prima
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = QObject::tr("Impossibile salvare %1").arg(path);
        return false;
    }
    return true;
}

std::pair<int, int> mergeSchema(plant::Project& project, const std::vector<cartesio::Circuit>& schema)
{
    int added = 0, updated = 0;
    for (const cartesio::Circuit& sc : schema) {
        std::string type, formation;
        plant::splitCableText(sc.cable, type, formation);
        if (formation.empty())   // "Cavo / GENERAL", "NESSUNA LINEA": nessun cavo indicato
            type.clear();
        plant::Circuit* c = project.circuit(sc.code);
        if (!c) {
            project.circuits.push_back({});
            c = &project.circuits.back();
            c->code = sc.code;
            ++added;
        } else {
            ++updated;
        }
        c->description = sc.description;
        c->cableType = type;
        c->formation = formation;
        c->ib = firstNumber(sc.current);
        c->lengthM = firstNumber(sc.length);
        c->breaker = sc.breaker;
        // Corrente nominale dall'interruttore ("SCHNEIDER / 4 x 25 - ..." = 4 poli da 25 A).
        static const QRegularExpression inRe(QStringLiteral("(\\d+)\\s*[xX]\\s*(\\d+(?:[.,]\\d+)?)"));
        const auto m = inRe.match(q(sc.breaker));
        if (m.hasMatch())
            c->breakerIn = m.captured(2).replace(u',', u'.').toDouble();
        c->fromPdf = true;
    }
    return {added, updated};
}

} // namespace cartesio
