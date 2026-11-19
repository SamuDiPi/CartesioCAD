// QSELECT: selezione rapida per tipo e proprietà (anche il valore di un attributo, es. tutte le prese del quadro QD).
#include "app/MainWindow.h"

#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "core/Document.h"
#include "core/QuickSelect.h"

#include <QMessageBox>

#include <algorithm>
#include <set>

namespace cartesio {

void MainWindow::showQuickSelect()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const Document& doc = d->document();
    const std::vector<EntityId> current = d->selection();
    const int space = doc.activeSpace();

    // Tipi presenti nello spazio e etichette di attributo dei blocchi.
    std::set<int> types;
    std::set<std::string> tags;
    for (const auto& e : doc.entities()) {
        if ((e->space() < 0 ? 0 : e->space()) != space)
            continue;
        types.insert(static_cast<int>(e->type()));
        if (e->type() == EntityType::Insert)
            for (const Attribute& a : static_cast<const InsertEntity&>(*e).attributes)
                tags.insert(a.tag);
    }

    int applyTo = current.empty() ? 0 : 1;
    int type = -1;
    QString property;
    int op = 0;
    QString value;
    int mode = 0;
    bool append = false;

    QList<QPair<QString, QVariant>> typeItems{{tr("Più tipi (tutti)"), -1}};
    for (int t : types)
        typeItems.append({QString::fromStdString(entityTypeName(static_cast<EntityType>(t))), t});
    QList<QPair<QString, QVariant>> propertyItems{{tr("(solo il tipo)"), QString()},
                                                  {tr("Colore"), QStringLiteral("color")},
                                                  {tr("Layer"), QStringLiteral("layer")},
                                                  {tr("Tipo di linea"), QStringLiteral("linetype")},
                                                  {tr("Spessore di linea (mm)"), QStringLiteral("lineweight")},
                                                  {tr("Nome del blocco"), QStringLiteral("block")},
                                                  {tr("Contenuto del testo"), QStringLiteral("text")},
                                                  {tr("Altezza del testo"), QStringLiteral("height")}};
    for (const std::string& tag : tags)
        propertyItems.append({tr("Attributo %1").arg(QString::fromStdString(tag)),
                              QStringLiteral("attribute:") + QString::fromStdString(tag)});

    FormDialog dialog(tr("Selezione rapida"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.choice(tr("Applica a:"),
                  {{tr("Tutto lo spazio corrente"), 0}, {tr("Selezione corrente (%1 oggetti)").arg(current.size()), 1}},
                  applyTo, [&applyTo](const QVariant& v) { applyTo = v.toInt(); });
    dialog.choice(tr("Tipo di oggetto:"), typeItems, type, [&type](const QVariant& v) { type = v.toInt(); });
    dialog.choice(tr("Proprietà:"), propertyItems, property, [&property](const QVariant& v) { property = v.toString(); });
    dialog.choice(tr("Operatore:"),
                  {{tr("= Uguale a"), 0}, {tr("<> Diverso da"), 1}, {tr("> Maggiore di"), 2}, {tr("< Minore di"), 3},
                   {tr("* Corrisponde a (con * e ?)"), 4}},
                  op, [&op](const QVariant& v) { op = v.toInt(); });
    dialog.text(tr("Valore:"), value, tr("es. EL-FM, DaLayer, 1, QD*, 2.5"));
    dialog.choice(tr("Come applicare:"),
                  {{tr("Includi nel nuovo gruppo di selezione"), 0}, {tr("Escludi dal nuovo gruppo di selezione"), 1}},
                  mode, [&mode](const QVariant& v) { mode = v.toInt(); });
    dialog.check(tr("Aggiungi al gruppo di selezione corrente"), append);
    dialog.note(tr("Colore: DaLayer, DaBlocco o il numero (1 rosso, 2 giallo...). Con \"Corrisponde a\" si usano * e ? "
                   "(es. QD* per tutte le sigle che iniziano con QD). Si selezionano solo oggetti su layer sbloccati."));
    dialog.setSize(560, 470);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QuickSelectQuery query;
    query.space = space;
    if (applyTo == 1)
        query.within = current;
    if (type >= 0)
        query.type = static_cast<EntityType>(type);
    query.property = property.toStdString();
    query.op = static_cast<QuickSelectQuery::Op>(op);
    query.value = value.trimmed().toStdString();
    query.exclude = mode == 1;
    std::vector<EntityId> found = quickSelect(doc, query);
    if (append) {
        std::vector<EntityId> merged = current;
        for (EntityId id : found)
            if (std::find(merged.begin(), merged.end(), id) == merged.end())
                merged.push_back(id);
        found = std::move(merged);
    }
    d->setSelection(found);
    print(tr("QSELECT: %1 oggetti selezionati.").arg(found.size()));
}

} // namespace cartesio
