// Fronte quadro (FRONTEQUADRO): dallo schema unifilare in PDF al fronte dei quadri (disegno), alla distinta CSV e al
// file IFC per il BIM. Il calcolo è in core/Switchboard; qui la finestra, la lettura del PDF e il salvataggio.
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PdfWords.h"
#include "core/SingleLine.h"
#include "core/Switchboard.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace cartesio {

namespace {

QSettings frontSettings() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

bool writeText(const QString& path, const std::string& text, bool bom)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && (!bom || file.write("\xEF\xBB\xBF") >= 0)
           && file.write(text.data(), static_cast<qint64>(text.size())) >= 0 && file.commit();
}

} // namespace

QString MainWindow::runSwitchboardFront(const SwitchboardRequest& r)
{
    QFile pdf(r.pdf);
    if (!pdf.open(QIODevice::ReadOnly))
        return tr("Impossibile leggere %1.").arg(QDir::toNativeSeparators(r.pdf));
    std::vector<PdfWord> words;
    std::string failure;
    if (!readPdfWords(pdf.readAll().toStdString(), words, &failure))
        return QString::fromStdString(failure);
    const std::vector<Circuit> circuits = parseSingleLineDiagram(words);
    if (circuits.empty())
        return tr("Nel PDF non ci sono schemi unifilari riconoscibili (manca la riga \"Sigla utenza\").");

    std::vector<EnclosureType> catalog = builtinEnclosures();
    if (!r.catalog.isEmpty()) {
        QFile file(r.catalog);
        if (!file.open(QIODevice::ReadOnly))
            return tr("Impossibile leggere il catalogo %1.").arg(QDir::toNativeSeparators(r.catalog));
        int skipped = 0;
        const auto own = parseEnclosureCatalog(file.readAll().toStdString(), &skipped);
        catalog.insert(catalog.end(), own.begin(), own.end());
        print(tr("Fronte quadro: %1 carpenterie dal catalogo %2%3.")
                  .arg(own.size())
                  .arg(QFileInfo(r.catalog).fileName(),
                       skipped ? tr(" (%1 righe non valide saltate)").arg(skipped) : QString()));
    }

    QStringList wanted;
    for (const QString& b : r.boards.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts))
        wanted << b.trimmed().toUpper();
    std::vector<BoardLayout> layouts;
    QStringList skipped;
    for (const BoardInput& in : boardsFromCircuits(circuits)) {
        if (!wanted.isEmpty() && !wanted.contains(QString::fromStdString(in.board).toUpper()))
            continue;
        BoardLayout L = layoutBoard(in, r.options, catalog);
        if (L.devices.empty()) {
            skipped << QString::fromStdString(in.board);
            continue;
        }
        print(tr("Fronte quadro %1: %2, %3 x %4 x %5 mm, IP%6, forma %7, moduli %8 su %9%10.")
                  .arg(QString::fromStdString(L.board),
                       L.enclosureFound ? QString::fromStdString(L.enclosure.reference.empty() ? L.enclosure.id
                                                                                                : L.enclosure.reference)
                                        : tr("carpenteria da scegliere"))
                  .arg(L.width, 0, 'f', 0)
                  .arg(L.mounting == BoardMounting::Floor ? 2100.0 : L.enclosure.height, 0, 'f', 0)
                  .arg(L.depth, 0, 'f', 0)
                  .arg(L.ip)
                  .arg(QString::fromStdString(L.form))
                  .arg(L.modules, 0, 'f', 0)
                  .arg(L.modulesInstalled, 0, 'f', 0)
                  .arg(L.enclosureCount > 1 ? tr(", %1 colonne").arg(L.enclosureCount) : QString()));
        for (const std::string& w : L.warnings)
            print(tr("   attenzione: %1").arg(QString::fromStdString(w)));
        layouts.push_back(std::move(L));
    }
    if (!skipped.isEmpty())
        print(tr("Fronte quadro: senza apparecchi di bassa tensione riconosciuti (saltati): %1.")
                  .arg(skipped.join(QStringLiteral(", "))));
    if (layouts.empty())
        return wanted.isEmpty() ? tr("Nessun quadro con apparecchi riconosciuti nello schema.")
                                : tr("I quadri indicati non sono nello schema o non hanno apparecchi riconosciuti.");

    if (r.draw) {
        DrawingWidget* d = r.newDrawing || !current() ? createDrawing(tr("Fronti quadri")) : current();
        Document& doc = d->document();
        if (r.newDrawing || doc.entities().empty())
            doc.setInsUnits(4);   // millimetri
        // Nel disegno in uso il fronte (in mm) si scala sulle sue unità.
        const double scale = (r.newDrawing || doc.insUnits() == 4) ? 1.0 : 0.001 / std::max(1e-12, plantMetersPerUnit(doc));
        const BBox ext = doc.extents(0);
        double x = ext.isValid() ? ext.max.x / scale + 2000.0 : 0.0;
        const double y = ext.isValid() ? ext.min.y / scale : 0.0;
        std::vector<std::unique_ptr<Entity>> all;
        for (const BoardLayout& L : layouts) {
            BBox box;
            for (auto& e : drawBoardFront(doc, L, {x, y}, &box)) {
                if (scale != 1.0)
                    e->transform(Affine::scaling({0.0, 0.0}, scale));
                e->setSpace(0);
                all.push_back(std::move(e));
            }
            x = box.max.x + 800.0;
        }
        doc.commit(std::move(all));
        doc.checkpoint();
        d->notifyChanged();
        if (zoomExtentsAct_)
            zoomExtentsAct_->trigger();
    }
    if (!r.ifc.isEmpty()) {
        const QString name = QFileInfo(r.ifc).completeBaseName();
        if (!writeText(r.ifc, boardsToIfc(layouts, name.toStdString()), false))
            return tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(r.ifc));
        print(tr("Fronte quadro: IFC salvato in %1.").arg(QDir::toNativeSeparators(r.ifc)));
    }
    if (!r.csv.isEmpty()) {
        if (!writeText(r.csv, boardBillOfMaterials(layouts), true))
            return tr("Impossibile scrivere %1.").arg(QDir::toNativeSeparators(r.csv));
        print(tr("Fronte quadro: distinta salvata in %1.").arg(QDir::toNativeSeparators(r.csv)));
    }
    print(tr("Fronte quadro: %1 quadri. Ingombri tipici: confermarli sul catalogo della carpenteria e degli apparecchi.")
              .arg(layouts.size()));
    return QString();
}

void MainWindow::showSwitchboardFront()
{
    QSettings s = frontSettings();
    SwitchboardRequest r;
    r.pdf = s.value(QStringLiteral("FronteQuadro/pdf")).toString();
    r.catalog = s.value(QStringLiteral("FronteQuadro/catalog")).toString();
    int environment = s.value(QStringLiteral("FronteQuadro/environment"), 0).toInt();
    int mounting = s.value(QStringLiteral("FronteQuadro/mounting"), 0).toInt();
    int ip = s.value(QStringLiteral("FronteQuadro/ip"), 0).toInt();
    QString form = s.value(QStringLiteral("FronteQuadro/form")).toString();
    int reserve = s.value(QStringLiteral("FronteQuadro/reserve"), 25).toInt();
    bool terminals = s.value(QStringLiteral("FronteQuadro/terminals"), true).toBool();
    bool cable = s.value(QStringLiteral("FronteQuadro/cable"), false).toBool();
    bool newDrawing = s.value(QStringLiteral("FronteQuadro/newDrawing"), true).toBool();
    bool ifc = s.value(QStringLiteral("FronteQuadro/ifc"), true).toBool();
    bool csv = s.value(QStringLiteral("FronteQuadro/csv"), true).toBool();

    FormDialog dialog(tr("Fronte quadro"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.group(tr("Schema"));
    dialog.path(tr("Schema unifilare (PDF):"), r.pdf, FormDialog::OpenFile, QString(), tr("PDF (*.pdf)"));
    dialog.text(tr("Quadri:"), r.boards, tr("vuoto = tutti; oppure sigle separate da virgola"));
    dialog.group(tr("Quadro"));
    dialog.choice(tr("Ambiente:"),
                  {{tr("Locale tecnico (IP31)"), 0}, {tr("Ambiente ordinario o pubblico (IP43)"), 1},
                   {tr("Esterno (IP55)"), 2}, {tr("Lavaggi o polveri (IP65)"), 3}},
                  environment, [&environment](const QVariant& v) { environment = v.toInt(); });
    dialog.integer(tr("Grado IP (0 = dall'ambiente):"), 0, 68, ip);
    dialog.choice(tr("Forma di segregazione:"),
                  {{tr("Proposta dalla corrente"), QString()}, {QStringLiteral("1"), QStringLiteral("1")},
                   {QStringLiteral("2a"), QStringLiteral("2a")}, {QStringLiteral("2b"), QStringLiteral("2b")},
                   {QStringLiteral("3a"), QStringLiteral("3a")}, {QStringLiteral("3b"), QStringLiteral("3b")},
                   {QStringLiteral("4a"), QStringLiteral("4a")}, {QStringLiteral("4b"), QStringLiteral("4b")}},
                  form, [&form](const QVariant& v) { form = v.toString(); });
    dialog.choice(tr("Installazione:"),
                  {{tr("Automatica"), 0}, {tr("A parete"), 1}, {tr("A pavimento"), 2}, {tr("Cassetta"), 3}}, mounting,
                  [&mounting](const QVariant& v) { mounting = v.toInt(); });
    dialog.integer(tr("Spazio di riserva:"), 0, 100, reserve, QStringLiteral(" %"));
    dialog.check(tr("Morsettiera nelle file in basso"), terminals);
    dialog.check(tr("Vano cavi laterale nei quadri a pavimento"), cable);
    dialog.path(tr("Catalogo carpenterie (CSV, facoltativo):"), r.catalog, FormDialog::OpenFile,
                tr("vuoto = tipi generici"), tr("CSV (*.csv)"));
    dialog.group(tr("Risultato"));
    dialog.check(tr("Disegno in un nuovo disegno (altrimenti in quello aperto)"), newDrawing);
    dialog.check(tr("File IFC per il BIM"), ifc);
    dialog.check(tr("Distinta CSV"), csv);
    dialog.note(tr("Gli apparecchi si riconoscono dall'esecuzione, dai poli e dalla corrente scritti nello schema; gli "
                   "ingombri sono valori tipici e la carpenteria va confermata sul catalogo prima dell'ordine. Il "
                   "catalogo CSV ha le colonne codice;tipo;larghezza;altezza;profondita;ip;moduli_per_fila;riferimento;fonte."));
    dialog.setSize(660, 700);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (r.pdf.isEmpty()) {
        print(tr("Fronte quadro: scegliere lo schema in PDF."));
        return;
    }
    s.setValue(QStringLiteral("FronteQuadro/pdf"), r.pdf);
    s.setValue(QStringLiteral("FronteQuadro/catalog"), r.catalog);
    s.setValue(QStringLiteral("FronteQuadro/environment"), environment);
    s.setValue(QStringLiteral("FronteQuadro/mounting"), mounting);
    s.setValue(QStringLiteral("FronteQuadro/ip"), ip);
    s.setValue(QStringLiteral("FronteQuadro/form"), form);
    s.setValue(QStringLiteral("FronteQuadro/reserve"), reserve);
    s.setValue(QStringLiteral("FronteQuadro/terminals"), terminals);
    s.setValue(QStringLiteral("FronteQuadro/cable"), cable);
    s.setValue(QStringLiteral("FronteQuadro/newDrawing"), newDrawing);
    s.setValue(QStringLiteral("FronteQuadro/ifc"), ifc);
    s.setValue(QStringLiteral("FronteQuadro/csv"), csv);

    r.options.environment = static_cast<BoardEnvironment>(std::clamp(environment, 0, 3));
    r.options.mounting = static_cast<BoardMounting>(std::clamp(mounting, 0, 3));
    r.options.ip = ip;
    r.options.form = form.toStdString();
    r.options.reserve = reserve / 100.0;
    r.options.terminals = terminals;
    r.options.cableCompartment = cable;
    r.newDrawing = newDrawing;
    const QFileInfo info(r.pdf);
    const QString base = info.dir().filePath(info.completeBaseName() + tr(" - fronti quadri"));
    if (ifc)
        r.ifc = base + QStringLiteral(".ifc");
    if (csv)
        r.csv = base + QStringLiteral(".csv");
    const QString error = runSwitchboardFront(r);
    if (!error.isEmpty())
        print(tr("Fronte quadro: %1").arg(error));
}

} // namespace cartesio
