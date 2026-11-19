// Confronto di revisioni: il disegno corrente confrontato con un'altra revisione (DWG o DXF). Scheda Revisioni (Confronta
// disegni) e scheda contestuale "Confronto" con le zone delle differenze, da percorrere una alla volta.

#include "app/MainWindow.h"

#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/Icons.h"
#include "app/Ribbon.h"
#include "core/Compare.h"
#include "core/QuickSelect.h"

#include <map>

#include <QFileDialog>
#include <QFileInfo>

namespace cartesio {

void MainWindow::setupCompareTab(Ribbon* ribbon)
{
    // Revisioni: Confronta disegni.
    RibbonPanel* collaborate = ribbon->addPanel(tr("Revisioni"), tr("Confronta"));
    QAction* compareAct = makeAction(tr("Confronta\ndisegni"), glyphIcon(QStringLiteral("A|B"), true), {},
                                     tr("Confronta il disegno con un'altra revisione (DWG o DXF): differenze colorate"));
    connect(compareAct, &QAction::triggered, this, [this] { startCompare(); });
    collaborate->addLargeButton(compareAct);

    // Scheda contestuale durante il confronto.
    const QString tab = tr("Confronto");
    RibbonPanel* nav = ribbon->addPanel(tab, tr("Modifiche"));
    nav->setPriority(9);
    QAction* prev = makeAction(tr("Precedente"), glyphIcon(QStringLiteral("◀")), {}, tr("Zona di modifica precedente"));
    QAction* next = makeAction(tr("Successiva"), glyphIcon(QStringLiteral("▶")), {}, tr("Zona di modifica successiva"));
    connect(prev, &QAction::triggered, this, [this] { compareStep(-1); });
    connect(next, &QAction::triggered, this, [this] { compareStep(1); });
    nav->addLargeButton(prev);
    nav->addLargeButton(next);
    RibbonPanel* show = ribbon->addPanel(tab, tr("Mostra"));
    show->setPriority(8);
    compareHereAct_ = makeAction(tr("Solo in questo (verde)"), glyphIcon(QStringLiteral("+")), {},
                                 tr("Oggetti che ci sono solo nel disegno corrente"));
    compareThereAct_ = makeAction(tr("Solo nell'altro (rosso)"), glyphIcon(QStringLiteral("−")), {},
                                  tr("Oggetti che c'erano solo nell'altra revisione"));
    for (QAction* a : {compareHereAct_, compareThereAct_}) {
        a->setCheckable(true);
        a->setChecked(true);
        connect(a, &QAction::toggled, this, [this] {
            if (DrawingWidget* d = current())
                d->view()->setCompareShown(compareHereAct_->isChecked(), compareThereAct_->isChecked());
        });
    }
    show->addSmallColumn({compareHereAct_, compareThereAct_});
    RibbonPanel* close = ribbon->addPanel(tab, tr("Chiudi"));
    close->setPriority(10);
    QAction* closeAct = makeAction(tr("Chiudi\nconfronto"), glyphIcon(QStringLiteral("✕"), true), {}, tr("Torna al disegno normale"));
    connect(closeAct, &QAction::triggered, this, [this] { closeCompare(); });
    close->addLargeButton(closeAct);
    ribbon->setContextual(tab);
}

void MainWindow::startCompare(QString path)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, tr("Confronta con"), QFileInfo(QString::fromStdString(d->document().filePath())).absolutePath(),
                                            tr("Disegni (*.dwg *.dxf)"));
    if (path.isEmpty())
        return;
    Document other;
    QString error;
    print(tr("Lettura di %1...").arg(QFileInfo(path).fileName()));
    if (!DrawingWidget::readFile(path, other, &error)) {
        print(tr("Confronto: impossibile leggere %1 (%2).").arg(QFileInfo(path).fileName(), error));
        return;
    }
    const DrawingDiff diff = compareDrawings(d->document(), other, 0, 0);
    CadView::CompareView c;
    c.onlyHere.insert(diff.onlyHere.begin(), diff.onlyHere.end());
    c.onlyThere = diff.onlyThere;
    c.regions = diff.regions;
    c.other = QFileInfo(path).fileName();
    c.same = diff.same;
    d->setSpace(0);
    d->view()->setCompare(std::move(c));
    compareHereAct_->setChecked(true);
    compareThereAct_->setChecked(true);
    if (ribbon_)
        ribbon_->showContextual(tr("Confronto"), true);
    print(tr("Confronto con %1: %2 oggetti solo qui (verde), %3 solo nell'altro (rosso), %4 uguali; %5 zone di modifica.")
              .arg(QFileInfo(path).fileName())
              .arg(diff.onlyHere.size())
              .arg(diff.onlyThere.size())
              .arg(diff.same)
              .arg(diff.regions.size()));
    // Per tipo: cosa è cambiato.
    std::map<std::string, std::pair<int, int>> byType;
    for (EntityId id : diff.onlyHere)
        if (const Entity* e = d->document().find(id))
            ++byType[entityTypeName(e->type())].first;
    for (const auto& e : diff.onlyThere)
        ++byType[entityTypeName(e->type())].second;
    QStringList parts;
    for (const auto& [type, n] : byType)
        parts << QStringLiteral("%1 +%2/-%3").arg(QString::fromStdString(type)).arg(n.first).arg(n.second);
    if (!parts.isEmpty())
        print(tr("Per tipo (+ solo qui, - solo nell'altro): %1").arg(parts.join(QStringLiteral(", "))));
    if (!diff.regions.empty())
        compareStep(1);
}

void MainWindow::compareStep(int delta)
{
    DrawingWidget* d = current();
    if (!d || !d->view()->compare())
        return;
    d->view()->compareGo(delta);
    const auto* c = d->view()->compare();
    if (!c->regions.empty())
        print(tr("Zona di modifica %1 di %2").arg(c->current + 1).arg(c->regions.size()));
}

void MainWindow::closeCompare()
{
    if (DrawingWidget* d = current())
        d->view()->setCompare(std::nullopt);
    if (ribbon_)
        ribbon_->showContextual(tr("Confronto"), false);
}

} // namespace cartesio
