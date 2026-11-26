#include "app/CommandLineTool.h"

#include "app/AppOptions.h"
#include "app/ConvertFile.h"
#include "app/DrawingWidget.h"
#include "core/DataExport.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace cartesio {

namespace {

const QStringList kTasks = {QStringLiteral("--converti"), QStringLiteral("--csv"), QStringLiteral("--aiuto"),
                            QStringLiteral("--help"), QStringLiteral("--versione"), QStringLiteral("--version")};

// Avviato direttamente da un terminale (non da cartesio-cli.exe), il programma per Windows non ha dove scrivere:
// si collega alla console di chi lo ha avviato.
void attachConsole()
{
#ifdef Q_OS_WIN
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void say(const QString& text, std::FILE* stream = stdout)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

QString native(const QString& path) { return QDir::toNativeSeparators(path); }

QString usage()
{
    return QObject::tr(
        "CartesioCAD %1 - riga di comando (senza finestra)\n"
        "\n"
        "  --converti <disegno.dwg|dxf> <uscita.dxf|dwg|pdf> [--r12] [--verifica] [--sovrascrivi]\n"
        "      Converte un disegno; il formato viene dall'estensione dell'uscita (--r12: DXF R12 invece di 2000).\n"
        "  --converti <cartella> <cartella di uscita> --formato dxf|dxf12|dwg|pdf [--sottocartelle] [--verifica] [--sovrascrivi]\n"
        "      Converte tutti i DWG/DXF di una cartella (con --sottocartelle anche quelli delle sottocartelle).\n"
        "  --csv <disegno.dwg|dxf> <uscita.csv> [--modo blocchi|conteggio|oggetti] [--solo-modello] [--separatore ,]\n"
        "      Dati del disegno in una tabella: blocchi con attributi (predefinito), distinta dei blocchi con le\n"
        "      quantità, oppure tutti gli oggetti con layer, lunghezza e area. Separatore predefinito \";\" con la\n"
        "      virgola decimale (Excel in italiano); \"--separatore ,\" per il CSV standard.\n"
        "  --versione   --aiuto\n"
        "\n"
        "--verifica rilegge ogni file scritto e lo confronta con l'originale. Il file originale non viene mai\n"
        "sovrascritto; un file di uscita già esistente solo con --sovrascrivi.\n"
        "Uscita: 0 riuscito, 1 errore, 2 argomenti non validi, 3 convertito ma con differenze alla verifica.")
        .arg(QCoreApplication::applicationVersion());
}

struct Args {
    QStringList positional;
    QStringList flags;
    QMap<QString, QString> values;
};

// Le opzioni con un valore (--formato, --modo, --separatore) prendono l'argomento seguente.
bool parse(const QStringList& in, Args* out, QString* error)
{
    static const QStringList withValue = {QStringLiteral("--formato"), QStringLiteral("--modo"), QStringLiteral("--separatore")};
    static const QStringList flags = {QStringLiteral("--r12"), QStringLiteral("--verifica"), QStringLiteral("--sovrascrivi"),
                                      QStringLiteral("--sottocartelle"), QStringLiteral("--solo-modello")};
    for (qsizetype i = 0; i < in.size(); ++i) {
        const QString a = in.at(i);
        if (withValue.contains(a)) {
            if (i + 1 >= in.size()) {
                *error = QObject::tr("manca il valore di %1").arg(a);
                return false;
            }
            out->values.insert(a, in.at(++i));
        } else if (flags.contains(a)) {
            out->flags << a;
        } else if (a.startsWith(QLatin1String("--"))) {
            *error = QObject::tr("opzione sconosciuta %1").arg(a);
            return false;
        } else {
            out->positional << a;
        }
    }
    return true;
}

// Una conversione: riga di esito; aggiorna i contatori. 1 = convertito, 0 = saltato, -1 = errore.
int convertOne(const QString& source, const QString& destination, int format, const Args& args, const QString& label,
               int* failed, int* withDifferences)
{
    if (QFileInfo(destination).absoluteFilePath().compare(QFileInfo(source).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        say(QObject::tr("SALTATO  %1: il risultato coinciderebbe con l'originale.").arg(label));
        return 0;
    }
    if (QFileInfo::exists(destination) && !args.flags.contains(QStringLiteral("--sovrascrivi"))) {
        say(QObject::tr("SALTATO  %1: %2 esiste già (--sovrascrivi per sostituirlo).").arg(label, native(destination)));
        return 0;
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    const bool verify = args.flags.contains(QStringLiteral("--verifica"));
    const ConvertFileResult r = convertDrawingFile(source, destination, format, verify, appOptions().draft, false);
    if (!r.ok()) {
        ++*failed;
        say(QObject::tr("ERRORE   %1: %2 non riuscita (%3).")
                .arg(label, r.stage == ConvertFileResult::Read ? QObject::tr("lettura") : QObject::tr("scrittura"),
                     r.error.simplified().left(300)),
            stderr);
        return -1;
    }
    QString line = QObject::tr("OK       %1 -> %2 (%3 oggetti)").arg(label, native(destination)).arg(r.objects);
    if (verify && format != ConvertFilePdf) {
        if (r.verified) {
            line += QObject::tr("  verificato");
        } else {
            ++*withDifferences;
            line += r.differences.isEmpty() ? QObject::tr("  VERIFICA: il file scritto non si rilegge (%1)").arg(r.verifyProblem.simplified())
                                            : QObject::tr("  VERIFICA: %1 differenze").arg(r.verifyProblem);
            for (const QString& d : r.differences)
                line += QStringLiteral("\n           ") + d;
        }
    }
    say(line);
    return 1;
}

int runConvert(const Args& args)
{
    if (args.positional.size() != 2) {
        say(QObject::tr("--converti vuole il disegno (o la cartella) e l'uscita.\n\n") + usage(), stderr);
        return 2;
    }
    const QString source = args.positional.at(0);
    const QString output = args.positional.at(1);
    int failed = 0, withDifferences = 0, converted = 0;

    if (QFileInfo(source).isDir()) {
        const QString name = args.values.value(QStringLiteral("--formato")).toLower();
        const int format = name == QLatin1String("dxf")     ? ConvertFileDxf2000
                           : name == QLatin1String("dxf12") ? ConvertFileDxfR12
                           : name == QLatin1String("dwg")   ? ConvertFileDwg
                           : name == QLatin1String("pdf")   ? ConvertFilePdf
                                                            : -1;
        if (format < 0) {
            say(QObject::tr("Con una cartella serve --formato dxf, dxf12, dwg o pdf."), stderr);
            return 2;
        }
        const QString extension = format == ConvertFilePdf ? QStringLiteral("pdf")
                                  : format == ConvertFileDwg ? QStringLiteral("dwg")
                                                             : QStringLiteral("dxf");
        const QDir from(source);
        const QString outputAbsolute = QFileInfo(output).absoluteFilePath();
        QStringList files;
        QDirIterator it(source, {QStringLiteral("*.dwg"), QStringLiteral("*.dxf")}, QDir::Files,
                        args.flags.contains(QStringLiteral("--sottocartelle")) ? QDirIterator::Subdirectories
                                                                              : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const QString f = it.next();
            if (!QFileInfo(f).absoluteFilePath().startsWith(outputAbsolute + QLatin1Char('/'), Qt::CaseInsensitive))
                files << f;
        }
        files.sort(Qt::CaseInsensitive);
        say(QObject::tr("%1 disegni da convertire.").arg(files.size()));
        for (const QString& f : std::as_const(files)) {
            const QString relative = from.relativeFilePath(f);
            const QString destination = QDir::cleanPath(QDir(output).filePath(QFileInfo(relative).path() + QLatin1Char('/')
                                                              + QFileInfo(relative).completeBaseName() + QLatin1Char('.') + extension));
            if (convertOne(f, destination, format, args, relative, &failed, &withDifferences) > 0)
                ++converted;
        }
        say(QObject::tr("%1 convertiti, %2 errori, %3 con differenze alla verifica.").arg(converted).arg(failed).arg(withDifferences));
    } else {
        if (!QFileInfo::exists(source)) {
            say(QObject::tr("File non trovato: %1").arg(native(source)), stderr);
            return 1;
        }
        int format = convertFormatFromPath(output);
        if (format < 0) {
            say(QObject::tr("L'uscita deve finire con .dxf, .dwg o .pdf: %1").arg(native(output)), stderr);
            return 2;
        }
        if (format == ConvertFileDxf2000 && args.flags.contains(QStringLiteral("--r12")))
            format = ConvertFileDxfR12;
        if (convertOne(source, output, format, args, QFileInfo(source).fileName(), &failed, &withDifferences) == 0)
            return 1;   // un solo file: non convertito
    }
    return failed > 0 ? 1 : withDifferences > 0 ? 3 : 0;
}

int runCsv(const Args& args)
{
    if (args.positional.size() != 2) {
        say(QObject::tr("--csv vuole il disegno e il file CSV.\n\n") + usage(), stderr);
        return 2;
    }
    const QString source = args.positional.at(0);
    const QString output = args.positional.at(1);
    DataExportOptions options;
    if (args.values.contains(QStringLiteral("--modo"))
        && !parseDataExportMode(args.values.value(QStringLiteral("--modo")).toStdString(), &options.mode)) {
        say(QObject::tr("--modo vale blocchi, conteggio o oggetti."), stderr);
        return 2;
    }
    if (args.values.contains(QStringLiteral("--separatore"))) {
        const QString sep = args.values.value(QStringLiteral("--separatore"));
        if (sep != QLatin1String(",") && sep != QLatin1String(";")) {
            say(QObject::tr("--separatore vale \";\" oppure \",\"."), stderr);
            return 2;
        }
        options.separator = sep.at(0).toLatin1();
    }
    options.layouts = !args.flags.contains(QStringLiteral("--solo-modello"));
    if (QFileInfo(output).suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) != 0) {
        say(QObject::tr("L'uscita deve finire con .csv: %1").arg(native(output)), stderr);
        return 2;
    }
    if (QFileInfo::exists(output) && !args.flags.contains(QStringLiteral("--sovrascrivi"))) {
        say(QObject::tr("%1 esiste già (--sovrascrivi per sostituirlo).").arg(native(output)), stderr);
        return 1;
    }
    Document doc;
    QString error;
    if (!DrawingWidget::readFile(source, doc, &error)) {
        say(QObject::tr("ERRORE   %1: lettura non riuscita (%2).").arg(native(source), error.simplified().left(300)), stderr);
        return 1;
    }
    const DataExportResult r = exportData(doc, options);
    QSaveFile file(output);
    // Con il BOM UTF-8 Excel riconosce le lettere accentate.
    if (!file.open(QIODevice::WriteOnly) || file.write("\xEF\xBB\xBF") < 0
        || file.write(r.csv.data(), static_cast<qint64>(r.csv.size())) < 0 || !file.commit()) {
        say(QObject::tr("Impossibile scrivere %1.").arg(native(output)), stderr);
        return 1;
    }
    say(QObject::tr("OK       %1 -> %2 (%3 righe, %4)").arg(QFileInfo(source).fileName(), native(output)).arg(r.rows)
            .arg(QString::fromLatin1(dataExportModeName(options.mode))));
    return 0;
}

} // namespace

bool isCommandLineTool(const QStringList& arguments)
{
    return arguments.size() > 1 && kTasks.contains(arguments.at(1));
}

int runCommandLineTool(const QStringList& arguments)
{
    attachConsole();
    const QString task = arguments.value(1);
    if (task == QLatin1String("--versione") || task == QLatin1String("--version")) {
        say(QStringLiteral("CartesioCAD ") + QCoreApplication::applicationVersion());
        return 0;
    }
    if (task == QLatin1String("--aiuto") || task == QLatin1String("--help")) {
        say(usage());
        return 0;
    }
    Args args;
    QString error;
    if (!parse(arguments.mid(2), &args, &error)) {
        say(error + QStringLiteral("\n\n") + usage(), stderr);
        return 2;
    }
    return task == QLatin1String("--csv") ? runCsv(args) : runConvert(args);
}

} // namespace cartesio
