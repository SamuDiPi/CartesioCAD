#pragma once

// Nome del canale locale fra CartesioCAD e cartesio-mcp: uno per utente, così due persone collegate allo stesso
// computer non si vedono i disegni. Usato da entrambi i programmi (solo QtCore).
//  - Windows: named pipe con il nome dell'utente in forma di impronta.
//  - Linux: file del canale nella cartella di sessione dell'utente ($XDG_RUNTIME_DIR, accessibile solo a lui); nel
//    pacchetto Flatpak nella sottocartella dell'applicazione, condivisa fra CartesioCAD e il connettore avviato con
//    "flatpak run --command=cartesio-mcp".

#include <QCryptographicHash>
#include <QDir>
#include <QString>
#include <QtGlobal>

namespace cartesio {

inline QString claudeChannelName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    const QByteArray hash = QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const QString name = QStringLiteral("CartesioCAD-claude-") + QString::fromLatin1(hash);
#ifdef Q_OS_WIN
    return name;
#else
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty() || !QDir(dir).exists())
        return name;   // senza cartella di sessione: cartella temporanea di Qt (il file resta riservato all'utente)
    const QString app = qEnvironmentVariable("FLATPAK_ID");
    if (!app.isEmpty() && QDir(dir + QStringLiteral("/app/") + app).exists())
        dir += QStringLiteral("/app/") + app;
    return dir + QLatin1Char('/') + name;
#endif
}

} // namespace cartesio
