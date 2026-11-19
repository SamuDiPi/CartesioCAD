#include "app/DrawingWidget.h"

#include "app/CadView.h"
#include "app/TextEditInPlace.h"
#include "app/AppOptions.h"
#include "app/DwgConverter.h"
#include "app/ExternalRefs.h"
#include "app/ImageLibrary.h"
#include "core/Import.h"
#include "core/InputParser.h"
#include "core/io/Dxf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QGridLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>

namespace cartesio {

namespace {

std::filesystem::path toFsPath(const QString& path)
{
    return std::filesystem::path(path.toStdU16String());
}

} // namespace

DrawingWidget::DrawingWidget(const DraftSettings& settings, const QString& untitledName, QWidget* parent)
    : QWidget(parent)
    , ctx_{doc_, selection_}
    , untitledName_(untitledName)
{
    registerBuiltinCommands(commands_);

    settings_ = &settings;
    view_ = new CadView(doc_, commands_, selection_, idlePicker_, settings, this);
    views_.push_back(view_);
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(view_, 0, 0);

    ctx_.print = [this](const std::string& text) { emit message(QString::fromStdString(text)); };
    commands_.finished = [this](const std::string& name, bool cancelled) {
        emit commandFinished(QString::fromStdString(name), cancelled);
    };
    ctx_.zoomExtents = [this] { view_->zoomExtents(); };
    ctx_.editAttributes = [this](EntityId id) { emit attributesRequested(id); };
    ctx_.zoomWindow = [this](const BBox& box) { view_->zoomWindow(box); };
    ctx_.zoomPrevious = [this] { return view_->zoomPrevious(); };
    ctx_.visibleArea = [this] { return view_->visibleArea(); };
    ctx_.shownSpace = [this] { return view_->shownSpace(); };
    setTextInPlaceAllowed([] { return true; });

    connectView(view_);
}

// Segnali di una vista verso il disegno (ogni vista: con le finestre affiancate sono più di una).
void DrawingWidget::connectView(CadView* view)
{
    // La vista cliccata diventa quella attiva: i comandi e lo zoom lavorano su di lei.
    connect(view, &CadView::activated, this, [this, view] {
        if (view == view_)
            return;
        view_ = view;
        for (CadView* v : views_)
            v->setTileState(views_.size() > 1, v == view_);
        emit viewsChanged();
    });
    connect(view, &CadView::pointPicked, this, &DrawingWidget::onPointPicked);
    // Doppio clic su una finestra del layout: si lavora sul modello attraverso di essa (MSPACE);
    // doppio clic fuori: si torna al foglio (PSPACE).
    connect(view, &CadView::viewportActivated, this, [this](EntityId id) { setViewport(id); });
    // Doppio clic su una cella di tabella: si scrive direttamente nella cella (Tab per la successiva).
    connect(view, &CadView::worldDoubleClicked, this, [this](Vec2 p) {
        for (const Entity* e : doc_.entitiesNear(p, 0.0))
            if (e->type() == EntityType::Table && editTextInPlace(*this, e->id(), p))
                return;
    });
    connect(view, &CadView::viewportViewChanged, this, [this](EntityId id, Vec2 center, double height) {
        const Entity* e = doc_.find(id);
        if (!e || e->type() != EntityType::Viewport || height <= 0.0)
            return;
        auto copy = e->clone();
        auto& vp = static_cast<ViewportEntity&>(*copy);
        vp.viewCenter = center;
        vp.viewHeight = height;
        doc_.replaceWithoutUndo(std::move(copy));
        afterStep();
    });
    connect(view, &CadView::lassoSelected, this, [this](const std::vector<Vec2>& polygon, bool crossing) {
        ctx_.shiftPressed = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        ctx_.pickTolerance = view_->pickTolerance();
        if (commands_.isActive())
            commands_.lasso(ctx_, polygon, crossing);
        else
            idlePicker_.lasso(ctx_, polygon, crossing);
        ctx_.shiftPressed = false;
        afterStep();
    });
    connect(view, &CadView::gripActionRequested, this, [this](EntityId id, int index, int action, Vec2 from) {
        if (commands_.isActive())
            return;
        ctx_.lastPoint = from;
        commands_.start(makeGripActionCommand(id, static_cast<std::size_t>(index), action, from), "STIRA", ctx_);
        afterStep();
    });
    connect(view, &CadView::cursorMoved, this, [this](Vec2 p) {
        ctx_.cursor = p;
        emit cursorMoved(p);
    });
}

void DrawingWidget::setTiles(Tiles tiles)
{
    static const int counts[] = {1, 2, 2, 3, 4};
    const std::size_t wanted = static_cast<std::size_t>(counts[static_cast<int>(tiles)]);
    tiles_ = tiles;
    const BBox area = view_->visibleArea();
    // Viste in più: partono dalla stessa zona della vista attiva.
    while (views_.size() < wanted) {
        auto* v = new CadView(doc_, commands_, selection_, idlePicker_, *settings_, this);
        views_.push_back(v);
        connectView(v);
        emit viewAdded(v);
        if (area.isValid())
            QTimer::singleShot(0, v, [v, area] { v->zoomWindow(area); });
    }
    while (views_.size() > wanted) {
        CadView* v = views_.back();
        views_.pop_back();
        if (v == view_)
            view_ = views_.front();
        v->deleteLater();
    }
    if (std::find(views_.begin(), views_.end(), view_) == views_.end())
        view_ = views_.front();
    for (CadView* v : views_) {
        if (v->shownSpace() != view_->shownSpace()) {
            v->setShownSpace(view_->shownSpace());
            v->onSpaceChanged();
        }
        v->setTileState(views_.size() > 1, v == view_);
    }
    layoutTiles();
    emit viewsChanged();
}

void DrawingWidget::layoutTiles()
{
    auto* grid = static_cast<QGridLayout*>(layout());
    for (CadView* v : views_)
        grid->removeWidget(v);
    const auto at = [&](std::size_t i, int r, int c, int rs = 1, int cs = 1) {
        if (i < views_.size())
            grid->addWidget(views_[i], r, c, rs, cs);
    };
    // Nei layout le finestre affiancate non valgono: solo la vista attiva, a tutta area.
    const bool model = view_->shownSpace() == 0;
    for (CadView* v : views_)
        v->setVisible(model || v == view_);
    if (!model) {
        grid->addWidget(view_, 0, 0);
        return;
    }
    switch (tiles_) {
    case Tiles::One: at(0, 0, 0); break;
    case Tiles::TwoVertical: at(0, 0, 0); at(1, 0, 1); break;
    case Tiles::TwoHorizontal: at(0, 0, 0); at(1, 1, 0); break;
    case Tiles::Three: at(0, 0, 0, 2, 1); at(1, 0, 1); at(2, 1, 1); break;
    case Tiles::Four: at(0, 0, 0); at(1, 0, 1); at(2, 1, 0); at(3, 1, 1); break;
    }
}

QString DrawingWidget::displayName() const
{
    return filePath_.isEmpty() ? untitledName_ : QFileInfo(filePath_).fileName();
}

QString DrawingWidget::prompt() const
{
    return commands_.isActive() ? QString::fromStdString(commands_.prompt()) : tr("Comando:");
}

// ------------------------------------------------------------ riga di comando

void DrawingWidget::submit(const QString& text)
{
    commitTextEditor();
    // Invio (o un nuovo comando) esce dal PAN.
    if (view_->panMode()) {
        view_->setPanMode(false);
        if (text.trimmed().isEmpty())
            return;
    }
    emit message(QStringLiteral("%1 %2").arg(prompt(), text).trimmed());

    // Testo libero (contenuto di un testo, nome di un blocco...): passato così com'è.
    if (wantsText()) {
        ctx_.cursor = view_->cursorWorld();
        if (text.isEmpty())
            commands_.enter(ctx_);
        else
            commands_.text(ctx_, text.toStdString());
        afterStep();
        return;
    }

    const auto parsed = parseInput(text.toStdString(), ctx_.lastPoint);
    if (!parsed) {
        emit message(tr("Input non valido: \"%1\".").arg(text.trimmed()));
        afterStep();
        return;
    }

    ctx_.cursor = view_->cursorWorld();
    ctx_.pickTolerance = view_->pickTolerance();
    const bool active = commands_.isActive();
    if (!active)
        idlePicker_.reset();

    switch (parsed->kind) {
    case ParsedInput::Kind::Empty:
        if (active) {
            commands_.enter(ctx_);
        } else if (!commands_.lastCommand().empty()) {   // Invio a vuoto ripete l'ultimo comando
            const std::string last = commands_.lastCommand();
            emit message(QString::fromStdString(last));
            commands_.execute(last, ctx_);
        }
        break;
    case ParsedInput::Kind::Word:
        if (active) {
            commands_.keyword(ctx_, parsed->word);
        } else if (uiHandler_ && uiHandler_(QString::fromStdString(parsed->word))) {
            // comando dell'interfaccia
        } else if (!startCommandWord(parsed->word)) {
            emit message(tr("Comando sconosciuto \"%1\". Scrivere AIUTO per l'elenco dei comandi, F1 per la guida.")
                             .arg(QString::fromStdString(parsed->word)));
        }
        break;
    case ParsedInput::Kind::Point:
        if (active)
            commands_.point(ctx_, parsed->point);
        else
            emit message(tr("Nessun comando attivo."));
        break;
    case ParsedInput::Kind::Value:
        if (active)
            commands_.value(ctx_, parsed->value);
        else
            emit message(tr("Nessun comando attivo."));
        break;
    }

    afterStep();
}

void DrawingWidget::runCommand(const QString& name)
{
    if (commands_.isActive())
        cancel();
    submit(name);
}

void DrawingWidget::startCommand(std::unique_ptr<Command> command, const QString& name)
{
    commitTextEditor();
    if (commands_.isActive())
        cancel();
    commands_.start(std::move(command), name.toStdString(), ctx_);
    afterStep();
}

// Senza "selezione nome/verbo" (opzione) i comandi non usano gli oggetti selezionati prima.
bool DrawingWidget::startCommandWord(const std::string& word)
{
    if (!appOptions().pickFirst)
        selection_.clear();
    return commands_.execute(word, ctx_);
}

void DrawingWidget::cancel()
{
    commitTextEditor();
    if (view_->panMode()) {   // Esc esce dal PAN
        view_->setPanMode(false);
        return;
    }
    if (commands_.isActive()) {
        commands_.cancel(ctx_);
        emit message(tr("*Annulla*"));
    } else {
        idlePicker_.reset();
        if (!selection_.empty())
            ctx_.previousSelection = selection_;   // SELECTPREVIOUS / opzione "P"
        selection_.clear();
    }
    afterStep();
}

void DrawingWidget::onPointPicked(Vec2 world)
{
    ctx_.cursor = world;
    ctx_.pickTolerance = view_->pickTolerance();
    ctx_.shiftPressed = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);

    ctx_.pickHint = view_->pickHint();   // oggetto scelto col ciclo di selezione (Maiusc+Spazio)

    if (const Command* command = commands_.active()) {
        commands_.point(ctx_, command->wantsSelection() ? view_->rawCursorWorld() : world);
    } else if (!startGripEdit(view_->rawCursorWorld())) {
        idlePicker_.click(ctx_, view_->rawCursorWorld());   // selezione con clic o finestra
    }

    ctx_.shiftPressed = false;
    ctx_.pickHint = 0;
    afterStep();
}

void DrawingWidget::afterStep()
{
    // ADDSELECTED: finito (o annullato) il comando che crea l'oggetto si torna alle proprietà correnti di prima;
    // se un comando ne ha chiesto un altro, parte adesso con i suoi input.
    if (!commands_.isActive()) {
        if (followUpRunning_) {
            followUpRunning_ = false;
            if (auto restore = std::exchange(ctx_.afterFollowUp, nullptr))
                restore();
        } else if (!ctx_.followUp.empty()) {
            const auto words = std::exchange(ctx_.followUp, {});
            followUpRunning_ = true;
            for (std::size_t i = 0; i < words.size(); ++i) {
                if (i > 0 && !commands_.isActive())
                    break;
                submit(QString::fromStdString(words[i]));
            }
        }
    }
    doc_.checkpoint();   // layer e blocchi cambiati in questo passo: annullabili
    std::erase_if(selection_, [this](EntityId id) { return doc_.find(id) == nullptr; });
    for (CadView* v : views_)
        v->refresh();
    emit changed();
}

void DrawingWidget::setTextInPlaceAllowed(std::function<bool()> allowed)
{
    ctx_.addTextInPlace = [this, allowed = std::move(allowed)](std::unique_ptr<Entity>& e) {
        if (!allowed() || view_->textEditor())
            return false;
        const EntityId id = doc_.add(std::move(e));
        // Dopo la fine del comando (il passo si chiude, poi si scrive).
        QTimer::singleShot(0, this, [this, id] {
            if (!editNewTextInPlace(*this, id))
                doc_.amendLastAdd(id, nullptr);
        });
        return true;
    };
}

void DrawingWidget::notifyChanged()
{
    afterStep();
}

// ------------------------------------------------------------------ tavolozze

void DrawingWidget::modifyEntities(const std::vector<EntityId>& ids, const std::function<void(Entity&)>& change)
{
    std::vector<std::unique_ptr<Entity>> modified;
    for (EntityId id : ids) {
        if (const Entity* e = doc_.find(id)) {
            auto copy = e->clone();
            change(*copy);
            modified.push_back(std::move(copy));
        }
    }
    doc_.commit(std::move(modified), ids);
    afterStep();
}

void DrawingWidget::setSelection(std::vector<EntityId> ids)
{
    selection_ = std::move(ids);
    afterStep();
}

void DrawingWidget::setViewport(EntityId id)
{
    const int layout = view_->shownSpace();
    if (layout == 0)
        return;
    const Entity* e = id ? doc_.find(id) : nullptr;
    if (e && e->type() != EntityType::Viewport)
        return;
    commands_.cancel(ctx_);
    selection_.clear();
    idlePicker_.reset();
    ctx_.lastPoint.reset();
    doc_.setActiveSpace(e ? 0 : layout);
    view_->setActiveViewport(e ? id : 0);
    afterStep();
}

void DrawingWidget::setSpace(int space)
{
    if (space == view_->shownSpace() && space == doc_.activeSpace())
        return;
    commands_.cancel(ctx_);
    selection_.clear();
    idlePicker_.reset();
    ctx_.lastPoint.reset();
    doc_.setActiveSpace(space);
    for (CadView* v : views_) {   // finestre affiancate: tutte sullo stesso spazio (nei layout se ne vede una)
        v->setShownSpace(space);
        v->onSpaceChanged();
    }
    layoutTiles();
    afterStep();
}

// ----------------------------------------------------------------------- file

namespace {

bool isDwgPath(const QString& path)
{
    return path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive) || path.endsWith(QStringLiteral(".dwt"), Qt::CaseInsensitive);
}

QString missingConverter()
{
    if (DwgConverter::engineName() == QLatin1String("LibreDWG"))
        return QObject::tr("Il supporto DWG non è installato: manca LibreDWG in %1.")
            .arg(QDir::toNativeSeparators(DwgConverter::toolsDirectory()));
    return QObject::tr("ODA File Converter non è installato: installarlo o scegliere LibreDWG nelle Opzioni "
                       "(scheda Apri e salva).");
}

// Opzione "Crea copia di backup": il file che sta per essere sovrascritto diventa .bak.
void makeBackup(const QString& path)
{
    if (!appOptions().backup || !QFileInfo::exists(path))
        return;
    const QString bak = QFileInfo(path).path() + QLatin1Char('/') + QFileInfo(path).completeBaseName()
                        + QStringLiteral(".bak");
    QFile::remove(bak);
    QFile::copy(path, bak);
}

} // namespace

bool DrawingWidget::load(const QString& path, QString* error, QStringList* notes)
{
    Document loaded;
    DxfReadResult result;
    QString readError;
    if (!readFile(path, loaded, &readError, &result)) {
        if (error)
            *error = readError;
        return false;
    }
    adopt(std::move(loaded), path, result, notes);
    return true;
}

void DrawingWidget::adopt(Document&& document, const QString& path, const DxfReadResult& result, QStringList* notes)
{
    commands_.cancel(ctx_);
    doc_ = std::move(document);
    view_->resetSpaces();
    selection_.clear();
    ctx_.lastPoint.reset();
    filePath_ = path;
    doc_.setFilePath(QFileInfo(path).absoluteFilePath().toStdString());
    fromDwg_ = isDwgPath(path);
    doc_.resetCheckpoint();
    savedRevision_ = doc_.revision();

    if (notes) {
        *notes << tr("Aperto %1: %2 oggetti%3.")
                      .arg(QDir::toNativeSeparators(path))
                      .arg(result.imported)
                      .arg(fromDwg_ ? tr(" (DWG letto con %1)").arg(DwgConverter::engineName()) : QString());
        if (!result.skippedTypes.empty()) {
            QStringList names;
            for (const auto& type : result.skippedTypes)
                names << QString::fromStdString(type);
            *notes << tr("Tipi di oggetto non ancora supportati (ignorati): %1").arg(names.join(", "));
        }
        for (const std::string& note : result.notes)
            *notes << QString::fromStdString(note);
        // Immagini raster: quelle non trovate si segnalano subito (gli XREF li segnala già la lettura).
        for (const ExternalRef& r : externalReferences(doc_))
            if (r.kind == ExternalRef::Image && !r.loaded)
                *notes << tr("Immagine \"%1\" (%2 nel disegno): %3 - percorso salvato: %4. Vedi XRIF.")
                              .arg(r.name)
                              .arg(r.uses)
                              .arg(externalRefStatus(r).toLower(), QString::fromStdString(r.saved));
    }
    afterStep();
}

void DrawingWidget::forgetFile()
{
    filePath_.clear();
    fromDwg_ = false;
    savedRevision_ = doc_.revision();
}

bool DrawingWidget::saveTo(const QString& path, QString* error, DxfVersion version)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    // Per il DWG si scrive un DXF temporaneo che il convertitore trasforma.
    QTemporaryDir temp;
    QString dxfPath = path;
    if (isDwgPath(path)) {
        if (!DwgConverter::available())
            return fail(missingConverter());
        if (!temp.isValid())
            return fail(tr("Impossibile creare una cartella temporanea."));
        dxfPath = temp.filePath(QStringLiteral("da_convertire.dxf"));
    }

    // Salvataggio in un'altra cartella: i percorsi relativi di XREF e immagini puntano ancora agli stessi file.
    if (const int rebased = rebaseRelativeRefs(doc_, QString::fromStdString(doc_.filePath()), QFileInfo(path).absoluteFilePath()))
        emit message(tr("Percorsi relativi di riferimenti esterni e immagini aggiornati per la nuova cartella: %1.").arg(rebased));

    std::string failure;
    std::vector<std::string> warnings;
    const DxfVersion written = isDwgPath(path) ? DxfVersion::R2000ForDwg : version;
    if (!isDwgPath(path))
        makeBackup(path);
    if (!writeDxf(doc_, toFsPath(dxfPath), &failure, &warnings, written))
        return fail(QString::fromStdString(failure));

    if (isDwgPath(path)) {
        QString conversionError, engine;
        // Diagnosi: copia del DXF intermedio passato al convertitore.
        if (const QString keep = qEnvironmentVariable("CARTESIO_KEEP_DWG_DXF"); !keep.isEmpty()) {
            QFile::remove(keep);
            QFile::copy(dxfPath, keep);
        }
        makeBackup(path);
        if (!DwgConverter::dxfToDwg(dxfPath, path, &conversionError, &engine))
            return fail(tr("Conversione in DWG non riuscita.\n%1").arg(conversionError));
        warnings.push_back(engine == QLatin1String("LibreDWG")
                               ? tr("Salvato in formato DWG R2000 con LibreDWG (funzione sperimentale).").toStdString()
                               : tr("Salvato in formato DWG con %1.").arg(engine).toStdString());
    }

    for (const auto& w : warnings)
        emit message(QString::fromStdString(w));
    if (!isDwgPath(path))
        dxfVersion_ = version;
    filePath_ = path;
    doc_.setFilePath(QFileInfo(path).absoluteFilePath().toStdString());
    fromDwg_ = isDwgPath(path);
    doc_.resetCheckpoint();
    savedRevision_ = doc_.revision();
    emit changed();
    return true;
}

} // namespace cartesio

namespace cartesio {

// Clic su una maniglia di un oggetto selezionato: parte la modifica con le maniglie.
bool DrawingWidget::startGripEdit(Vec2 p)
{
    if (selection_.empty() || idlePicker_.corner()   // niente maniglie durante una finestra di selezione
        || selection_.size() > CadView::kGripObjectLimit)   // né oltre GRIPOBJLIMIT (non si vedono)
        return false;
    // Solo dentro il quadratino disegnato: con la tolleranza del mirino di selezione, più larga,
    // un clic vicino a una maniglia la "prendeva" e restava in attesa del trascinamento invece di selezionare.
    const double tolerance = view_->pixelsToWorld(appOptions().gripSize + 1.0);
    for (EntityId id : selection_) {
        const Entity* e = doc_.find(id);
        if (!e || !doc_.isLayerSelectable(e->layer()))
            continue;
        const auto points = e->gripPoints();
        // Le stesse maniglie disegnate dalla vista: per i blocchi solo il punto di inserimento.
        const std::size_t count = e->type() == EntityType::Insert ? std::min<std::size_t>(1, points.size()) : points.size();
        for (std::size_t i = 0; i < count; ++i) {
            if (points[i].kind == SnapKind::Quadrant && e->type() == EntityType::Arc)
                continue;
            if (std::abs(points[i].position.x - p.x) <= tolerance && std::abs(points[i].position.y - p.y) <= tolerance) {
                ctx_.lastPoint = points[i].position;
                commands_.start(makeGripEditCommand(id, i, points[i].position), "STIRA", ctx_);
                return true;
            }
        }
    }
    return false;
}

} // namespace cartesio

namespace cartesio {

bool DrawingWidget::attachXref(const QString& path, QString* error, int pathType)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        if (error)
            *error = tr("File non trovato: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    // Nome del blocco: quello del file; se è già usato da un altro blocco si aggiunge un numero.
    const int type = pathType >= 0 ? pathType : appOptions().refPathType;
    if (type == RefPathRelative && doc_.filePath().empty())
        emit message(tr("Il disegno non è ancora salvato: percorso completo (si può cambiare in XRIF dopo il salvataggio)."));
    const std::string saved = refPath(info.absoluteFilePath(), QString::fromStdString(doc_.filePath()), type);
    const std::string base = info.completeBaseName().toStdString();
    std::string name = base;
    for (int n = 2;; ++n) {
        const auto existing = doc_.findBlock(name);
        if (!existing || (existing->isXref() && existing->xrefPath == saved))
            break;
        name = base + "(" + std::to_string(n) + ")";
    }
    if (!doc_.findBlock(name)) {
        auto def = std::make_shared<BlockDef>();
        def->name = name;
        def->xrefPath = saved;
        doc_.addBlock(def);
    }
    DxfReadResult result;
    loadXrefs(QString::fromStdString(doc_.filePath()), doc_, result);
    for (const std::string& note : result.notes)
        emit message(QString::fromStdString(note));
    const auto def = doc_.findBlock(name);
    if (!def || !def->xrefLoaded) {
        if (error)
            *error = tr("Riferimento esterno non caricato: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    doc_.touch();
    afterStep();
    submit(QStringLiteral("INSERT"));
    submit(QString::fromStdString(name));
    return true;
}

int DrawingWidget::shownSpace() const
{
    return view_->shownSpace();
}

} // namespace cartesio

namespace cartesio {

// Legge un DXF, o un DWG convertito da LibreDWG in una cartella temporanea.
bool DrawingWidget::readFile(const QString& path, Document& out, QString* error, DxfReadResult* details,
                             const std::function<void(const QString&)>& phase)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    QTemporaryDir temp;
    QString dxfPath = path;
    QString alternative;   // seconda conversione di LibreDWG (DXF 2000), da unire alla prima
    if (isDwgPath(path)) {
        if (!DwgConverter::available())
            return fail(missingConverter());
        if (!temp.isValid())
            return fail(tr("Impossibile creare una cartella temporanea."));
        dxfPath = temp.filePath(QStringLiteral("convertito.dxf"));
        if (phase)
            phase(tr("Conversione del DWG (%1)").arg(DwgConverter::engineName()));
        QString conversionError;
        if (DwgConverter::engineName() == QLatin1String("LibreDWG")) {
            alternative = temp.filePath(QStringLiteral("convertito_r2000.dxf"));
            if (!DwgConverter::libreDwgToDxfPair(path, dxfPath, alternative, &conversionError))
                return fail(tr("Conversione DWG non riuscita.\n%1").arg(conversionError));
        } else if (!DwgConverter::dwgToDxf(path, dxfPath, &conversionError)) {
            return fail(tr("Conversione DWG non riuscita.\n%1").arg(conversionError));
        }
    }
    if (phase)
        phase(tr("Lettura del disegno"));
    DxfReadResult result = readDxf(toFsPath(dxfPath), out);
    if (!result.ok)
        return fail(QString::fromStdString(result.error));
    if (!alternative.isEmpty() && QFileInfo::exists(alternative)) {
        if (phase)
            phase(tr("Unione delle due conversioni"));
        Document second;
        if (readDxf(toFsPath(alternative), second).ok) {
            const DwgMergeReport merged = mergeAlternativeConversion(out, second);
            result.imported += static_cast<int>(merged.entities);
            if (merged.blocks || merged.layouts)
                result.notes.push_back(tr("LibreDWG: recuperati dalla seconda conversione %1 blocchi e %2 layout.")
                                              .arg(merged.blocks)
                                              .arg(merged.layouts)
                                              .toStdString());
        }
    }
    loadXrefs(path, out, result);
    if (details)
        *details = std::move(result);
    return true;
}

// Riferimenti esterni (XREF): ogni blocco collegato a un file viene riempito con lo spazio modello di quel
// disegno, cercato come le immagini (percorso salvato, cartella del disegno...). Riferimenti annidati fino a
// 8 livelli; le sovrapposizioni dei disegni riferiti non si caricano, i riferimenti circolari si saltano.
void DrawingWidget::loadXrefs(const QString& hostPath, Document& doc, DxfReadResult& result)
{
    thread_local QStringList chain;   // disegni in corso di lettura, dal principale al riferimento attuale
    const bool nested = !chain.isEmpty();
    chain << QFileInfo(hostPath).absoluteFilePath();
    std::vector<std::shared_ptr<BlockDef>> xrefs;
    for (const auto& b : doc.blocks())
        if (b->isXref() && !b->xrefLoaded && !(nested && b->xrefOverlay))
            xrefs.push_back(b);
    for (const auto& x : xrefs) {
        const QString saved = QString::fromStdString(x->xrefPath);
        QString file = ImageLibrary::locate(hostPath, x->xrefPath);
        // Riferimento salvato senza estensione o con un'estensione diversa: si prova anche DWG/DXF.
        if (file.isEmpty())
            for (const char* ext : {".dwg", ".dxf"}) {
                const QFileInfo info(QDir::fromNativeSeparators(saved));
                file = ImageLibrary::locate(hostPath, (info.path() + QLatin1Char('/') + info.completeBaseName()
                                                       + QLatin1String(ext)).toStdString());
                if (!file.isEmpty())
                    break;
            }
        if (file.isEmpty()) {
            result.notes.push_back(tr("Riferimento esterno \"%1\" non trovato: %2")
                                       .arg(QString::fromStdString(x->name), saved).toStdString());
            continue;
        }
        if (chain.contains(QFileInfo(file).absoluteFilePath()) || chain.size() > 8) {
            result.notes.push_back(tr("Riferimento esterno \"%1\" circolare o troppo annidato: ignorato.")
                                       .arg(QString::fromStdString(x->name)).toStdString());
            continue;
        }
        Document source;
        QString error;
        DxfReadResult inner;
        if (!readFile(file, source, &error, &inner)) {
            result.notes.push_back(tr("Riferimento esterno \"%1\" non letto: %2")
                                       .arg(QString::fromStdString(x->name), error).toStdString());
            continue;
        }
        // Unità diverse: il riferimento si scala (solo se entrambi i disegni le dichiarano).
        double scale = 1.0;
        if (source.insUnits() > 0 && doc.insUnits() > 0)
            scale = unitsFactor(source.insUnits(), doc.insUnits());
        x->xrefFound = file.toStdString();
        loadXref(doc, *x, source, scale);
        for (const std::string& note : inner.notes)
            if (note.rfind(tr("Riferimento esterno").toStdString(), 0) == 0)
                result.notes.push_back(note);
        result.notes.push_back(tr("Riferimento esterno \"%1\" caricato da %2")
                                   .arg(QString::fromStdString(x->name), QDir::toNativeSeparators(file))
                                   .toStdString());
    }
    chain.removeLast();
}

// INSERT di un file: il disegno diventa un blocco con il nome del file, poi si posiziona con INSERT.
bool DrawingWidget::insertFile(const QString& path, QString* error)
{
    Document source;
    if (!readFile(path, source, error))
        return false;
    const std::string name = QFileInfo(path).completeBaseName().toStdString();
    // Unità diverse (es. un file in mm in un disegno in cm): il blocco viene scalato.
    const AppOptions& options = appOptions();
    const int from = source.insUnits() > 0 ? source.insUnits() : options.insertSourceUnits;
    const int to = doc_.insUnits() > 0 ? doc_.insUnits() : options.insertTargetUnits;
    const double scale = unitsFactor(from, to);
    if (scale != 1.0)
        emit message(tr("Unità del file: %1, del disegno: %2 - inserito in scala %3.")
                         .arg(unitsName(from), unitsName(to))
                         .arg(scale, 0, 'g', 6));
    importDrawingAsBlock(doc_, source, name, scale);
    doc_.touch();
    afterStep();
    submit(QStringLiteral("INSERT"));
    submit(QString::fromStdString(name));
    return true;
}

} // namespace cartesio
