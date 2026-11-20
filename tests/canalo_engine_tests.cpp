// Motore di CanaloCAD in CartesioCAD (app/CanaloEngine): la tavola DXF deve essere identica, byte per byte, a
// quella esportata dalla pagina vera di CanaloCAD (tests/canalo_dxf, generati con scripts/canalo/gendxf.js).
#include "app/CanaloEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>

using namespace cartesio;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);   // i font per misurare i testi
    CanaloEngine& engine = CanaloEngine::instance();
    if (!engine.ok()) {
        std::printf("Motore non caricato: %s\n", qPrintable(engine.error()));
        return 1;
    }

    // Database e stato iniziale della pagina.
    const QJsonObject db = engine.database();
    CHECK(db.value(QStringLiteral("cables")).toArray().size() > 50);
    CHECK(db.value(QStringLiteral("series")).toArray().size() == 4);
    const QJsonObject fresh = engine.defaultState();
    CHECK(fresh.value(QStringLiteral("width")).toDouble() == 200.0 && fresh.value(QStringLiteral("cables")).toArray().isEmpty());

    // Cavi dal database come il "+" della pagina: quantità con le fasi, terne a trifoglio.
    QJsonObject metrics;
    QJsonObject st = engine.addCable(fresh, QStringLiteral("FG16_3G2.5"), 2, false, &metrics);
    QJsonArray cables = st.value(QStringLiteral("cables")).toArray();
    CHECK(cables.size() == 2 && cables[0].toObject().value(QStringLiteral("code")).toString().endsWith(QStringLiteral(" - L1")));
    CHECK(metrics.value(QStringLiteral("fillPct")).toDouble() > 0.0);
    st = engine.addCable(st, QStringLiteral("FG16_1X95"), 1, true, &metrics);
    CHECK(st.value(QStringLiteral("cables")).toArray().size() == 5);   // terna: 3 unipolari
    // Dimensiona: con molti cavi la canalina si allarga, con la riserva chiesta.
    for (int i = 0; i < 4; ++i)
        st = engine.addCable(st, QStringLiteral("FG16_4G16"), 5, false);
    st.insert(QStringLiteral("width"), 50);
    const QJsonObject sized = engine.autoSize(st, 30.0, &metrics);
    CHECK(sized.value(QStringLiteral("width")).toDouble() > 50.0);
    CHECK(metrics.value(QStringLiteral("fillPct")).toDouble() <= 70.0);
    // Cavo proprio nel database della pagina.
    QJsonObject own{{QStringLiteral("id"), QStringLiteral("USR_BUS")}, {QStringLiteral("cat"), QStringLiteral("custom")},
                    {QStringLiteral("code"), QStringLiteral("BUS")}, {QStringLiteral("desc"), QStringLiteral("Bus allarme")},
                    {QStringLiteral("type"), QStringLiteral("Bus allarme")}, {QStringLiteral("form"), QStringLiteral("2x2x0,5")},
                    {QStringLiteral("d"), 9.4}, {QStringLiteral("w"), 0.09}, {QStringLiteral("sec"), 0.5}, {QStringLiteral("cores"), 2},
                    {QStringLiteral("iz0"), 2}, {QStringLiteral("bendK"), 10}, {QStringLiteral("sys"), QStringLiteral("SPECIAL_ELV")}};
    engine.setOwnCables(QJsonArray{own});
    const QJsonObject withOwn = engine.addCable(fresh, QStringLiteral("USR_BUS"), 1, false);
    CHECK(withOwn.value(QStringLiteral("cables")).toArray().size() == 1
          && withOwn.value(QStringLiteral("cables")).toArray()[0].toObject().value(QStringLiteral("sys")).toString() == QStringLiteral("SPECIAL_ELV"));
    engine.setOwnCables({});
    CHECK(engine.addCable(fresh, QStringLiteral("USR_BUS"), 1, false).value(QStringLiteral("cables")).toArray().isEmpty());
    if (const QString expr = qEnvironmentVariable("CANALO_EVAL"); !expr.isEmpty())
        std::printf("%s\n", qPrintable(engine.evaluate(expr)));   // diagnosi: CANALO_EVAL=espressione

    // Tavole identiche a quelle della pagina.
    const QDir dir(QStringLiteral(CARTESIO_SOURCE_DIR "/tests/canalo_dxf"));
    int cases = 0;
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile json(info.filePath());
        QFile dxf(dir.filePath(info.completeBaseName() + QStringLiteral(".dxf")));
        if (!json.open(QIODevice::ReadOnly) || !dxf.open(QIODevice::ReadOnly)) {
            std::printf("FALLITO: %s senza la sua tavola\n", qPrintable(info.fileName()));
            ++failures;
            continue;
        }
        const QJsonObject state = QJsonDocument::fromJson(json.readAll()).object();
        const QByteArray expected = dxf.readAll();
        const QByteArray got = engine.dxfBytes(state, info.completeBaseName());
        ++cases;
        // La stessa tavola riletta con l'importazione della pagina: stesse misure e stessi cavi della pagina.
        QFile importRef(dir.filePath(info.completeBaseName() + QStringLiteral(".import.txt")));
        if (importRef.open(QIODevice::ReadOnly)) {
            const QJsonObject back = engine.importDxf(engine.defaultState(), QString::fromLatin1(expected),
                                                      info.completeBaseName() + QStringLiteral(".dxf"));
            QStringList lines{QStringLiteral("%1x%2").arg(back.value(QStringLiteral("width")).toDouble())
                                  .arg(back.value(QStringLiteral("height")).toDouble())};
            for (const QJsonValue& c : back.value(QStringLiteral("cables")).toArray())
                lines << QStringLiteral("%1 | %2").arg(c.toObject().value(QStringLiteral("code")).toString())
                             .arg(c.toObject().value(QStringLiteral("d")).toDouble());
            const QString gotImport = lines.join(u'\n') + u'\n';
            const QString wantImport = QString::fromUtf8(importRef.readAll());
            if (gotImport != wantImport) {
                ++failures;
                std::printf("FALLITO %s: importazione diversa dalla pagina:\n--- pagina\n%s--- CartesioCAD\n%s%s\n",
                            qPrintable(info.completeBaseName()), qPrintable(wantImport), qPrintable(gotImport), qPrintable(engine.error()));
            }
        }
        // Diagnosi: CANALO_DUMP=cartella salva le tavole ottenute, per confrontarle con quelle della pagina.
        if (const QString dump = qEnvironmentVariable("CANALO_DUMP"); !dump.isEmpty()) {
            QFile out(QDir(dump).filePath(info.completeBaseName() + QStringLiteral(".dxf")));
            if (out.open(QIODevice::WriteOnly))
                out.write(got);
        }
        if (got == expected)
            continue;
        ++failures;
        // Prima riga diversa, per capire cosa non torna.
        const QList<QByteArray> a = expected.split('\n'), b = got.split('\n');
        qsizetype line = 0;
        while (line < a.size() && line < b.size() && a[line] == b[line])
            ++line;
        std::printf("FALLITO %s: tavola diversa dalla pagina alla riga %lld: atteso \"%s\", ottenuto \"%s\" (%lld / %lld byte)\n",
                    qPrintable(info.completeBaseName()), static_cast<long long>(line + 1),
                    line < a.size() ? a[line].trimmed().constData() : "(fine)", line < b.size() ? b[line].trimmed().constData() : "(fine)",
                    static_cast<long long>(expected.size()), static_cast<long long>(got.size()));
        if (!engine.ok())
            std::printf("  %s\n", qPrintable(engine.error()));
    }
    CHECK(cases >= 4);

    if (failures)
        std::printf("%d controlli falliti.\n", failures);
    else
        std::printf("Motore CanaloCAD: %d tavole identiche alla pagina.\n", cases);
    return failures ? 1 : 0;
}
