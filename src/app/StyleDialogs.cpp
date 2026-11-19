#include "app/StyleDialogs.h"

#include "app/DrawingWidget.h"
#include "app/QmlUi.h"
#include "app/TextRender.h"
#include "core/commands/CommandManager.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <set>

namespace cartesio {

namespace {

constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;

// Nome nuovo (campo di testo); vuoto se annullato.
QString askName(QWidget* parent, const QString& title, const QString& initial)
{
    QString name = initial;
    FormDialog ask(title, FormDialog::Ok | FormDialog::Cancel, parent);
    ask.text(QObject::tr("Nome:"), name);
    ask.setSize(420, 150);
    if (ask.exec() != QDialog::Accepted)
        return {};
    return name.trimmed();
}

// Font SHX trovati nelle cartelle dei font (nome del file, minuscolo).
QStringList shxFonts()
{
    std::set<QString> names;
    for (const QString& folder : FontLibrary::instance().searchFolders())
        for (const QString& f : QDir(folder).entryList({QStringLiteral("*.shx")}, QDir::Files))
            names.insert(f.toLower());
    return QStringList(names.begin(), names.end());
}

} // namespace

// =================================================================== STYLE

TextStyleDialog::TextStyleDialog(DrawingWidget* drawing, QWidget* parent)
    : FormDialog(tr("Stili di testo"), Close, parent), drawing_(drawing)
{
    group(tr("Stili di testo del disegno"));
    currentNote_ = note(QString());
    list_ = list({}, -1, [this](int index) { selected_ = index; }, [this](int) { modifyStyle(); });
    buttonsRow({{tr("Imposta corrente"), [this] { makeCurrent(); }},
                {tr("Nuovo..."), [this] { newStyle(); }},
                {tr("Modifica..."), [this] { modifyStyle(); }},
                {tr("Rinomina..."), [this] { renameStyle(); }},
                {tr("Elimina"), [this] { removeStyle(); }}});
    note(tr("I testi che usano uno stile cambiano con lo stile. Lo stile Standard, quello corrente e quelli "
            "usati dai testi non si eliminano."));
    refresh();
    setSize(520, 480);
}

void TextStyleDialog::refresh()
{
    const Document& doc = drawing_->document();
    const QString previous = selectedName();
    names_.clear();
    for (const TextStyle& s : doc.textStyles())
        names_ << QString::fromStdString(s.name);
    const QString current = QString::fromStdString(doc.currentTextStyle());
    setFieldText(currentNote_, tr("Stile di testo corrente: <b>%1</b>").arg(current.toHtmlEscaped()));
    selected_ = static_cast<int>(names_.indexOf(previous.isEmpty() ? current : previous));
    setItems(list_, names_, selected_);
}

QString TextStyleDialog::selectedName() const
{
    return selected_ >= 0 && selected_ < names_.size() ? names_[selected_] : QString();
}

void TextStyleDialog::makeCurrent()
{
    if (selectedName().isEmpty())
        return;
    drawing_->document().setCurrentTextStyle(selectedName().toStdString());
    drawing_->notifyChanged();
    refresh();
}

void TextStyleDialog::newStyle()
{
    Document& doc = drawing_->document();
    const QString name = askName(this, tr("Nuovo stile di testo"), tr("Copia di %1").arg(selectedName()));
    if (name.isEmpty())
        return;
    if (doc.findTextStyle(name.toStdString())) {
        QMessageBox::information(this, windowTitle(), tr("Esiste già uno stile con questo nome."));
        return;
    }
    TextStyle style = doc.textStyleOrStandard(selectedName().toStdString());
    style.name = name.toStdString();
    if (!edit(style, this))
        return;
    doc.setTextStyle(style);
    doc.setCurrentTextStyle(style.name);
    drawing_->notifyChanged();
    selected_ = -1;
    names_.clear();
    refresh();
}

void TextStyleDialog::modifyStyle()
{
    Document& doc = drawing_->document();
    const TextStyle* existing = doc.findTextStyle(selectedName().toStdString());
    if (!existing)
        return;
    TextStyle style = *existing;
    if (!edit(style, this))
        return;
    doc.setTextStyle(std::move(style));
    drawing_->notifyChanged();
    refresh();
}

void TextStyleDialog::renameStyle()
{
    const QString from = selectedName();
    if (from.isEmpty())
        return;
    const QString to = askName(this, tr("Rinomina stile di testo"), from);
    if (to.isEmpty() || to == from)
        return;
    std::string error;
    if (!drawing_->document().renameTextStyle(from.toStdString(), to.toStdString(), &error)) {
        QMessageBox::information(this, windowTitle(), QString::fromStdString(error));
        return;
    }
    drawing_->notifyChanged();
    selected_ = -1;
    names_.clear();
    refresh();
}

void TextStyleDialog::removeStyle()
{
    const QString name = selectedName();
    if (name.isEmpty())
        return;
    std::string error;
    if (!drawing_->document().removeTextStyle(name.toStdString(), &error)) {
        QMessageBox::information(this, windowTitle(), QString::fromStdString(error));
        return;
    }
    drawing_->notifyChanged();
    selected_ = -1;
    names_.clear();
    refresh();
}

bool TextStyleDialog::edit(TextStyle& style, QWidget* parent)
{
    // Carattere: i font SHX trovati, poi le famiglie TrueType installate.
    QList<QPair<QString, QVariant>> fonts;
    QStringList shx = shxFonts();
    QString font = QString::fromStdString(style.font).toLower();
    if (style.isShx() && !font.endsWith(QStringLiteral(".shx")))
        font += QStringLiteral(".shx");
    if (style.isShx() && !shx.contains(font))
        shx.prepend(font);   // il font dello stile anche se qui non c'è
    for (const QString& f : shx)
        fonts.append({f, QStringLiteral("shx:") + f});
    for (const QString& family : QFontDatabase::families())
        fonts.append({family, QStringLiteral("ttf:") + family});
    QString current = style.isShx() ? QStringLiteral("shx:") + font
                                    : QStringLiteral("ttf:")
                                          + (style.family.empty() ? QFileInfo(QString::fromStdString(style.font)).completeBaseName()
                                                                  : QString::fromStdString(style.family));
    TextStyle copy = style;
    double oblique = style.oblique / kRadPerDeg;
    FormDialog dialog(QObject::tr("Modifica stile di testo: %1").arg(QString::fromStdString(style.name)),
                      FormDialog::Ok | FormDialog::Cancel, parent);
    dialog.group(QObject::tr("Carattere"));
    dialog.choice(QObject::tr("Font:"), fonts, current, [&current](const QVariant& v) { current = v.toString(); });
    dialog.check(QObject::tr("Grassetto (solo TrueType)"), copy.bold);
    dialog.check(QObject::tr("Corsivo (solo TrueType)"), copy.italic);
    dialog.group(QObject::tr("Dimensioni ed effetti"));
    dialog.number(QObject::tr("Altezza (0 = la chiede il comando):"), 0.0, 100000.0, 3, copy.height);
    dialog.number(QObject::tr("Fattore di larghezza:"), 0.01, 100.0, 3, copy.widthFactor);
    dialog.number(QObject::tr("Angolo obliquo (gradi):"), -85.0, 85.0, 1, oblique);
    dialog.check(QObject::tr("Capovolto"), copy.upsideDown);
    dialog.check(QObject::tr("Al contrario (specchiato)"), copy.backwards);
    dialog.check(QObject::tr("Annotativo (l'altezza è quella sulla carta: i testi seguono la scala di annotazione)"),
                 copy.annotative);
    dialog.setSize(520, 500);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (current.startsWith(QStringLiteral("shx:"))) {
        copy.font = current.mid(4).toStdString();
        copy.family.clear();
    } else {
        copy.family = current.mid(4).toStdString();
        copy.font.clear();
    }
    copy.oblique = oblique * kRadPerDeg;
    style = std::move(copy);
    return true;
}

// =================================================================== MLEADERSTYLE

MLeaderStyleDialog::MLeaderStyleDialog(DrawingWidget* drawing, QWidget* parent)
    : FormDialog(tr("Stili delle direttrici"), Close, parent), drawing_(drawing)
{
    group(tr("Stili delle direttrici del disegno"));
    currentNote_ = note(QString());
    list_ = list({}, -1, [this](int index) { selected_ = index; }, [this](int) { modifyStyle(); });
    buttonsRow({{tr("Imposta corrente"), [this] { makeCurrent(); }},
                {tr("Nuovo..."), [this] { newStyle(); }},
                {tr("Modifica..."), [this] { modifyStyle(); }},
                {tr("Elimina"), [this] { removeStyle(); }}});
    note(tr("Le nuove direttrici usano lo stile corrente; senza stili prendono le misure dello stile di quota. "
            "Modificando uno stile si aggiornano le direttrici che lo usano."));
    refresh();
    setSize(500, 460);
}

void MLeaderStyleDialog::refresh()
{
    const Document& doc = drawing_->document();
    const QString previous = selectedName();
    names_.clear();
    for (const auto& s : doc.mleaderStyles())
        names_ << QString::fromStdString(s.name);
    const QString current = QString::fromStdString(doc.currentMLeaderStyle());
    setFieldText(currentNote_, current.isEmpty() ? tr("Nessuno stile corrente: misure dallo stile di quota.")
                                                 : tr("Stile corrente: <b>%1</b>").arg(current.toHtmlEscaped()));
    selected_ = static_cast<int>(names_.indexOf(previous.isEmpty() ? current : previous));
    setItems(list_, names_, selected_);
}

QString MLeaderStyleDialog::selectedName() const
{
    return selected_ >= 0 && selected_ < names_.size() ? names_[selected_] : QString();
}

void MLeaderStyleDialog::makeCurrent()
{
    if (selectedName().isEmpty())
        return;
    drawing_->document().setCurrentMLeaderStyle(selectedName().toStdString());
    drawing_->notifyChanged();
    refresh();
}

void MLeaderStyleDialog::newStyle()
{
    Document& doc = drawing_->document();
    const QString name = askName(this, tr("Nuovo stile di direttrice"),
                                 selectedName().isEmpty() ? tr("Standard") : tr("Copia di %1").arg(selectedName()));
    if (name.isEmpty())
        return;
    if (doc.findMLeaderStyle(name.toStdString())) {
        QMessageBox::information(this, windowTitle(), tr("Esiste già uno stile con questo nome."));
        return;
    }
    Document::MLeaderStyle style;
    if (const auto* from = doc.findMLeaderStyle(selectedName().toStdString())) {
        style = *from;
    } else {   // il primo stile parte dallo stile di quota corrente
        const DimStyle& d = doc.currentDimStyle();
        const double k = d.scale > 0.0 ? d.scale : 1.0;
        style.textHeight = d.textHeight * k;
        style.arrowSize = d.arrowSize * k;
        style.textStyle = doc.currentTextStyle();
    }
    style.name = name.toStdString();
    if (!edit(style))
        return;
    doc.setMLeaderStyle(style);
    doc.setCurrentMLeaderStyle(style.name);
    drawing_->notifyChanged();
    selected_ = -1;
    names_.clear();
    refresh();
}

void MLeaderStyleDialog::modifyStyle()
{
    Document& doc = drawing_->document();
    const auto* existing = doc.findMLeaderStyle(selectedName().toStdString());
    if (!existing)
        return;
    Document::MLeaderStyle style = *existing;
    if (!edit(style))
        return;
    const std::string name = style.name;
    doc.setMLeaderStyle(std::move(style));
    const int updated = doc.restyleLeaders(name);
    drawing_->notifyChanged();
    if (updated > 0)
        QMessageBox::information(this, windowTitle(), tr("Direttrici aggiornate: %1.").arg(updated));
    refresh();
}

void MLeaderStyleDialog::removeStyle()
{
    const QString name = selectedName();
    if (name.isEmpty())
        return;
    std::string error;
    if (!drawing_->document().removeMLeaderStyle(name.toStdString(), &error)) {
        QMessageBox::information(this, windowTitle(), QString::fromStdString(error));
        return;
    }
    drawing_->notifyChanged();
    selected_ = -1;
    names_.clear();
    refresh();
}

bool MLeaderStyleDialog::edit(Document::MLeaderStyle& style)
{
    const Document& doc = drawing_->document();
    QList<QPair<QString, QVariant>> textStyles;
    for (const TextStyle& s : doc.textStyles())
        textStyles.append({QString::fromStdString(s.name), QString::fromStdString(s.name)});
    Document::MLeaderStyle copy = style;
    QString textStyle = QString::fromStdString(style.textStyle);
    bool autoGap = style.landingGap < 0.0;
    double gap = style.landingGap < 0.0 ? style.textHeight / 2.0 : style.landingGap;
    FormDialog dialog(tr("Modifica stile di direttrice: %1").arg(QString::fromStdString(style.name)), Ok | Cancel, this);
    dialog.group(tr("Testo"));
    dialog.choice(tr("Stile di testo:"), textStyles, textStyle, [&textStyle](const QVariant& v) { textStyle = v.toString(); });
    dialog.number(tr("Altezza del testo:"), 0.01, 100000.0, 3, copy.textHeight);
    const int automatic = dialog.check(tr("Distanza dall'approdo automatica (metà altezza del testo)"), autoGap);
    const int gapField = dialog.number(tr("Distanza dall'approdo:"), 0.0, 100000.0, 3, gap);
    (void)automatic;
    (void)gapField;
    dialog.group(tr("Freccia"));
    dialog.number(tr("Dimensione della freccia (0 = nessuna):"), 0.0, 100000.0, 3, copy.arrowSize);
    dialog.group(tr("Scala"));
    dialog.check(tr("Annotativo (misure sulla carta: le direttrici seguono la scala di annotazione)"), copy.annotative);
    dialog.setSize(500, 430);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    copy.textStyle = textStyle.toStdString();
    copy.landingGap = autoGap ? -1.0 : gap;
    style = std::move(copy);
    return true;
}

// =================================================================== motivo di tratteggio

namespace {

// Anteprima di un motivo: le famiglie di linee (con tratti e punti) in un quadrato di 20 unità.
QIcon patternIcon(const std::string& name)
{
    constexpr int px = 72;
    constexpr double side = 20.0;
    QPixmap pixmap(px, px);
    pixmap.fill(QColor(43, 50, 60));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(214, 219, 227);
    if (name == "SOLID") {
        p.fillRect(QRect(6, 6, px - 12, px - 12), ink);
        return QIcon(pixmap);
    }
    const double k = px / side;
    const auto toPx = [k](Vec2 v) { return QPointF(v.x * k, px - v.y * k); };
    p.setPen(QPen(ink, 1.0));
    p.setClipRect(QRect(0, 0, px, px));
    const Vec2 corners[4] = {{0, 0}, {side, 0}, {side, side}, {0, side}};
    for (const HatchPatternLine& line : standardHatchPattern(name, 1.0, 0.0)) {
        const Vec2 d = Vec2::polar(1.0, line.angle);
        const Vec2 n{-d.y, d.x};
        const double step = line.offset.dot(n);
        if (std::abs(step) < 1e-9)
            continue;
        double kMin = 1e300, kMax = -1e300, tMin = 1e300, tMax = -1e300;
        for (Vec2 c : corners) {
            const double kk = (c - line.base).dot(n) / step;
            kMin = std::min(kMin, kk);
            kMax = std::max(kMax, kk);
        }
        for (int i = static_cast<int>(std::floor(kMin)) - 1; i <= static_cast<int>(std::ceil(kMax)) + 1; ++i) {
            const Vec2 origin = line.base + line.offset * i;
            tMin = 1e300;
            tMax = -1e300;
            for (Vec2 c : corners) {
                const double t = (c - origin).dot(d);
                tMin = std::min(tMin, t);
                tMax = std::max(tMax, t);
            }
            if (line.dashes.empty()) {
                p.drawLine(toPx(origin + d * tMin), toPx(origin + d * tMax));
                continue;
            }
            double length = 0.0;
            for (double v : line.dashes)
                length += std::abs(v);
            if (length < 1e-9)
                continue;
            double t = std::floor(tMin / length) * length;
            while (t < tMax) {
                for (double v : line.dashes) {
                    if (v > 0.0)
                        p.drawLine(toPx(origin + d * t), toPx(origin + d * (t + v)));
                    else if (v == 0.0)
                        p.drawPoint(toPx(origin + d * t));
                    t += std::abs(v);
                }
            }
        }
    }
    return QIcon(pixmap);
}

} // namespace

bool chooseHatchPattern(QWidget* parent)
{
    HatchDefaults defaults = hatchDefaults();
    QStringList names{QObject::tr("SOLIDO")};
    QStringList images{IconProvider::url(patternIcon("SOLID"))};
    std::vector<std::string> keys{"SOLID"};
    for (const std::string& n : standardHatchPatternNames()) {
        if (n == "SOLID")
            continue;
        names << QString::fromStdString(n);
        images << IconProvider::url(patternIcon(n));
        keys.push_back(n);
    }
    int index = 0;
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == defaults.pattern)
            index = static_cast<int>(i);
    double scale = defaults.scale;
    double angle = defaults.angle;
    FormDialog dialog(QObject::tr("Motivo di tratteggio"), FormDialog::Ok | FormDialog::Cancel, parent);
    dialog.gallery(names, images, index, [&index](int i) { index = i; }, [&dialog](int) { dialog.accept(); });
    dialog.number(QObject::tr("Scala:"), 0.0001, 100000.0, 4, scale);
    dialog.number(QObject::tr("Angolo (gradi):"), -360.0, 360.0, 1, angle);
    dialog.setSize(560, 520);
    if (dialog.exec() != QDialog::Accepted || index < 0 || index >= static_cast<int>(keys.size()))
        return false;
    defaults.pattern = keys[static_cast<std::size_t>(index)];
    defaults.scale = scale;
    defaults.angle = angle;
    return true;
}

} // namespace cartesio
