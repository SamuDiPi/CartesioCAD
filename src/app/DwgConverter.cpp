#include "app/DwgConverter.h"

#include "app/AppOptions.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace cartesio {

namespace {

constexpr int kTimeoutMs = 180000;
constexpr int kInteractiveTimeoutMs = 20 * 60 * 1000;   // con la finestra di avanzamento si può annullare prima

// Attesa del convertitore. Nel thread dell'interfaccia (salvataggio) una finestra di avanzamento con Annulla tiene
// viva la finestra: LibreDWG può impiegare minuti sui disegni grandi. Negli altri thread (apertura) si aspetta.
bool waitForConverter(QProcess& process, const QString& what, QString* failure)
{
    const bool gui = qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr
                     && QThread::currentThread() == QCoreApplication::instance()->thread();
    if (!gui) {
        if (process.waitForFinished(kTimeoutMs))
            return true;
        process.kill();
        process.waitForFinished(3000);
        *failure = QObject::tr("La conversione ha impiegato troppo tempo ed è stata interrotta.");
        return false;
    }
    QProgressDialog progress(QObject::tr("%1...\nCon LibreDWG i disegni grandi richiedono anche alcuni minuti.").arg(what),
                             QObject::tr("Annulla"), 0, 0, QApplication::activeWindow());
    progress.setWindowTitle(QStringLiteral("CartesioCAD"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(1000);
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const int seconds = static_cast<int>(timer.elapsed() / 1000);
        if (seconds >= 1)
            progress.setLabelText(QObject::tr("%1... %2:%3\nCon LibreDWG i disegni grandi richiedono anche alcuni minuti.")
                                      .arg(what)
                                      .arg(seconds / 60)
                                      .arg(seconds % 60, 2, 10, QLatin1Char('0')));
        if (progress.wasCanceled() || timer.elapsed() > kInteractiveTimeoutMs) {
            const bool cancelled = progress.wasCanceled();
            process.kill();
            process.waitForFinished(3000);
            *failure = cancelled ? QObject::tr("Conversione annullata.")
                                 : QObject::tr("La conversione ha impiegato troppo tempo ed è stata interrotta.");
            return false;
        }
    }
    return true;
}

#ifdef Q_OS_WIN
const QString kExe = QStringLiteral(".exe");
#else
const QString kExe;
#endif

QString tool(const QString& name)
{
    return QDir(DwgConverter::toolsDirectory()).filePath(name + kExe);
}

// Esegue un convertitore. I file vengono passati con nomi semplici in una cartella temporanea,
// perché i programmi di LibreDWG non gestiscono bene i percorsi con caratteri non ASCII.
bool run(const QString& program, const QString& input, const QString& output, const QStringList& extraArgs,
         QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (!QFileInfo::exists(program))
        return fail(QObject::tr("Convertitore non trovato: %1").arg(QDir::toNativeSeparators(program)));

    QTemporaryDir temp;
    if (!temp.isValid())
        return fail(QObject::tr("Impossibile creare una cartella temporanea."));
    const QString in = temp.filePath(QStringLiteral("input.") + QFileInfo(input).suffix().toLower());
    const QString out = temp.filePath(QStringLiteral("output.") + QFileInfo(output).suffix().toLower());
    if (!QFile::copy(input, in))
        return fail(QObject::tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(input)));

    QProcess process;
    process.setWorkingDirectory(temp.path());
    process.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = extraArgs;
    args << QStringLiteral("-y") << QStringLiteral("-o") << QFileInfo(out).fileName() << QFileInfo(in).fileName();
    process.start(program, args);
    if (!process.waitForStarted(10000))
        return fail(QObject::tr("Impossibile avviare %1.").arg(QFileInfo(program).fileName()));
    QString waitError;
    if (!waitForConverter(process, QObject::tr("Conversione con LibreDWG"), &waitError))
        return fail(waitError);

    const QString log = QString::fromLocal8Bit(process.readAll()).trimmed();
    const bool produced = QFileInfo(out).exists() && QFileInfo(out).size() > 0;
    if (process.exitStatus() != QProcess::NormalExit || !produced) {
        QString message = process.exitStatus() != QProcess::NormalExit
                              ? QObject::tr("Il convertitore LibreDWG si è interrotto in modo anomalo.")
                              : QObject::tr("Il convertitore LibreDWG non ha prodotto il file.");
        if (!log.isEmpty())
            message += QStringLiteral("\n\n") + log.right(1500);
        return fail(message);
    }

    QFile::remove(output);
    if (!QFile::copy(out, output))
        return fail(QObject::tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(output)));
    return true;
}


// ODA File Converter: ODAFileConverter "cartella input" "cartella output" versione tipo ricorsivo audit filtro.
// Lavora su cartelle: il file va copiato da solo in una cartella temporanea.
bool runOda(const QString& program, const QString& input, const QString& output, const QString& version,
            const QString& type, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    QTemporaryDir temp;
    if (!temp.isValid())
        return fail(QObject::tr("Impossibile creare una cartella temporanea."));
    const QDir root(temp.path());
    root.mkdir(QStringLiteral("in"));
    root.mkdir(QStringLiteral("out"));
    const QString name = QStringLiteral("input.") + QFileInfo(input).suffix().toLower();
    if (!QFile::copy(input, root.filePath(QStringLiteral("in/") + name)))
        return fail(QObject::tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(input)));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    // Il convertitore ha anche un'interfaccia grafica: la finestra va nascosta.
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start(program, {QDir::toNativeSeparators(root.filePath(QStringLiteral("in"))),
                            QDir::toNativeSeparators(root.filePath(QStringLiteral("out"))), version, type,
                            QStringLiteral("0"), QStringLiteral("1"), name});
    if (!process.waitForStarted(10000))
        return fail(QObject::tr("Impossibile avviare ODA File Converter."));
    QString waitError;
    if (!waitForConverter(process, QObject::tr("Conversione con ODA File Converter"), &waitError))
        return fail(waitError);
    const QString produced = root.filePath(QStringLiteral("out/input.") + type.toLower());
    if (!QFileInfo::exists(produced) || QFileInfo(produced).size() == 0) {
        QString message = QObject::tr("ODA File Converter non ha prodotto il file.");
        const QString log = QString::fromLocal8Bit(process.readAll()).trimmed();
        if (!log.isEmpty())
            message += QStringLiteral("\n\n") + log.right(1500);
        return fail(message);
    }
    QFile::remove(output);
    if (!QFile::copy(produced, output))
        return fail(QObject::tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(output)));
    return true;
}

bool useOda()
{
    const int engine = appOptions().dwgEngine;
    if (engine == AppOptions::DwgLibre)
        return false;
    if (engine == AppOptions::DwgOda)
        return true;
    return DwgConverter::odaAvailable();
}

} // namespace

QString DwgConverter::toolsDirectory()
{
    const QString fromEnv = qEnvironmentVariable("CARTESIO_LIBREDWG_DIR");
    if (!fromEnv.isEmpty())
        return fromEnv;
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("libredwg"));
}

bool DwgConverter::libreDwgAvailable()
{
    return QFileInfo::exists(tool(QStringLiteral("dwg2dxf")));
}

QString DwgConverter::odaPath()
{
    const QString fromEnv = qEnvironmentVariable("CARTESIO_ODA_PATH");
    if (!fromEnv.isEmpty())
        return QFileInfo::exists(fromEnv) ? fromEnv : QString();
    const QString configured = appOptions().odaPath;
    if (!configured.isEmpty())
        return QFileInfo::exists(configured) ? configured : QString();
#ifdef Q_OS_WIN
    // Installazione predefinita: "C:\Program Files\ODA\ODAFileConverter <versione>\ODAFileConverter.exe".
    for (const QString& base : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramW6432")}) {
        if (base.isEmpty())
            continue;
        const QDir oda(base + QStringLiteral("/ODA"));
        QStringList folders = oda.entryList({QStringLiteral("ODAFileConverter*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(folders.begin(), folders.end(), [](const QString& a, const QString& b) {
            return QString::localeAwareCompare(a, b) > 0;   // prima la versione più recente
        });
        for (const QString& folder : folders) {
            const QString exe = oda.filePath(folder + QStringLiteral("/ODAFileConverter.exe"));
            if (QFileInfo::exists(exe))
                return exe;
        }
    }
#endif
    return {};
}

bool DwgConverter::available()
{
    return useOda() ? odaAvailable() : libreDwgAvailable();
}

QString DwgConverter::engineName()
{
    return useOda() ? QStringLiteral("ODA File Converter") : QStringLiteral("LibreDWG");
}

bool DwgConverter::dwgToDxf(const QString& dwgPath, const QString& dxfPath, QString* error, QString* used)
{
    if (useOda()) {
        const QString exe = odaPath();
        if (exe.isEmpty()) {
            if (error)
                *error = QObject::tr("ODA File Converter non è installato (scelto nelle Opzioni).");
            return false;
        }
        if (used)
            *used = QStringLiteral("ODA File Converter");
        return runOda(exe, dwgPath, dxfPath, QStringLiteral("ACAD2018"), QStringLiteral("DXF"), error);
    }
    if (used)
        *used = QStringLiteral("LibreDWG");
    return run(tool(QStringLiteral("dwg2dxf")), dwgPath, dxfPath, {}, error);
}

bool DwgConverter::dxfToDwg(const QString& dxfPath, const QString& dwgPath, QString* error, QString* used)
{
    if (useOda()) {
        const QString exe = odaPath();
        if (exe.isEmpty()) {
            if (error)
                *error = QObject::tr("ODA File Converter non è installato (scelto nelle Opzioni).");
            return false;
        }
        if (used)
            *used = QStringLiteral("ODA File Converter");
        return runOda(exe, dxfPath, dwgPath, appOptions().odaDwgVersion, QStringLiteral("DWG"), error);
    }
    if (used)
        *used = QStringLiteral("LibreDWG");
    return run(tool(QStringLiteral("dxf2dwg")), dxfPath, dwgPath, {QStringLiteral("--as"), QStringLiteral("r2000")},
               error);
}

} // namespace cartesio

namespace cartesio {

bool DwgConverter::libreDwgToDxfPair(const QString& dwgPath, const QString& nativeDxf, const QString& r2000Dxf,
                                     QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const QString program = tool(QStringLiteral("dwg2dxf"));
    if (!QFileInfo::exists(program))
        return fail(QObject::tr("Convertitore non trovato: %1").arg(QDir::toNativeSeparators(program)));
    QTemporaryDir temp;
    if (!temp.isValid())
        return fail(QObject::tr("Impossibile creare una cartella temporanea."));
    if (!QFile::copy(dwgPath, temp.filePath(QStringLiteral("input.dwg"))))
        return fail(QObject::tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(dwgPath)));

    // Le due conversioni insieme, in parallelo.
    QProcess native, r2000;
    for (QProcess* p : {&native, &r2000}) {
        p->setWorkingDirectory(temp.path());
        p->setProcessChannelMode(QProcess::MergedChannels);
    }
    native.start(program, {QStringLiteral("-y"), QStringLiteral("-o"), QStringLiteral("native.dxf"), QStringLiteral("input.dwg")});
    r2000.start(program, {QStringLiteral("--as"), QStringLiteral("r2000"), QStringLiteral("-y"), QStringLiteral("-o"),
                          QStringLiteral("r2000.dxf"), QStringLiteral("input.dwg")});
    if (!native.waitForStarted(10000))
        return fail(QObject::tr("Impossibile avviare %1.").arg(QFileInfo(program).fileName()));
    const bool nativeDone = native.waitForFinished(kTimeoutMs);
    const bool r2000Done = r2000.waitForFinished(kTimeoutMs);
    if (!nativeDone)
        native.kill();
    if (!r2000Done)
        r2000.kill();

    const QString nativeOut = temp.filePath(QStringLiteral("native.dxf"));
    if (!nativeDone || native.exitStatus() != QProcess::NormalExit || !QFileInfo(nativeOut).size()) {
        QString message = nativeDone ? QObject::tr("Il convertitore LibreDWG non ha prodotto il file.")
                                     : QObject::tr("La conversione ha impiegato troppo tempo ed è stata interrotta.");
        const QString log = QString::fromLocal8Bit(native.readAll()).trimmed();
        if (!log.isEmpty())
            message += QStringLiteral("\n\n") + log.right(1500);
        return fail(message);
    }
    QFile::remove(nativeDxf);
    if (!QFile::copy(nativeOut, nativeDxf))
        return fail(QObject::tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(nativeDxf)));
    // La seconda è un aiuto: se non riesce si va avanti con la prima.
    const QString r2000Out = temp.filePath(QStringLiteral("r2000.dxf"));
    QFile::remove(r2000Dxf);
    if (r2000Done && r2000.exitStatus() == QProcess::NormalExit && QFileInfo(r2000Out).size() > 0)
        QFile::copy(r2000Out, r2000Dxf);
    return true;
}

} // namespace cartesio
