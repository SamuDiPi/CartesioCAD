#include "app/CommandLine.h"

#include "app/QmlUi.h"

#include <QKeyEvent>
#include <QQmlContext>
#include <QQuickItem>

namespace cartesio {

void CommandInput::setText(const QString& text)
{
    if (text_ == text)
        return;
    text_ = text;
    emit textReset();
    emit textChanged(text_);
}

void CommandInput::edited(const QString& text)
{
    if (text_ == text)
        return;
    text_ = text;
    emit textChanged(text_);
}

void CommandInput::setPlaceholderText(const QString& text)
{
    if (placeholder_ != text) {
        placeholder_ = text;
        emit changed();
    }
}

void CommandInput::setFocus()
{
    emit focusRequested();
}

CommandLine::CommandLine(QWidget* parent) : QQuickWidget(parent), input_(new CommandInput(this))
{
    setupQuickEngine(this);
    // Sopra il disegno (anch'esso Qt Quick), con lo sfondo trasparente dove non c'è la barra.
    setAttribute(Qt::WA_AlwaysStackOnTop);
    setClearColor(Qt::transparent);
    rootContext()->setContextProperty(QStringLiteral("cmd"), this);
    if (!qmlRootUi()) {   // nella scena unica il componente lo carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/CommandLine.qml")));
        if (QQuickItem* root = rootObject())
            connect(root, &QQuickItem::implicitHeightChanged, this, &CommandLine::heightChanged);
    }
    input_->setPlaceholderText(tr("Scrivi un comando"));
    connect(input_, &CommandInput::textChanged, this, &CommandLine::updateMatches);
    connect(input_, &CommandInput::focusRequested, this, [this] {
        if (qmlRootUi()) {   // la riga di comando è nella scena unica
            emit focusInRootRequested();
            return;
        }
        QQuickWidget::setFocus();
        if (QQuickItem* root = rootObject())
            QMetaObject::invokeMethod(root, "focusInput");
    });
}

int CommandLine::wantedHeight() const
{
    const QQuickItem* root = rootObject();
    return root ? std::max(30, static_cast<int>(root->implicitHeight())) : 30;
}

void CommandLine::setPrompt(const QString& prompt)
{
    if (prompt_ != prompt) {
        prompt_ = prompt;
        emit changed();
    }
}

void CommandLine::appendHistory(const QString& line)
{
    lines_ << line;
    if (lines_.size() > 5000)
        lines_.removeFirst();
    emit historyChanged();
}

void CommandLine::clearHistory()
{
    lines_.clear();
    emit historyChanged();
}

void CommandLine::setCompletionEnabled(bool enabled)
{
    if (completion_ != enabled) {
        completion_ = enabled;
        updateMatches();
    }
}

// Suggerimenti: comandi e alias che cominciano con il testo digitato (al massimo 10).
void CommandLine::updateMatches()
{
    QStringList found;
    const QString typed = input_->text().trimmed();
    if (completion_ && !typed.isEmpty() && !typed.contains(QLatin1Char(' ')) && !typed.at(0).isDigit()
        && typed.at(0) != QLatin1Char('@') && typed.at(0) != QLatin1Char('-')) {
        for (const QString& w : words_)
            if (w.startsWith(typed, Qt::CaseInsensitive) && w.compare(typed, Qt::CaseInsensitive) != 0) {
                found << w;
                if (found.size() >= 10)
                    break;
            }
    }
    if (found != matches_) {
        matches_ = found;
        emit matchesChanged();
    }
}

bool CommandLine::handleKey(int key, int modifiers, const QString& text)
{
    if (!keyHandler_)
        return false;
    QKeyEvent event(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers), text);
    return keyHandler_(event);
}

} // namespace cartesio

#include "app/Icons.h"

namespace cartesio {

QString CommandLine::closeIcon() const
{
    return IconProvider::url(icon(Icon::Close));
}

QString CommandLine::historyIcon() const
{
    return IconProvider::url(icon(Icon::History));
}

} // namespace cartesio
