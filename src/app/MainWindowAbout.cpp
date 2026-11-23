// Informazioni su CartesioCAD: versione e commit, licenza (GPL v3) e assenza di garanzia, componenti di terze
// parti con le loro licenze e i sorgenti. Sono le "note legali appropriate" che la GPL v3 chiede a un programma
// interattivo (sezione 5d).
#include "app/MainWindow.h"

#include "app/FormDialog.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtGlobal>

#ifdef CARTESIO_HAS_POPPLER
#include <poppler/cpp/poppler-version.h>
#endif

#ifndef CARTESIO_COMMIT
#define CARTESIO_COMMIT ""
#endif

namespace cartesio {

namespace {

const QString kRepository = QStringLiteral("https://github.com/SamuDiPi/CartesioCAD");

// Commit e data della versione compilata ("abc1234|2026-11-19"), vuoto fuori da un repository git.
QStringList commitInfo()
{
    const QString info = QString::fromLatin1(CARTESIO_COMMIT);
    return info.isEmpty() ? QStringList{} : info.split(u'|');
}

QString canaloVersion()
{
    QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/CanaloCAD/VERSIONE.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

void openLocalOrWeb(const QString& fileName, const QString& webPath)
{
    const QString local = QCoreApplication::applicationDirPath() + u'/' + fileName;
    if (QFileInfo::exists(local))
        QDesktopServices::openUrl(QUrl::fromLocalFile(local));
    else
        QDesktopServices::openUrl(QUrl(kRepository + webPath));
}

} // namespace

QString MainWindow::aboutText(bool html)
{
    const QStringList commit = commitInfo();
    QString version = tr("Versione %1").arg(QCoreApplication::applicationVersion());
    if (commit.size() >= 2)
        version += tr(" (commit %1 del %2)").arg(commit[0], commit[1]);
    const QString source = commit.isEmpty() ? kRepository : kRepository + QStringLiteral("/tree/") + commit[0];

    QStringList components;
    components << tr("Qt %1 (LGPL-3.0)").arg(QString::fromLatin1(qVersion()));
#ifdef CARTESIO_HAS_POPPLER
    components << tr("Poppler %1 per i PDF degli schemi (GPL-2.0-or-later)").arg(QString::fromStdString(poppler::version_string()));
#endif
    if (QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/libredwg/dwg2dxf.exe")))
        components << tr("LibreDWG 0.14.8413 per i file DWG (GPL-3.0-or-later)");
    const QString canalo = canaloVersion();
    if (!canalo.isEmpty())
        components << tr("%1, plugin (GPL-3.0-or-later)").arg(canalo.section(QStringLiteral(" - "), 0, 0));
    components << tr("Caratteri Hershey per il font a tratti incorporato (uso libero con attribuzione)");
    components << tr("e le librerie elencate in LICENZE-TERZE-PARTI.txt (OpenSSL, FreeType, HarfBuzz, ICU, ...)");

    if (!html) {
        return QStringLiteral("CartesioCAD - %1\nCopyright (C) 2026 SamuDiPi (%2)\n%3\n%4\n%5")
            .arg(version, kRepository,
                 tr("Software libero: si può ridistribuire e modificare secondo la GNU General Public License v3.0."),
                 tr("Distribuito SENZA ALCUNA GARANZIA, nemmeno implicita di commerciabilità o di idoneità a un uso "
                    "particolare. I calcoli (per esempio quelli di CanaloCAD) vanno verificati dal progettista."),
                 tr("Componenti: %1. Sorgenti: %2").arg(components.join(QStringLiteral("; ")), source));
    }
    QString list;
    for (const QString& c : components)
        list += QStringLiteral("<li>%1</li>").arg(c.toHtmlEscaped());
    return tr("<p><b>CartesioCAD</b> - %1<br>Copyright &copy; 2026 SamuDiPi</p>"
              "<p>CartesioCAD è software libero: si può ridistribuire e modificare secondo i termini della "
              "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General Public License v3.0</a>.</p>"
              "<p>È distribuito <b>senza alcuna garanzia</b>, nemmeno implicita di commerciabilità o di idoneità a un uso "
              "particolare. I risultati dei calcoli (per esempio quelli di CanaloCAD) sono indicativi e vanno verificati "
              "dal progettista.</p>"
              "<p>CartesioCAD è un progetto indipendente: non è collegato, approvato né sostenuto da altri produttori "
              "di software CAD. I nomi dei formati di file (DWG, DXF, PDF...) indicano solo la compatibilità; i marchi "
              "citati appartengono ai rispettivi titolari.</p>"
              "<p>Sorgenti di questa versione: <a href=\"%2\">%2</a></p>"
              "<p>Componenti di terze parti, ciascuno con la sua licenza:</p><ul>%3</ul>"
              "<p>I testi delle licenze e gli indirizzi dei sorgenti sono nei file LICENSE.txt e LICENZE-TERZE-PARTI.txt "
              "e nella cartella \"licenze\", accanto a CartesioCAD.exe.</p>")
        .arg(version.toHtmlEscaped(), source, list);
}

void MainWindow::showAbout()
{
    FormDialog dialog(tr("Informazioni su CartesioCAD"), FormDialog::Close, this);
    dialog.note(aboutText(true));
    dialog.buttonsRow({
        {tr("Licenza (GPL v3)"), [] { openLocalOrWeb(QStringLiteral("LICENSE.txt"), QStringLiteral("/blob/main/LICENSE")); }},
        {tr("Licenze di terze parti"),
         [] { openLocalOrWeb(QStringLiteral("LICENZE-TERZE-PARTI.txt"), QStringLiteral("/blob/main/scripts/collect-licenses.sh")); }},
        {tr("Sorgenti"), [] { QDesktopServices::openUrl(QUrl(kRepository)); }},
    });
    dialog.setSize(560, 520);
    dialog.exec();
}

} // namespace cartesio
