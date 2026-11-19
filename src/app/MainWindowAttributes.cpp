// Attributi dei blocchi (cartigli, simboli con sigla):
// - EATTEDIT (anche doppio clic su un blocco con attributi): i valori di un inserimento;
// - BATTMAN: le definizioni di un blocco (etichetta, richiesta, valore predefinito, ordine) e la sincronizzazione.
#include "app/MainWindow.h"

#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "core/Attributes.h"
#include "core/Document.h"

#include <QMessageBox>

#include <map>

namespace cartesio {

void MainWindow::editAttributes(EntityId id)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    Document& doc = d->document();
    const Entity* e = doc.find(id);
    if (!e || e->type() != EntityType::Insert)
        return;
    const auto& insert = static_cast<const InsertEntity&>(*e);
    if (insert.attributes.empty()) {
        print(tr("Il blocco %1 non ha attributi.").arg(QString::fromStdString(insert.blockName)));
        return;
    }
    // Richieste dalle definizioni del blocco (più chiare delle etichette), se ci sono.
    std::map<std::string, std::string> prompts;
    if (insert.block)
        for (const AttDefEntity* def : attributeDefinitions(*insert.block))
            prompts[def->tag] = def->prompt;

    std::vector<QString> values;
    for (const Attribute& a : insert.attributes)
        values.push_back(QString::fromStdString(a.text.text));
    FormDialog dialog(tr("Editor attributi - %1").arg(QString::fromStdString(insert.blockName)), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.group(tr("Valori degli attributi"));
    for (std::size_t i = 0; i < insert.attributes.size(); ++i) {
        const Attribute& a = insert.attributes[i];
        const auto prompt = prompts.find(a.tag);
        QString label = QString::fromStdString(a.tag);
        if (prompt != prompts.end() && !prompt->second.empty() && prompt->second != a.tag)
            label = QStringLiteral("%1 (%2)").arg(QString::fromStdString(prompt->second), label);
        dialog.text(label + QLatin1Char(':'), values[i]);
    }
    dialog.note(tr("Le modifiche si annullano con UNDO. Per cambiare etichette, richieste e valori predefiniti del "
                   "blocco: BATTMAN."));
    dialog.setSize(560, std::min(760, 190 + 32 * static_cast<int>(insert.attributes.size())));
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::vector<std::pair<std::string, std::string>> changes;
    for (std::size_t i = 0; i < insert.attributes.size(); ++i)
        changes.emplace_back(insert.attributes[i].tag, values[i].toStdString());
    if (setAttributeValues(doc, id, changes))
        d->notifyChanged();
}

// BATTMAN: definizioni di attributo di un blocco.
void MainWindow::showAttributeManager(const QString& blockName)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    Document& doc = d->document();
    QStringList blocks;
    for (const auto& b : doc.blocks())
        if (!b->name.empty() && b->name.front() != '*' && !b->isXref() && !attributeDefinitions(*b).empty())
            blocks << QString::fromStdString(b->name);
    blocks.sort(Qt::CaseInsensitive);
    if (blocks.isEmpty()) {
        QMessageBox::information(this, tr("Gestione attributi blocco"), tr("Nel disegno non ci sono blocchi con attributi."));
        return;
    }
    QString block = blocks.contains(blockName) ? blockName : blocks.front();
    int selected = 0;
    int listField = -1, detailsField = -1;
    FormDialog dialog(tr("Gestione attributi blocco"), FormDialog::Close, this);

    const auto definitions = [&]() -> std::vector<AttDefEntity*> {
        std::vector<AttDefEntity*> out;
        if (auto b = doc.editableBlock(block.toStdString()))   // copia: Annulla ritrova la definizione di prima
            for (auto& child : b->entities)
                if (child->type() == EntityType::AttDef)
                    out.push_back(static_cast<AttDefEntity*>(child.get()));
        return out;
    };
    const auto countInserts = [&] {
        int n = 0;
        for (const auto& e : doc.entities())
            if (e->type() == EntityType::Insert && static_cast<const InsertEntity&>(*e).blockName == block.toStdString())
                ++n;
        return n;
    };
    const auto refill = [&](int select) {
        QStringList rows;
        for (const AttDefEntity* def : definitions())
            rows << QStringLiteral("%1   —   %2   [%3]")
                        .arg(QString::fromStdString(def->tag), QString::fromStdString(def->prompt),
                             QString::fromStdString(def->text.text));
        selected = rows.isEmpty() ? -1 : std::clamp(select, 0, static_cast<int>(rows.size()) - 1);
        dialog.setItems(listField, rows, selected);
        dialog.setFieldText(detailsField, tr("Blocco <b>%1</b>: %2 definizioni, %3 inserimenti nel disegno. "
                                             "Le modifiche cambiano la definizione del blocco; con <b>Sincronizza</b> "
                                             "gli inserimenti la seguono (i valori restano).")
                                              .arg(block.toHtmlEscaped())
                                              .arg(definitions().size())
                                              .arg(countInserts()));
    };
    // Una definizione modificata: il blocco si ridefinisce (gli inserimenti si ricollegano).
    const auto touched = [&] {
        doc.touch();
        d->notifyChanged();
    };

    QList<QPair<QString, QVariant>> blockItems;
    for (const QString& b : blocks)
        blockItems.append({b, b});
    dialog.choice(tr("Blocco:"), blockItems, block, [&](const QVariant& v) {
        block = v.toString();
        refill(0);
    });
    dialog.group(tr("Definizioni (etichetta — richiesta [valore predefinito])"));
    listField = dialog.list({}, 0, [&](int index) { selected = index; });
    dialog.buttonsRow({
        {tr("Modifica..."), [&] {
             auto defs = definitions();
             if (selected < 0 || selected >= static_cast<int>(defs.size()))
                 return;
             AttDefEntity* def = defs[static_cast<std::size_t>(selected)];
             QString tag = QString::fromStdString(def->tag), prompt = QString::fromStdString(def->prompt),
                     value = QString::fromStdString(def->text.text);
             double height = def->text.height;
             bool invisible = def->flags & AttInvisible;
             FormDialog edit(tr("Modifica attributo"), FormDialog::Ok | FormDialog::Cancel, &dialog);
             edit.text(tr("Etichetta:"), tag);
             edit.text(tr("Richiesta:"), prompt);
             edit.text(tr("Valore predefinito:"), value);
             edit.number(tr("Altezza testo:"), 0.001, 100000, 3, height);
             edit.check(tr("Invisibile"), invisible);
             edit.setSize(460, 290);
             if (edit.exec() != QDialog::Accepted || tag.trimmed().isEmpty())
                 return;
             def->tag = tag.trimmed().toUpper().replace(QLatin1Char(' '), QLatin1Char('_')).toStdString();
             def->prompt = prompt.toStdString();
             def->text.text = value.toStdString();
             def->text.height = height;
             def->flags = invisible ? (def->flags | AttInvisible) : (def->flags & ~AttInvisible);
             touched();
             refill(selected);
         }},
        {tr("Su"), [&] {
             auto b = doc.editableBlock(block.toStdString());
             auto defs = definitions();
             if (!b || selected <= 0 || selected >= static_cast<int>(defs.size()))
                 return;
             // Scambio di posto delle due definizioni nella lista degli oggetti del blocco.
             auto& list = b->entities;
             const auto pos = [&](const Entity* e) {
                 return std::find_if(list.begin(), list.end(), [e](const auto& x) { return x.get() == e; });
             };
             std::iter_swap(pos(defs[static_cast<std::size_t>(selected)]), pos(defs[static_cast<std::size_t>(selected - 1)]));
             b->invalidateBounds();
             touched();
             refill(selected - 1);
         }},
        {tr("Giù"), [&] {
             auto b = doc.editableBlock(block.toStdString());
             auto defs = definitions();
             if (!b || selected < 0 || selected + 1 >= static_cast<int>(defs.size()))
                 return;
             auto& list = b->entities;
             const auto pos = [&](const Entity* e) {
                 return std::find_if(list.begin(), list.end(), [e](const auto& x) { return x.get() == e; });
             };
             std::iter_swap(pos(defs[static_cast<std::size_t>(selected)]), pos(defs[static_cast<std::size_t>(selected + 1)]));
             b->invalidateBounds();
             touched();
             refill(selected + 1);
         }},
        {tr("Sincronizza"), [&] {
             const int n = syncBlockAttributes(doc, block.toStdString());
             d->notifyChanged();
             print(tr("ATTSYNC %1: %2 inserimenti aggiornati.").arg(block).arg(n));
             refill(selected);
         }},
    });
    dialog.group(tr("Informazioni"));
    detailsField = dialog.note(QString());
    refill(0);
    dialog.setSize(640, 560);
    dialog.exec();
}

} // namespace cartesio
