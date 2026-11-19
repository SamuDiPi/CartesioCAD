#include "app/GuidePanel.h"

#include "app/QmlUi.h"
#include "app/Theme.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlContext>
#include <QRegularExpression>

#include <algorithm>

namespace cartesio {

GuidePanel::GuidePanel(QWidget* parent) : QQuickWidget(parent)
{
    loadTours();
    loadManual();
    advance_.setSingleShot(true);
    advance_.setInterval(1400);
    connect(&advance_, &QTimer::timeout, this, &GuidePanel::next);
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    rootContext()->setContextProperty(QStringLiteral("guide"), this);
    if (!qmlRootUi())   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/GuidePanel.qml")));
    setMinimumWidth(320);
    search(QString());
}

void GuidePanel::setPage(int page)
{
    if (page == page_)
        return;
    page_ = page;
    emit pageChanged();
}

// ---------------------------------------------------------------- percorsi

void GuidePanel::loadTours()
{
    QFile f(QStringLiteral(":/guide/percorsi.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    toursJson_ = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("percorsi")).toArray();
    for (const QJsonValue& v : toursJson_) {
        const QJsonObject t = v.toObject();
        toursList_.append(QVariantMap{{QStringLiteral("title"), t.value(QStringLiteral("titolo")).toString()},
                                      {QStringLiteral("description"), t.value(QStringLiteral("descrizione")).toString()},
                                      {QStringLiteral("minutes"), t.value(QStringLiteral("minuti")).toInt()},
                                      {QStringLiteral("steps"), t.value(QStringLiteral("passi")).toArray().size()}});
    }
}

int GuidePanel::stepCount() const
{
    return tour_ < 0 ? 0 : toursJson_.at(tour_).toObject().value(QStringLiteral("passi")).toArray().size();
}

QString GuidePanel::tourTitle() const
{
    return tour_ < 0 ? QString() : toursJson_.at(tour_).toObject().value(QStringLiteral("titolo")).toString();
}

QJsonObject GuidePanel::currentStep() const
{
    if (tour_ < 0 || finished_)
        return {};
    return toursJson_.at(tour_).toObject().value(QStringLiteral("passi")).toArray().at(step_).toObject();
}

QString GuidePanel::stepText() const
{
    return currentStep().value(QStringLiteral("testo")).toString();
}

bool GuidePanel::stepManual() const
{
    const QJsonObject done = currentStep().value(QStringLiteral("fatto")).toObject();
    return done.value(QStringLiteral("tipo")).toString() == QLatin1String("next") || done.value(QStringLiteral("oppureAvanti")).toBool();
}

void GuidePanel::enterStep(int index)
{
    advance_.stop();
    step_ = std::clamp(index, 0, std::max(0, stepCount() - 1));
    stepDone_ = false;
    finished_ = false;
    emit tourChanged();
    emit stepEntered();
}

void GuidePanel::startTour(int index)
{
    if (index < 0 || index >= toursJson_.size())
        return;
    tour_ = index;
    page_ = 0;
    emit pageChanged();
    enterStep(0);
}

void GuidePanel::markDone()
{
    if (tour_ < 0 || stepDone_ || finished_)
        return;
    stepDone_ = true;
    emit tourChanged();
    advance_.start();   // "Fatto!" per un attimo, poi il passo successivo
}

void GuidePanel::next()
{
    if (tour_ < 0)
        return;
    if (step_ + 1 >= stepCount()) {
        advance_.stop();
        finished_ = true;
        stepDone_ = true;
        emit tourChanged();
        emit stepEntered();   // niente da evidenziare
        return;
    }
    enterStep(step_ + 1);
}

void GuidePanel::back()
{
    if (tour_ >= 0 && step_ > 0)
        enterStep(step_ - 1);
}

void GuidePanel::stopTour()
{
    advance_.stop();
    tour_ = -1;
    finished_ = false;
    stepDone_ = false;
    emit tourChanged();
    emit stepEntered();
}

// ---------------------------------------------------------------- manuale

void GuidePanel::loadManual()
{
    QDir dir(QStringLiteral(":/manuale"));
    QStringList files = dir.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    static const QRegularExpression code(QStringLiteral("`([A-Z\\-][A-Z0-9\\-]*)`"));
    for (const QString& name : files) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n');
        QString chapter;
        Section current;
        auto flush = [&] {
            if (!current.title.isEmpty() || !current.markdown.trimmed().isEmpty()) {
                if (current.title.isEmpty())
                    current.title = chapter;
                current.chapter = chapter;
                current.plain = current.markdown;
                current.plain.remove(u'`').remove(u'*');
                manual_.push_back(current);
            }
            current = Section{};
        };
        for (const QString& line : lines) {
            if (line.startsWith(QLatin1String("# "))) {
                flush();
                chapter = line.mid(2).trimmed();
                continue;
            }
            if (line.startsWith(QLatin1String("## ")) || line.startsWith(QLatin1String("### "))) {
                flush();
                current.title = line.mid(line.indexOf(u' ') + 1).trimmed();
                for (auto it = code.globalMatch(current.title); it.hasNext();)
                    current.commands << it.next().captured(1);
                QString shown = current.title;
                current.title = shown.remove(u'`');
                current.markdown = QStringLiteral("## ") + current.title + u'\n';
                continue;
            }
            current.markdown += line + u'\n';
        }
        flush();
    }
}

void GuidePanel::search(const QString& text)
{
    query_ = text.trimmed();
    shown_.clear();
    shownIndex_.clear();
    const QString q = query_.toUpper();
    struct Hit { int index; int score; };
    std::vector<Hit> hits;
    for (int i = 0; i < static_cast<int>(manual_.size()); ++i) {
        const Section& s = manual_[static_cast<std::size_t>(i)];
        int score = 0;
        if (q.isEmpty())
            score = 1;   // indice: tutto, nell'ordine
        else if (s.commands.contains(q))
            score = 100;
        else if (s.title.toUpper().contains(q))
            score = 50;
        else if (s.plain.toUpper().contains(q))
            score = 10 + std::min(20, static_cast<int>(s.plain.toUpper().count(q)));
        if (score > 0)
            hits.push_back({i, score});
    }
    if (!q.isEmpty())
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    for (const Hit& h : hits) {
        const Section& s = manual_[static_cast<std::size_t>(h.index)];
        shown_.append(QVariantMap{{QStringLiteral("chapter"), s.chapter}, {QStringLiteral("title"), s.title}});
        shownIndex_.push_back(h.index);
    }
    emit manualChanged();
}

void GuidePanel::openSection(int index)
{
    if (index < 0 || index >= static_cast<int>(shownIndex_.size()))
        return;
    const Section& s = manual_[static_cast<std::size_t>(shownIndex_[static_cast<std::size_t>(index)])];
    pageTitle_ = s.chapter;
    // Nella tavolozza il carattere a spaziatura fissa non è affidabile: i `comandi` in grassetto, i blocchi di
    // codice (script) come citazioni, una riga per paragrafo.
    QString text;
    bool inCode = false;
    for (QString line : s.markdown.split(u'\n')) {
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            inCode = !inCode;
            continue;
        }
        if (inCode) {
            text += QStringLiteral("> ") + line.toHtmlEscaped() + QStringLiteral("  \n");
            continue;
        }
        static const QRegularExpression code(QStringLiteral("`([^`]+)`"));
        line.replace(code, QStringLiteral("**\\1**"));
        text += line + u'\n';
    }
    pageText_ = text;
    emit manualChanged();
}

void GuidePanel::closeSection()
{
    pageTitle_.clear();
    pageText_.clear();
    emit manualChanged();
}

bool GuidePanel::openCommand(const QString& command)
{
    const QString c = command.toUpper().trimmed();
    for (const Section& s : manual_)
        if (s.commands.contains(c)) {
            page_ = 1;
            emit pageChanged();
            search(c);
            openSection(0);
            return true;
        }
    return false;
}

} // namespace cartesio
