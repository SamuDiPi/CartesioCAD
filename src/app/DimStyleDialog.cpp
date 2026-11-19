#include "app/DimStyleDialog.h"

#include "app/DrawingWidget.h"
#include "core/Document.h"

#include <QMessageBox>

namespace cartesio {

DimStyleDialog::DimStyleDialog(DrawingWidget* drawing, QWidget* parent)
    : FormDialog(tr("Gestione stili di quota"), Close, parent), drawing_(drawing)
{
    group(tr("Stili di quota del disegno"));
    currentNote_ = note(QString());
    list_ = list({}, -1, [this](int index) { selected_ = index; }, [this](int) { modifyStyle(); });
    buttonsRow({{tr("Imposta corrente"), [this] { makeCurrent(); }},
                {tr("Nuovo..."), [this] { newStyle(); }},
                {tr("Modifica..."), [this] { modifyStyle(); }}});
    note(tr("Doppio clic su uno stile per modificarlo; le quote che lo usano si aggiornano."));
    refresh();
    setSize(460, 470);
}

void DimStyleDialog::refresh()
{
    const Document& doc = drawing_->document();
    const QString previous = selectedName();
    names_.clear();
    for (const DimStyle& s : doc.dimStyles())
        names_ << QString::fromStdString(s.name);
    const QString current = QString::fromStdString(doc.currentDimStyle().name);
    setFieldText(currentNote_, tr("Stile di quota corrente: <b>%1</b>").arg(current.toHtmlEscaped()));
    selected_ = static_cast<int>(names_.indexOf(previous.isEmpty() ? current : previous));
    setItems(list_, names_, selected_);
}

QString DimStyleDialog::selectedName() const
{
    return selected_ >= 0 && selected_ < names_.size() ? names_[selected_] : QString();
}

void DimStyleDialog::makeCurrent()
{
    if (selectedName().isEmpty())
        return;
    drawing_->document().setCurrentDimStyle(selectedName().toStdString());
    drawing_->notifyChanged();
    refresh();
}

void DimStyleDialog::newStyle()
{
    Document& doc = drawing_->document();
    QString name = tr("Copia di %1").arg(selectedName());
    {
        FormDialog ask(tr("Nuovo stile di quota"), Ok | Cancel, this);
        ask.text(tr("Nome del nuovo stile:"), name);
        ask.setSize(420, 150);
        if (ask.exec() != QDialog::Accepted || name.trimmed().isEmpty())
            return;
    }
    name = name.trimmed();
    if (doc.findDimStyle(name.toStdString())) {
        QMessageBox::information(this, windowTitle(), tr("Esiste già uno stile con questo nome."));
        return;
    }
    // Il nuovo stile parte da quello selezionato.
    DimStyle style = doc.findDimStyle(selectedName().toStdString()) ? *doc.findDimStyle(selectedName().toStdString())
                                                                    : DimStyle{};
    style.name = name.toStdString();
    if (!edit(style, this))
        return;
    doc.setDimStyle(style);
    doc.setCurrentDimStyle(style.name);
    drawing_->notifyChanged();
    selected_ = -1;
    names_.clear();
    refresh();
}

void DimStyleDialog::modifyStyle()
{
    Document& doc = drawing_->document();
    const DimStyle* existing = doc.findDimStyle(selectedName().toStdString());
    if (!existing)
        return;
    DimStyle style = *existing;
    if (!edit(style, this))
        return;
    const std::string name = style.name;
    doc.setDimStyle(std::move(style));
    const int updated = doc.restyleDimensions(name);
    drawing_->notifyChanged();
    if (updated > 0)
        QMessageBox::information(this, windowTitle(), tr("Quote aggiornate: %1.").arg(updated));
    refresh();
}

bool DimStyleDialog::edit(DimStyle& style, QWidget* parent)
{
    DimStyle copy = style;
    bool ticks = style.tickSize > 0.0;
    int decimals = style.decimals;
    QString post = QString::fromStdString(style.post);
    FormDialog dialog(QObject::tr("Modifica stile di quota: %1").arg(QString::fromStdString(style.name)),
                      FormDialog::Ok | FormDialog::Cancel, parent);
    dialog.group(QObject::tr("Testo e frecce"));
    dialog.number(QObject::tr("Altezza testo (DIMTXT):"), 0.01, 1000, 3, copy.textHeight);
    dialog.number(QObject::tr("Dimensione frecce (DIMASZ):"), 0.0, 1000, 3, copy.arrowSize);
    dialog.check(QObject::tr("Trattini obliqui al posto delle frecce (architettonico)"), ticks);
    dialog.number(QObject::tr("Distanza testo (DIMGAP):"), 0.0, 1000, 3, copy.textGap);
    dialog.group(QObject::tr("Linee di estensione"));
    dialog.number(QObject::tr("Distacco (DIMEXO):"), 0.0, 1000, 3, copy.extOffset);
    dialog.number(QObject::tr("Prolungamento (DIMEXE):"), 0.0, 1000, 3, copy.extExtension);
    dialog.group(QObject::tr("Scala e unità"));
    dialog.check(QObject::tr("Annotativo (misure sulla carta: DIMSCALE = scala di annotazione)"), copy.annotative);
    dialog.number(QObject::tr("Scala globale (DIMSCALE, se non annotativo):"), 0.0001, 100000, 4, copy.scale);
    dialog.number(QObject::tr("Fattore di misura (DIMLFAC):"), 0.0001, 100000, 4, copy.linearFactor);
    dialog.integer(QObject::tr("Decimali (DIMDEC):"), 0, 8, decimals);
    dialog.text(QObject::tr("Prefisso/suffisso (DIMPOST):"), post, QObject::tr("es. <> mm"));
    dialog.setSize(520, 500);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    copy.tickSize = ticks ? std::max(0.01, copy.arrowSize / 2.0) : 0.0;
    copy.decimals = decimals;
    copy.post = post.toStdString();
    style = std::move(copy);
    return true;
}

} // namespace cartesio
