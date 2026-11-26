// Segnalazione di un problema: una finestra semplice (tipo, due righe di descrizione, anteprima di quello che si
// aggiunge) che apre nel browser la pagina "nuova segnalazione" di GitHub già compilata. L'utente la rilegge, allega
// i file che vuole e la invia con il suo account. CartesioCAD non invia niente da sé e non ha un server: apre solo il
// browser. Le segnalazioni su GitHub sono pubbliche: percorsi e nomi dei file si tolgono, e la finestra lo ricorda.
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/CommandLine.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/MainView.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QScreen>
#include <QSysInfo>
#include <QUrl>

namespace cartesio {

namespace {

const QString kNewIssue = QStringLiteral("https://github.com/SamuDiPi/CartesioCAD/issues/new");
const QString kPrivateReport = QStringLiteral("https://github.com/SamuDiPi/CartesioCAD/security/advisories/new");
// Oltre circa 8000 caratteri GitHub rifiuta l'indirizzo: sopra questa soglia il testo va negli appunti.
constexpr int kMaxUrlLength = 7500;
constexpr int kHistoryLines = 25;

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("software");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("nessuno");
    default: return QStringLiteral("sconosciuto");
    }
}

QString enc(const QString& s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

} // namespace

// Percorsi e nomi dei file non finiscono in una segnalazione pubblica: un file diventa "<file>.dwg" (resta il tipo),
// una cartella "<cartella>". Vale per i percorsi di Windows (C:\..., \\server\...) e per la cartella dell'utente.
QString MainWindow::anonymizeForReport(QString text)
{
    static const QRegularExpression file(
        QStringLiteral(R"((?:[A-Za-z]:|\\\\[^\\/\s"]+)[\\/][^\r\n"<>|*?]*?\.([A-Za-z0-9]{1,5})(?=["'\s),;:]|$))"));
    static const QRegularExpression folder(QStringLiteral(R"((?:[A-Za-z]:|\\\\[^\\/\s"]+)[\\/][^\r\n"<>|]*)"));
    text.replace(file, QStringLiteral("<file>.\\1"));
    text.replace(folder, QStringLiteral("<cartella>"));
    for (const QString& home : {QDir::homePath(), QDir::toNativeSeparators(QDir::homePath())}) {
        if (home.length() > 3) {
            static const QRegularExpression after(QStringLiteral(R"(^[\\/][^\s"]*)"));
            for (qsizetype i = text.indexOf(home, 0, Qt::CaseInsensitive); i >= 0; i = text.indexOf(home, i, Qt::CaseInsensitive)) {
                const auto m = after.match(text.mid(i + home.length()));
                const qsizetype len = home.length() + (m.hasMatch() ? m.capturedLength() : 0);
                const QString rest = text.mid(i, len);
                const qsizetype dot = rest.lastIndexOf(u'.');
                const QString replacement = dot > rest.lastIndexOf(u'/') && dot > rest.lastIndexOf(u'\\') && rest.length() - dot <= 6
                                                ? QStringLiteral("<file>") + rest.mid(dot)
                                                : QStringLiteral("<cartella>");
                text.replace(i, len, replacement);
                i += replacement.length();
            }
        }
    }
    return text;
}

QString MainWindow::reportTechnical(bool withHistory) const
{
    QStringList lines;
    lines << aboutText(false).section(u'\n', 0, 0).remove(QStringLiteral("CartesioCAD - ")).remove(QRegularExpression(QStringLiteral("^\\S+\\s")));
    QString package = QStringLiteral("cartella copiata");
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID"))
        package = QStringLiteral("Flatpak");
    else if (QCoreApplication::applicationDirPath().contains(QStringLiteral("Program Files"), Qt::CaseInsensitive))
        package = QStringLiteral("installato in Programmi");
    else if (QCoreApplication::applicationDirPath().contains(QStringLiteral("/build"), Qt::CaseInsensitive))
        package = QStringLiteral("compilato dai sorgenti");
    QStringList out;
    out << QStringLiteral("- Versione: %1 (%2)").arg(lines.first(), package);
    out << QStringLiteral("- Sistema: %1, %2, Qt %3")
               .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), QString::fromLatin1(qVersion()));
    QString graphics = QStringLiteral("finestre classiche");
    if (mainView_ && mainView_->quickWindow() && mainView_->quickWindow()->rendererInterface())
        graphics = graphicsApiName(mainView_->quickWindow()->rendererInterface()->graphicsApi());
    if (!qEnvironmentVariableIsEmpty("CARTESIO_RENDER"))
        graphics += QStringLiteral(" (CARTESIO_RENDER=%1)").arg(qEnvironmentVariable("CARTESIO_RENDER"));
    if (const QScreen* screen = QGuiApplication::primaryScreen())
        graphics += QStringLiteral(", schermo %1 × %2 al %3%")
                        .arg(screen->size().width())
                        .arg(screen->size().height())
                        .arg(qRound(screen->devicePixelRatio() * 100));
    out << QStringLiteral("- Grafica: %1").arg(graphics);
    out << QStringLiteral("- Lingua: interfaccia %1, sistema %2")
               .arg(appOptions().language == 2 ? QStringLiteral("inglese") : appOptions().language == 1 ? QStringLiteral("italiano")
                                                                                                         : QStringLiteral("automatica"),
                    QLocale::system().name());
    if (const DrawingWidget* d = const_cast<MainWindow*>(this)->current()) {
        const QString type = d->filePath().isEmpty() ? QStringLiteral("non ancora salvato") : QFileInfo(d->filePath()).suffix().toUpper();
        out << QStringLiteral("- Disegno attivo: %1, %2 oggetti").arg(type).arg(d->document().entities().size());
    }
    if (previousCrash_)
        out << QStringLiteral("- La sessione precedente si è chiusa in modo imprevisto");
    QString text = QStringLiteral("### Informazioni tecniche\n") + out.join(u'\n');
    if (withHistory && commandLine_) {
        const QStringList history = commandLine_->history();
        const QStringList last = history.mid(std::max<qsizetype>(0, history.size() - kHistoryLines));
        if (!last.isEmpty())
            text += QStringLiteral("\n\n### Ultime righe della riga di comando\n```\n%1\n```").arg(anonymizeForReport(last.join(u'\n')));
    }
    return text;
}

QString MainWindow::reportBody(const QString& what, const QString& steps, bool technical, bool withHistory) const
{
    QStringList parts;
    parts << QStringLiteral("### Che cosa è successo\n%1").arg(what.trimmed().isEmpty() ? QStringLiteral("_(da descrivere)_") : what.trimmed());
    if (!steps.trimmed().isEmpty())
        parts << QStringLiteral("### Come si ripete\n%1").arg(steps.trimmed());
    if (technical)
        parts << reportTechnical(withHistory);
    return parts.join(QStringLiteral("\n\n"));
}

// Indirizzo della pagina "nuova segnalazione" già compilata. Se è troppo lungo, `overflow` riceve il testo completo
// (va negli appunti) e nella pagina resta solo la parte scritta dall'utente.
QUrl MainWindow::reportUrl(const QString& kind, const QString& title, const QString& what, const QString& steps, bool technical,
                           bool withHistory, QString* overflow) const
{
    const QString tmpl = kind == QLatin1String("enhancement") ? QStringLiteral("suggerimento.md")
                         : kind == QLatin1String("question")  ? QStringLiteral("domanda.md")
                                                              : QStringLiteral("errore.md");
    const QString cleanTitle = title.trimmed().isEmpty() ? what.trimmed().section(u'\n', 0, 0).left(80) : title.trimmed().left(120);
    auto build = [&](const QString& body) {
        return kNewIssue + QStringLiteral("?template=") + enc(tmpl) + QStringLiteral("&title=") +
               enc(cleanTitle.isEmpty() ? QStringLiteral("Segnalazione") : cleanTitle) + QStringLiteral("&body=") + enc(body);
    };
    const QString full = reportBody(what, steps, technical, withHistory);
    QString url = build(full);
    if (url.length() > kMaxUrlLength) {
        if (overflow)
            *overflow = full;
        const QString note = QStringLiteral("\n\n_Le informazioni tecniche sono negli appunti: incollale qui con Ctrl+V._");
        url = build(reportBody(what, steps, false, false) + note);
        if (url.length() > kMaxUrlLength)
            url = build(QStringLiteral("_Il testo della segnalazione è negli appunti: incollalo qui con Ctrl+V._"));
    }
    return QUrl::fromEncoded(url.toLatin1());
}

void MainWindow::showReportDialog()
{
    QVariant kind = QStringLiteral("bug");
    QString title, what, steps;
    bool technical = true, history = true;
    FormDialog dialog(tr("Segnala un problema"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.setButtonText(FormDialog::Ok, tr("Apri su GitHub"));
    dialog.note(tr("Scrivi in due righe cosa è successo: si apre la pagina di GitHub già compilata, la rileggi e la invii "
                   "con il pulsante verde. Serve un account GitHub (gratuito)."));
    dialog.choice(tr("Che cosa vuoi segnalare"),
                  {{tr("Qualcosa non funziona"), QStringLiteral("bug")},
                   {tr("Un suggerimento"), QStringLiteral("enhancement")},
                   {tr("Una domanda"), QStringLiteral("question")}},
                  kind, [&](const QVariant& v) { kind = v; });
    dialog.text(tr("In breve"), title, tr("es. aprendo il DWG i testi non si vedono"));
    dialog.textArea(tr("Cosa è successo? Cosa ti aspettavi?"), QString(), [&](const QString& t) { what = t; }, 5);
    dialog.textArea(tr("Come si ripete, se lo sai"), QString(), [&](const QString& t) { steps = t; }, 3,
                    tr("1. apro il disegno...  2. uso il comando...  3. ..."));
    int preview = -1;
    auto refresh = [&] {
        if (preview >= 0)
            dialog.setFieldValue(preview, technical ? reportTechnical(history) : tr("(niente)"));
    };
    const int techField = dialog.check(tr("Aggiungi le informazioni tecniche (versione, sistema, grafica)"), technical, [&](bool on) {
        technical = on;
        refresh();
    });
    const int historyField = dialog.check(tr("Aggiungi le ultime righe della riga di comando (percorsi e nomi dei file tolti)"), history,
                                          [&](bool on) {
                                              history = on;
                                              refresh();
                                          },
                                          1);
    dialog.enableWhen(historyField, techField);
    preview = dialog.textArea(tr("Cosa viene aggiunto (si può rileggere e modificare anche su GitHub prima di inviare)"),
                              reportTechnical(history), {}, 7);
    dialog.note(tr("La segnalazione è <b>pubblica</b>: non scrivere nomi di clienti, indirizzi o altri dati personali. Disegni, PDF e "
                   "schermate si allegano nella pagina di GitHub trascinandoli, solo se si possono condividere.<br>"
                   "Un problema di sicurezza? Usa la <a href=\"%1\">segnalazione privata</a>.")
                    .arg(kPrivateReport));
    dialog.buttonsRow({{tr("Copia il testo"), [&] {
                            QGuiApplication::clipboard()->setText(reportBody(what, steps, technical, history));
                            print(tr("Testo della segnalazione copiato negli appunti."));
                        }}});
    dialog.setSize(640, 700);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString overflow;
    const QUrl url = reportUrl(kind.toString(), title, what, steps, technical, history, &overflow);
    if (!overflow.isEmpty()) {
        QGuiApplication::clipboard()->setText(overflow);
        print(tr("Segnalazione: il testo completo è negli appunti, incollalo nella pagina di GitHub con Ctrl+V."));
    }
    if (QDesktopServices::openUrl(url))
        print(tr("Segnalazione: si apre la pagina di GitHub già compilata. Rileggila, allega i file se servono e premi il pulsante verde per inviarla."));
    else
        print(tr("Segnalazione: il browser non si è aperto. Indirizzo: %1").arg(kNewIssue));
}

// Prove senza finestra: scrive in un file il testo della segnalazione e l'indirizzo che si aprirebbe.
bool MainWindow::writeReportForTest(const QString& path, const QString& what)
{
    QString overflow;
    const QUrl url = reportUrl(QStringLiteral("bug"), QString(), what, QStringLiteral("1. prova"), true, true, &overflow);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(reportBody(what, QStringLiteral("1. prova"), true, true).toUtf8());
    f.write("\n\n--- URL ---\n");
    f.write(url.toEncoded());
    f.write("\n--- TROPPO LUNGO ---\n");
    f.write(overflow.isEmpty() ? "no" : "si");
    return true;
}

} // namespace cartesio
