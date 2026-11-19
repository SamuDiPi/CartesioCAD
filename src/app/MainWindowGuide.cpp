// Guida nella finestra principale: F1 (la pagina del manuale del comando in corso), percorsi guidati (pulsante
// evidenziato e condizioni dei passi controllate sullo stato del programma).
#include "app/MainWindow.h"
#include "app/MainView.h"

#include "app/CircuitsPanel.h"
#include "app/DrawingWidget.h"
#include "app/GuidePanel.h"
#include "app/PlantPanel.h"
#include "app/Ribbon.h"

#include <QJsonObject>
#include <QTimer>

namespace cartesio {

namespace {

// Tipo di oggetto dal nome usato nei percorsi (percorsi.json).
int entityKind(const QString& name)
{
    static const std::map<QString, EntityType> kinds = {
        {QStringLiteral("Line"), EntityType::Line},         {QStringLiteral("Circle"), EntityType::Circle},
        {QStringLiteral("Arc"), EntityType::Arc},           {QStringLiteral("Polyline"), EntityType::Polyline},
        {QStringLiteral("Text"), EntityType::Text},         {QStringLiteral("MText"), EntityType::MText},
        {QStringLiteral("Dimension"), EntityType::Dimension}, {QStringLiteral("Leader"), EntityType::Leader},
        {QStringLiteral("Viewport"), EntityType::Viewport}, {QStringLiteral("Hatch"), EntityType::Hatch},
        {QStringLiteral("Insert"), EntityType::Insert},     {QStringLiteral("Table"), EntityType::Table},
    };
    const auto it = kinds.find(name);
    return it == kinds.end() ? -1 : static_cast<int>(it->second);
}

int taggedCount(const Document& doc, const std::string& prefix)
{
    int n = 0;
    for (const auto& e : doc.entities())
        n += e->plantRun().rfind(prefix, 0) == 0;
    return n;
}

} // namespace

void MainWindow::showGuide()
{
    // Durante un comando: la sua pagina del manuale.
    if (DrawingWidget* d = current(); d && d->commands().isActive())
        if (guide_->openCommand(QString::fromStdString(d->commands().activeName()))) {
            showPanel(MainView::Guide, true);
            return;
        }
    showPanel(MainView::Guide, !panelShown(MainView::Guide));
}

void MainWindow::guideStepEntered()
{
    const QJsonObject step = guide_->currentStep();
    ribbon_->setHighlight(step.value(QStringLiteral("pulsante")).toString());
    // Stato da cui misurare la condizione del passo.
    guideSnap_ = GuideSnapshot{};
    if (DrawingWidget* d = current()) {
        for (const auto& e : d->document().entities()) {
            ++guideSnap_.entities[static_cast<int>(e->type())];
            ++guideSnap_.total;
        }
        for (const char* prefix : {"etichetta:", "sezione:", "distinta:"})
            guideSnap_.tagged[prefix] = taggedCount(d->document(), prefix);
    }
    guideSnap_.runs = static_cast<int>(plant_->project().runs.size());
    guideSnap_.circuits = static_cast<int>(plant_->project().circuits.size());
    guideSnap_.verify = plant_->verifyCount();
    guideSnap_.canalo = canaloChecks_;
    if (!guideTimer_) {
        guideTimer_ = new QTimer(this);
        guideTimer_->setInterval(300);
        connect(guideTimer_, &QTimer::timeout, this, &MainWindow::guideCheckStep);
    }
    if (guide_->tour() >= 0 && !guide_->finished()) {
        guideTimer_->start();
        guideCheckStep();   // già fatto (per esempio un disegno già aperto)
    } else {
        guideTimer_->stop();
    }
}

// Comandi finiti (non annullati) dall'inizio del passo, per le condizioni "command".
void MainWindow::guideCommandFinished(const QString& name, bool cancelled)
{
    if (cancelled || guide_->tour() < 0)
        return;
    ++guideSnap_.finishedCommands[name.toStdString()];
    guideCheckStep();
}

void MainWindow::guideCheckStep()
{
    if (guide_->tour() < 0 || guide_->finished() || guide_->stepDone()) {
        return;
    }
    DrawingWidget* d = current();

    const QJsonObject done = guide_->currentStep().value(QStringLiteral("fatto")).toObject();
    const QString type = done.value(QStringLiteral("tipo")).toString();
    bool ok = false;
    if (type == QLatin1String("drawing")) {
        ok = d != nullptr;
    } else if (type == QLatin1String("entity") && d) {
        const QString kind = done.value(QStringLiteral("kind")).toString();
        const int count = done.value(QStringLiteral("count")).toInt(1);
        int now = 0, before = 0;
        if (kind == QLatin1String("*")) {
            now = static_cast<int>(d->document().entities().size());
            before = guideSnap_.total;
        } else {
            const int k = entityKind(kind);
            for (const auto& e : d->document().entities())
                now += static_cast<int>(e->type()) == k;
            before = guideSnap_.entities[k];
        }
        ok = now - before >= count;
    } else if (type == QLatin1String("selection") && d) {
        ok = static_cast<int>(d->selection().size()) >= done.value(QStringLiteral("min")).toInt(1);
    } else if (type == QLatin1String("command")) {
        ok = guideSnap_.finishedCommands[done.value(QStringLiteral("name")).toString().toStdString()] > 0;
    } else if (type == QLatin1String("undo") && d) {
        ok = d->document().canRedo();
    } else if (type == QLatin1String("saved") && d) {
        ok = !d->filePath().isEmpty() && !d->isModified();
    } else if (type == QLatin1String("layout") && d) {
        ok = d->shownSpace() != 0;
    } else if (type == QLatin1String("tab")) {
        ok = ribbon_->currentTabTitle() == done.value(QStringLiteral("name")).toString();
    } else if (type == QLatin1String("panel")) {
        static const QStringList names = {QStringLiteral("Properties"), QStringLiteral("Layers"), QStringLiteral("Circuits"),
                                          QStringLiteral("Library"), QStringLiteral("Plant"), QStringLiteral("Guide")};
        const int index = names.indexOf(done.value(QStringLiteral("name")).toString());
        ok = index >= 0 && panelShown(index);
    } else if (type == QLatin1String("plantProject")) {
        ok = plant_->hasProject();
    } else if (type == QLatin1String("plantCircuits")) {
        ok = plant_->hasProject() && static_cast<int>(plant_->project().circuits.size()) >= done.value(QStringLiteral("min")).toInt(1);
    } else if (type == QLatin1String("plantRuns")) {
        ok = static_cast<int>(plant_->project().runs.size()) - guideSnap_.runs >= done.value(QStringLiteral("min")).toInt(1);
    } else if (type == QLatin1String("tagged") && d) {
        const std::string prefix = done.value(QStringLiteral("prefix")).toString().toStdString();
        ok = taggedCount(d->document(), prefix) > guideSnap_.tagged[prefix];
    } else if (type == QLatin1String("counter")) {
        const QString name = done.value(QStringLiteral("name")).toString();
        ok = name == QLatin1String("verify") ? plant_->verifyCount() > guideSnap_.verify : canaloChecks_ > guideSnap_.canalo;
    } else if (type == QLatin1String("circuits")) {
        ok = !circuits_->circuits().empty();
    }
    if (ok)
        guide_->markDone();
}

} // namespace cartesio
