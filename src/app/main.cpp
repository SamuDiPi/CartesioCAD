#include "app/MainWindow.h"
#include "app/Theme.h"
#include "app/Updater.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    // Qt Quick raggruppa i sottoalberi grandi in "batch root" e, unendo la geometria, dal secondo fotogramma
    // perdeva gruppi (testi e linee che sparivano zoomando). Ora le geometrie del disegno non si uniscono
    // (RequiresFullMatrix in GpuView.cpp) e i batch root restano: zoom circa 5 volte più veloce sui disegni grandi.
    // CARTESIO_SAFEBATCH=1 rimette le soglie altissime di prima (niente batch root, più lento).
    if (qEnvironmentVariableIsSet("CARTESIO_SAFEBATCH")) {
        if (!qEnvironmentVariableIsSet("QSG_RENDERER_BATCH_NODE_THRESHOLD"))
            qputenv("QSG_RENDERER_BATCH_NODE_THRESHOLD", "10000000");
        if (!qEnvironmentVariableIsSet("QSG_RENDERER_BATCH_VERTEX_THRESHOLD"))
            qputenv("QSG_RENDERER_BATCH_VERTEX_THRESHOLD", "100000000");
    }
    // Lavori senza finestra (aggiornamento con i diritti di amministratore, disinstallazione): argomenti con "--".
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == '-') {
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("CartesioCAD");
        QCoreApplication::setOrganizationName("CartesioCAD");
        QCoreApplication::setApplicationVersion(CARTESIO_VERSION);
        const int code = cartesio::Updater::commandLineTask(QCoreApplication::arguments());
        if (code >= 0)
            return code;
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName("CartesioCAD");
    QApplication::setOrganizationName("CartesioCAD");
    QApplication::setApplicationVersion(CARTESIO_VERSION);
    cartesio::theme::apply(app);

    cartesio::MainWindow window;
    window.showMaximized();

    // Apre il file passato da riga di comando dopo che la finestra ha le sue dimensioni,
    // così lo zoom estensioni è corretto.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        const QString path = args.at(1);
        QTimer::singleShot(0, &window, [&window, path] { window.openFile(path); });
    } else {
        // Avvio normale: disegni da recuperare se l'ultima volta il programma non si è chiuso bene.
        QTimer::singleShot(800, &window, [&window] { window.checkAutosaveRecovery(); });
    }

    return app.exec();
}
