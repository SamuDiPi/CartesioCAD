// cartesio-cli: CartesioCAD dal terminale di Windows (cmd, PowerShell, file .bat).
//
// CartesioCAD.exe è un programma a finestre: il terminale non aspetta che finisca e non ne mostra i messaggi. Questo
// programmino da console avvia CartesioCAD.exe della propria cartella con gli stessi argomenti (solo i lavori senza
// finestra: --converti, --csv, --versione, --aiuto), gli passa la console, aspetta la fine e restituisce il suo
// codice di uscita. Esempio:
//   cartesio-cli --converti pianta.dwg pianta.pdf
//   cartesio-cli --csv pianta.dwg prese.csv --modo conteggio

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QStringList args = QCoreApplication::arguments().mid(1);
    static const QStringList allowed = {QStringLiteral("--converti"), QStringLiteral("--csv"), QStringLiteral("--aiuto"),
                                        QStringLiteral("--help"), QStringLiteral("--versione"), QStringLiteral("--version")};
    if (args.isEmpty())
        args << QStringLiteral("--aiuto");
    if (!allowed.contains(args.first())) {
        std::fprintf(stderr, "cartesio-cli: usare --converti, --csv, --versione o --aiuto (cartesio-cli --aiuto).\n");
        return 2;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.start(QCoreApplication::applicationDirPath() + QStringLiteral("/CartesioCAD.exe"), args);
    if (!process.waitForStarted(30000)) {
        std::fprintf(stderr, "cartesio-cli: impossibile avviare CartesioCAD.exe dalla cartella di cartesio-cli.\n");
        return 1;
    }
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}
