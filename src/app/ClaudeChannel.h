#pragma once

// Nome del canale locale fra CartesioCAD e cartesio-mcp.exe: uno per utente di Windows, così due persone collegate
// allo stesso PC non si vedono i disegni. Usato da entrambi i programmi (solo QtCore).

#include <QCryptographicHash>
#include <QString>
#include <QtGlobal>

namespace cartesio {

inline QString claudeChannelName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    const QByteArray hash = QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("CartesioCAD-claude-") + QString::fromLatin1(hash);
}

} // namespace cartesio
