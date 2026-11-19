// FIND: trova e sostituisci (testi, testi multilinea, attributi dei blocchi, quote e direttrici), con zoom sul
// risultato.
#include "app/MainWindow.h"

#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/TextRender.h"
#include "core/Document.h"
#include "core/FindReplace.h"

#include <QMessageBox>

namespace cartesio {

void MainWindow::showFind(const QString& initial)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    Document& doc = d->document();
    QString query = initial, replacement;
    int where = 0;   // 0 tutto il disegno, 1 spazio corrente, 2 selezione
    FindOptions options;
    std::vector<FindMatch> matches;
    int selected = -1;
    int listField = -1, statusField = -1;
    const std::vector<EntityId> selection = d->selection();
    FormDialog dialog(tr("Trova e sostituisci"), FormDialog::Close, this);

    const auto spaceName = [&doc](int space) {
        if (space == 0)
            return tr("Modello");
        if (const Layout* l = doc.findLayout(space))
            return QString::fromStdString(l->name);
        return tr("Layout");
    };
    const auto search = [&](int select) {
        options.space = where == 1 ? d->document().activeSpace() : -1;
        options.only = where == 2 ? selection : std::vector<EntityId>{};
        matches = findText(doc, query.toStdString(), options);
        QStringList rows;
        for (const FindMatch& m : matches)
            rows << QStringLiteral("%1   ·   %2   ·   %3")
                        .arg(spaceName(m.space), QString::fromStdString(m.kind),
                             plainMText(m.text).left(90));
        selected = matches.empty() ? -1 : std::clamp(select, 0, static_cast<int>(matches.size()) - 1);
        dialog.setItems(listField, rows, selected);
        dialog.setFieldText(statusField, query.isEmpty()
                                             ? tr("Scrivere il testo da cercare e premere Trova.")
                                             : tr("Trovati: <b>%1</b>.").arg(matches.size()));
    };
    const auto zoomTo = [&](int index) {
        if (index < 0 || index >= static_cast<int>(matches.size()))
            return;
        const FindMatch& m = matches[static_cast<std::size_t>(index)];
        if (d->shownSpace() != m.space)
            d->setSpace(m.space);
        BBox box = m.bounds;
        if (box.isValid()) {
            const double margin = std::max({box.width(), box.height(), 1.0}) * 2.0;
            box.expand(box.min - Vec2{margin, margin});
            box.expand(box.max + Vec2{margin, margin});
            d->view()->zoomWindow(box);
        }
        d->setSelection({m.id});
    };

    dialog.group(tr("Testo"));
    dialog.text(tr("Trova:"), query);
    dialog.text(tr("Sostituisci con:"), replacement);
    dialog.choice(tr("Cerca in:"),
                  {{tr("Tutto il disegno"), 0}, {tr("Spazio corrente"), 1}, {tr("Oggetti selezionati (%1)").arg(selection.size()), 2}},
                  selection.empty() ? 0 : 2, [&where](const QVariant& v) { where = v.toInt(); });
    where = selection.empty() ? 0 : 2;
    dialog.check(tr("Maiuscole/minuscole"), options.matchCase);
    dialog.check(tr("Solo parole intere"), options.wholeWord);
    dialog.check(tr("Testi e testi multilinea"), options.texts);
    dialog.check(tr("Attributi dei blocchi (cartigli, sigle)"), options.attributes);
    dialog.check(tr("Quote e direttrici"), options.dimensions);
    dialog.buttonsRow({
        {tr("Trova"), [&] { search(0); }},
        {tr("Zoom"), [&] { zoomTo(selected); }},
        {tr("Sostituisci"), [&] {
             if (selected < 0 || selected >= static_cast<int>(matches.size()))
                 return;
             const int n = replaceText(doc, {matches[static_cast<std::size_t>(selected)]}, query.toStdString(),
                                       replacement.toStdString(), options);
             d->notifyChanged();
             print(tr("Sostituzioni: %1.").arg(n));
             search(selected);
         }},
        {tr("Sostituisci tutto"), [&] {
             if (matches.empty())
                 search(0);
             if (matches.empty())
                 return;
             if (QMessageBox::question(&dialog, dialog.windowTitle(),
                                       tr("Sostituire \"%1\" con \"%2\" in %3 oggetti?").arg(query, replacement).arg(matches.size()))
                 != QMessageBox::Yes)
                 return;
             const int n = replaceText(doc, matches, query.toStdString(), replacement.toStdString(), options);
             d->notifyChanged();
             print(tr("Sostituzioni: %1 (annullabili con UNDO).").arg(n));
             search(0);
         }},
    });
    dialog.group(tr("Risultati (doppio clic: zoom sull'oggetto)"));
    listField = dialog.list({}, -1, [&](int index) { selected = index; }, [&](int index) {
        selected = index;
        zoomTo(index);
    });
    statusField = dialog.note(QString());
    search(0);
    dialog.setSize(680, 640);
    dialog.exec();
}

} // namespace cartesio
