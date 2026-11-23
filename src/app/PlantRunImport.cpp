// Tratte da layer (tavolozza Impianto, scheda Tratte): le linee dei layer delle canalizzazioni diventano tratte,
// con tipo e misura dalla legenda o dalle scritte vicine (core/RunImport). E le unità reali del disegno per
// l'impianto, quando quelle dichiarate dal file sono sbagliate.

#include "app/AppOptions.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlantPanel.h"
#include "core/RunImport.h"

#include <QMessageBox>

#include <map>
#include <numeric>

namespace cartesio {

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QString num(double v, int decimals = 0) { return QString::number(v, 'f', decimals).replace(u'.', u','); }

QString specText(const plant::RunSpec& s)
{
    switch (s.kind) {
    case plant::RunKind::Tube: return s.tube > 0.0 ? QObject::tr("Tubo Ø%1").arg(num(s.tube)) : QObject::tr("Tubo");
    case plant::RunKind::Duct: return QObject::tr("Canale %1×%2").arg(num(s.width), num(s.height));
    default: return QObject::tr("Passerella %1×%2").arg(num(s.width), num(s.height));
    }
}

// Codice di una scelta: "tipo:larghezza:altezza:diametro" ("" = ignora).
QString code(const plant::RunSpec& s)
{
    return QStringLiteral("%1:%2:%3:%4").arg(static_cast<int>(s.kind)).arg(s.width).arg(s.height).arg(s.tube);
}

plant::RunSpec fromCode(const QString& c)
{
    const QStringList p = c.split(u':');
    plant::RunSpec s;
    s.kind = static_cast<plant::RunKind>(p.value(0).toInt());
    s.width = p.value(1).toDouble();
    s.height = p.value(2).toDouble();
    s.tube = p.value(3).toDouble();
    return s;
}

QString styleText(const plant::PathStyle& st, double toM)
{
    QString lt = q(st.linetype);
    if (lt == QLatin1String("CONTINUOUS"))
        lt = QObject::tr("continua");
    return st.width > 0.0 ? QObject::tr("linea %1, spessore %2 (%3 m)").arg(lt, num(st.width, 2), num(st.width * toM, 3))
                          : QObject::tr("linea %1, sottile").arg(lt);
}

// Voce della legenda per uno stile: se più righe (più legende nel disegno) danno misure diverse, la più frequente.
std::optional<plant::RunSpec> legendFor(const std::vector<plant::LegendItem>& legend, const plant::PathStyle& style)
{
    std::map<QString, std::pair<int, plant::RunSpec>> votes;
    for (const plant::LegendItem& l : legend)
        if (l.style == style) {
            auto& v = votes[code(l.spec)];
            ++v.first;
            v.second = l.spec;
        }
    std::optional<plant::RunSpec> best;
    int count = 0;
    for (const auto& [c, v] : votes)
        if (v.first > count) {
            count = v.first;
            best = v.second;
        }
    return best;
}

const QList<QPair<QString, double>>& unitChoices()
{
    static const QList<QPair<QString, double>> items = {
        {QObject::tr("Come dichiarato dal file"), 0.0}, {QObject::tr("Millimetri"), 0.001}, {QObject::tr("Centimetri"), 0.01},
        {QObject::tr("Decimetri"), 0.1}, {QObject::tr("Metri"), 1.0}};
    return items;
}

} // namespace

void PlantPanel::addUnitsField(FormDialog& dialog, double& metersPerUnit) const
{
    QList<QPair<QString, QVariant>> items;
    for (const auto& [label, value] : unitChoices())
        items.append({label, value});
    dialog.choice(tr("Unità reali del disegno"), items, metersPerUnit, [&metersPerUnit](const QVariant& v) { metersPerUnit = v.toDouble(); });
    const int declared = drawing_ ? drawing_->document().insUnits() : 0;
    dialog.note(tr("Il file dichiara: %1. Se il disegno è stato fatto con unità diverse (per esempio porte di 90 cm che "
                   "misurano 9 unità: decimetri), sceglierle qui: valgono per tratte, percorsi, distinta e CanaloCAD, e si "
                   "salvano con il disegno.")
                    .arg(declared == 4   ? tr("millimetri")
                         : declared == 5 ? tr("centimetri")
                         : declared == 6 ? tr("metri")
                         : declared == 14 ? tr("decimetri")
                                          : tr("nessuna unità (si usano quelle delle Opzioni)")));
}

void PlantPanel::setDrawingUnits(double metersPerUnit)
{
    if (!drawing_)
        return;
    Document& doc = drawing_->document();
    if (std::abs(doc.plantMetersPerUnit() - metersPerUnit) < 1e-12)
        return;
    doc.setPlantMetersPerUnit(metersPerUnit);
    drawing_->notifyChanged();
}

PlantPanel::LayerImport PlantPanel::importRunsFromLayers(const QStringList& layerNames, const std::map<std::string, std::string>& choices,
                                                         bool useLegend)
{
    LayerImport result;
    if (!drawing_ || path_.isEmpty())
        return result;
    Document& doc = drawing_->document();
    std::vector<int> layers;
    for (const QString& name : layerNames)
        if (const int i = doc.findLayer(name.trimmed().toStdString()); i >= 0)
            layers.push_back(i);
    const auto groups = plant::pathStyles(doc, layers);
    const auto legend = plant::readRunLegend(doc);
    // Tipo e misura per stile: scelta dell'utente, altrimenti (se richiesto) la legenda.
    std::map<std::string, plant::RunSpec> byStyle;
    std::vector<EntityId> ids;
    for (const plant::StyleGroup& g : groups) {
        const std::string key = g.style.linetype + '|' + std::to_string(g.style.width);
        std::optional<plant::RunSpec> spec;
        if (const auto it = choices.find(key); it != choices.end()) {
            if (!it->second.empty())
                spec = fromCode(q(it->second));
        } else if (useLegend) {
            spec = legendFor(legend, g.style);
        }
        if (!spec)
            continue;
        byStyle[key] = *spec;
        ids.insert(ids.end(), g.ids.begin(), g.ids.end());
    }
    const double toM = plantMetersPerUnit(doc);
    const auto chains = plant::chainRuns(doc, ids, 0.005 / toM, 2.0 / toM, legend);   // 5 mm, scritte entro 2 m

    std::vector<std::unique_ptr<Entity>> copies;
    std::vector<EntityId> modified;
    for (const plant::RunChain& ch : chains) {
        const std::string key = ch.style.linetype + '|' + std::to_string(ch.style.width);
        const auto it = byStyle.find(key);
        if (it == byStyle.end())
            continue;
        if (ch.length * toM < 0.5) {   // pezzetti e polilinee degeneri, non canalizzazioni
            ++result.tooShort;
            continue;
        }
        const plant::RunSpec spec = ch.label ? *ch.label : it->second;
        plant::Run run;
        run.id = project_.nextRunId();
        run.kind = spec.kind;
        if (spec.kind == plant::RunKind::Tube) {
            if (spec.tube > 0.0)
                run.tubeOuter = spec.tube;
        } else {
            run.width = spec.width;
            run.height = spec.height;
        }
        run.name = ch.label ? ch.label->text : std::string();
        project_.runs.push_back(run);
        ++result.runs;
        result.fromLabels += ch.label.has_value();
        result.byKind[specText(spec)] += 1;
        for (EntityId id : ch.ids)
            if (const Entity* e = doc.find(id)) {
                auto copy = e->clone();
                copy->setPlantRun(run.id);
                copies.push_back(std::move(copy));
                modified.push_back(id);
            }
    }
    if (result.runs > 0) {
        doc.commit(std::move(copies), modified);   // un solo passo annullabile per tutte le linee
        if (doc.plantFile().empty())
            doc.setPlantFile(storedPath(path_).toStdString());
        drawing_->notifyChanged();
        save();
        fill();
        status_ = tr("Create %n tratte dai layer.", nullptr, result.runs);
        emit changed();
    }
    return result;
}

void PlantPanel::runsFromLayersDialog()
{
    if (!drawing_ || path_.isEmpty()) {
        QMessageBox::information(nullptr, tr("Tratte da layer"), tr("Prima creare o aprire un progetto d'impianto."));
        return;
    }
    Document& doc = drawing_->document();
    QStringList suggested;
    for (int i : plant::suggestRunLayers(doc))
        suggested << q(doc.layers()[i].name);
    QString layerText = suggested.join(QStringLiteral("; "));
    double units = doc.plantMetersPerUnit();
    {
        FormDialog dialog(tr("Tratte da layer"), FormDialog::Ok | FormDialog::Cancel);
        dialog.note(tr("Le linee e polilinee dei layer indicati (non ancora in una tratta) diventano tratte: una per ramo, "
                       "fra derivazioni ed estremi. Tipo e misura per stile di linea, proposti dalla legenda del disegno."));
        dialog.text(tr("Layer (separati da ;)"), layerText, QStringLiteral("EL-CANALINE"));
        addUnitsField(dialog, units);
        if (dialog.exec() != QDialog::Accepted)
            return;
    }
    setDrawingUnits(units);
    const QStringList layerNames = layerText.split(u';', Qt::SkipEmptyParts);
    std::vector<int> layers;
    for (const QString& name : layerNames)
        if (const int i = doc.findLayer(name.trimmed().toStdString()); i >= 0)
            layers.push_back(i);
    const auto groups = plant::pathStyles(doc, layers);
    if (groups.empty()) {
        QMessageBox::information(nullptr, tr("Tratte da layer"), tr("Nessuna linea libera sui layer indicati."));
        return;
    }
    const auto legend = plant::readRunLegend(doc);
    const double toM = plantMetersPerUnit(doc);

    // Scelte: ignora, le voci della legenda, le misure più comuni.
    QList<QPair<QString, QVariant>> options{{tr("Ignora"), QString()}};
    QStringList seen;
    auto addOption = [&](const plant::RunSpec& s, const QString& suffix) {
        const QString c = code(s);
        if (seen.contains(c))
            return;
        seen << c;
        options.append({specText(s) + suffix, c});
    };
    for (const plant::LegendItem& l : legend)
        addOption(l.spec, tr(" (legenda)"));
    for (double w : {100.0, 150.0, 200.0, 300.0, 400.0, 500.0})
        for (double h : {60.0, 75.0})
            addOption({plant::RunKind::Duct, w, h, 0.0, {}}, {});
    for (double w : {100.0, 200.0, 300.0, 400.0, 500.0, 600.0})
        for (double h : {60.0, 75.0, 100.0})
            addOption({plant::RunKind::Tray, w, h, 0.0, {}}, {});
    for (double d : {20.0, 25.0, 32.0, 40.0, 50.0, 63.0, 90.0, 110.0, 125.0})
        addOption({plant::RunKind::Tube, 0.0, 0.0, d, {}}, {});

    std::map<std::string, std::string> choices;
    std::vector<QString> current(groups.size());
    double total = 0.0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        total += groups[i].length * toM;
        if (const auto spec = legendFor(legend, groups[i].style))
            current[i] = code(*spec);
    }
    {
        FormDialog dialog(tr("Tratte da layer: tipi e misure"), FormDialog::Ok | FormDialog::Cancel);
        dialog.note(tr("%n linee sui layer scelti, %1 m in tutto. Se i metri non sono plausibili, le unità del disegno sono "
                       "sbagliate: Annulla e sceglierle nel passo precedente.", nullptr,
                       static_cast<int>(std::accumulate(groups.begin(), groups.end(), std::size_t{0},
                                                        [](std::size_t n, const plant::StyleGroup& g) { return n + g.ids.size(); })))
                        .arg(num(total, 1)));
        dialog.note(legend.empty() ? tr("Legenda: non trovata nel disegno. Scegliere tipo e misura per ogni stile.")
                                   : tr("Legenda: %n voci lette; le proposte qui sotto vengono da lì.", nullptr, static_cast<int>(legend.size())));
        for (std::size_t i = 0; i < groups.size() && i < 16; ++i) {
            const plant::StyleGroup& g = groups[i];
            dialog.choice(tr("%1 - %n linee, %2 m", nullptr, static_cast<int>(g.ids.size()))
                              .arg(styleText(g.style, toM), num(g.length * toM, 1)),
                          options, current[i], [&current, i](const QVariant& v) { current[i] = v.toString(); });
        }
        dialog.note(tr("Una scritta vicina a un ramo (per esempio \"CANALINA METALLICA 200x75mm\") vale per quel ramo. Le tratte "
                       "create si modificano poi una per una; Ctrl+Z toglie il collegamento delle linee alle tratte."));
        if (dialog.exec() != QDialog::Accepted)
            return;
    }
    for (std::size_t i = 0; i < groups.size(); ++i)
        choices[groups[i].style.linetype + '|' + std::to_string(groups[i].style.width)] = current[i].toStdString();
    const LayerImport r = importRunsFromLayers(layerNames, choices, false);
    QStringList kinds;
    for (const auto& [k, n] : r.byKind)
        kinds << tr("%1: %2").arg(k).arg(n);
    const QString text = r.runs == 0 ? tr("Nessuna tratta creata (tutti gli stili su Ignora?).")
                                     : tr("Create %1 tratte (%2 con la misura dalla scritta vicina; %3 pezzi sotto 50 cm "
                                          "scartati).\n%4")
                                           .arg(r.runs)
                                           .arg(r.fromLabels)
                                           .arg(r.tooShort)
                                           .arg(kinds.join(u'\n'));
    emit message(tr("Tratte da layer: ") + text.left(text.indexOf(u'\n') < 0 ? text.size() : text.indexOf(u'\n')));
    QMessageBox::information(nullptr, tr("Tratte da layer"), text);
}

} // namespace cartesio
