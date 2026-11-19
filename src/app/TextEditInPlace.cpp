#include "app/TextEditInPlace.h"
#include "app/SpellCheck.h"

#include "app/CadView.h"
#include "app/ColorDialog.h"
#include "app/DrawingWidget.h"
#include "app/TextRender.h"
#include "core/Document.h"

#include <QAction>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace cartesio {

namespace {

enum class Kind { Text, MText, Leader, Attribute, Dimension, TableCell };

constexpr double kDegPerRad = 57.29577951308232;

// Un carattere del testo con il suo formato (a capo = '\n').
struct EdChar {
    QChar c;
    MTextFormat f;
};

struct Snapshot {
    std::vector<EdChar> chars;
    int caret = 0;
    int anchor = 0;
};

QString num(double v)
{
    return QString::number(v, 'g', 8);
}

QString escapeMText(const QString& s)
{
    QString out;
    for (const QChar c : s) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// Codici di formattazione di un pezzo rispetto al formato di partenza del testo.
QString formatCodes(const MTextFormat& f, const MTextFormat& base, const QString& family)
{
    QString codes;
    if (std::abs(f.height - base.height) > 1e-9)
        codes += QStringLiteral("\\H") + num(f.height) + QLatin1Char(';');
    if (std::abs(f.width - base.width) > 1e-9)
        codes += QStringLiteral("\\W") + num(f.width) + QLatin1Char(';');
    if (std::abs(f.oblique - base.oblique) > 1e-9)
        codes += QStringLiteral("\\Q") + num(f.oblique * kDegPerRad) + QLatin1Char(';');
    if (f.family != base.family || f.shxFile != base.shxFile || f.bold != base.bold || f.italic != base.italic) {
        if (!f.shxFile.isEmpty())
            codes += QStringLiteral("\\F") + f.shxFile + QLatin1Char(';');
        else
            codes += QStringLiteral("\\f%1|b%2|i%3;")
                         .arg(f.family.isEmpty() ? family : f.family)
                         .arg(f.bold ? 1 : 0)
                         .arg(f.italic ? 1 : 0);
    }
    if ((f.aci != base.aci || f.rgb != base.rgb) && (f.rgb >= 0 || f.aci > 0))
        codes += f.rgb >= 0 ? QStringLiteral("\\c%1;").arg(f.rgb) : QStringLiteral("\\C%1;").arg(f.aci);
    if (f.underline)
        codes += QStringLiteral("\\L");
    if (f.overline)
        codes += QStringLiteral("\\O");
    if (f.strike)
        codes += QStringLiteral("\\K");
    return codes;
}

// Codici d'impaginazione all'inizio del testo (allineamento \A, rientri \p, spaziatura \T): restano davanti.
QString layoutPrefix(const QString& s)
{
    int i = s.startsWith(QLatin1Char('{')) ? 1 : 0;
    QString out;
    while (i + 1 < s.size() && s[i] == QLatin1Char('\\')) {
        const QChar code = s[i + 1];
        const int end = s.indexOf(QLatin1Char(';'), i);
        if (end < 0)
            break;
        if (QStringLiteral("ApT").contains(code))
            out += s.mid(i, end - i + 1);
        else if (!QStringLiteral("fFHWQCc").contains(code))
            break;
        i = end + 1;
    }
    return out;
}

std::vector<EdChar> charsFromMText(const QString& source, const MTextFormat& initial)
{
    std::vector<EdChar> chars;
    const auto paragraphs = parseMText(source, initial);
    for (std::size_t p = 0; p < paragraphs.size(); ++p) {
        if (p > 0)
            chars.push_back({QLatin1Char('\n'), chars.empty() ? initial : chars.back().f});
        for (const MTextRun& run : paragraphs[p])
            for (const QChar c : run.text)
                chars.push_back({c, run.format});
    }
    return chars;
}

// TEXT e attributi: i simboli tornano nei codici %% (come si scrivono nei DXF).
std::string encodeSymbols(const QString& s)
{
    QString t = s;
    t.replace(QChar(0x00B0), QStringLiteral("%%d"));
    t.replace(QChar(0x00B1), QStringLiteral("%%p"));
    t.replace(QChar(0x00D8), QStringLiteral("%%c"));
    return t.toStdString();
}

bool wordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

class InPlaceTextEditor final : public QWidget, public ViewTextEditor, public RichTextControls {
public:
    InPlaceTextEditor(DrawingWidget& drawing, EntityId id, Kind kind, int a, int b, std::optional<Vec2> at)
        : QWidget(drawing.view()->overlayHost()), drawing_(&drawing), view_(drawing.view()), id_(id), kind_(kind),
          index_(a), row_(a), col_(b)
    {
        setObjectName(QStringLiteral("InPlaceTextEditor"));
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_InputMethodEnabled);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        resize(1, 1);

        Document& doc = drawing.document();
        original_ = doc.find(id)->clone();
        rich_ = kind == Kind::MText || kind == Kind::Leader || kind == Kind::TableCell;
        multiline_ = rich_;
        loadModel(doc);

        // Durante la modifica l'oggetto non si vede nella scena: lo ridisegna l'editor, com'è mentre si scrive.
        doc.hideObjects({id});
        drawing.setSelection({});
        rebuild();
        if (at && rich_) {
            caret_ = anchor_ = nearestIndex(view_->screenPoint(*at));
        } else {   // testi su una riga, attributi, quote: tutto selezionato
            anchor_ = 0;
            caret_ = static_cast<int>(chars_.size());
        }
        view_->setTextEditor(this);
        drawing.setTextEditorCommit([this] { finish(true); });

        blink_.setInterval(530);
        connect(&blink_, &QTimer::timeout, this, [this] {
            caretOn_ = !caretOn_;
            if (view_)
                view_->update();
        });
        blink_.start();
        if (rich_)
            TextEditorHub::instance().setActive(this);   // la scheda Editor di testo della barra multifunzione
        reposition_.setInterval(150);   // la barra segue il testo durante zoom e pan
        connect(&reposition_, &QTimer::timeout, this, [this] { placeWidgets(); });
        reposition_.start();
        show();
        raise();
        setFocus();
        placeWidgets();
        syncBar();
    }

    ~InPlaceTextEditor() override
    {
        if (view_ && view_->textEditor() == this)
            view_->setTextEditor(nullptr);
        if (TextEditorHub::instance().active() == this)
            TextEditorHub::instance().setActive(nullptr);
    }

    static void startCell(DrawingWidget& drawing, EntityId id, int row, int col)
    {
        new InPlaceTextEditor(drawing, id, Kind::TableCell, row, col, std::nullopt);
    }

    // --------------------------------------------------------------- vista

    bool viewMousePress(QMouseEvent* event) override
    {
        if (done_)
            return false;
        const QPointF pos = event->position();
        if (event->button() == Qt::RightButton) {
            showMenu(event->globalPosition().toPoint(), nearestIndex(pos));
            return true;
        }
        if (event->button() != Qt::LeftButton)
            return false;
        if (onRuler(pos)) {   // righello: la maniglia cambia la larghezza, il resto non chiude la modifica
            const QPointF h = rulerHandle();
            rulerDrag_ = std::hypot(pos.x() - h.x(), pos.y() - h.y()) <= 9.0;
            setFocus();
            return true;
        }
        if (!insideFrame(pos)) {   // clic fuori dal testo: conferma
            finish(true);
            return true;
        }
        caret_ = nearestIndex(pos);
        if (!(event->modifiers() & Qt::ShiftModifier))
            anchor_ = caret_;
        dragging_ = true;
        caretMoved();
        setFocus();
        return true;
    }

    bool viewMouseMove(QMouseEvent* event) override
    {
        if (!done_ && rulerDrag_ && (event->buttons() & Qt::LeftButton)) {
            dragRuler(event->position());
            return true;
        }
        if (done_ || !dragging_ || !(event->buttons() & Qt::LeftButton))
            return false;
        caret_ = nearestIndex(event->position());
        caretMoved();
        return true;
    }

    bool viewMouseRelease(QMouseEvent*) override
    {
        if (rulerDrag_) {
            rulerDrag_ = false;
            return true;
        }
        if (!dragging_)
            return false;
        dragging_ = false;
        return true;
    }

    bool viewMouseDoubleClick(QMouseEvent* event) override
    {
        if (done_)
            return false;
        if (insideFrame(event->position())) {   // doppio clic: la parola
            const int i = nearestIndex(event->position());
            int a = i, b = i;
            while (a > 0 && wordChar(chars_[static_cast<std::size_t>(a - 1)].c))
                --a;
            while (b < static_cast<int>(chars_.size()) && wordChar(chars_[static_cast<std::size_t>(b)].c))
                ++b;
            anchor_ = a;
            caret_ = b;
            caretMoved();
        }
        return true;
    }

    bool viewIBeam(QPointF screen) const override { return !done_ && insideFrame(screen) && !onRuler(screen); }

    void paintInView(QPainter& p) override
    {
        if (done_ || !view_ || !preview_)
            return;
        view_->paintEntityWorld(p, *preview_);   // l'oggetto com'è adesso
        p.save();
        paintRuler(p);
        // Parole sbagliate: ondina rossa sotto le lettere.
        p.setPen(QPen(QColor(235, 60, 60), 1.2));
        p.setBrush(Qt::NoBrush);
        for (const auto& [start, length] : spellErrors_)
            for (int i = start; i < start + length && i < static_cast<int>(geom_.boxes.size()); ++i) {
                const QPolygonF box = geom_.boxes[static_cast<std::size_t>(i)];
                if (box.size() < 2)
                    continue;
                const QPointF a = toScreen(box[0]), b = toScreen(box[1]);
                const QPointF d = b - a;
                const double len = std::hypot(d.x(), d.y());
                if (len < 1.0)
                    continue;
                const QPointF u = d / len, n(u.y(), -u.x());
                QPolygonF wave;
                for (double t = 0.0; t <= len + 0.01; t += 2.0)
                    wave << a + u * t + n * (static_cast<int>(t / 2.0) % 2 == 0 ? 0.0 : -2.0) + n * 2.0;
                p.drawPolyline(wave);
            }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(61, 139, 253), 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(toScreen(geom_.frame));
        const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        if (a != b) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(61, 139, 253, 110));
            for (int i = a; i < b && i < static_cast<int>(geom_.boxes.size()); ++i)
                if (!geom_.boxes[static_cast<std::size_t>(i)].isEmpty())
                    p.drawPolygon(toScreen(geom_.boxes[static_cast<std::size_t>(i)]));
        }
        if (caretOn_ || dragging_) {
            if (const auto* s = stopFor(caret_)) {
                const bool paper = view_->shownSpace() != 0;
                p.setPen(QPen(paper ? QColor(0, 0, 0) : QColor(255, 255, 255), 1.6));
                p.drawLine(toScreen(s->bottom), toScreen(s->top));
            }
        }
        p.restore();
    }

    void finish(bool apply)
    {
        if (done_)
            return;
        done_ = true;
        blink_.stop();
        reposition_.stop();
        if (view_ && view_->textEditor() == this)
            view_->setTextEditor(nullptr);
        if (bar_)
            bar_->deleteLater();
        if (TextEditorHub::instance().active() == this)
            TextEditorHub::instance().setActive(nullptr);
        if (DrawingWidget* d = drawing_) {
            d->setTextEditorCommit(nullptr);
            d->document().showObjects({id_});
            if (newObject_) {
                // Testo nuovo (TEXT, MTEXT): anche Esc tiene quello che si è scritto; il testo finisce nello stesso
                // passo della creazione (un solo Ctrl+Z); vuoto, sparisce senza lasciare traccia.
                if (const Entity* e = d->document().find(id_)) {
                    auto copy = e->clone();
                    applyTo(*copy);
                    if (chars_.empty())
                        copy.reset();
                    d->document().amendLastAdd(id_, std::move(copy));
                }
                d->notifyChanged();
            } else if (apply && changed_ && d->document().find(id_))
                d->modifyEntities({id_}, [this](Entity& e) { applyTo(e); });   // annullabile con Ctrl+Z
            else
                d->notifyChanged();
            if (view_)
                view_->overlayHost()->setFocus();
        }
        hide();
        deleteLater();
    }

protected:
    bool event(QEvent* e) override
    {
        // I tasti sono dell'editor: niente scorciatoie della finestra (Ctrl+C, Canc...) né Tab per il fuoco.
        if (e->type() == QEvent::ShortcutOverride) {
            e->accept();
            return true;
        }
        if (e->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
                keyPressEvent(k);
                return true;
            }
        }
        return QWidget::event(e);
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (done_)
            return;
        const Qt::KeyboardModifiers mods = e->modifiers();
        const bool shift = mods & Qt::ShiftModifier;
        const bool ctrl = (mods & Qt::ControlModifier) && !(mods & Qt::AltModifier);   // AltGr = Ctrl+Alt
        const int size = static_cast<int>(chars_.size());
        const auto move = [&](int pos) {
            caret_ = std::clamp(pos, 0, size);
            if (!shift)
                anchor_ = caret_;
            caretMoved();
        };
        switch (e->key()) {
        case Qt::Key_Escape:
            finish(false);
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (ctrl || !multiline_)
                finish(true);
            else
                replaceSelection(QStringLiteral("\n"));
            return;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            if (kind_ == Kind::TableCell)
                nextCell(e->key() == Qt::Key_Backtab || shift ? -1 : 1);
            return;
        case Qt::Key_Left:
            if (!shift && caret_ != anchor_)
                move(std::min(caret_, anchor_));
            else
                move(ctrl ? wordLeft(caret_) : caret_ - 1);
            return;
        case Qt::Key_Right:
            if (!shift && caret_ != anchor_)
                move(std::max(caret_, anchor_));
            else
                move(ctrl ? wordRight(caret_) : caret_ + 1);
            return;
        case Qt::Key_Up:
        case Qt::Key_Down:
            if (multiline_)
                move(lineMove(e->key() == Qt::Key_Up ? -1 : 1));
            return;
        case Qt::Key_Home:
            move(ctrl ? 0 : lineEdge(false));
            return;
        case Qt::Key_End:
            move(ctrl ? size : lineEdge(true));
            return;
        case Qt::Key_Backspace:
            if (caret_ == anchor_) {
                if (caret_ == 0)
                    return;
                anchor_ = ctrl ? wordLeft(caret_) : caret_ - 1;
            }
            replaceSelection(QString());
            return;
        case Qt::Key_Delete:
            if (caret_ == anchor_) {
                if (caret_ >= size)
                    return;
                anchor_ = ctrl ? wordRight(caret_) : caret_ + 1;
            }
            replaceSelection(QString());
            return;
        default:
            break;
        }
        if (ctrl) {
            switch (e->key()) {
            case Qt::Key_A:
                anchor_ = 0;
                caret_ = size;
                caretMoved();
                return;
            case Qt::Key_C:
                copy();
                return;
            case Qt::Key_X:
                copy();
                replaceSelection(QString());
                return;
            case Qt::Key_V:
                paste();
                return;
            case Qt::Key_Z:
                shift ? redo() : undo();
                return;
            case Qt::Key_Y:
                redo();
                return;
            case Qt::Key_B:
                if (rich_)
                    toggleBold();
                return;
            case Qt::Key_I:
                if (rich_)
                    toggleItalic();
                return;
            default:
                return;
            }
        }
        const QString text = e->text();
        if (!text.isEmpty() && text[0].isPrint())
            replaceSelection(text);
    }

    void inputMethodEvent(QInputMethodEvent* e) override
    {
        if (!e->commitString().isEmpty())
            replaceSelection(e->commitString());
        e->accept();
    }

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override
    {
        if (query == Qt::ImCursorRectangle)
            return QRect(0, 0, 1, 1);
        if (query == Qt::ImEnabled)
            return true;
        return QWidget::inputMethodQuery(query);
    }

private:
    // ------------------------------------------------------------ modello

    void loadModel(const Document& doc)
    {
        const auto plain = [this](const QString& s) {
            for (const QChar c : s)
                chars_.push_back({c, initial_});
        };
        switch (kind_) {
        case Kind::Text: {
            const auto& t = static_cast<const TextEntity&>(*original_);
            initial_.height = t.height;
            plain(QString::fromStdString(t.displayText()));
            break;
        }
        case Kind::Attribute: {
            const auto& t = static_cast<const InsertEntity&>(*original_).attributes[static_cast<std::size_t>(index_)].text;
            initial_.height = t.height;
            plain(QString::fromStdString(t.displayText()));
            break;
        }
        case Kind::Dimension:
            plain(QString::fromStdString(static_cast<const DimensionEntity&>(*original_).displayText()));
            break;
        case Kind::MText: {
            const auto& m = static_cast<const MTextEntity&>(*original_);
            loadRich(doc, m, QString::fromStdString(m.text));
            break;
        }
        case Kind::Leader: {
            const auto& l = static_cast<const LeaderEntity&>(*original_);
            LeaderEntity probe = l;
            if (probe.text.empty())
                probe.text = "x";
            if (auto a = probe.annotation())
                loadRich(doc, *a, QString::fromStdString(l.text));
            break;
        }
        case Kind::TableCell: {
            const auto& t = static_cast<const TableEntity&>(*original_);
            if (auto m = t.cellText(row_, col_))
                loadRich(doc, *m, QString::fromStdString(m->text));
            break;
        }
        }
    }

    void loadRich(const Document& doc, const MTextEntity& m, const QString& source)
    {
        initial_ = mtextInitialFormat(doc, m);
        const TextStyle& style = doc.textStyleOrStandard(m.style);
        family_ = style.family.empty() ? QStringLiteral("Arial") : QString::fromStdString(style.family);
        prefix_ = layoutPrefix(source);
        chars_ = charsFromMText(source, initial_);
    }

    QString plainText() const
    {
        QString s;
        for (const EdChar& c : chars_)
            s += c.c;
        return s;
    }

    // Allineamento del paragrafo che comincia in `i` (il suo primo carattere, o l'a capo prima se è vuoto).
    int paragraphAlign(std::size_t i) const
    {
        if (i < chars_.size() && chars_[i].c != QLatin1Char('\n'))
            return chars_[i].f.align;
        if (i > 0 && i - 1 < chars_.size())
            return chars_[i - 1].f.align;
        return hasTyping_ ? typing_.align : initial_.align;
    }

    QString serialized() const
    {
        QString out = prefix_;
        std::size_t i = 0;
        int emitted = -1;   // allineamento già scritto (vale per i paragrafi seguenti, come nei DXF)
        const auto alignCode = [&](std::size_t at) {
            const int a = paragraphAlign(at);
            if (a >= 0 && a != emitted) {
                static const char letters[] = "lcrjd";
                out += QStringLiteral("\\pxq") + QLatin1Char(letters[a]) + QLatin1Char(';');
                emitted = a;
            }
        };
        if (!chars_.empty())
            alignCode(0);
        while (i < chars_.size()) {
            if (chars_[i].c == QLatin1Char('\n')) {
                out += QStringLiteral("\\P");
                ++i;
                alignCode(i);
                continue;
            }
            std::size_t j = i;
            QString text;
            while (j < chars_.size() && chars_[j].c != QLatin1Char('\n') && chars_[j].f.sameAs(chars_[i].f))
                text += chars_[j++].c;
            const QString codes = formatCodes(chars_[i].f, initial_, family_);
            out += codes.isEmpty() ? escapeMText(text) : QLatin1Char('{') + codes + escapeMText(text) + QLatin1Char('}');
            i = j;
        }
        return out;
    }

    // Il testo modificato dentro l'oggetto.
    void applyTo(Entity& e) const
    {
        switch (kind_) {
        case Kind::Text:
            static_cast<TextEntity&>(e).text = encodeSymbols(plainText());
            break;
        case Kind::Attribute:
            static_cast<InsertEntity&>(e).attributes[static_cast<std::size_t>(index_)].text.text = encodeSymbols(plainText());
            break;
        case Kind::Dimension: {
            auto& d = static_cast<DimensionEntity&>(e);
            DimensionEntity measured = d;
            measured.textOverride.clear();
            const std::string value = measured.displayText();
            const std::string text = plainText().toStdString();
            if (text == value) {
                d.textOverride.clear();   // di nuovo la misura (associativa)
            } else if (const auto pos = text.find(value); !value.empty() && pos != std::string::npos) {
                d.textOverride = text.substr(0, pos) + "<>" + text.substr(pos + value.size());
            } else {
                d.textOverride = text;
            }
            d.block.reset();   // il blocco della quota (dal file) va ricostruito con il testo nuovo
            d.blockName.clear();
            break;
        }
        case Kind::MText: {
            auto& m = static_cast<MTextEntity&>(e);
            m.text = serialized().toStdString();
            if (!entityStyle_.isEmpty())
                m.style = entityStyle_.toStdString();
            if (attachment_ > 0)
                m.attachment = attachment_;
            if (lineSpacing_ > 0.0)
                m.lineSpacing = lineSpacing_;
            if (mask_)
                m.backgroundFill = *mask_;
            if (width_) {
                m.width = *width_;
                m.position = widthPosition_;
            }
            if (columns_ > 0) {
                m.columns = columns_;
                if (m.columns > 1 && m.width <= 0.0)   // le colonne vogliono una larghezza: quella del testo di adesso
                    m.width = std::max(m.height * 10.0, std::abs(geom_.frame.size() >= 4 ? QLineF(geom_.frame[3], geom_.frame[2]).length() : 0.0));
            }
            if (annotative_ && drawing_) {
                if (*annotative_ && !m.annotative())
                    drawing_->document().markAnnotative(m);
                else if (!*annotative_) {
                    m.setAnnoScales({});
                    m.setAnnoFactor(1.0);
                }
            }
            break;
        }
        case Kind::Leader:
            static_cast<LeaderEntity&>(e).text = serialized().toStdString();
            break;
        case Kind::TableCell:
            static_cast<TableEntity&>(e).cell(row_, col_) = serialized().toStdString();
            break;
        }
    }

    // Anteprima (oggetto com'è ora) e posizione delle lettere.
    void rebuild()
    {
        if (!drawing_)
            return;
        const Document& doc = drawing_->document();
        preview_ = original_->clone();
        if (changed_)
            applyTo(*preview_);
        geom_ = {};
        switch (kind_) {
        case Kind::Text:
            geom_ = caretGeometry(doc, static_cast<const TextEntity&>(*preview_));
            break;
        case Kind::Attribute:
            geom_ = caretGeometry(doc, static_cast<const InsertEntity&>(*preview_).attributes[static_cast<std::size_t>(index_)].text);
            break;
        case Kind::Dimension:
            for (const auto& part : static_cast<const DimensionEntity&>(*preview_).geometry()) {
                if (part->type() == EntityType::Text) {
                    geom_ = caretGeometry(doc, static_cast<const TextEntity&>(*part));
                    break;
                }
                if (part->type() == EntityType::MText) {
                    geom_ = caretGeometry(doc, static_cast<const MTextEntity&>(*part));
                    break;
                }
            }
            if (geom_.stops.empty()) {
                const Vec2 p = static_cast<const DimensionEntity&>(*preview_).textPos;
                geom_.stops.push_back({0, QPointF(p.x, p.y), QPointF(p.x, p.y + 2.5), 0});
                geom_.frame = QPolygonF(QRectF(QPointF(p.x - 2, p.y - 1), QPointF(p.x + 2, p.y + 3)));
            }
            break;
        case Kind::MText:
            geom_ = caretGeometry(doc, static_cast<const MTextEntity&>(*preview_));
            break;
        case Kind::Leader: {
            LeaderEntity probe = static_cast<const LeaderEntity&>(*preview_);
            const bool empty = probe.text.empty();
            if (empty)
                probe.text = "x";   // posizione del testo che non c'è ancora
            if (auto a = probe.annotation())
                geom_ = caretGeometry(doc, *a);
            if (empty) {
                geom_.boxes.clear();
                if (geom_.stops.size() > 1)
                    geom_.stops.resize(1);
            }
            break;
        }
        case Kind::TableCell:
            if (auto m = static_cast<const TableEntity&>(*preview_).cellText(row_, col_))
                geom_ = caretGeometry(doc, *m);
            break;
        }
        spellErrors_ = SpellChecker::instance().errors(plainText());
        setProperty("spellErrors", static_cast<int>(spellErrors_.size()));
        setProperty("editorText", plainText());   // per le prove automatiche
        setProperty("editorSource", serialized());
        placeWidgets();
        if (view_)
            view_->update();
    }

    // ------------------------------------------------------------ modifica

    MTextFormat typingFormat() const
    {
        if (hasTyping_)
            return typing_;
        const int size = static_cast<int>(chars_.size());
        if (caret_ > 0 && chars_[static_cast<std::size_t>(caret_ - 1)].c != QLatin1Char('\n'))
            return chars_[static_cast<std::size_t>(caret_ - 1)].f;
        if (caret_ < size && chars_[static_cast<std::size_t>(caret_)].c != QLatin1Char('\n'))
            return chars_[static_cast<std::size_t>(caret_)].f;
        if (caret_ > 0)
            return chars_[static_cast<std::size_t>(caret_ - 1)].f;
        return initial_;
    }

    void pushUndo()
    {
        undo_.push_back({chars_, caret_, anchor_});
        if (undo_.size() > 300)
            undo_.erase(undo_.begin());
        redo_.clear();
    }

    void replaceSelection(const QString& text)
    {
        const MTextFormat format = typingFormat();
        pushUndo();
        const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        chars_.erase(chars_.begin() + a, chars_.begin() + b);
        caret_ = a;
        QString clean = text;
        clean.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).remove(QLatin1Char('\r'));
        if (!multiline_)
            clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
        for (const QChar c : clean)
            chars_.insert(chars_.begin() + caret_++, EdChar{c, format});
        anchor_ = caret_;
        changed_ = true;
        caretOn_ = true;
        rebuild();
        syncBar();
    }

    void restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to)
    {
        if (from.empty())
            return;
        to.push_back({chars_, caret_, anchor_});
        const Snapshot s = from.back();
        from.pop_back();
        chars_ = s.chars;
        caret_ = s.caret;
        anchor_ = s.anchor;
        changed_ = true;
        rebuild();
        syncBar();
    }

    void undo() { restore(undo_, redo_); }
    void redo() { restore(redo_, undo_); }

    void copy() const
    {
        const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        if (a == b)
            return;
        QString s;
        for (int i = a; i < b; ++i)
            s += chars_[static_cast<std::size_t>(i)].c;
        QGuiApplication::clipboard()->setText(s);
    }

    void paste()
    {
        const QString s = QGuiApplication::clipboard()->text();
        if (!s.isEmpty())
            replaceSelection(s);
    }

    // Formato della selezione (o di quello che si scriverà, senza selezione).
    void applyFormat(const std::function<void(MTextFormat&)>& change)
    {
        if (caret_ == anchor_) {
            typing_ = typingFormat();
            change(typing_);
            hasTyping_ = true;
        } else {
            pushUndo();
            const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
            for (int i = a; i < b; ++i)
                if (chars_[static_cast<std::size_t>(i)].c != QLatin1Char('\n'))
                    change(chars_[static_cast<std::size_t>(i)].f);
            changed_ = true;
            rebuild();
        }
        syncBar();
        setFocus();
    }

    bool selectionAll(const std::function<bool(const MTextFormat&)>& test) const
    {
        const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        if (a == b)
            return test(typingFormat());
        for (int i = a; i < b; ++i)
            if (chars_[static_cast<std::size_t>(i)].c != QLatin1Char('\n') && !test(chars_[static_cast<std::size_t>(i)].f))
                return false;
        return true;
    }

    void toggleBold()
    {
        const bool on = !selectionAll([](const MTextFormat& f) { return f.bold; });
        applyFormat([on](MTextFormat& f) { f.bold = on; });
    }

    void toggleItalic()
    {
        const bool on = !selectionAll([](const MTextFormat& f) { return f.italic; });
        applyFormat([on](MTextFormat& f) { f.italic = on; });
    }

    // ------------------------------------------------------------ comandi della scheda Editor di testo

    bool entityKind() const { return kind_ == Kind::MText; }
    const MTextEntity* mtextNow() const
    {
        return preview_ && preview_->type() == EntityType::MText ? static_cast<const MTextEntity*>(preview_.get()) : nullptr;
    }

    State state() const override
    {
        State s;
        s.entity = entityKind();
        const MTextFormat f = typingFormat();
        s.height = f.height;
        s.family = f.family.isEmpty() ? family_ : f.family;
        s.bold = selectionAll([](const MTextFormat& x) { return x.bold; });
        s.italic = selectionAll([](const MTextFormat& x) { return x.italic; });
        s.underline = selectionAll([](const MTextFormat& x) { return x.underline; });
        s.overline = selectionAll([](const MTextFormat& x) { return x.overline; });
        s.strike = selectionAll([](const MTextFormat& x) { return x.strike; });
        s.aci = f.aci;
        s.rgb = f.rgb;
        s.align = paragraphAlign(paragraphStart(caret_));
        s.ruler = ruler_;
        s.selection = caret_ != anchor_;
        if (const MTextEntity* m = mtextNow()) {
            s.style = QString::fromStdString(m->style);
            s.annotative = m->annotative();
            s.mask = m->backgroundFill;
            s.attachment = m->attachment;
            s.lineSpacing = m->lineSpacing;
            s.columns = m->columns;
        }
        s.spell = SpellChecker::instance().enabled();
        s.spellAvailable = SpellChecker::instance().available();
        return s;
    }

    // Una modifica dell'oggetto (stile, giustificazione...): anteprima subito, conferma con il resto.
    void entityChanged()
    {
        changed_ = true;
        rebuild();
        syncBar();
        setFocus();
    }

    void setStyle(const QString& name) override
    {
        if (!entityKind())
            return;
        entityStyle_ = name;
        entityChanged();
    }
    void setAnnotative(bool on) override
    {
        if (!entityKind())
            return;
        annotative_ = on;
        entityChanged();
    }
    void setMask(bool on) override
    {
        if (!entityKind())
            return;
        mask_ = on;
        entityChanged();
    }
    void setHeight(double h) override
    {
        if (h > 0.0)
            applyFormat([h](MTextFormat& f) { f.height = h; });
    }
    void setFamily(const QString& family) override
    {
        applyFormat([family](MTextFormat& f) {
            f.family = family;
            f.shxFile.clear();
        });
    }
    void toggle(Flag flag) override
    {
        const auto field = [flag](MTextFormat& f) -> bool& {
            switch (flag) {
            case Flag::Bold: return f.bold;
            case Flag::Italic: return f.italic;
            case Flag::Underline: return f.underline;
            case Flag::Overline: return f.overline;
            default: return f.strike;
            }
        };
        const bool on = !selectionAll([&field](const MTextFormat& f) {
            MTextFormat copy = f;
            return field(copy);
        });
        applyFormat([&field, on](MTextFormat& f) { field(f) = on; });
    }
    void setColor(int aci, long long rgb) override
    {
        applyFormat([aci, rgb](MTextFormat& f) {
            f.aci = aci;
            f.rgb = rgb;
        });
    }
    void clearFormatting() override
    {
        const MTextFormat base = initial_;
        applyFormat([base](MTextFormat& f) {
            const int align = f.align;
            f = base;
            f.align = align;
        });
    }
    void changeCase(bool upper) override
    {
        if (caret_ == anchor_)
            return;
        pushUndo();
        const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        for (int i = a; i < b; ++i) {
            QChar& c = chars_[static_cast<std::size_t>(i)].c;
            c = upper ? c.toUpper() : c.toLower();
        }
        changed_ = true;
        rebuild();
        syncBar();
        setFocus();
    }

    // Inizio del paragrafo che contiene `i` e fine (indice dell'a capo che lo chiude, o la fine del testo).
    int paragraphStart(int i) const
    {
        i = std::clamp(i, 0, static_cast<int>(chars_.size()));
        while (i > 0 && chars_[static_cast<std::size_t>(i - 1)].c != QLatin1Char('\n'))
            --i;
        return i;
    }
    int paragraphEnd(int i) const
    {
        const int size = static_cast<int>(chars_.size());
        i = std::clamp(i, 0, size);
        while (i < size && chars_[static_cast<std::size_t>(i)].c != QLatin1Char('\n'))
            ++i;
        return i;
    }
    // Inizi dei paragrafi toccati dalla selezione (o quello del cursore).
    std::vector<int> selectedParagraphs() const
    {
        std::vector<int> starts;
        const int a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        for (int p = paragraphStart(a);;) {
            starts.push_back(p);
            const int end = paragraphEnd(p);
            if (end >= b || end >= static_cast<int>(chars_.size()))
                break;
            p = end + 1;
        }
        return starts;
    }

    void setAlign(int align) override
    {
        if (!multiline_)
            return;
        pushUndo();
        for (int start : selectedParagraphs()) {
            const int end = paragraphEnd(start);
            for (int i = start; i < end; ++i)
                chars_[static_cast<std::size_t>(i)].f.align = align;
            if (start > 0)   // l'a capo prima: vale per il paragrafo vuoto
                chars_[static_cast<std::size_t>(start - 1)].f.align = align;
        }
        typing_ = typingFormat();
        typing_.align = align;
        hasTyping_ = true;
        changed_ = true;
        rebuild();
        syncBar();
        setFocus();
    }
    void setAttachment(int attachment) override
    {
        if (!entityKind() || attachment < 1 || attachment > 9)
            return;
        attachment_ = attachment;
        entityChanged();
    }
    void setLineSpacing(double factor) override
    {
        if (!entityKind() || factor <= 0.0)
            return;
        lineSpacing_ = factor;
        entityChanged();
    }
    void setList(int kind) override
    {
        if (!multiline_)
            return;
        pushUndo();
        const std::vector<int> starts = selectedParagraphs();
        static const QRegularExpression marker(QStringLiteral(R"(^(\x{2022} |\d+\. |[a-z]\. ))"));
        int number = 0;
        // Dal fondo: gli inizi dei paragrafi prima non si spostano.
        std::vector<QString> prefixes;
        for (std::size_t k = 0; k < starts.size(); ++k)
            prefixes.push_back(kind == 1 ? QStringLiteral("\u2022 ")
                                         : kind == 2 ? QString::number(++number) + QStringLiteral(". ")
                                         : kind == 3 ? QString(QChar(u'a' + static_cast<char16_t>(number++ % 26))) + QStringLiteral(". ")
                                                     : QString());
        for (std::size_t k = starts.size(); k-- > 0;) {
            const int start = starts[k];
            QString head;
            for (int i = start; i < paragraphEnd(start) && head.size() < 8; ++i)
                head += chars_[static_cast<std::size_t>(i)].c;
            const auto m = marker.match(head);
            if (m.hasMatch())
                chars_.erase(chars_.begin() + start, chars_.begin() + start + m.capturedLength());
            const MTextFormat f = start < static_cast<int>(chars_.size()) ? chars_[static_cast<std::size_t>(start)].f : typingFormat();
            const QString& prefix = prefixes[k];
            for (int i = 0; i < prefix.size(); ++i)
                chars_.insert(chars_.begin() + start + i, EdChar{prefix[i], f});
        }
        caret_ = anchor_ = std::min(caret_, static_cast<int>(chars_.size()));
        changed_ = true;
        rebuild();
        syncBar();
        setFocus();
    }
    void insertText(const QString& text) override
    {
        replaceSelection(text);
        setFocus();
    }
    int replaceAll(const QString& find, const QString& replace, bool matchCase) override
    {
        if (find.isEmpty())
            return 0;
        const Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int count = 0;
        pushUndo();
        for (int i = 0; i + find.size() <= static_cast<int>(chars_.size());) {
            QString here;
            for (int k = 0; k < find.size(); ++k)
                here += chars_[static_cast<std::size_t>(i + k)].c;
            if (here.compare(find, cs) != 0) {
                ++i;
                continue;
            }
            const MTextFormat f = chars_[static_cast<std::size_t>(i)].f;
            chars_.erase(chars_.begin() + i, chars_.begin() + i + find.size());
            for (int k = 0; k < replace.size(); ++k)
                chars_.insert(chars_.begin() + i + k, EdChar{replace[k], f});
            i += static_cast<int>(replace.size());
            ++count;
        }
        if (count == 0) {
            undo_.pop_back();
        } else {
            caret_ = anchor_ = std::min(caret_, static_cast<int>(chars_.size()));
            changed_ = true;
            rebuild();
            syncBar();
        }
        setFocus();
        return count;
    }
    void setRuler(bool on) override
    {
        ruler_ = on;
        syncBar();
        if (view_)
            view_->update();
        setFocus();
    }
    void setColumns(int count) override
    {
        if (!entityKind() || count < 1)
            return;
        columns_ = count;
        entityChanged();
    }
    void setSpellCheck(bool on) override
    {
        SpellChecker::instance().setEnabled(on);
        rebuild();
        syncBar();
        setFocus();
    }
    void close(bool apply) override { finish(apply); }
    void refocus() override
    {
        if (!done_)
            setFocus();
    }

    // ------------------------------------------------------------ righello (testo multilinea)

    // Lato superiore del riquadro sullo schermo (da sinistra a destra) e la sua normale verso l'alto.
    bool rulerGeometry(QPointF& left, QPointF& right, QPointF& up) const
    {
        if (!entityKind() || !ruler_ || !view_ || geom_.frame.size() < 4)
            return false;
        left = toScreen(geom_.frame[3]);
        right = toScreen(geom_.frame[2]);
        const QPointF d = right - left;
        const double len = std::hypot(d.x(), d.y());
        if (len < 1.0)
            return false;
        up = QPointF(d.y() / len, -d.x() / len);
        return true;
    }

    QPointF rulerHandle() const
    {
        QPointF l, r, up;
        return rulerGeometry(l, r, up) ? r + up * 8.0 : QPointF(-1e6, -1e6);
    }

    bool onRuler(QPointF screen) const
    {
        QPointF l, r, up;
        if (!rulerGeometry(l, r, up))
            return false;
        const QPointF d = r - l;
        const double len = std::hypot(d.x(), d.y());
        const QPointF dir = d / len;
        const QPointF rel = screen - l;
        const double along = QPointF::dotProduct(rel, dir);
        const double across = QPointF::dotProduct(rel, up);
        return along >= -6.0 && along <= len + 10.0 && across >= 0.0 && across <= 18.0;
    }

    void paintRuler(QPainter& p) const
    {
        QPointF l, r, up;
        if (!rulerGeometry(l, r, up))
            return;
        const QPointF d = r - l;
        const double len = std::hypot(d.x(), d.y());
        const QPointF dir = d / len;
        QPolygonF band{l, r, r + up * 16.0, l + up * 16.0};
        p.setPen(QPen(QColor(120, 128, 140), 1.0));
        p.setBrush(QColor(52, 58, 68, 235));
        p.drawPolygon(band);
        // Tacche ogni mezza altezza del testo, più lunghe ogni cinque.
        const MTextEntity* m = mtextNow();
        const double worldLen = std::hypot(geom_.frame[2].x() - geom_.frame[3].x(), geom_.frame[2].y() - geom_.frame[3].y());
        const double step = m ? m->height * 0.5 : worldLen / 20.0;
        const double pxStep = worldLen > 0.0 ? len * step / worldLen : len;
        if (pxStep >= 2.5) {
            p.setPen(QPen(QColor(200, 205, 214), 1.0));
            int k = 0;
            for (double x = 0.0; x <= len + 0.5; x += pxStep, ++k) {
                const double t = k % 5 == 0 ? 7.0 : 3.5;
                p.drawLine(l + dir * x + up * 2.0, l + dir * x + up * (2.0 + t));
            }
        }
        // Maniglia della larghezza: un rombo alla fine del righello.
        const QPointF h = rulerHandle();
        p.setPen(QPen(QColor(20, 40, 80), 1.0));
        p.setBrush(QColor(61, 139, 253));
        p.drawPolygon(QPolygonF{h + QPointF(0, -6), h + QPointF(6, 0), h + QPointF(0, 6), h + QPointF(-6, 0)});
    }

    // Trascinamento della maniglia: la larghezza nuova (il lato sinistro resta fermo).
    void dragRuler(QPointF screen)
    {
        const MTextEntity* m = mtextNow();
        if (!m || !view_)
            return;
        MTextEntity probe = *m;
        const Vec2 w = view_->worldFromScreen(screen);
        probe.dragWidth(true, w);
        width_ = probe.width;
        widthPosition_ = probe.position;
        changed_ = true;
        rebuild();
        syncBar();
    }

    // ------------------------------------------------------------ posizioni

    QPolygonF toScreen(const QPolygonF& world) const
    {
        QPolygonF out;
        for (const QPointF& w : world)
            out << view_->screenPoint({w.x(), w.y()});
        return out;
    }

    QPointF toScreen(QPointF w) const { return view_->screenPoint({w.x(), w.y()}); }

    const TextCaretGeometry::Stop* stopFor(int index) const
    {
        const TextCaretGeometry::Stop* best = nullptr;
        for (const auto& s : geom_.stops) {
            if (s.index == index)
                return &s;
            if (s.index < index && (!best || s.index >= best->index))
                best = &s;
        }
        return best ? best : (geom_.stops.empty() ? nullptr : &geom_.stops.front());
    }

    int nearestIndex(QPointF screen) const
    {
        int best = static_cast<int>(chars_.size());
        double bestDistance = 1e300;
        for (const auto& s : geom_.stops) {
            const QPointF a = toScreen(s.bottom), b = toScreen(s.top);
            const QPointF ab = b - a;
            const double len2 = QPointF::dotProduct(ab, ab);
            const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(screen - a, ab) / len2, 0.0, 1.0) : 0.0;
            const QPointF q = a + ab * t;
            const double d = std::hypot(screen.x() - q.x(), screen.y() - q.y());
            if (d < bestDistance) {
                bestDistance = d;
                best = s.index;
            }
        }
        return std::clamp(best, 0, static_cast<int>(chars_.size()));
    }

    bool insideFrame(QPointF screen) const
    {
        if (!view_ || geom_.frame.isEmpty())
            return false;
        return toScreen(geom_.frame).boundingRect().adjusted(-8, -8, 8, 8).contains(screen) || onRuler(screen);
    }

    int wordLeft(int i) const
    {
        while (i > 0 && !wordChar(chars_[static_cast<std::size_t>(i - 1)].c))
            --i;
        while (i > 0 && wordChar(chars_[static_cast<std::size_t>(i - 1)].c))
            --i;
        return i;
    }

    int wordRight(int i) const
    {
        const int size = static_cast<int>(chars_.size());
        while (i < size && wordChar(chars_[static_cast<std::size_t>(i)].c))
            ++i;
        while (i < size && !wordChar(chars_[static_cast<std::size_t>(i)].c))
            ++i;
        return i;
    }

    int lineMove(int direction) const
    {
        const auto* current = stopFor(caret_);
        if (!current)
            return caret_;
        const int line = current->line + direction;
        int best = caret_;
        double bestDistance = 1e300;
        for (const auto& s : geom_.stops) {
            if (s.line != line)
                continue;
            const double d = std::hypot(s.bottom.x() - current->bottom.x(), s.bottom.y() - current->bottom.y());
            if (d < bestDistance) {
                bestDistance = d;
                best = s.index;
            }
        }
        if (bestDistance == 1e300)   // prima o ultima riga: all'inizio o alla fine
            return direction < 0 ? 0 : static_cast<int>(chars_.size());
        return best;
    }

    int lineEdge(bool end) const
    {
        const auto* current = stopFor(caret_);
        if (!current)
            return caret_;
        int edge = caret_;
        bool first = true;
        for (const auto& s : geom_.stops)
            if (s.line == current->line) {
                if (first || (end ? s.index > edge : s.index < edge))
                    edge = s.index;
                first = false;
            }
        return edge;
    }

    void caretMoved()
    {
        hasTyping_ = false;
        caretOn_ = true;
        blink_.start();
        placeWidgets();
        syncBar();
        if (view_)
            view_->update();
    }

    // ------------------------------------------------------------ barra e posizione

    void createBar()
    {
        bar_ = new QFrame(view_->overlayHost());
        bar_->setObjectName(QStringLiteral("TextEditorBar"));
        bar_->setStyleSheet(QStringLiteral(
            "QFrame#TextEditorBar { background: rgba(38, 44, 54, 245); border: 1px solid #5a6476; border-radius: 3px; }"
            "QToolButton { color: #e6e9ee; padding: 2px 7px; border: 1px solid transparent; border-radius: 2px; }"
            "QToolButton:hover { border-color: #5a6476; }"
            "QToolButton:checked { background: #3d8bfd; }"));
        auto* layout = new QHBoxLayout(bar_);
        layout->setContentsMargins(5, 3, 5, 3);
        layout->setSpacing(4);
        font_ = new QFontComboBox(bar_);
        font_->setMaximumWidth(170);
        font_->setFocusPolicy(Qt::ClickFocus);
        font_->setToolTip(tr("Carattere della parte selezionata"));
        height_ = new QDoubleSpinBox(bar_);
        height_->setDecimals(3);
        height_->setRange(0.001, 100000.0);
        height_->setMaximumWidth(90);
        height_->setToolTip(tr("Altezza del testo selezionato"));
        bold_ = new QToolButton(bar_);
        bold_->setText(tr("G"));
        QFont bf = bold_->font();
        bf.setBold(true);
        bold_->setFont(bf);
        bold_->setCheckable(true);
        bold_->setToolTip(tr("Grassetto (Ctrl+B)"));
        italic_ = new QToolButton(bar_);
        italic_->setText(tr("C"));
        QFont itf = italic_->font();
        itf.setItalic(true);
        italic_->setFont(itf);
        italic_->setCheckable(true);
        italic_->setToolTip(tr("Corsivo (Ctrl+I)"));
        color_ = new QToolButton(bar_);
        color_->setText(tr("Colore"));
        color_->setPopupMode(QToolButton::InstantPopup);
        auto* colors = new QMenu(color_);
        const auto setColor = [this](int aci, long long rgb) {
            applyFormat([aci, rgb](MTextFormat& f) {
                f.aci = aci;
                f.rgb = rgb;
            });
        };
        colors->addAction(tr("Dell'oggetto"), this, [setColor] { setColor(0, -1); });
        const std::pair<int, const char*> basic[] = {{1, "Rosso"}, {2, "Giallo"}, {3, "Verde"}, {4, "Ciano"},
                                                     {5, "Blu"},   {6, "Magenta"}, {7, "Bianco"}};
        for (const auto& [aci, name] : basic)
            colors->addAction(tr(name), this, [setColor, aci = aci] { setColor(aci, -1); });
        colors->addSeparator();
        colors->addAction(tr("Altri colori..."), this, [this, setColor] {
            const int current = typingFormat().aci;
            if (const auto picked = ColorDialog::pick(current > 0 ? current : 7, false, this))
                setColor(*picked, -1);
            setFocus();
        });
        color_->setMenu(colors);
        auto* ok = new QToolButton(bar_);
        ok->setText(tr("OK"));
        ok->setToolTip(tr("Chiude la modifica e conferma (Ctrl+Invio o clic fuori dal testo)"));
        for (QWidget* w : std::initializer_list<QWidget*>{font_, height_, bold_, italic_, color_, ok})
            layout->addWidget(w);

        connect(font_, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
            applyFormat([family = font.family()](MTextFormat& f) {
                f.family = family;
                f.shxFile.clear();
            });
        });
        connect(height_, &QDoubleSpinBox::editingFinished, this, [this] {
            const double h = height_->value();
            if (std::abs(h - typingFormat().height) > 1e-9)
                applyFormat([h](MTextFormat& f) { f.height = h; });
            setFocus();
        });
        connect(bold_, &QToolButton::clicked, this, [this] { toggleBold(); });
        connect(italic_, &QToolButton::clicked, this, [this] { toggleItalic(); });
        connect(ok, &QToolButton::clicked, this, [this] { finish(true); });
        bar_->adjustSize();
        bar_->show();
    }

    void syncBar()
    {
        if (rich_)
            TextEditorHub::instance().notify();
        if (!bar_)
            return;
        const MTextFormat f = typingFormat();
        const QSignalBlocker b1(font_), b2(height_), b3(bold_), b4(italic_);
        font_->setCurrentFont(QFont(f.family.isEmpty() ? family_ : f.family));
        height_->setValue(f.height);
        bold_->setChecked(selectionAll([](const MTextFormat& x) { return x.bold; }));
        italic_->setChecked(selectionAll([](const MTextFormat& x) { return x.italic; }));
    }

    // Il widget dei tasti sul cursore (per l'input dei caratteri accentati) e la barra sopra il testo.
    void placeWidgets()
    {
        if (!view_ || done_)
            return;
        const QPoint offset = view_->overlayOffset();
        if (const auto* s = stopFor(caret_))
            move(offset + toScreen(s->bottom).toPoint());
        if (bar_) {
            const QRectF r = toScreen(geom_.frame).boundingRect();
            bar_->adjustSize();
            QPoint at = offset + QPoint(static_cast<int>(r.left()), static_cast<int>(r.top()) - bar_->height() - 6);
            if (at.y() < offset.y())   // non c'è posto sopra: sotto il testo
                at.setY(offset.y() + static_cast<int>(r.bottom()) + 6);
            const QWidget* host = view_->overlayHost();
            at.setX(std::clamp(at.x(), 0, std::max(0, host->width() - bar_->width())));
            at.setY(std::clamp(at.y(), 0, std::max(0, host->height() - bar_->height())));
            bar_->move(at);
            bar_->raise();
        }
    }

    void showMenu(QPoint global, int at = -1)
    {
        QMenu menu(this);
        // Parola sbagliata sotto il clic: i suggerimenti del correttore, aggiungi al dizionario, ignora.
        for (const auto& [start, length] : spellErrors_) {
            if (at < start || at > start + length)
                continue;
            QString word;
            for (int i = start; i < start + length && i < static_cast<int>(chars_.size()); ++i)
                word += chars_[static_cast<std::size_t>(i)].c;
            const QStringList suggestions = SpellChecker::instance().suggestions(word);
            for (const QString& s : suggestions) {
                QAction* a = menu.addAction(s, this, [this, start = start, length = length, s] {
                    anchor_ = start;
                    caret_ = start + length;
                    replaceSelection(s);
                });
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
            }
            if (suggestions.isEmpty())
                menu.addAction(tr("(nessun suggerimento)"))->setEnabled(false);
            menu.addAction(tr("Aggiungi al dizionario"), this, [this, word] {
                SpellChecker::instance().addWord(word);
                rebuild();
            });
            menu.addAction(tr("Ignora"), this, [this, word] {
                SpellChecker::instance().ignoreWord(word);
                rebuild();
            });
            menu.addSeparator();
            break;
        }
        menu.addAction(tr("Annulla"), this, [this] { undo(); })->setEnabled(!undo_.empty());
        menu.addAction(tr("Ripeti"), this, [this] { redo(); })->setEnabled(!redo_.empty());
        menu.addSeparator();
        const bool selected = caret_ != anchor_;
        menu.addAction(tr("Taglia"), this, [this] {
            copy();
            replaceSelection(QString());
        })->setEnabled(selected);
        menu.addAction(tr("Copia"), this, [this] { copy(); })->setEnabled(selected);
        menu.addAction(tr("Incolla"), this, [this] { paste(); });
        menu.addAction(tr("Seleziona tutto"), this, [this] {
            anchor_ = 0;
            caret_ = static_cast<int>(chars_.size());
            caretMoved();
        });
        menu.addSeparator();
        menu.addAction(tr("Chiudi e conferma"), this, [this] { finish(true); });
        menu.addAction(tr("Chiudi senza salvare"), this, [this] { finish(false); });
        menu.exec(global);
        if (!done_)
            setFocus();
    }

    // Tab nelle tabelle: conferma la cella e passa alla successiva (Maiusc+Tab alla precedente).
    void nextCell(int direction)
    {
        const auto& table = static_cast<const TableEntity&>(*original_);
        const int index = row_ * table.columns + col_ + direction;
        const int columns = table.columns;
        if (index < 0 || index >= table.rows * table.columns) {
            finish(true);
            return;
        }
        QPointer<DrawingWidget> drawing = drawing_;
        const EntityId id = id_;
        finish(true);
        if (drawing)
            startCell(*drawing, id, index / columns, index % columns);
    }

    QPointer<DrawingWidget> drawing_;
    QPointer<CadView> view_;
    EntityId id_;
    Kind kind_;
    int index_ = 0;   // attributo
    int row_ = 0, col_ = 0;
    std::unique_ptr<Entity> original_;
    std::unique_ptr<Entity> preview_;
    TextCaretGeometry geom_;

    std::vector<EdChar> chars_;
    MTextFormat initial_;
    QString family_ = QStringLiteral("Arial");
    QString prefix_;
    MTextFormat typing_;
    bool hasTyping_ = false;
    bool rich_ = false;
    bool multiline_ = false;
    bool changed_ = false;
    bool done_ = false;

public:
    bool newObject_ = false;   // testo appena creato da TEXT o MTEXT (vedi finish)

private:
    bool dragging_ = false;
    bool caretOn_ = true;
    int caret_ = 0;
    int anchor_ = 0;
    std::vector<Snapshot> undo_, redo_;
    QTimer blink_;
    QTimer reposition_;

    QFrame* bar_ = nullptr;
    QFontComboBox* font_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QToolButton* bold_ = nullptr;
    QToolButton* italic_ = nullptr;
    QToolButton* color_ = nullptr;

    // Impostazioni dell'oggetto cambiate dalla scheda Editor di testo (testo multilinea).
    QString entityStyle_;
    int attachment_ = 0;
    double lineSpacing_ = 0.0;
    std::optional<bool> mask_;
    std::optional<bool> annotative_;
    std::optional<double> width_;
    Vec2 widthPosition_;
    bool ruler_ = true;
    bool rulerDrag_ = false;
    int columns_ = 0;   // 0 = invariate
    std::vector<std::pair<int, int>> spellErrors_;   // parole sbagliate (inizio, lunghezza) nei caratteri
};

} // namespace

bool canEditTextInPlace(const Entity& e)
{
    switch (e.type()) {
    case EntityType::Text:
    case EntityType::MText:
    case EntityType::Dimension:
    case EntityType::Table:
        return true;
    case EntityType::Leader:
        return !static_cast<const LeaderEntity&>(e).text.empty();
    case EntityType::Insert:
        return !static_cast<const InsertEntity&>(e).attributes.empty();
    default:
        return false;
    }
}

int attributeAt(const InsertEntity& insert, Vec2 p, double tolerance)
{
    int best = -1;
    double bestDistance = tolerance;
    for (std::size_t i = 0; i < insert.attributes.size(); ++i) {
        const Attribute& a = insert.attributes[i];
        if (a.flags & AttInvisible)
            continue;
        const double d = a.text.distanceTo(p);
        if (d <= bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

TextEditorHub& TextEditorHub::instance()
{
    static TextEditorHub hub;
    return hub;
}

bool editNewTextInPlace(DrawingWidget& drawing, EntityId id)
{
    const Entity* e = drawing.document().find(id);
    if (!e || (e->type() != EntityType::Text && e->type() != EntityType::MText) || drawing.view()->textEditor())
        return false;
    auto* editor = new InPlaceTextEditor(drawing, id, e->type() == EntityType::Text ? Kind::Text : Kind::MText, 0, 0, std::nullopt);
    editor->newObject_ = true;
    return true;
}

bool editTextInPlace(DrawingWidget& drawing, EntityId id, std::optional<Vec2> at)
{
    const Entity* e = drawing.document().find(id);
    if (!e || !canEditTextInPlace(*e) || drawing.view()->textEditor())
        return false;
    switch (e->type()) {
    case EntityType::Text:
        new InPlaceTextEditor(drawing, id, Kind::Text, 0, 0, at);
        return true;
    case EntityType::MText:
        new InPlaceTextEditor(drawing, id, Kind::MText, 0, 0, at);
        return true;
    case EntityType::Leader:
        new InPlaceTextEditor(drawing, id, Kind::Leader, 0, 0, at);
        return true;
    case EntityType::Dimension:
        new InPlaceTextEditor(drawing, id, Kind::Dimension, 0, 0, at);
        return true;
    case EntityType::Insert: {
        const auto& insert = static_cast<const InsertEntity&>(*e);
        int index = at ? attributeAt(insert, *at, drawing.view()->pickTolerance()) : -1;
        if (!at)   // senza punto (TEXTEDIT): il primo attributo visibile
            for (std::size_t i = 0; i < insert.attributes.size() && index < 0; ++i)
                if (!(insert.attributes[i].flags & AttInvisible))
                    index = static_cast<int>(i);
        if (index < 0)
            return false;
        new InPlaceTextEditor(drawing, id, Kind::Attribute, index, 0, at);
        return true;
    }
    case EntityType::Table: {
        const auto& table = static_cast<const TableEntity&>(*e);
        const auto cell = at ? table.cellAt(*at) : std::pair<int, int>{0, 0};
        if (cell.first < 0 || cell.second < 0)
            return false;
        InPlaceTextEditor::startCell(drawing, id, cell.first, cell.second);
        return true;
    }
    default:
        return false;
    }
}

} // namespace cartesio
