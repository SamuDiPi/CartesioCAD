// Fronte quadro (FRONTEQUADRO): dallo schema unifilare in PDF al fronte dei quadri (disegno), alla distinta CSV e al
// file IFC per il BIM. Il calcolo è in core/Switchboard; qui la finestra, la lettura del PDF e il salvataggio.
#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PdfWords.h"
#include "core/Import.h"
#include "core/SingleLine.h"
#include "core/Switchboard.h"

#include <QAction>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace cartesio {

namespace {

QSettings frontSettings() { return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD")); }

// Blocchi dei cataloghi: ogni file (DWG/DXF, relativo alla cartella del catalogo) diventa un blocco "FQ_codice"
// di `library` (in mm); le misure mancanti si prendono dall'ingombro del blocco. Le voci con il file del blocco
// assente si tolgono dal catalogo (vale la voce successiva) e si contano in `missing`. Restituisce i file non letti.
template <typename T>
QStringList loadCatalogBlocks(std::vector<T>& items, const QString& catalogPath, Document& library, int* missing = nullptr)
{
    QStringList failed;
    const QDir dir = QFileInfo(catalogPath).dir();
    const auto absent = [&dir](const T& item) {
        return !item.blockFile.empty()
               && !QFileInfo::exists(QDir::cleanPath(dir.absoluteFilePath(QString::fromStdString(item.blockFile))));
    };
    const auto removed = std::remove_if(items.begin(), items.end(), absent);
    if (missing)
        *missing = static_cast<int>(std::distance(removed, items.end()));
    items.erase(removed, items.end());
    std::map<QString, std::shared_ptr<Document>> files;   // un file letto una volta sola
    for (T& item : items) {
        if (item.blockFile.empty())
            continue;
        const QString path = QDir::cleanPath(dir.absoluteFilePath(QString::fromStdString(item.blockFile)));
        auto& source = files[path];
        if (!source) {
            source = std::make_shared<Document>();
            QString error;
            if (!DrawingWidget::readFile(path, *source, &error)) {
                failed << QStringLiteral("%1 (%2)").arg(QDir::toNativeSeparators(path), error);
                source = std::make_shared<Document>();
                continue;
            }
        }
        if (source->entities().empty() && source->blocks().empty())
            continue;
        std::string name = "FQ_" + item.id;
        for (char& c : name)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                c = '_';
        const double scale = unitsFactor(source->insUnits() > 0 ? source->insUnits() : 4, 4);
        if (item.blockName.empty()) {
            importDrawingAsBlock(library, *source, name, scale);
        } else {
            if (!importBlock(library, *source, item.blockName, scale)) {
                failed << QStringLiteral("%1: blocco %2 assente").arg(QDir::toNativeSeparators(path),
                                                                     QString::fromStdString(item.blockName));
                continue;
            }
            name = item.blockName;
        }
        const auto block = library.findBlock(name);
        if (!block)
            continue;
        BBox box;
        for (const auto& e : block->entities) {
            const BBox b = e->bounds();
            if (b.isValid()) {
                box.expand(b.min);
                box.expand(b.max);
            }
        }
        if (!box.isValid())
            continue;
        item.block = name;
        if constexpr (std::is_same_v<T, DeviceType>) {
            const bool frame = item.kind == BoardDevice::Kind::MouldedCase || item.kind == BoardDevice::Kind::AirCircuit
                               || (item.kind == BoardDevice::Kind::Switch && item.modules <= 0.0);
            if (item.width <= 0.0)
                item.width = box.max.x - box.min.x;
            if (item.height <= 0.0)
                item.height = box.max.y - box.min.y;
            if (!frame && item.modules <= 0.0)
                item.modules = std::round(item.width / 18.0 * 2.0) / 2.0;
        }
    }
    return failed;
}

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

    Document library;   // blocchi dei costruttori (mm)
    library.setInsUnits(4);
    QStringList failedBlocks;
    int missingBlocks = 0;
    std::vector<EnclosureType> catalog = builtinEnclosures();
    if (!r.catalog.isEmpty()) {
        QFile file(r.catalog);
        if (!file.open(QIODevice::ReadOnly))
            return tr("Impossibile leggere il catalogo %1.").arg(QDir::toNativeSeparators(r.catalog));
        int skipped = 0;
        auto own = parseEnclosureCatalog(file.readAll().toStdString(), &skipped);
        int absent = 0;
        failedBlocks << loadCatalogBlocks(own, r.catalog, library, &absent);
        missingBlocks += absent;
        catalog.insert(catalog.end(), own.begin(), own.end());
        print(tr("Fronte quadro: %1 carpenterie dal catalogo %2%3.")
                  .arg(own.size())
                  .arg(QFileInfo(r.catalog).fileName(),
                       skipped ? tr(" (%1 righe non valide saltate)").arg(skipped) : QString()));
    }

    std::vector<DeviceType> deviceCatalog;
    std::vector<DeviceFamily> families;
    if (!r.devices.isEmpty()) {
        QFile file(r.devices);
        if (!file.open(QIODevice::ReadOnly))
            return tr("Impossibile leggere il catalogo %1.").arg(QDir::toNativeSeparators(r.devices));
        int skipped = 0;
        const std::string text = file.readAll().toStdString();
        deviceCatalog = parseDeviceCatalog(text, &skipped);
        families = parseDeviceFamilies(text);
        int absent = 0;
        failedBlocks << loadCatalogBlocks(deviceCatalog, r.devices, library, &absent);
        missingBlocks += absent;
        int withBlock = 0;
        for (const DeviceType& t : deviceCatalog)
            withBlock += t.block.empty() ? 0 : 1;
        print(tr("Fronte quadro: %1 apparecchi dal catalogo %2 (%3 con il blocco)%4.")
                  .arg(deviceCatalog.size())
                  .arg(QFileInfo(r.devices).fileName())
                  .arg(withBlock)
                  .arg(skipped ? tr(", %1 righe non valide saltate").arg(skipped) : QString()));
    }
    for (const QString& f : failedBlocks)
        print(tr("   blocco non caricato: %1").arg(f));
    if (missingBlocks > 0)
        print(tr("   %1 voci dei cataloghi saltate: il file del blocco non c'è (vale la voce successiva).")
                  .arg(missingBlocks));

    QStringList wanted;
    for (const QString& b : r.boards.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts))
        wanted << b.trimmed().toUpper();
    std::vector<BoardLayout> layouts;
    QStringList skipped;
    for (BoardInput in : boardsFromCircuits(circuits)) {
        if (!wanted.isEmpty() && !wanted.contains(QString::fromStdString(in.board).toUpper()))
            continue;
        const std::string family = assignFamilies(in.devices, families);
        const int matched = deviceCatalog.empty() ? 0 : applyDeviceCatalog(in.devices, deviceCatalog);
        BoardLayout L = layoutBoard(in, r.options, catalog);
        L.family = family;
        applyPlates(L, deviceCatalog);
        if (!deviceCatalog.empty() && matched < static_cast<int>(in.devices.size()))
            L.warnings.push_back(tr("%1 apparecchi su %2 senza voce nel catalogo: ingombri tipici")
                                     .arg(in.devices.size() - matched)
                                     .arg(in.devices.size())
                                     .toStdString());
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
        if (!L.family.empty())
            print(tr("   famiglia di blocchi: %1").arg(QString::fromStdString(L.family)));
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
        for (const BoardLayout& L : layouts)
            for (const std::string& name : boardBlocks(L))
                if (!doc.findBlock(name))
                    importBlock(doc, library, name, 1.0);
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
    int risers = s.value(QStringLiteral("FronteQuadro/risers"), 0).toInt();
    bool busbars = s.value(QStringLiteral("FronteQuadro/busbars"), true).toBool();
    r.devices = s.value(QStringLiteral("FronteQuadro/devices")).toString();
    // Primo uso: i cataloghi nelle cartelle consigliate dei Documenti, se ci sono (sempre modificabili).
    // Documenti può essere spostata (sincronizzazione): si guarda anche quella locale del profilo.
    const QStringList documents{QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                QDir::home().filePath(QStringLiteral("Documents"))};
    const auto standard = [&documents](const QString& relative) {
        for (const QString& folder : documents) {
            const QString path = QDir(folder).filePath(relative);
            if (QFileInfo::exists(path))
                return QDir::toNativeSeparators(path);
        }
        return QString();
    };
    if (r.catalog.isEmpty())
        r.catalog = standard(QStringLiteral("CartesioCAD-cataloghi/carpenterie.csv"));
    if (r.devices.isEmpty())
        r.devices = standard(QStringLiteral("CartesioCAD-blocchi/apparecchi.csv"));
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
    dialog.choice(tr("Risalita cavi (a pavimento):"),
                  {{tr("Automatica (un vano cavi ogni due colonne quando basta)"), 0}, {tr("Nessuna"), 1},
                   {tr("Un vano cavi per ogni colonna"), 2}, {tr("Una sola colonna cavi"), 3}},
                  risers, [&risers](const QVariant& v) { risers = v.toInt(); });
    dialog.check(tr("Colonna sbarre verticali con la portata (forme 2a e oltre)"), busbars);
    dialog.path(tr("Catalogo carpenterie (CSV, facoltativo):"), r.catalog, FormDialog::OpenFile,
                tr("vuoto = tipi generici"), tr("CSV (*.csv)"));
    dialog.path(tr("Catalogo apparecchi con i blocchi (CSV):"), r.devices, FormDialog::OpenFile,
                tr("vuoto = ingombri tipici"), tr("CSV (*.csv)"));
    dialog.group(tr("Risultato"));
    dialog.check(tr("Disegno in un nuovo disegno (altrimenti in quello aperto)"), newDrawing);
    dialog.check(tr("File IFC per il BIM"), ifc);
    dialog.check(tr("Distinta CSV"), csv);
    dialog.note(tr("Gli apparecchi si riconoscono dall'esecuzione, dai poli e dalla corrente scritti nello schema; gli "
                   "ingombri sono valori tipici e la carpenteria va confermata sul catalogo prima dell'ordine. Il "
                   "catalogo delle carpenterie ha le colonne codice;tipo;larghezza;altezza;profondita;ip;moduli_per_fila;"
                   "riferimento;fonte;blocco;nome_blocco, quello degli apparecchi codice;tipo;poli;in_min;in_max;moduli;"
                   "larghezza;altezza;blocco;nome_blocco;riferimento;fonte (blocchi DWG o DXF in mm, accanto al CSV)."));
    dialog.setSize(700, 800);
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
    s.setValue(QStringLiteral("FronteQuadro/risers"), risers);
    s.setValue(QStringLiteral("FronteQuadro/busbars"), busbars);
    s.setValue(QStringLiteral("FronteQuadro/devices"), r.devices);
    s.setValue(QStringLiteral("FronteQuadro/newDrawing"), newDrawing);
    s.setValue(QStringLiteral("FronteQuadro/ifc"), ifc);
    s.setValue(QStringLiteral("FronteQuadro/csv"), csv);

    r.options.environment = static_cast<BoardEnvironment>(std::clamp(environment, 0, 3));
    r.options.mounting = static_cast<BoardMounting>(std::clamp(mounting, 0, 3));
    r.options.ip = ip;
    r.options.form = form.toStdString();
    r.options.reserve = reserve / 100.0;
    r.options.terminals = terminals;
    r.options.risers = static_cast<RiserMode>(std::clamp(risers, 0, 3));
    r.options.busbars = busbars;
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
