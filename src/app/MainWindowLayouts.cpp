// Parte della finestra principale dedicata a layout, stampa su PDF, script (.scr) e variabili di sistema.
#include "app/MainWindow.h"
#include "app/CanaloPanel.h"
#include "app/PlantPanel.h"
#include "app/GuidePanel.h"
#include "app/Ribbon.h"
#include <map>
#include <QQuickItem>
#include <QCursor>
#include "app/MainView.h"
#include "app/FormDialog.h"
#include "app/PlotTools.h"
#include "app/PlotEngine.h"

#include "app/CadView.h"
#include "app/TextRender.h"
#include "app/TextEditInPlace.h"
#include "app/CanvasItem.h"
#include <QQuickWindow>
#include <qpa/qwindowsysteminterface.h>
#include "app/CommandLine.h"
#include "app/QmlUi.h"
#include "app/DrawingWidget.h"
#include "app/Icons.h"
#include "app/LayerManager.h"
#include "app/CircuitsPanel.h"
#include "app/OptionsDialog.h"
#include "app/ExternalRefs.h"
#include "app/ColorDialog.h"
#include "app/DimStyleDialog.h"
#include "app/StyleDialogs.h"
#include "app/StartPage.h"
#include "core/DocCompare.h"
#include "core/QuickSelect.h"
#include "core/Import.h"
#include "app/BlockLibrary.h"
#include "app/PropertiesPalette.h"

#include <QQmlError>
#include <QComboBox>
#include <QElapsedTimer>
#include <QMimeData>
#include <QDropEvent>
#include <QMouseEvent>
#include <QMap>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QEnterEvent>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

namespace cartesio {

// ================================================================= schede layout

void MainWindow::buildLayoutTabs()
{
    layoutTabs_ = status_->layoutTabs();
    connect(layoutTabs_, &TabsModel::newRequested, this, &MainWindow::newLayout);
    connect(layoutTabs_, &TabsModel::currentChanged, this, [this](int index) {
        DrawingWidget* d = current();
        if (updatingLayoutTabs_ || !d || index < 0)
            return;
        const int id = layoutTabs_->tabData(index).toInt();
        if (id < 0)   // scheda di un gruppo: il menu dei suoi layout (clicked)
            return;
        d->setSpace(id);
        commandLine_->input()->setFocus();
    });
    // Scheda di un gruppo: menu dei suoi layout, sopra la scheda.
    connect(layoutTabs_, &TabsModel::clicked, this, [this](int index) {
        if (!current() || index < 0 || index >= layoutTabs_->count())
            return;
        const int id = layoutTabs_->tabData(index).toInt();
        if (id < 0)
            QTimer::singleShot(0, this, [this, group = layoutTabs_->tabText(index)] { showLayoutGroupMenu(group, QCursor::pos()); });
    });
    connect(layoutTabs_, &TabsModel::menuRequested, this, &MainWindow::showLayoutMenu);
    connect(status_, &StatusBar::layoutListClicked, this, [this](QPoint pos) { showLayoutList(pos); });
}

// Gruppi delle schede: nome del gruppo di ogni layout (con le schede raggruppate e nessun gruppo definito, tutti
// i layout stanno in "Layout").
QString MainWindow::layoutGroupOf(const Document& doc, const Layout& layout) const
{
    if (!layout.group.empty())
        return QString::fromStdString(layout.group);
    bool anyGroup = false;
    for (const Layout& l : doc.layouts())
        anyGroup = anyGroup || !l.group.empty();
    return anyGroup ? QString() : tr("Layout");
}

void MainWindow::showLayoutGroupMenu(const QString& tabText, QPoint globalPos)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    // Il testo della scheda è "Gruppo: layout ▾" o "Gruppo (n) ▾": il nome del gruppo è prima di ":" o " (".
    QString group = tabText;
    group = group.left(group.indexOf(QStringLiteral(": ")) >= 0 ? group.indexOf(QStringLiteral(": ")) : group.size());
    group = group.left(group.indexOf(QStringLiteral(" (")) >= 0 ? group.indexOf(QStringLiteral(" (")) : group.size());
    group.remove(QStringLiteral(" \u25BE"));
    QMenu menu(this);
    for (const Layout& l : d->document().layouts()) {
        if (layoutGroupOf(d->document(), l) != group)
            continue;
        QAction* a = menu.addAction(QString::fromStdString(l.name));
        a->setCheckable(true);
        a->setChecked(l.id == d->shownSpace());
        const int id = l.id;
        connect(a, &QAction::triggered, this, [this, id] {
            if (DrawingWidget* dw = current())
                dw->setSpace(id);
            refreshLayoutTabs();
        });
    }
    menu.exec(globalPos - QPoint(0, menu.sizeHint().height()));
    refreshLayoutTabs();   // la scheda scelta torna quella dello spazio mostrato
}

void MainWindow::showLayoutList(QPoint globalPos)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const Document& doc = d->document();
    QMenu menu(this);
    const auto add = [this, d](QMenu* into, const QString& text, int id) {
        QAction* a = into->addAction(text);
        a->setCheckable(true);
        a->setChecked(id == d->shownSpace());
        connect(a, &QAction::triggered, this, [this, id] {
            if (DrawingWidget* dw = current())
                dw->setSpace(id);
            refreshLayoutTabs();
        });
    };
    add(&menu, tr("Modello"), 0);
    menu.addSeparator();
    std::map<QString, QMenu*> groups;
    for (const Layout& l : doc.layouts()) {
        const QString group = QString::fromStdString(l.group);
        if (group.isEmpty()) {
            add(&menu, QString::fromStdString(l.name), l.id);
            continue;
        }
        QMenu*& sub = groups[group];
        if (!sub)
            sub = menu.addMenu(group);
        add(sub, QString::fromStdString(l.name), l.id);
    }
    menu.addSeparator();
    QAction* grouped = menu.addAction(tr("Raggruppa le schede dei layout"));
    grouped->setCheckable(true);
    grouped->setChecked(appOptions().groupLayoutTabs);
    connect(grouped, &QAction::toggled, this, [this](bool on) {
        appOptions().groupLayoutTabs = on;
        appOptions().save();
        refreshLayoutTabs();
    });
    menu.exec(globalPos - QPoint(0, menu.sizeHint().height()));
}

// Mette i layout indicati nel gruppo `group` (vuoto = nessun gruppo).
void MainWindow::setLayoutGroup(const std::vector<int>& ids, const QString& group)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    for (int id : ids)
        if (Layout* l = d->document().findLayout(id))
            l->group = group.trimmed().toStdString();
    d->document().touch();
    d->notifyChanged();
    refreshLayoutTabs();
}

void MainWindow::refreshLayoutTabs()
{
    if (!layoutTabs_)
        return;
    DrawingWidget* d = current();
    status_->setTabsVisible(d != nullptr && appOptions().showLayoutTabs);
    if (!d) {
        status_->setSpace(QString(), false);
        status_->setViewport(false);
        status_->setAnnotation(false);
        return;
    }
    const Document& doc = d->document();

    // Si ricostruiscono le schede solo se nomi o layout sono cambiati. Con le schede raggruppate: una scheda per
    // gruppo (dato negativo: si apre il menu dei suoi layout) e i layout senza gruppo.
    QStringList wanted{tr("Modello")};
    QList<int> ids{0};
    int activeGroupTab = -1;
    if (appOptions().groupLayoutTabs) {
        QStringList seen;
        for (const Layout& l : doc.layouts()) {
            const QString group = layoutGroupOf(doc, l);
            if (group.isEmpty()) {
                wanted << QString::fromStdString(l.name);
                ids << l.id;
                continue;
            }
            int count = 0;
            const Layout* currentIn = nullptr;
            for (const Layout& o : doc.layouts())
                if (layoutGroupOf(doc, o) == group) {
                    ++count;
                    if (o.id == d->shownSpace())
                        currentIn = &o;
                }
            if (seen.contains(group)) {
                continue;
            }
            seen << group;
            wanted << (currentIn ? group + QStringLiteral(": ") + QString::fromStdString(currentIn->name)
                                 : group + QStringLiteral(" (%1)").arg(count))
                          + QStringLiteral(" \u25BE");
            ids << -static_cast<int>(seen.size());
            if (currentIn)
                activeGroupTab = static_cast<int>(wanted.size()) - 1;
        }
    } else {
        for (const Layout& l : doc.layouts()) {
            wanted << QString::fromStdString(l.name);
            ids << l.id;
        }
    }
    QStringList present;
    for (int i = 0; i < layoutTabs_->count(); ++i)
        present << layoutTabs_->tabText(i);

    updatingLayoutTabs_ = true;
    if (present != wanted || layoutTabs_->count() != ids.size()) {
        while (layoutTabs_->count() > 0)
            layoutTabs_->removeTab(0);
        for (int i = 0; i < wanted.size(); ++i) {
            const int index = layoutTabs_->addTab(wanted[i]);
            layoutTabs_->setTabData(index, ids[i]);
        }
    } else {
        for (int i = 0; i < ids.size(); ++i)
            layoutTabs_->setTabData(i, ids[i]);
    }
    const int active = activeGroupTab >= 0 ? activeGroupTab : static_cast<int>(std::max<qsizetype>(0, ids.indexOf(d->shownSpace())));
    layoutTabs_->setCurrentIndex(active);
    updatingLayoutTabs_ = false;

    status_->setSpace(doc.activeSpace() == 0 ? tr("MODELLO") : tr("CARTA"), d->shownSpace() != 0);
    syncMainViews();
    if (ribbon_) {   // scheda Tavola: solo quando si è in un layout; Confronto: se il disegno è in confronto
        ribbon_->showContextual(tr("Tavola"), d->shownSpace() != 0, false);
        ribbon_->showContextual(tr("Confronto"), d->view()->compare() != nullptr, false);
    }
    updateViewportStatus();
}

void MainWindow::showLayoutMenu(int index, const QPoint& globalPos)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const int id = index >= 0 ? layoutTabs_->tabData(index).toInt() : 0;

    QMenu menu(this);
    menu.addAction(icon(Icon::LayoutNew), tr("Nuovo layout"), this, &MainWindow::newLayout);
    // Gruppi: sposta il layout in un gruppo (nuovo o esistente), toglilo; raggruppa le schede.
    const auto groupMenu = [this, d, &menu](const std::vector<int>& ids) {
        QMenu* sub = menu.addMenu(tr("Sposta nel gruppo"));
        QStringList existing;
        for (const Layout& l : d->document().layouts())
            if (!l.group.empty() && !existing.contains(QString::fromStdString(l.group)))
                existing << QString::fromStdString(l.group);
        for (const QString& g : existing)
            sub->addAction(g, this, [this, ids, g] { setLayoutGroup(ids, g); });
        if (!existing.isEmpty())
            sub->addSeparator();
        sub->addAction(tr("Nuovo gruppo..."), this, [this, ids] {
            QString name;
            FormDialog ask(tr("Nuovo gruppo di layout"), FormDialog::Ok | FormDialog::Cancel, this);
            ask.text(tr("Nome del gruppo:"), name, tr("es. Piano terra"));
            ask.setSize(360, 140);
            if (ask.exec() == QDialog::Accepted && !name.trimmed().isEmpty())
                setLayoutGroup(ids, name);
        });
        sub->addAction(tr("Nessun gruppo"), this, [this, ids] { setLayoutGroup(ids, QString()); });
    };
    if (id < 0) {   // scheda di un gruppo
        std::vector<int> members;
        const QString text = layoutTabs_->tabText(index);
        for (const Layout& l : d->document().layouts())
            if (!l.group.empty() && text.startsWith(QString::fromStdString(l.group)))
                members.push_back(l.id);
        if (!members.empty()) {
            menu.addAction(tr("Rinomina il gruppo..."), this, [this, members, d] {
                QString name = QString::fromStdString(d->document().findLayout(members.front())->group);
                FormDialog ask(tr("Rinomina il gruppo"), FormDialog::Ok | FormDialog::Cancel, this);
                ask.text(tr("Nome del gruppo:"), name);
                ask.setSize(360, 140);
                if (ask.exec() == QDialog::Accepted && !name.trimmed().isEmpty())
                    setLayoutGroup(members, name);
            });
            menu.addAction(tr("Sciogli il gruppo"), this, [this, members] { setLayoutGroup(members, QString()); });
        }
    } else if (id != 0) {
        groupMenu({id});
    }
    QAction* grouped = menu.addAction(tr("Raggruppa le schede dei layout"));
    grouped->setCheckable(true);
    grouped->setChecked(appOptions().groupLayoutTabs);
    connect(grouped, &QAction::toggled, this, [this](bool on) {
        appOptions().groupLayoutTabs = on;
        appOptions().save();
        refreshLayoutTabs();
    });
    menu.addSeparator();
    if (id > 0) {
        menu.addAction(tr("Rinomina..."), this, [this, id] { renameLayout(id); });
        menu.addAction(tr("Elimina"), this, [this, id] { deleteLayout(id); });
        menu.addSeparator();
        menu.addAction(icon(Icon::PageSetup), tr("Imposta pagina..."), this, [this, id] { pageSetup(id); });
        menu.addAction(icon(Icon::PlotIcon), tr("Stampa..."), this, [this, id, d] {
            d->setSpace(id);
            plotDialog();
        });
        menu.addAction(icon(Icon::PreviewIcon), tr("Anteprima di stampa"), this, [this, id, d] {
            d->setSpace(id);
            plotPreview();
        });
    }
    menu.exec(globalPos);
}

void MainWindow::newLayout()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const int id = d->document().addLayout();
    // Foglio, orientamento, dispositivo e opzioni di stampa dalle Opzioni.
    if (Layout* layout = d->document().findLayout(id))
        applyPlotSettings(*layout, newLayoutPlotSettings());
    d->notifyChanged();
    d->setSpace(id);
    // Un nuovo layout ha una finestra che mostra tutto il modello (si può togliere).
    if (appOptions().viewportInNewLayouts) {
        d->runCommand(QStringLiteral("MVIEW"));
        d->submit(QStringLiteral("A"));
    }
    d->view()->zoomExtents();
    refreshLayoutTabs();
    if (appOptions().pageSetupForNewLayouts)
        QTimer::singleShot(0, this, [this, id] { pageSetup(id); });
}

void MainWindow::renameLayout(int id)
{
    DrawingWidget* d = current();
    const Layout* layout = d ? d->document().findLayout(id) : nullptr;
    if (!layout)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rinomina layout"), tr("Nome:"), QLineEdit::Normal,
                                               QString::fromStdString(layout->name), &ok);
    if (!ok)
        return;
    std::string error;
    if (!d->document().renameLayout(id, name.trimmed().toStdString(), &error))
        QMessageBox::warning(this, tr("Rinomina layout"), QString::fromStdString(error));
    d->notifyChanged();
}

void MainWindow::deleteLayout(int id)
{
    DrawingWidget* d = current();
    const Layout* layout = d ? d->document().findLayout(id) : nullptr;
    if (!layout)
        return;
    if (QMessageBox::question(this, tr("Elimina layout"),
                              tr("Eliminare il layout \"%1\" e tutto il suo contenuto?")
                                  .arg(QString::fromStdString(layout->name)))
        != QMessageBox::Yes)
        return;
    if (d->shownSpace() == id)
        d->setSpace(0);
    std::string error;
    if (!d->document().removeLayout(id, &error))
        QMessageBox::warning(this, tr("Elimina layout"), QString::fromStdString(error));
    d->notifyChanged();
}

void MainWindow::pageSetup(int id)
{
    if (DrawingWidget* d = current())
        showPageSetupManager(this, *d, d->document().findLayout(id) ? id : 0);
}

// ================================================================== script

void MainWindow::runScriptDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Esegui script"), QString(),
                                                      tr("Script (*.scr);;Tutti i file (*)"));
    if (!path.isEmpty())
        runScript(path);
}

// Esegue uno script: il testo viene passato alla riga di comando, dove spazi e
// a capo valgono come Invio; i testi (TEXT, nomi...) arrivano fino a fine riga; ";" commenta una riga.
bool MainWindow::runScript(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        print(tr("Impossibile aprire lo script %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QByteArray data = file.readAll();
    QString text = QString::fromUtf8(data);
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLatin1(data);   // script scritti con la codifica di Windows
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    const bool nested = scriptRunning_;
    scriptRunning_ = true;
    print(tr("Esecuzione dello script %1").arg(QDir::toNativeSeparators(path)));
    const QString scriptDir = QFileInfo(path).absolutePath();

    qsizetype i = 0;
    const qsizetype n = text.size();
    // Prossima parola (anche tra virgolette); consuma il carattere che la termina.
    const auto nextToken = [&]() {
        QString token;
        if (i < n && text[i] == '"') {
            const qsizetype end = text.indexOf('"', i + 1);
            token = text.mid(i + 1, (end < 0 ? n : end) - i - 1);
            i = end < 0 ? n : end + 1;
        } else {
            while (i < n && text[i] != ' ' && text[i] != '\n' && text[i] != '\t')
                token += text[i++];
        }
        if (i < n && (text[i] == ' ' || text[i] == '\n' || text[i] == '\t'))
            ++i;
        return token;
    };
    const auto resolve = [&](const QString& name) {
        return QFileInfo(name).isRelative() ? QDir(scriptDir).filePath(name) : name;
    };

    bool quit = false;
    while (i < n && !quit) {
        DrawingWidget* d = current();

        // Testo libero (contenuto di un testo, nome di blocco...): fino a fine riga.
        if (d && d->wantsText() && !d->wantsLine()) {   // un nome: fino al primo spazio
            d->submit(nextToken());
            continue;
        }
        if (d && d->wantsLine()) {
            qsizetype end = text.indexOf('\n', i);
            if (end < 0)
                end = n;
            d->submit(text.mid(i, end - i));
            i = end + 1;
            continue;
        }
        // Riga di commento.
        if ((i == 0 || text[i - 1] == '\n') && text[i] == ';') {
            const qsizetype end = text.indexOf('\n', i);
            i = end < 0 ? n : end + 1;
            continue;
        }

        const QString token = nextToken();
        QString word = token.trimmed().toUpper();
        while (word.startsWith('_') || word.startsWith('.'))
            word.remove(0, 1);

        // Diagnosi (non documentato): tempi delle ricerche nel disegno (indice spaziale): -BENCHQUERY n.
        // vicino = entitiesNear in n punti della vista, pick, rettangolo = entitiesInRect della vista e di un
        // decimo, finestra/intersezione = selectInRect, modifica = spostare un oggetto e la prima ricerca dopo.
        // Diagnosi (non documentato): -EXTENTSINFO n, gli n oggetti del modello con l'ingombro più lontano dal centro
        // della mediana degli oggetti (chi allarga ZOOM Estensioni).
        if (d && word == QLatin1String("-EXTENTSINFO")) {
            const int n = std::max(1, nextToken().toInt());
            // Facoltativo: il nome di un blocco ("-" = il modello), per guardare dentro.
            const QString blockName = nextToken();
            std::vector<const Entity*> pool;
            if (blockName.isEmpty() || blockName == QLatin1String("-")) {
                for (const auto& e : d->document().entities())
                    if (e->space() == 0)
                        pool.push_back(e.get());
            } else if (const auto block = d->document().findBlock(QString(blockName).replace(u'_', u' ').toStdString())) {   // "_" = spazio
                for (const auto& e : block->entities)
                    pool.push_back(e.get());
            }
            std::vector<std::pair<double, const Entity*>> far;
            std::vector<double> xs, ys;
            for (const Entity* e : pool)
                if (true)
                    if (const BBox b = e->bounds(); b.isValid()) {
                        xs.push_back(b.center().x);
                        ys.push_back(b.center().y);
                    }
            if (xs.empty())
                continue;
            std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
            std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
            const Vec2 mid{xs[xs.size() / 2], ys[ys.size() / 2]};
            for (const Entity* e : pool)
                if (const BBox b = e->bounds(); b.isValid())
                    far.emplace_back(std::max({distance(b.min, mid), distance(b.max, mid)}), e);
            std::sort(far.begin(), far.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            const BBox ext = d->document().extents(0);
            print(QStringLiteral("-EXTENTSINFO: estensione %1,%2 - %3,%4; mediana %5,%6")
                      .arg(ext.min.x).arg(ext.min.y).arg(ext.max.x).arg(ext.max.y).arg(mid.x).arg(mid.y));
            for (int i = 0; i < n && i < static_cast<int>(far.size()); ++i) {
                const Entity* e = far[static_cast<std::size_t>(i)].second;
                const BBox b = e->bounds();
                QString extra;
                if (e->type() == EntityType::Insert)
                    extra = QString::fromStdString(static_cast<const InsertEntity*>(e)->blockName);
                print(QStringLiteral("  %1 %2 layer %3: %4,%5 - %6,%7")
                          .arg(QString::fromStdString(entityTypeName(e->type())), extra,
                               QString::fromStdString(d->document().layers()[static_cast<std::size_t>(std::max(0, e->layer()))].name))
                          .arg(b.min.x).arg(b.min.y).arg(b.max.x).arg(b.max.y));
            }
            continue;
        }
        if (d && word == QLatin1String("-BENCHQUERY")) {
            bool ok = false;
            int n = nextToken().toInt(&ok);
            if (!ok || n <= 0)
                n = 200;
            Document& doc = d->document();
            CadView* v = d->view();
            const BBox view = v->visibleArea();
            const double tol = v->pickTolerance();
            QElapsedTimer t;
            const auto pointAt = [&view](int k) {
                return Vec2{view.min.x + view.width() * ((k * 37) % 101) / 100.0,
                            view.min.y + view.height() * ((k * 61) % 103) / 102.0};
            };
            doc.entitiesNear({0, 0}, 0.0);   // ingombri pronti
            std::size_t found = 0;
            t.start();
            for (int k = 0; k < n; ++k)
                found += doc.entitiesNear(pointAt(k), tol).size();
            const double nearMs = t.nsecsElapsed() / 1e6 / n;
            t.start();
            for (int k = 0; k < n; ++k)
                doc.pick(pointAt(k), tol);
            const double pickMs = t.nsecsElapsed() / 1e6 / n;
            t.start();
            const std::size_t inView = doc.entitiesInRect(view).size();
            const double rectMs = t.nsecsElapsed() / 1e6;
            BBox small;
            small.expand(view.min + Vec2{view.width() * 0.45, view.height() * 0.45});
            small.expand(view.min + Vec2{view.width() * 0.55, view.height() * 0.55});
            t.start();
            for (int k = 0; k < 20; ++k)
                doc.entitiesInRect(small);
            const double smallMs = t.nsecsElapsed() / 1e6 / 20;
            t.start();
            const std::size_t window = doc.selectInRect(small, false).size();
            const double windowMs = t.nsecsElapsed() / 1e6;
            t.start();
            const std::size_t crossing = doc.selectInRect(small, true).size();
            const double crossingMs = t.nsecsElapsed() / 1e6;
            // Modifica: un oggetto spostato e rimesso (due passi), poi una ricerca.
            double editMs = 0.0;
            if (const Entity* any = doc.entitiesNear(pointAt(1), view.width()).empty() ? nullptr
                                                                                       : doc.entitiesNear(pointAt(1), view.width()).front()) {
                const EntityId id = any->id();
                t.start();
                for (int k = 0; k < 4; ++k) {
                    auto copy = doc.find(id)->clone();
                    copy->translate({k % 2 ? -1.0 : 1.0, 0.0});
                    std::vector<std::unique_ptr<Entity>> added;
                    added.push_back(std::move(copy));
                    doc.commit(std::move(added), {id});
                    doc.entitiesNear(pointAt(k), tol);
                }
                editMs = t.nsecsElapsed() / 1e6 / 4;
                for (int k = 0; k < 4; ++k)
                    doc.undo();
            }
            print(tr("-BENCHQUERY: %1 oggetti; vicino %2 ms (%3 trovati), pick %4 ms, rettangolo vista %5 ms (%6), "
                     "rettangolo piccolo %7 ms, finestra %8 ms (%9), intersezione %10 ms (%11), modifica+ricerca %12 ms")
                      .arg(doc.entities().size())
                      .arg(nearMs, 0, 'f', 3).arg(found)
                      .arg(pickMs, 0, 'f', 3)
                      .arg(rectMs, 0, 'f', 2).arg(inView)
                      .arg(smallMs, 0, 'f', 3)
                      .arg(windowMs, 0, 'f', 2).arg(window)
                      .arg(crossingMs, 0, 'f', 2).arg(crossing)
                      .arg(editMs, 0, 'f', 2));
            continue;
        }
        // Diagnosi (non documentato): tempo medio di un movimento del mouse nella vista corrente.
        if (d && word == QLatin1String("-BENCHMOUSE")) {
            bool ok = false;
            const int n = nextToken().toInt(&ok);
            print(tr("-BENCHMOUSE: %1 ms per movimento").arg(d->view()->benchmarkCursor(ok ? n : 200), 0, 'f', 3));
            continue;
        }
        // Diagnosi (non documentato): fotogrammi della finestra QML vera (GPU): -BENCHFRAMES n MOUSE|ZOOM file|-.
        // Con MOUSE misura quanto costa un fotogramma quando il disegno non cambia.
        if (d && word == QLatin1String("-BENCHFRAMES")) {
            bool ok = false;
            int n = nextToken().toInt(&ok);
            if (!ok || n <= 0)
                n = 30;
            if (!mainView_) {
                print(tr("-BENCHFRAMES: solo con la finestra unica QML."));
                continue;
            }
            // MOUSE (si muove solo il mouse) o ZOOM (ogni fotogramma zooma un poco), poi il file PNG dell'ultimo
            // fotogramma o "-".
            const QString mode = nextToken().toUpper();
            const bool zoom = mode == QLatin1String("ZOOM");
            const bool click = mode == QLatin1String("CLICK");   // ogni fotogramma cambia la selezione (un clic)
            const bool wheelOut = mode == QLatin1String("WHEELOUT");   // rotellina vera, solo zoom indietro
            const bool wheel = mode == QLatin1String("WHEEL") || wheelOut;   // rotellina vera (pan e zoom veloci)
            const bool edit = mode == QLatin1String("EDIT");     // ogni fotogramma sposta un oggetto (una modifica)
            const QString file = nextToken();
            const QString image = file == QLatin1String("-") ? QString() : resolve(file);
            CadView* v = d->view();
            std::vector<EntityId> visible;
            for (const Entity* e : d->document().entitiesInRect(v->visibleArea(), 0.0, d->shownSpace()))
                visible.push_back(e->id());
            double ms = 0.0;
            QString info;
            const QImage last = mainView_->renderOffscreenGpu(mainView_->size(), &info, [v, zoom, click, wheel, wheelOut, edit, d, &visible](int k) {
                if (edit && !visible.empty()) {
                    const EntityId id = visible[(static_cast<std::size_t>(k) * 7919) % visible.size()];
                    d->modifyEntities({id}, [k](Entity& e) { e.translate({k % 2 ? -25.0 : 25.0, 10.0}); });
                    return;
                }
                if (wheel) {
                    const QPointF p(v->width() * 0.4, v->height() * 0.6);
                    QWheelEvent e(p, v->mapToGlobal(p), QPoint(), QPoint(0, wheelOut ? -120 : k % 20 < 10 ? 120 : -120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(v, &e);
                    return;
                }
                if (zoom) {
                    v->zoomBy(1.02);
                    return;
                }
                if (click && !visible.empty()) {
                    d->setSelection({visible[(static_cast<std::size_t>(k) * 7919) % visible.size()]});
                    return;
                }
                const QPointF p(100.0 + (k * 37) % 600, 100.0 + (k * 23) % 400);
                QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &move);
            }, n, &ms);
            if (!image.isEmpty())
                last.save(image);
            print(tr("-BENCHFRAMES: %1 fotogrammi%2, %3 ms per fotogramma (%4)")
                      .arg(n).arg(edit ? tr(" con una modifica") : zoom || wheel ? tr(" con zoom") : click ? tr(" con un clic di selezione") : QString())
                      .arg(ms, 0, 'f', 1).arg(info));
            continue;
        }
        // Diagnosi (non documentato): clic di selezione simulati sugli oggetti visibili, con i tempi delle fasi:
        // -BENCHCLICK n. "clic" = eventi del mouse (aggancio e selezione), "dopo" = eventi in coda (tavolozze...),
        // "immagine" = disegno della vista con la selezione, "deseleziona" = Esc.
        if (d && word == QLatin1String("-BENCHCLICK")) {
            bool ok = false;
            int n = nextToken().toInt(&ok);
            if (!ok || n <= 0)
                n = 20;
            CadView* v = d->view();
            QWidget* target = v;
            std::vector<QPointF> points;
            const auto visible = d->document().entitiesInRect(v->visibleArea(), 0.0, d->shownSpace());
            const std::size_t stride = std::max<std::size_t>(1, visible.size() / static_cast<std::size_t>(n));
            for (std::size_t k = 0; k < visible.size() && static_cast<int>(points.size()) < n; k += stride) {
                const BBox b = visible[k]->bounds();
                if (!b.isValid())
                    continue;
                const QPointF s = v->screenPoint(b.min);   // un vertice: sull'oggetto, non nel vuoto al centro
                if (QRectF(v->rect()).contains(s))
                    points.push_back(s);
            }
            QElapsedTimer t;
            double click = 0, after = 0, frame = 0, clear = 0;
            int selected = 0;
            for (const QPointF& p : points) {
                const QPointF g = target->mapToGlobal(p);
                QMouseEvent move(QEvent::MouseMove, p, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &move);
                QCoreApplication::processEvents();
                t.start();
                QMouseEvent press(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &release);
                const double c = t.nsecsElapsed() / 1e6;
                click += c;
                t.restart();
                QCoreApplication::processEvents();
                const double a = t.nsecsElapsed() / 1e6;
                after += a;
                selected += d->selection().empty() ? 0 : 1;
                if (qEnvironmentVariableIsSet("CARTESIO_BENCHDETAIL"))
                    print(QStringLiteral("  clic %1 ms, dopo %2 ms, selezionati %3").arg(c, 0, 'f', 1).arg(a, 0, 'f', 1).arg(d->selection().size()));
                t.restart();
                v->grabImage();
                frame += t.nsecsElapsed() / 1e6;
                t.restart();
                d->setSelection({});
                QCoreApplication::processEvents();
                clear += t.nsecsElapsed() / 1e6;
            }
            const double m = std::max<std::size_t>(1, points.size());
            print(tr("-BENCHCLICK: %1 clic (%2 con selezione), ms medi: clic %3, dopo %4, immagine %5, deseleziona %6")
                      .arg(points.size()).arg(selected)
                      .arg(click / m, 0, 'f', 1).arg(after / m, 0, 'f', 1).arg(frame / m, 0, 'f', 1).arg(clear / m, 0, 'f', 1));
            continue;
        }
        // Diagnosi (non documentato): la stessa scena GPU a più scale: -GPUZOOMSEQ prefisso n f1 ... fn.
        if (d && word == QLatin1String("-GPUZOOMSEQ")) {
            const QString prefix = resolve(nextToken());
            const int n = nextToken().toInt();
            std::vector<double> factors;
            for (int k = 0; k < n; ++k)
                factors.push_back(nextToken().toDouble());
            QString info;
            const auto images = d->view()->grabGpuZoomSequence(factors, &info);
            for (std::size_t k = 0; k < images.size(); ++k)
                images[k].save(prefix + QString::number(k) + QStringLiteral(".png"));
            print(tr("-GPUZOOMSEQ: %1 immagini %2").arg(images.size()).arg(info));
            continue;
        }
        // Diagnosi (non documentato): la finestra principale in QML disegnata dalla GPU (Direct3D 11), senza schermo.
        if (word == QLatin1String("-ROOTGPUPNG")) {
            const QString target = resolve(nextToken());
            if (mainView_) {
                QString info;
                const QImage image = mainView_->renderOffscreenGpu(mainView_->size(), &info);
                if (image.isNull())
                    print(tr("-ROOTGPUPNG: %1").arg(info));
                else
                    image.save(target);
            }
            continue;
        }
        // Diagnosi (non documentato): OTRACK con un punto agganciato. -OTRACK ax,ay x,y
        if (d && word == QLatin1String("-OTRACK")) {
            const auto a = nextToken().split(QLatin1Char(','));
            const auto c = nextToken().split(QLatin1Char(','));
            std::vector<Vec2> acquired;
            for (int k = 0; k + 1 < a.size(); k += 2)
                acquired.push_back({a[k].toDouble(), a[k + 1].toDouble()});
            print(tr("-OTRACK: %1").arg(d->view()->simulateTracking(acquired, {c.value(0).toDouble(), c.value(1).toDouble()})));
            continue;
        }
        // Comandi sui file: nello script il nome del file segue il comando. Le diagnosi del mouse valgono anche
        // dentro un comando (es. il menu di una maniglia, la misura rapida).
        static const QStringList kMouseDiagnostics = {"-EDITFIRST", "-HOVER", "-CYCLE", "-DRAG", "-GRIPMENU", "-ESC", "-DBLCLICK", "-TYPE", "-KEY",
                                                      "-QPPNG", "-CLICK", "-REALCLICK", "-WHEEL"};
        if (!d || !d->commands().isActive() || kMouseDiagnostics.contains(word)) {
            if (word == QLatin1String("OPEN") || word == QLatin1String("APRI")) {
                openFile(resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("NEW") || word == QLatin1String("NUOVO")) {
                newDrawing();
                continue;
            }
            // RECOVER file: apre e corregge (AUDIT); ETRANSMIT archivio.zip: disegno e file collegati.
            if (word == QLatin1String("RECOVER") || word == QLatin1String("RECUPERA")) {
                recoverFile(resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("ETRANSMIT") || word == QLatin1String("ARCHIVIA")) {
                etransmit(resolve(nextToken()));
                continue;
            }
            // Disegno nuovo da un modello, come "Inizia disegno" con un modello scelto (prove automatiche).
            if (word == QLatin1String("-NEWFROM")) {
                newFromTemplate(resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("SCRIPT") || word == QLatin1String("SCR")) {
                runScript(resolve(nextToken()));
                continue;
            }
            // LOGFILEON [file]: copia dei messaggi della riga di comando.
            if (word == QLatin1String("LOGFILEON")) {
                const qsizetype save = i;
                const QString next = nextToken();
                if (next.endsWith(QLatin1String(".log"), Qt::CaseInsensitive) || next.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive))
                    logFile_ = resolve(next);
                else {
                    i = save;
                    logFile_ = QDir(scriptDir).filePath(QStringLiteral("CartesioCAD.log"));
                }
                print(tr("Registro attivo: %1").arg(QDir::toNativeSeparators(logFile_)));
                continue;
            }
            if (word == QLatin1String("LOGFILEOFF")) {
                logFile_.clear();
                continue;
            }
            if (word == QLatin1String("DELAY")) {   // pausa in millisecondi, al massimo 10 s
                const int ms = std::clamp(nextToken().toInt(), 0, 10000);
                if (ms > 0) {
                    QEventLoop loop;
                    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                    loop.exec();
                }
                continue;
            }
            if (word == QLatin1String("RESUME") || word == QLatin1String("RSCRIPT"))
                continue;
            if (word == QLatin1String("QUIT") || word == QLatin1String("EXIT") || word == QLatin1String("ESCI")) {
                quit = true;
                continue;
            }
            if (d && (word == QLatin1String("QSAVE") || word == QLatin1String("SAVE") || word == QLatin1String("SALVA")
                      || word == QLatin1String("SAVEAS") || word == QLatin1String("SALVACOME"))) {
                QString target = d->filePath();
                const bool saveAs = word.startsWith(QLatin1String("SAVEAS")) || word == QLatin1String("SALVACOME");
                if (saveAs || target.isEmpty()) {
                    QString t = nextToken();
                    // Eventuale formato prima del nome del file (es. "SAVEAS 2000 disegno.dwg").
                    if (!t.contains('.') && !t.contains('/') && !t.contains('\\') && i < n)
                        t = nextToken();
                    target = resolve(t);
                }
                QString error;
                if (d->saveTo(target, &error, DxfVersion::R2000)) {
                    print(tr("Salvato: %1").arg(QDir::toNativeSeparators(target)));
                    saveThumbnail(d, target);
                    addRecentFile(target);
                } else {
                    print(tr("Salvataggio non riuscito: %1").arg(error));
                }
                continue;
            }
            // Libreria simboli negli script: -BLOCKLIB file|cartella; -BLOCKINSERT file nome (poi i punti di INSERT).
            if (word == QLatin1String("-BLOCKLIB")) {
                library_->setLibrarySource(resolve(nextToken()));
                continue;
            }
            // Diagnosi: clic su n pixel disegnati della vista (non sfondo): quante volte la selezione non trova
            // l'oggetto e partirebbe una finestra (-PICKTEST n). Per i mancati: tipo dell'oggetto più vicino e pixel.
            if (d && word == QLatin1String("-PICKTEST")) {
                bool ok = false;
                int count = nextToken().toInt(&ok);
                if (!ok || count <= 0)
                    count = 300;
                CadView* v = d->view();
                QImage image = v->grabImage().convertToFormat(QImage::Format_RGB32);
                const double sx = static_cast<double>(image.width()) / std::max(1, v->width());
                const double sy = static_cast<double>(image.height()) / std::max(1, v->height());
                const double tol = v->pixelsToWorld(appOptions().pickboxSize);
                std::vector<QPoint> candidates;
                for (int y = 140; y < v->height() - 150; y += 3)
                    for (int x = 10; x < v->width() - 160; x += 3) {
                        const QColor c = image.pixelColor(static_cast<int>(x * sx), static_cast<int>(y * sy));
                        if (std::abs(c.red() - 33) + std::abs(c.green() - 40) + std::abs(c.blue() - 48) > 90)
                            candidates.emplace_back(x, y);
                    }
                std::map<QString, int> missedNear;
                int missed = 0, tried = 0;
                const std::size_t step = std::max<std::size_t>(1, candidates.size() / static_cast<std::size_t>(count));
                for (std::size_t k = 0; k < candidates.size() && tried < count; k += step) {
                    ++tried;
                    const Vec2 w = v->worldPoint(QPointF(candidates[k]));
                    if (d->document().pick(w, tol))
                        continue;
                    ++missed;
                    if (!qEnvironmentVariableIsEmpty("CARTESIO_PICKTEST_PNG")) {
                        QPainter mark(&image);
                        mark.setPen(QPen(QColor(255, 0, 255), 2));
                        mark.drawEllipse(QPointF(candidates[k].x() * sx, candidates[k].y() * sy), 5, 5);
                    }
                    const Entity* nearest = nullptr;
                    double best = v->pixelsToWorld(40);
                    for (const Entity* e : d->document().entitiesNear(w, best))
                        if (const double dist = e->distanceTo(w); dist < best) {
                            best = dist;
                            nearest = e;
                        }
                    const QString key = nearest ? QStringLiteral("%1 a %2 px")
                                                      .arg(QString::fromStdString(entityTypeName(nearest->type())))
                                                      .arg(std::lround(best / v->pixelsToWorld(1)))
                                                : QStringLiteral("niente entro 40 px");
                    ++missedNear[key];
                }
                if (const QString file = qEnvironmentVariable("CARTESIO_PICKTEST_PNG"); !file.isEmpty())
                    print(QStringLiteral("-PICKTEST immagine %1: %2").arg(file).arg(image.save(file) ? QStringLiteral("salvata") : QStringLiteral("NON salvata")));
                QStringList parts;
                for (const auto& [key, n] : missedNear)
                    parts << QStringLiteral("%1: %2").arg(key).arg(n);
                print(tr("-PICKTEST: %1 clic su pixel disegnati, %2 a vuoto (%3%). %4")
                          .arg(tried).arg(missed).arg(tried ? 100.0 * missed / tried : 0.0, 0, 'f', 1).arg(parts.join(QStringLiteral("; "))));
                continue;
            }
            // Diagnosi: clic veri (eventi del mouse) in un punto del disegno: -CLICK x,y [SHIFT]. Scrive quanti oggetti
            // sono selezionati dopo il clic e se è rimasta aperta una finestra di selezione o una modifica con maniglie.
            // Diagnosi: clic vero (coda degli eventi di sistema, con doppio clic sintetizzato da Qt) sulla vista QML.
            // -REALCLICK x,y: utile per i clic rapidi in sequenza.
            if (d && word == QLatin1String("-REALCLICK")) {
                const QStringList xy = nextToken().split(QLatin1Char(','));
                CadView* v = d->view();
                CadCanvas* c = v->canvas();
                if (xy.size() != 2 || !c || !c->window()) {
                    print(tr("-REALCLICK: serve l'interfaccia QML"));
                    continue;
                }
                QQuickWindow* w = c->window();
                const QPointF local = c->mapToScene(v->screenPoint({xy[0].toDouble(), xy[1].toDouble()}));
                const QPointF global = w->mapToGlobal(local);
                QWindowSystemInterface::handleMouseEvent(w, local, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
                QWindowSystemInterface::handleMouseEvent(w, local, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
                QWindowSystemInterface::handleMouseEvent(w, local, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                QWindowSystemInterface::flushWindowSystemEvents();
                QCoreApplication::processEvents();
                QStringList ids;
                for (EntityId id : d->selection())
                    ids << QString::number(id);
                print(tr("-REALCLICK %1,%2: selezione %3%4%5")
                          .arg(xy[0], xy[1], ids.join(QLatin1Char(' ')))
                          .arg(v->selectionCornerOpen() ? tr(", finestra di selezione aperta") : QString())
                          .arg(d->commands().active() ? tr(", comando attivo: %1").arg(d->prompt()) : QString()));
                continue;
            }
            // Diagnosi: -HOVER x,y = mouse fermo in un punto: oggetto evidenziato, oggetti sovrapposti, maniglia.
            if (d && word == QLatin1String("-HOVER")) {
                const QStringList xy = nextToken().split(QLatin1Char(','));
                if (xy.size() != 2)
                    continue;
                CadView* v = d->view();
                const QPointF s = v->screenPoint({xy[0].toDouble(), xy[1].toDouble()});
                QEnterEvent enter(s, s, v->mapToGlobal(s));   // il mouse entra nella vista (mirino, misura rapida)
                QCoreApplication::sendEvent(v, &enter);
                QMouseEvent move(QEvent::MouseMove, s, v->mapToGlobal(s), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &move);
                QCoreApplication::processEvents();
                print(tr("-HOVER %1,%2: evidenziato %3, sovrapposti %4%5%6")
                          .arg(xy[0], xy[1])
                          .arg(v->hoveredEntity())
                          .arg(v->hoverCandidateCount())
                          .arg(v->gripMenuOpen() ? tr(", menu maniglia aperto") : QString())
                          .arg(quickPropertiesVisible() ? tr(", proprietà rapide (%1 righe)").arg(quickPropertiesRows()) : QString()));
                continue;
            }
            // Diagnosi: -DBLCLICK x,y = doppio clic; se si apre la modifica del testo nel disegno ne scrive il
            // contenuto e la chiude con Invio (testo), oppure scrive la selezione.
            if (d && word == QLatin1String("-DBLCLICK")) {
                const QStringList xy = nextToken().split(QLatin1Char(','));
                if (xy.size() != 2)
                    continue;
                CadView* v = d->view();
                const QPointF s = v->screenPoint({xy[0].toDouble(), xy[1].toDouble()});
                const QPointF g = v->mapToGlobal(s);
                QMouseEvent move(QEvent::MouseMove, s, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &move);
                for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick,
                                                QEvent::MouseButtonRelease}) {
                    const bool down = type != QEvent::MouseButtonRelease;
                    QMouseEvent e(type, s, g, Qt::LeftButton, down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
                    static quint64 clock = 1000;
                    e.setTimestamp(clock += 40);   // tempi diversi come gli eventi veri (doppio clic dei widget)
                    QCoreApplication::sendEvent(v, &e);
                }
                QCoreApplication::processEvents();
                QWidget* editor = v->overlayHost()->findChild<QWidget*>(QStringLiteral("InPlaceTextEditor"));
                if (editor && editor->isVisible()) {
                    print(tr("-DBLCLICK: modifica del testo \"%1\"").arg(editor->property("editorText").toString().replace(QLatin1Char('\n'), QStringLiteral(" | "))));
                } else {
                    print(tr("-DBLCLICK: nessuna modifica del testo, %1 selezionati").arg(d->selection().size()));
                }
                continue;
            }
            // Diagnosi (non documentato): -TEXTINPLACE 1 fa scrivere a TEXT e MTEXT il testo nel disegno anche negli
            // script (poi -TYPE e -KEY), 0 torna alla riga di comando.
            // Diagnosi (non documentato): -AZIONE nome preme l'azione della barra con quel testo ("_" = spazio).
            if (word == QLatin1String("-AZIONE")) {
                const QString wanted = nextToken().replace(u'_', u' ');
                bool found = false;
                for (QAction* a : findChildren<QAction*>())
                    if (a->text().replace(u'\n', u' ').compare(wanted, Qt::CaseInsensitive) == 0) {
                        a->trigger();
                        found = true;
                        break;
                    }
                QCoreApplication::processEvents();
                print(QStringLiteral("-AZIONE %1: %2").arg(wanted, found ? QStringLiteral("premuta") : QStringLiteral("non trovata")));
                continue;
            }
            if (word == QLatin1String("-SCHEDA")) {   // diagnosi: seleziona una scheda della barra ("_" = spazio)
                if (ribbon_)
                    ribbon_->selectTab(nextToken().replace(u'_', u' '));
                QCoreApplication::processEvents();
                continue;
            }
            // Diagnosi: -GRUPPOLAYOUT gruppo inizio ("_" = spazio; gruppo "-" = nessuno), -RAGGRUPPA 1/0.
            if (d && word == QLatin1String("-GRUPPOLAYOUT")) {
                const QString group = nextToken().replace(u'_', u' ');
                const QString prefix = nextToken().replace(u'_', u' ');
                std::vector<int> ids;
                for (const Layout& l : d->document().layouts())
                    if (QString::fromStdString(l.name).startsWith(prefix, Qt::CaseInsensitive))
                        ids.push_back(l.id);
                setLayoutGroup(ids, group == QLatin1String("-") ? QString() : group);
                print(QStringLiteral("-GRUPPOLAYOUT: %1 layout nel gruppo %2").arg(ids.size()).arg(group));
                continue;
            }
            if (d && word == QLatin1String("-COMPARE")) {   // confronto senza finestra di scelta del file
                startCompare(resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("-COMPAREGO")) {
                compareStep(nextToken().toInt());
                continue;
            }
            if (word == QLatin1String("-COMPARECLOSE")) {
                closeCompare();
                continue;
            }
            if (d && word == QLatin1String("-VPORTS")) {   // diagnosi: 0 una, 1 due verticali, 2 due orizzontali, 3, 4
                d->setTiles(static_cast<DrawingWidget::Tiles>(std::clamp(nextToken().toInt(), 0, 4)));
                QCoreApplication::processEvents();
                continue;
            }
            if (word == QLatin1String("-RAGGRUPPA")) {
                appOptions().groupLayoutTabs = nextToken().toInt() != 0;
                refreshLayoutTabs();
                continue;
            }
            if (word == QLatin1String("-TEXTINPLACE")) {
                textInPlaceInScripts_ = nextToken().toInt() != 0;
                continue;
            }
            // Diagnosi della modifica dei testi nel disegno: -TYPE testo (scrive, "_" = spazio) e -KEY tasto
            // (Invio, CtrlInvio, Esc, Sinistra, Destra, Su, Giu, Home, Fine, Canc, Indietro, CtrlA, MaiuscSinistra,
            // MaiuscHome, CtrlB, Tab, CtrlZ). Poi scrive il testo e il codice dell'editor.
            if (d && (word == QLatin1String("-TYPE") || word == QLatin1String("-KEY"))) {
                const QString arg = nextToken();
                QWidget* editor = d->view()->overlayHost()->findChild<QWidget*>(QStringLiteral("InPlaceTextEditor"));
                if (!editor || !editor->isVisible()) {
                    print(tr("%1: nessuna modifica del testo in corso").arg(word));
                    continue;
                }
                const auto press = [editor](int key, Qt::KeyboardModifiers mods, const QString& text) {
                    QKeyEvent e(QEvent::KeyPress, key, mods, text);
                    QCoreApplication::sendEvent(editor, &e);
                };
                if (word == QLatin1String("-TYPE")) {
                    QString typed = arg;   // copia: il ciclo non deve scorrere un temporaneo
                    typed.replace(QLatin1Char('_'), QLatin1Char(' '));
                    for (const QChar c : typed)
                        press(0, Qt::NoModifier, QString(c));
                } else {
                    static const QMap<QString, std::pair<int, Qt::KeyboardModifiers>> keys = {
                        {"INVIO", {Qt::Key_Return, Qt::NoModifier}}, {"CTRLINVIO", {Qt::Key_Return, Qt::ControlModifier}},
                        {"ESC", {Qt::Key_Escape, Qt::NoModifier}}, {"SINISTRA", {Qt::Key_Left, Qt::NoModifier}},
                        {"DESTRA", {Qt::Key_Right, Qt::NoModifier}}, {"SU", {Qt::Key_Up, Qt::NoModifier}},
                        {"GIU", {Qt::Key_Down, Qt::NoModifier}}, {"HOME", {Qt::Key_Home, Qt::NoModifier}},
                        {"FINE", {Qt::Key_End, Qt::NoModifier}}, {"CANC", {Qt::Key_Delete, Qt::NoModifier}},
                        {"INDIETRO", {Qt::Key_Backspace, Qt::NoModifier}}, {"CTRLA", {Qt::Key_A, Qt::ControlModifier}},
                        {"MAIUSCSINISTRA", {Qt::Key_Left, Qt::ShiftModifier}}, {"MAIUSCHOME", {Qt::Key_Home, Qt::ShiftModifier}},
                        {"CTRLB", {Qt::Key_B, Qt::ControlModifier}}, {"TAB", {Qt::Key_Tab, Qt::NoModifier}},
                        {"CTRLZ", {Qt::Key_Z, Qt::ControlModifier}}};
                    const auto it = keys.find(arg.toUpper());
                    if (it == keys.end()) {
                        print(tr("-KEY: tasto sconosciuto %1").arg(arg));
                        continue;
                    }
                    press(it->first, it->second, QString());
                }
                QCoreApplication::processEvents();
                QWidget* still = d->view()->overlayHost()->findChild<QWidget*>(QStringLiteral("InPlaceTextEditor"));
                if (still && still->isVisible())
                    print(tr("%1 %2: \"%3\"  codice: %4").arg(word, arg,
                                                            still->property("editorText").toString().replace(QLatin1Char('\n'), QStringLiteral(" | ")),
                                                            still->property("editorSource").toString()));
                else
                    print(tr("%1 %2: modifica chiusa").arg(word, arg));
                continue;
            }
            // Diagnosi: -EDITFIRST = modifica nel disegno del primo testo multilinea visibile (prove sui disegni veri).
            if (d && word == QLatin1String("-EDITFIRST")) {
                const Entity* found = nullptr;
                for (const Entity* e : d->document().entitiesInRect(d->view()->visibleArea(), 0.0, d->shownSpace()))
                    if (e->type() == EntityType::MText && !static_cast<const MTextEntity*>(e)->text.empty()) {
                        found = e;
                        break;
                    }
                if (!found) {
                    print(tr("-EDITFIRST: nessun testo multilinea visibile"));
                    continue;
                }
                const auto* m = static_cast<const MTextEntity*>(found);
                editTextInPlace(*d, found->id(), m->position);
                QCoreApplication::processEvents();
                QWidget* editor = d->view()->overlayHost()->findChild<QWidget*>(QStringLiteral("InPlaceTextEditor"));
                print(tr("-EDITFIRST: testo in %1,%2 \"%3\"").arg(m->position.x).arg(m->position.y)
                          .arg(editor ? editor->property("editorText").toString() : QStringLiteral("(editor non aperto)")));
                continue;
            }
            // Diagnosi: -CARTELLE = dove si cercano font e tabelle stili.
            // Diagnosi: aggiornamenti. -AGGIORNAMENTICONTROLLA (GitHub), -AGGIORNAMENTOLOCALE manifesto archivio cartella
            // (verifica la firma e installa in quella cartella), -VERSIONEPRECEDENTE cartella.
            // Diagnosi: -ASSISTENTEDESKTOP configura Claude Desktop (cartelle da APPDATA e LOCALAPPDATA) e scrive l'esito.
            if (word == QLatin1String("-ASSISTENTEDESKTOP")) {
                print(QStringLiteral("-ASSISTENTEDESKTOP: ") + configureClaudeDesktop());
                continue;
            }
            // Diagnosi: -ASSISTENTEATTIVA [AUTO | FOTO file.png] accende il connettore per questa sessione; con AUTO le proposte si
            // applicano senza la finestra di conferma (solo per le prove automatiche).
            if (word == QLatin1String("-ASSISTENTEATTIVA")) {
                QString error;
                if (startClaude(&error))
                    print(tr("-ASSISTENTEATTIVA: canale %1 attivo").arg(ClaudeLink::channelName()));
                else
                    print(tr("-ASSISTENTEATTIVA: errore %1").arg(error));
                const qsizetype mark = i;
                const QString option = nextToken().toUpper();
                if (option == QLatin1String("AUTO"))
                    claudeAutoApprove_ = true;
                else if (option == QLatin1String("FOTO"))   // la prossima proposta: immagine e chiusura (= rifiutata)
                    FormDialog::grabNext(resolve(nextToken()));
                else
                    i = mark;
                continue;
            }
            if (word == QLatin1String("-AGGIORNAMENTICONTROLLA")) {
                scriptUpdateCheck();
                continue;
            }
            if (word == QLatin1String("-AGGIORNAMENTOLOCALE")) {
                const QString manifest = resolve(nextToken());
                const QString archive = resolve(nextToken());
                scriptUpdateLocal(manifest, archive, resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("-AGGIORNAMENTOSCARICA")) {   // tutto il percorso: GitHub, firma, installazione
                scriptUpdateDownload(resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("-VERSIONEPRECEDENTE")) {
                scriptUpdateRollback(resolve(nextToken()));
                continue;
            }
            if (word == QLatin1String("-CARTELLE")) {
                for (const QString& f : FontLibrary::instance().searchFolders())
                    print(tr("  font: %1").arg(QDir::toNativeSeparators(f)));
                print(tr("  tabelle stili: %1").arg(QDir::toNativeSeparators(plotStylesFolder())));
                continue;
            }
            // Diagnosi: -ESC = tasto Esc (annulla il comando o deseleziona).
            if (d && word == QLatin1String("-ESC")) {
                d->cancel();
                continue;
            }
            // Diagnosi: -QPPNG file = immagine delle Proprietà rapide.
            if (word == QLatin1String("-QPPNG")) {
                const QString target = resolve(nextToken());
                if (quickPropsWindow_ && quickPropsWindow_->isVisible()) {
                    QCoreApplication::processEvents();
                    quickProps_->grabFramebuffer().save(target);   // QQuickWidget: grab() non cattura la scena
                    print(tr("-QPPNG: %1 righe").arg(quickPropertiesRows()));
                } else {
                    print(tr("-QPPNG: proprietà rapide non visibili"));
                }
                continue;
            }
            // Diagnosi: -CYCLE = Maiusc+Spazio (ciclo degli oggetti sovrapposti).
            if (d && word == QLatin1String("-CYCLE")) {
                d->view()->cycleHover();
                print(tr("-CYCLE: evidenziato %1").arg(d->view()->hoveredEntity()));
                continue;
            }
            // Diagnosi: -DRAG x1,y1 x2,y2 ... = tasto sinistro premuto nel primo punto, trascinato per gli altri,
            // rilasciato nell'ultimo (lazo).
            if (d && word == QLatin1String("-DRAG")) {
                CadView* v = d->view();
                std::vector<QPointF> pts;
                while (true) {
                    const QString t = nextToken();
                    const QStringList xy = t.split(QLatin1Char(','));
                    if (xy.size() != 2) {
                        if (!t.isEmpty() && t != QLatin1String("-"))
                            print(tr("-DRAG: punto non valido \"%1\"").arg(t));
                        break;
                    }
                    pts.push_back(v->screenPoint({xy[0].toDouble(), xy[1].toDouble()}));
                }
                if (pts.size() < 2)
                    continue;
                const auto send = [v](QEvent::Type type, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
                    QMouseEvent e(type, p, v->mapToGlobal(p), b, bs, Qt::NoModifier);
                    QCoreApplication::sendEvent(v, &e);
                };
                send(QEvent::MouseMove, pts.front(), Qt::NoButton, Qt::NoButton);
                send(QEvent::MouseButtonPress, pts.front(), Qt::LeftButton, Qt::LeftButton);
                for (std::size_t i = 1; i < pts.size(); ++i) {   // passi intermedi: il lazo raccoglie i punti
                    for (int k = 1; k <= 8; ++k)
                        send(QEvent::MouseMove, pts[i - 1] + (pts[i] - pts[i - 1]) * (k / 8.0), Qt::NoButton, Qt::LeftButton);
                }
                send(QEvent::MouseButtonRelease, pts.back(), Qt::LeftButton, Qt::NoButton);
                QCoreApplication::processEvents();
                QStringList ids;
                for (EntityId id : d->selection())
                    ids << QString::number(id);
                print(tr("-DRAG: selezione %1%2").arg(ids.join(QLatin1Char(' ')),
                                                   v->selectionCornerOpen() ? tr(", finestra aperta") : QString()));
                continue;
            }
            // Diagnosi: -GRIPMENU n = apre subito il menu della maniglia sotto il mouse e sceglie la voce n (da 0).
            if (d && word == QLatin1String("-GRIPMENU")) {
                const int item = nextToken().toInt();
                CadView* v = d->view();
                v->openGripMenuNow();
                if (!v->gripMenuOpen()) {
                    print(tr("-GRIPMENU: nessun menu (il mouse non è su una maniglia con più azioni)"));
                    continue;
                }
                const QPointF p = v->gripMenuItemCenter(item);
                QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &move);
                QMouseEvent press(QEvent::MouseButtonPress, p, v->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, p, v->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &release);
                QCoreApplication::processEvents();
                print(tr("-GRIPMENU %1: %2").arg(item).arg(d->commands().active() ? d->prompt() : tr("nessun comando")));
                continue;
            }
            // Diagnosi: -WHEEL n = n scatti della rotellina (avanti se positivo) al centro della vista.
            if (d && word == QLatin1String("-WHEEL")) {
                const int n = nextToken().toInt();
                CadView* v = d->view();
                const QPointF p(v->width() * 0.4, v->height() * 0.6);
                for (int i = 0; i < std::abs(n); ++i) {
                    QWheelEvent e(p, v->mapToGlobal(p), QPoint(), QPoint(0, n > 0 ? 120 : -120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(v, &e);
                    QCoreApplication::processEvents();
                }
                print(tr("-WHEEL %1").arg(n));
                continue;
            }
            if (d && word == QLatin1String("-CLICK")) {
                const QStringList xy = nextToken().split(QLatin1Char(','));
                if (xy.size() != 2)
                    continue;
                CadView* v = d->view();
                const QPointF s = v->screenPoint({xy[0].toDouble(), xy[1].toDouble()});
                const QPointF g = v->mapToGlobal(s);
                if (const Entity* hit = d->document().pick({xy[0].toDouble(), xy[1].toDouble()}, v->pixelsToWorld(appOptions().pickboxSize)))
                    print(QStringLiteral("  sotto il clic: %1 (id %2)").arg(QString::fromStdString(entityTypeName(hit->type()))).arg(hit->id()));
                else
                    print(QStringLiteral("  sotto il clic: niente"));
                QMouseEvent move(QEvent::MouseMove, s, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &move);
                QMouseEvent press(QEvent::MouseButtonPress, s, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, s, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(v, &release);
                QCoreApplication::processEvents();
                print(tr("-CLICK %1,%2: %3 selezionati%4%5")
                          .arg(xy[0], xy[1])
                          .arg(d->selection().size())
                          .arg(v->selectionCornerOpen() ? tr(", finestra di selezione aperta") : QString())
                          .arg(d->commands().active() ? tr(", comando attivo: %1").arg(d->prompt()) : QString()));
                {
                    QStringList ids;
                    for (EntityId id : d->selection())
                        ids << QString::number(id);
                    print(QStringLiteral("  selezione: %1; punto grezzo %2,%3").arg(ids.join(QLatin1Char(' '))).arg(v->rawCursorWorld().x).arg(v->rawCursorWorld().y));
                }
                continue;
            }
            // Diagnosi: immagine del mirino usato come cursore di Windows (-CURSORPNG file).
            if (d && word == QLatin1String("-CURSORPNG")) {
                const QCursor c = d->view()->currentIdleCursor();
                const QPixmap pm = c.pixmap();
                print(tr("-CURSORPNG: %1 x %2 pixel (rapporto %3), punto caldo %4,%5")
                          .arg(pm.width()).arg(pm.height()).arg(pm.devicePixelRatio())
                          .arg(c.hotSpot().x()).arg(c.hotSpot().y()));
                if (!pm.isNull())
                    pm.save(resolve(nextToken()));
                else
                    nextToken();
                continue;
            }
            // Conversione in serie negli script: -DWGCONVERT sorgente destinazione DXF|DXF12|DWG|PDF [SUB] [SOVRASCRIVI].
            if (word == QLatin1String("-DWGCONVERT")) {
                BatchConvertOptions o;
                o.source = resolve(nextToken());
                o.output = resolve(nextToken());
                const QString format = nextToken().toUpper();
                o.format = format == QLatin1String("DXF12") ? ConvertDxfR12
                           : format == QLatin1String("DWG")  ? ConvertDwg
                           : format == QLatin1String("PDF")  ? ConvertPdf
                                                             : ConvertDxf2000;
                while (i > 0 && i < n && text[i - 1] != QLatin1Char(10)) {
                    const QString flag = nextToken().toUpper();
                    if (flag == QLatin1String("SUB"))
                        o.recursive = true;
                    else if (flag == QLatin1String("SOVRASCRIVI") || flag == QLatin1String("OVERWRITE"))
                        o.overwrite = true;
                    else if (flag.isEmpty())
                        break;
                }
                runBatchConvert(o, false);
                continue;
            }
            // Stati dei layer negli script: -LAYERSTATE S|R|D nome (salva, ripristina, elimina).
            if (d && word == QLatin1String("-LAYERSTATE")) {
                const QString action = nextToken().toUpper();
                const std::string name = nextToken().toStdString();
                Document& doc = d->document();
                if (action == QLatin1String("S"))
                    doc.saveLayerState(name);
                else if (action == QLatin1String("R"))
                    print(tr("Stato ripristinato: %1 layer.").arg(doc.restoreLayerState(name)));
                else if (action == QLatin1String("D"))
                    doc.removeLayerState(name);
                d->notifyChanged();
                continue;
            }
            // Diagnosi: salva in `file` (DWG o DXF), lo rilegge e confronta con il disegno: -ROUNDTRIP file [rapporto.txt].
            if (d && word == QLatin1String("-ROUNDTRIP")) {
                const QString out = resolve(nextToken());
                QString reportFile;
                if (i > 0 && i < n && text[i - 1] != QLatin1Char(10))   // il rapporto sulla stessa riga
                    reportFile = resolve(nextToken());
                QString error;
                QElapsedTimer timer;
                timer.start();
                if (!d->saveTo(out, &error, out.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive) ? DxfVersion::R2000 : d->dxfVersion())) {
                    print(tr("-ROUNDTRIP: salvataggio non riuscito: %1").arg(error));
                    continue;
                }
                const qint64 saveMs = timer.restart();
                Document again;
                if (!DrawingWidget::readFile(out, again, &error)) {
                    print(tr("-ROUNDTRIP: rilettura non riuscita: %1").arg(error));
                    continue;
                }
                const DocComparison c = compareDocuments(d->document(), again);
                print(tr("-ROUNDTRIP %1 (salvato in %2 ms, riletto in %3 ms): %4")
                          .arg(QFileInfo(out).fileName())
                          .arg(saveMs)
                          .arg(timer.elapsed())
                          .arg(QString::fromStdString(c.report(15))));
                if (!reportFile.isEmpty()) {
                    QFile f(reportFile);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                        f.write(QString::fromStdString(c.report(100000)).toUtf8());
                }
                continue;
            }
            // Rilascio di un simbolo trascinato (prove automatiche): un vero evento di drop sulla vista.
            if (d && word == QLatin1String("-BLOCKDROP")) {
                const QString file = resolve(nextToken());
                const QString name = nextToken();
                const QStringList xy = nextToken().split(QLatin1Char(','));
                if (xy.size() != 2)
                    continue;
                const QPointF screen = d->view()->screenPoint({xy[0].toDouble(), xy[1].toDouble()});
                // Qt consegna i drop solo durante un trascinamento vero: qui il passo successivo, come dropEvent.
                d->view()->dropBlock(QStringLiteral("block") + QLatin1Char(9) + file + QLatin1Char(9) + name, screen);
                continue;
            }
            if (d && word == QLatin1String("-BLOCKINSERT")) {
                const QString file = resolve(nextToken());
                const QString name = nextToken();
                Document source;
                QString error;
                if (!DrawingWidget::readFile(file, source, &error)) {
                    print(error);
                    continue;
                }
                if (!importBlock(d->document(), source, name.toStdString())) {
                    print(tr("Blocco \"%1\" non trovato in %2.").arg(name, file));
                    continue;
                }
                d->document().touch();
                library_->addRecent(file, name);
                d->notifyChanged();
                d->submit(QStringLiteral("INSERT"));
                d->submit(name);
                continue;
            }
            // Editor blocchi negli script: -BEDIT nome, BSAVE, BCLOSE [S/N] (senza domande a video).
            if (d && word == QLatin1String("-BEDIT")) {
                editBlock(d, nextToken());
                continue;
            }
            if (d && d->isBlockEditor() && (word == QLatin1String("BSAVE") || word == QLatin1String("SALVABLOCCO"))) {
                saveBlockEdit(d);
                continue;
            }
            if (d && d->isBlockEditor() && (word == QLatin1String("BCLOSE") || word == QLatin1String("CHIUDIBLOCCO"))) {
                if (d->isModified()) {
                    const qsizetype save = i;
                    const QString answer = nextToken().toUpper();
                    if (answer == QLatin1String("S") || answer == QLatin1String("Y"))
                        saveBlockEdit(d);
                    else if (answer != QLatin1String("N"))
                        i = save;
                }
                DrawingWidget* host = d->blockHost();
                stack_->removeWidget(d);
                fileTabs_->removeTab(fileTabs_->currentIndex());
                d->deleteLater();
                if (host)
                    fileTabs_->setCurrentIndex(stack_->indexOf(host));
                onTabChanged(fileTabs_->currentIndex());
                continue;
            }
            if (d && (word == QLatin1String("CLOSE") || word == QLatin1String("CHIUDI"))) {
                if (d->isModified()) {   // eventuale risposta S/N alla domanda di salvataggio
                    const qsizetype save = i;
                    const QString answer = nextToken().toUpper();
                    if (answer == QLatin1String("S") || answer == QLatin1String("Y")) {
                        QString error;
                        d->saveTo(d->filePath(), &error);
                    } else if (answer != QLatin1String("N")) {
                        i = save;
                    }
                }
                stack_->removeWidget(d);
                fileTabs_->removeTab(fileTabs_->currentIndex());
                d->deleteLater();
                onTabChanged(fileTabs_->currentIndex());
                continue;
            }
            if (d && (word == QLatin1String("EXPORTPDF") || word == QLatin1String("ESPORTAPDF"))) {
                QString error;
                if (!exportPdfTo(d, resolve(nextToken()), &error))
                    print(error);
                continue;
            }
            // -PLOT chiave=valore ... fino a fine riga (i percorsi relativi sono rispetto allo script).
            if (d && (word == QLatin1String("-PLOT") || word == QLatin1String("-STAMPA"))) {
                QString line;
                if (i > 0 && text[i - 1] != '\n') {   // altrimenti la riga è finita col comando
                    qsizetype end = text.indexOf('\n', i);
                    if (end < 0)
                        end = n;
                    line = text.mid(i, end - i).trimmed();
                    i = end + 1;
                }
                // FILE= relativo: rispetto alla cartella dello script.
                static const QRegularExpression fileOption(QStringLiteral("(FILE=)(\"[^\"]*\"|\\S+)"),
                                                           QRegularExpression::CaseInsensitiveOption);
                const auto m = fileOption.match(line);
                if (m.hasMatch()) {
                    QString value = m.captured(2);
                    value.remove('"');
                    line.replace(m.capturedStart(2), m.capturedLength(2),
                                 QLatin1Char('"') + resolve(value) + QLatin1Char('"'));
                }
                QString error;
                if (!plotFromOptions(d, line, &error))
                    print(tr("-PLOT: %1").arg(error));
                continue;
            }
            // Diagnosi (non documentato): scrive una tabella stili in un file .ctb.
            if (word == QLatin1String("-CTBEXPORT")) {
                const QString name = nextToken();
                const QString target = resolve(nextToken());
                QString error;
                const auto table = loadPlotStyleTable(name, &error);
                if (!table || !savePlotStyleTable(*table, target, &error))
                    print(error);
                continue;
            }
            // Plugin CanaloCAD negli script: con il nome del file, senza dialoghi né browser.
            if (d && (word == QLatin1String("CANALOINVIA") || word == QLatin1String("CANALOSEND"))) {
                canaloSend(resolve(nextToken()));
                continue;
            }
            if (d && (word == QLatin1String("XATTACH") || word == QLatin1String("XA") || word == QLatin1String("RIFX"))) {
                QString error;   // nello script il file segue il comando, poi i dati di INSERT
                if (!d->attachXref(resolve(nextToken()), &error))
                    print(error);
                continue;
            }
            if (d && (word == QLatin1String("IMAGEATTACH") || word == QLatin1String("IAT") || word == QLatin1String("IMMAGINE"))) {
                attachImage(resolve(nextToken()));   // poi punto di inserimento e larghezza
                continue;
            }
            // Impianto negli script: -IMPIANTONUOVO file.cdb, -IMPIANTOAPRI file.cdb, -IMPIANTOPDF schema.pdf,
            // -IMPIANTOCIRCUITO sigla|descrizione|tipo|formazione|sistema|Ib (con "_" al posto degli spazi),
            // -TRATTA tubo|rigido|32|C1;C2  canale|200x60|C1  passerella|300x75|wall_med|1.5|C1 (con le linee
            // selezionate), -VERIFICAIMPIANTO, -IMPIANTOSCHEDA n.
            if (word == QLatin1String("-IMPIANTONUOVO") || word == QLatin1String("-IMPIANTOAPRI")) {
                const QString path = resolve(nextToken());
                QString error;
                showPanel(MainView::Plant);
                const bool ok = word == QLatin1String("-IMPIANTONUOVO") ? plant_->createProject(path, &error) : plant_->openProject(path, &error);
                print(ok ? tr("%1: %2").arg(word, plant_->statusText()) : tr("%1: %2").arg(word, error));
                continue;
            }
            if (word == QLatin1String("-IMPIANTOPDF")) {
                QString error;
                print(plant_->importSchema(resolve(nextToken()), &error) ? plant_->statusText() : tr("-IMPIANTOPDF: %1").arg(error));
                continue;
            }
            if (word == QLatin1String("-IMPIANTOCIRCUITO")) {
                const QStringList f = nextToken().replace(u'_', u' ').split(u'|');
                plant::Circuit c;
                c.code = f.value(0).toStdString();
                c.description = f.value(1).toStdString();
                c.cableType = f.value(2).toStdString();
                c.formation = f.value(3).toStdString();
                c.system = plant::systemFromName(f.value(4).toStdString());
                c.ib = f.value(5).toDouble();
                plant_->putCircuit(c);
                print(tr("-IMPIANTOCIRCUITO %1").arg(f.value(0)));
                continue;
            }
            if (word == QLatin1String("-TRATTA")) {
                const QStringList f = nextToken().replace(u'_', u' ').split(u'|');
                plant::Run r;
                const QString kind = f.value(0).toLower();
                std::vector<std::string> codes;
                auto codesFrom = [&codes](const QString& text) {
                    for (const QString& part : text.split(u';'))
                        if (!part.trimmed().isEmpty())
                            codes.push_back(part.trimmed().toStdString());
                };
                auto size = [&r](const QString& text) {
                    r.width = text.section(u'x', 0, 0).toDouble();
                    r.height = text.section(u'x', 1, 1).toDouble();
                };
                if (kind == QLatin1String("tubo")) {
                    r.kind = plant::RunKind::Tube;
                    r.tubeType = f.value(1).toStdString();
                    r.tubeOuter = f.value(2).toDouble();
                    codesFrom(f.value(3));
                } else if (kind == QLatin1String("canale")) {
                    r.kind = plant::RunKind::Duct;
                    size(f.value(1));
                    codesFrom(f.value(2));
                } else {
                    r.kind = plant::RunKind::Tray;
                    size(f.value(1));
                    r.support = f.value(2).replace(u' ', u'_').toStdString();   // chiave: wall_med...
                    r.spanM = f.value(3).toDouble();
                    codesFrom(f.value(4));
                }
                r.circuits = codes;
                QString error;
                const QString id = plant_->newRun(r, &error);
                print(id.isEmpty() ? tr("-TRATTA: %1").arg(error) : tr("-TRATTA: %1").arg(id));
                continue;
            }
            if (d && word == QLatin1String("-SELEZIONAULTIMI")) {   // diagnosi: seleziona gli ultimi n oggetti disegnati
                const int n = std::max(1, nextToken().toInt());
                const auto& all = d->document().entities();
                std::vector<EntityId> ids;
                for (int i = static_cast<int>(all.size()) - 1; i >= 0 && static_cast<int>(ids.size()) < n; --i)
                    ids.push_back(all[static_cast<std::size_t>(i)]->id());
                d->setSelection(ids);
                continue;
            }
            if (word == QLatin1String("-DISTINTAIMPIANTO")) {   // -DISTINTAIMPIANTO file.csv (anche la tabella nel disegno)
                QString error;
                const auto items = plant_->bom(resolve(nextToken()), true, &error);
                if (items.empty())
                    print(tr("-DISTINTAIMPIANTO: %1").arg(error));
                for (const plant::BomItem& it : items)
                    print(QStringLiteral("  %1 | %2 | %3 %4 | %5")
                              .arg(QString::fromStdString(it.category), QString::fromStdString(it.description))
                              .arg(it.qty)
                              .arg(QString::fromStdString(it.unit), QString::fromStdString(it.note)));
                continue;
            }
            if (word == QLatin1String("-ETICHETTETRATTE")) {
                print(tr("-ETICHETTETRATTE: %1").arg(plant_->writeLabels()));
                continue;
            }
            if (word == QLatin1String("-SEZIONETRATTA")) {
                QString error;
                const QString id = nextToken();
                print(plant_->drawSection(id, &error) ? tr("-SEZIONETRATTA %1").arg(id) : tr("-SEZIONETRATTA: %1").arg(error));
                continue;
            }
            // Diagnosi: -PERCORSIIMPIANTO distanza_m aggiunta_m (calcola e applica), -QUADRO nome (oggetti selezionati).
            if (word == QLatin1String("-PERCORSIIMPIANTO")) {
                const double maxAccess = nextToken().toDouble();
                const double extra = nextToken().toDouble();
                const auto report = plant_->computeRoutes(maxAccess > 0 ? maxAccess : 5.0, extra);
                for (const QString& line : report.lines)
                    print(line);
                print(tr("-PERCORSIIMPIANTO: %1 circuiti con sigla, %2 applicati").arg(report.circuitsWithTags).arg(plant_->applyRoutes(report)));
                continue;
            }
            // Diagnosi: -TRATTEDALAYER layer;layer (spazi come _): tratte con la legenda; -UNITAIMPIANTO metri_per_unità.
            if (word == QLatin1String("-TRATTEDALAYER")) {
                const QStringList layers = nextToken().replace(u'_', u' ').split(u';', Qt::SkipEmptyParts);
                const auto r = plant_->importRunsFromLayers(layers, {}, true);
                QStringList kinds;
                for (const auto& [k, n] : r.byKind)
                    kinds << tr("%1 %2").arg(k).arg(n);
                print(tr("-TRATTEDALAYER: %1 tratte (%2 dalla scritta vicina, %3 pezzi corti scartati) %4")
                          .arg(r.runs).arg(r.fromLabels).arg(r.tooShort).arg(kinds.join(QStringLiteral(", "))));
                continue;
            }
            if (word == QLatin1String("-UNITAIMPIANTO")) {
                plant_->setDrawingUnits(nextToken().toDouble());
                print(tr("-UNITAIMPIANTO: %1 m per unità").arg(d ? d->document().plantMetersPerUnit() : 0.0));
                continue;
            }
            if (word == QLatin1String("-QUADRO")) {
                QString error;
                const QString name = nextToken().replace(u'_', u' ');
                print(plant_->tagBoard(name, &error) ? tr("-QUADRO: %1 segnato").arg(name) : tr("-QUADRO: %1").arg(error));
                continue;
            }
            if (word == QLatin1String("-VERIFICAIMPIANTO")) {
                for (const QString& line : plant_->checkAll())
                    print(line);
                continue;
            }
            if (word == QLatin1String("-IMPIANTOSCHEDA")) {
                showPanel(MainView::Plant);
                plant_->setTab(nextToken().toInt());
                QCoreApplication::processEvents();
                continue;
            }
            if (word == QLatin1String("-MAPPABARRA")) {   // diagnosi: struttura della barra multifunzione in un file
                QFile f(resolve(nextToken()));
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    f.write(ribbon_->describe().join(u'\n').toUtf8());
                continue;
            }
            if (word == QLatin1String("-GUIDAAZIONE")) {   // come "Fammi vedere" (prove): tab:Nome, panel:Nome, cmd:COMANDO
                guideAction(nextToken());
                QCoreApplication::processEvents();
                continue;
            }
            if (word == QLatin1String("-GUIDA")) {   // prove: avvia il percorso guidato n (-1 lo chiude)
                showPanel(MainView::Guide);
                const int n = nextToken().toInt();
                if (n < 0)
                    guide_->stopTour();
                else
                    guide_->startTour(n);
                QCoreApplication::processEvents();
                continue;
            }
            if (word == QLatin1String("-GUIDAAVANTI")) {   // prove: come il pulsante Avanti
                guide_->next();
                QCoreApplication::processEvents();
                continue;
            }
            if (word == QLatin1String("-GUIDASTATO")) {   // prove: stato del percorso nella riga di comando
                guideCheckStep();
                print(QStringLiteral("-GUIDASTATO percorso %1 passo %2 di %3 %4%5 evidenziato [%6]")
                          .arg(guide_->tour())
                          .arg(guide_->step() + 1)
                          .arg(guide_->stepCount())
                          .arg(guide_->stepDone() ? QStringLiteral("FATTO") : QStringLiteral("in attesa"))
                          .arg(guide_->finished() ? QStringLiteral(" (finito)") : QString())
                          .arg(ribbon_->highlight()));
                continue;
            }
            if (word == QLatin1String("-GUIDAMANUALE")) {   // prove: apre il manuale su un comando
                showPanel(MainView::Guide);
                const QString c = nextToken();
                print(guide_->openCommand(c) ? QStringLiteral("-GUIDAMANUALE %1: %2").arg(c, guide_->pageText().section(u'\n', 0, 0))
                                             : QStringLiteral("-GUIDAMANUALE %1: non trovato").arg(c));
                QCoreApplication::processEvents();
                continue;
            }
            if (word == QLatin1String("-SEGNALA")) {   // testo e indirizzo della segnalazione in un file (prove)
                const QString path = resolve(nextToken());
                const QString what = nextToken();
                print(writeReportForTest(path, what) ? tr("Segnalazione di prova scritta in %1").arg(QDir::toNativeSeparators(path))
                                                     : tr("Impossibile scrivere %1").arg(QDir::toNativeSeparators(path)));
                continue;
            }
            if (word == QLatin1String("-INFORMAZIONI")) {   // testo della finestra Informazioni (prove)
                for (const QString& line : aboutText(false).split(u'\n'))
                    print(line);
                continue;
            }
            if (word == QLatin1String("-CANALOSCHEDA")) {   // scheda CanaloCAD senza schermo: sezione.dxf -> tavola.dxf
                const QString section = resolve(nextToken());
                canaloTabSelfTest(section, resolve(nextToken()));
                continue;
            }
            if (d && word == QLatin1String("-CANALOVERIFICA")) {   // senza finestra: dati del tratto predefiniti
                canaloCheck(false);
                continue;
            }
            if (d && word == QLatin1String("-CANALOSEZIONE")) {   // sezione dal catalogo: FG16_4G10*2,DATA_CAT6...
                canaloCompose(nextToken());
                continue;
            }
            if (d && word == QLatin1String("-CANALOTAVOLA")) {   // come -CANALOVERIFICA, con la tavola nel disegno
                canaloCheck(false, true);
                continue;
            }
            if (d && (word == QLatin1String("CANALOINS") || word == QLatin1String("CANALOINSERISCI"))) {
                canaloInsert(resolve(nextToken()));
                continue;
            }
            // Circuiti negli script: -CIRCUITI schema.pdf (importa e verifica), -CIRCUITIASSEGNA sigla
            // (ai punti selezionati), -CIRCUITILEGENDA (legge e interpreta la legenda delle lettere),
            // -CIRCUITIPNG file (immagine della tavolozza, per diagnosi).
            if (d && word == QLatin1String("-CIRCUITI")) {
                QString error;
                if (!circuits_->importSchema(resolve(nextToken()), &error))
                    print(tr("-CIRCUITI: %1").arg(error));
                else
                    print(circuits_->runCheck());
                continue;
            }
            if (d && word == QLatin1String("-CIRCUITILEGENDA")) {
                showPanel(MainView::Circuits);
                circuits_->showLegend();
                print(circuits_->fillLegend());
                continue;
            }
            if (d && word == QLatin1String("-CIRCUITIASSEGNA")) {
                qsizetype end = text.indexOf('\n', i);
                if (end < 0)
                    end = n;
                const QString code = text.mid(i, end - i).trimmed();
                i = end + 1;
                print(tr("-CIRCUITIASSEGNA: %1 punti").arg(circuits_->assignToSelection(code)));
                continue;
            }
            // -CANALOIMPORTA file POLILINEA|DETTAGLIO|NIENTE|CNx: un DWG/DXF di progetto come Importa DWG/DXF di
            // CanaloCAD, collocato senza finestra (POLILINEA = la polilinea selezionata).
            if (d && word == QLatin1String("-CANALOIMPORTA")) {
                const QString file = resolve(nextToken());
                const QString where = nextToken().toUpper();
                canalo_->setDrawing(d);
                canalo_->setScriptPlacement(where == QLatin1String("POLILINEA") ? QStringLiteral("pick")
                                            : where == QLatin1String("DETTAGLIO") ? QStringLiteral("detail")
                                            : where == QLatin1String("NIENTE")    ? QStringLiteral("none")
                                                                                  : where);
                canalo_->importFile(file);
                canalo_->setScriptPlacement({});
                for (const QString& line : canalo_->reportLines())
                    print(line);
                continue;
            }
            // Diagnosi (non documentato): immagine della finestra Canalina, con la scheda indicata (0 Cavi, 1 Riepilogo).
            if (word == QLatin1String("-CANALINAPNG")) {
                const QString target = resolve(nextToken());
                const int tab = nextToken().toInt();
                canalo_->showWindow();
                canalo_->resize(1180, 740);
                if (QQuickItem* rootItem = canalo_->rootObject())
                    rootItem->setProperty("tab", tab);
                for (int k = 0; k < 5; ++k)
                    QCoreApplication::processEvents();
                QElapsedTimer grabTimer;
                grabTimer.start();
                const QPixmap shot = canalo_->grab();
                const qint64 grabMs = grabTimer.elapsed();
                shot.save(target);
                print(QStringLiteral("-CANALINAPNG: disegno %1 ms").arg(grabMs));
                continue;
            }
            if (word == QLatin1String("-CIRCUITIPNG")) {
                const QString target = resolve(nextToken());
                showPanel(MainView::Circuits);
                circuits_->resize(560, 820);
                circuits_->refresh();
                for (int k = 0; k < 5; ++k)
                    QCoreApplication::processEvents();
                circuits_->grab().save(target);
                continue;
            }
            // Diagnosi (non documentato): immagine dell'area di disegno, com'è a schermo.
            if (d && word == QLatin1String("-VIEWPNG")) {
                const QString target = resolve(nextToken());
                QCoreApplication::processEvents();
                d->view()->grabImage().save(target);
                continue;
            }
            // Diagnosi (non documentato): la vista disegnata dalla GPU (Direct3D 11) in una texture, anche
            // senza schermo; nel registro il motore usato e il tempo di costruzione della scena.
            if (d && word == QLatin1String("-GPUPNG")) {
                const QString target = resolve(nextToken());
                QCoreApplication::processEvents();
                QString info;
                const QImage image = d->view()->grabGpuImage(&info);
                if (image.isNull())
                    print(tr("-GPUPNG: %1").arg(info));
                else {
                    image.save(target);
                    print(tr("-GPUPNG: %1").arg(info));
                }
                continue;
            }
            // Diagnosi (non documentato): apertura in un altro thread come fuori dagli script, e attesa della fine.
            if (word == QLatin1String("-APRIASINCRONO")) {
                forceAsyncOpen_ = true;
                openFile(resolve(nextToken()));
                forceAsyncOpen_ = false;
                continue;
            }
            if (word == QLatin1String("-ATTENDIAPERTURA")) {
                QElapsedTimer waited;
                waited.start();
                while (!opening_.empty() && waited.elapsed() < 120000)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                QCoreApplication::processEvents();
                continue;
            }
            // Diagnosi (non documentato): immagine dell'intera finestra (dimensione facoltativa: L A).
            if (word == QLatin1String("-WINDOWPNG")) {
                const QString target = resolve(nextToken());
                const qsizetype before = i;
                bool okW = false, okH = false;
                const int w = nextToken().toInt(&okW);
                const int h = okW ? nextToken().toInt(&okH) : 0;
                if (okW && okH && w > 0 && h > 0)
                    resize(w, h);
                else
                    i = before;   // niente dimensioni: il testo seguente è il comando dopo
                for (int k = 0; k < 5; ++k)
                    QCoreApplication::processEvents();
                grab().save(target);
                continue;
            }
            // Diagnosi (non documentato): immagine del gestore dei layer.
            if (word == QLatin1String("-LAYERSPNG")) {
                const QString target = resolve(nextToken());
                showPanel(MainView::Layers);
                layerManager_->refresh();
                layerManager_->resize(760, 420);
                for (int k = 0; k < 5; ++k)
                    QCoreApplication::processEvents();
                layerManager_->grab().save(target);
                continue;
            }
            // Diagnosi (non documentato): immagine della tavolozza Proprietà.
            if (word == QLatin1String("-PROPERTIESPNG")) {
                const QString target = resolve(nextToken());
                showPanel(MainView::Properties);
                properties_->refresh();
                properties_->resize(320, 900);
                for (int k = 0; k < 5; ++k)
                    QCoreApplication::processEvents();
                properties_->grab().save(target);
                continue;
            }
            // Diagnosi (non documentato): immagine di una scheda delle Opzioni.
            if (word == QLatin1String("-OPTIONSPNG")) {
                const QString target = resolve(nextToken());
                OptionsDialog dialog(this);
                dialog.setCurrentTab(nextToken().toInt());
                dialog.show();
                QCoreApplication::processEvents();
                dialog.grab().save(target);
                dialog.close();
                continue;
            }
            // Elenco dei riferimenti esterni (XREF e immagini) nella riga di comando, anche negli script.
            if (d && (word == QLatin1String("-XRIF") || word == QLatin1String("-XREF") || word == QLatin1String("-EXTERNALREFERENCES"))) {
                const auto refs = externalReferences(d->document());
                if (refs.empty())
                    print(tr("Nessun riferimento esterno (XREF o immagine) nel disegno."));
                for (const ExternalRef& r : refs)
                    print(tr("%1 %2 [%3, %4 uso/i]: salvato \"%5\"%6")
                              .arg(r.kind == ExternalRef::Drawing ? tr("XREF") : tr("Immagine"), r.name,
                                   externalRefStatus(r))
                              .arg(r.uses)
                              .arg(QString::fromStdString(r.saved),
                                   r.found.isEmpty() ? QString() : tr(", trovato in %1").arg(QDir::toNativeSeparators(r.found))));
                continue;
            }
            // Diagnosi (non documentato): immagine di un dialogo in QML (COLORE, STILIQUOTA, INIZIO).
            if (word == QLatin1String("-DIALOGPNG")) {
                const QString which = nextToken().toUpper();
                const QString target = resolve(nextToken());
                std::unique_ptr<QWidget> dialog;
                if (which == QLatin1String("COLORE"))
                    dialog = std::make_unique<ColorDialog>(3, true, this);
                else if (which == QLatin1String("STILIQUOTA") && d)
                    dialog = std::make_unique<DimStyleDialog>(d, this);
                else if (which == QLatin1String("STILITESTO") && d)
                    dialog = std::make_unique<TextStyleDialog>(d, this);
                else if (which == QLatin1String("STILIDIRETTRICE") && d)
                    dialog = std::make_unique<MLeaderStyleDialog>(d, this);
                else if (which == QLatin1String("ASSISTENTE")) {
                    FormDialog::grabNext(target);
                    showClaudeDialog();
                    continue;
                } else if (which == QLatin1String("SEGNALA")) {
                    FormDialog::grabNext(target);
                    showReportDialog();
                    continue;
                } else if (which == QLatin1String("MOTIVI")) {
                    FormDialog::grabNext(target);
                    chooseHatchPattern(this);
                    continue;
                } else if (which == QLatin1String("CANALOSEZIONE") && d) {
                    FormDialog::grabNext(target);
                    canaloCompose();
                    continue;
                }
                else if (which == QLatin1String("TROVA") && d) {
                    FormDialog::grabNext(target);
                    showFind(QStringLiteral("QUADRO"));
                    continue;
                } else if (which == QLatin1String("ATTRIBUTI") && d) {
                    for (const auto& e : d->document().entities())
                        if (e->type() == EntityType::Insert && !static_cast<const InsertEntity&>(*e).attributes.empty()) {
                            FormDialog::grabNext(target);
                            editAttributes(e->id());
                            break;
                        }
                    continue;
                } else if (which == QLatin1String("BATTMAN") && d) {
                    FormDialog::grabNext(target);
                    showAttributeManager();
                    continue;
                } else if (which == QLatin1String("EDITORCTB")) {
                    if (const auto table = loadPlotStyleTable(QStringLiteral("Colori.ctb")))
                        dialog = std::make_unique<StyleEditor>(this, *table, QStringLiteral("Colori.ctb"));
                } else if (which == QLatin1String("ANTEPRIMA") && d) {
                    QString error;
                    const QImage image = renderPlotPreview(*d, plotJobFor(*d, d->shownSpace()), 60.0 / 25.4, &error);
                    if (!image.isNull())
                        dialog = std::make_unique<PreviewDialog>(this, image, tr("Anteprima di stampa"));
                } else if (which == QLatin1String("STILI")) {
                    grabNextPlotStyleManager(target);
                    showPlotStyleManager(this);
                    continue;
                } else if (which == QLatin1String("RIFERIMENTI") && d) {
                    dialogGrab_ = target;
                    showExternalReferences();
                    continue;
                } else if (which == QLatin1String("IMPOSTAPAGINA") && d) {
                    auto setup = std::make_unique<PlotDialog>(*d, d->shownSpace(), true, this);
                    setup->setExpanded(true, false);
                    dialog = std::move(setup);
                }
                else if (which.startsWith(QLatin1String("BLOCCHI"))) {
                    // Tavolozza Blocchi: BLOCCHI (disegno), BLOCCHILIB (libreria), BLOCCHIREC (recenti).
                    auto page = std::make_unique<BlockLibrary>();
                    page->setDrawing(d);
                    if (page->source().isEmpty())
                        page->setSource(QUrl(QStringLiteral("qrc:/qml/BlockLibrary.qml")));
                    page->setTab(which == QLatin1String("BLOCCHILIB") ? 1 : which == QLatin1String("BLOCCHIREC") ? 2 : 0);
                    page->resize(360, 720);
                    QElapsedTimer waited;
                    waited.start();
                    while (page->statusText().startsWith(tr("Lettura")) && waited.elapsed() < 120000)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    for (const QQmlError& e : page->errors())
                        print(e.toString());
                    dialog = std::move(page);
                }
                else if (which == QLatin1String("INIZIO") || which == QLatin1String("APPRENDI")) {
                    auto page = std::make_unique<StartPage>();
                    fillStartPage(page.get());
                    page->setPage(which == QLatin1String("APPRENDI") ? 1 : 0);
                    if (page->source().isEmpty())   // nella scena unica la carica MainView.qml
                        page->setSource(QUrl(QStringLiteral("qrc:/qml/StartPage.qml")));
                    for (const QQmlError& e : page->errors())
                        print(e.toString());
                    page->resize(1600, 900);
                    dialog = std::move(page);
                }
                if (dialog) {
                    dialog->show();
                    // Un attimo per il QML (adattamenti rinviati con Qt.callLater).
                    QEventLoop wait;
                    QTimer::singleShot(300, &wait, &QEventLoop::quit);
                    wait.exec();
                    dialog->grab().save(target);
                    dialog->close();
                }
                continue;
            }
            // Diagnosi (non documentato): immagine del dialogo Stampa, per controllarne l'aspetto.
            if (d && word == QLatin1String("-PLOTDIALOGPNG")) {
                const QString target = resolve(nextToken());
                PlotDialog dialog(*d, d->shownSpace(), false, this);
                dialog.setExpanded(true, false);
                dialog.show();
                QCoreApplication::processEvents();
                dialog.grab().save(target);
                dialog.close();
                continue;
            }
        }

        if (!d) {
            if (!token.isEmpty())
                print(tr("Nessun disegno aperto per \"%1\".").arg(token));
            continue;
        }
        d->submit(token);
    }

    scriptRunning_ = nested;
    print(tr("Script terminato."));
    if (quit) {
        discardOnClose_ = true;   // come QUIT negli script: nessuna domanda
        close();
    }
    return true;
}

// ======================================================= variabili di sistema

QString MainWindow::systemVariable(const QString& name) const
{
    const auto flag = [](const QAction* a) { return a->isChecked() ? QStringLiteral("1") : QStringLiteral("0"); };
    if (name == QLatin1String("OSMODE"))
        return QString::number(osnapAct_->isChecked() ? settings_.osmode : settings_.osmode | Osnap::Off);
    if (name == QLatin1String("ORTHOMODE"))
        return flag(orthoAct_);
    if (name == QLatin1String("GRIDMODE"))
        return flag(gridAct_);
    if (name == QLatin1String("SNAPMODE"))
        return flag(snapAct_);
    if (name == QLatin1String("LWDISPLAY"))
        return flag(lwAct_);
    if (name == QLatin1String("DYNMODE"))
        return dynAct_->isChecked() ? QStringLiteral("3") : QStringLiteral("0");
    if (name == QLatin1String("TILEMODE")) {
        const DrawingWidget* d = current();
        return d && d->shownSpace() != 0 ? QStringLiteral("0") : QStringLiteral("1");
    }
    if (name == QLatin1String("CTAB")) {
        const DrawingWidget* d = current();
        const Layout* l = d ? d->document().findLayout(d->shownSpace()) : nullptr;
        return l ? QString::fromStdString(l->name) : tr("Modello");
    }
    if (name == QLatin1String("CVPORT")) {   // finestra in cui si lavora (0 = nessuna)
        const DrawingWidget* d = current();
        return QString::number(d && d->inViewport() ? d->view()->activeViewport() : 0);
    }
    static const QMap<QString, QString> defaults = {
        {QStringLiteral("FILEDIA"), QStringLiteral("1")}, {QStringLiteral("CMDECHO"), QStringLiteral("1")},
        {QStringLiteral("ATTREQ"), QStringLiteral("1")}, {QStringLiteral("ATTDIA"), QStringLiteral("0")},
        {QStringLiteral("EXPERT"), QStringLiteral("0")},
    };
    if (defaults.contains(name))
        return extraVariables_.value(name, defaults.value(name));
    return {};
}

bool MainWindow::setSystemVariable(const QString& name, const QString& value)
{
    if (name == QLatin1String("CVPORT"))
        return false;   // sola lettura: si cambia con MSPACE o con un doppio clic
    if (name == QLatin1String("CTAB")) {   // scheda corrente: "Modello" o il nome di un layout
        DrawingWidget* d = current();
        if (!d)
            return false;
        if (value.compare(tr("Modello"), Qt::CaseInsensitive) == 0 || value.compare(QLatin1String("Model"), Qt::CaseInsensitive) == 0) {
            d->setSpace(0);
            return true;
        }
        for (const Layout& l : d->document().layouts())
            if (QString::fromStdString(l.name).compare(value, Qt::CaseInsensitive) == 0) {
                d->setSpace(l.id);
                return true;
            }
        return false;
    }
    bool ok = false;
    const int v = value.toInt(&ok);
    if (!ok)
        return false;
    if (name == QLatin1String("TILEMODE")) {   // 1 = modello, 0 = primo layout
        DrawingWidget* d = current();
        if (!d)
            return false;
        const auto& layouts = d->document().layouts();
        d->setSpace(v != 0 || layouts.empty() ? 0 : layouts.front().id);
        return true;
    }
    if (name == QLatin1String("OSMODE")) {
        // Le modalità sono i bit bassi; il bit 16384 spegne gli snap senza perdere le modalità.
        const int modes = v & ~Osnap::Off;
        if (modes != 0)
            settings_.osmode = modes;
        osnapAct_->setChecked(modes != 0 && (v & Osnap::Off) == 0);
        return true;
    }
    QAction* toggle = name == QLatin1String("ORTHOMODE") ? orthoAct_
                      : name == QLatin1String("GRIDMODE") ? gridAct_
                      : name == QLatin1String("SNAPMODE") ? snapAct_
                      : name == QLatin1String("LWDISPLAY") ? lwAct_
                      : name == QLatin1String("DYNMODE")  ? dynAct_
                                                         : nullptr;
    if (toggle) {
        toggle->setChecked(v != 0);
        return true;
    }
    if (!systemVariable(name).isEmpty()) {
        extraVariables_[name] = QString::number(v);
        return true;
    }
    return false;
}

} // namespace cartesio

namespace cartesio {

namespace {
QString scaleLabel(double s)
{
    const auto fmt = [](double v) {
        return std::abs(v - std::round(v)) < 1e-6 ? QString::number(std::llround(v)) : QString::number(v, 'g', 6);
    };
    if (s <= 0.0)
        return QStringLiteral("?");
    return s >= 1.0 ? fmt(s) + QStringLiteral(":1") : QStringLiteral("1:") + fmt(1.0 / s);
}
} // namespace

// Finestra a cui si riferiscono scala e lucchetto: quella in cui si lavora, oppure l'unica finestra
// selezionata sul foglio.
EntityId MainWindow::statusViewport() const
{
    const DrawingWidget* d = current();
    if (!d || d->shownSpace() == 0)
        return 0;
    if (d->inViewport())
        return d->view()->activeViewport();
    EntityId found = 0;
    for (EntityId id : d->selection()) {
        const Entity* e = d->document().find(id);
        if (!e || e->type() != EntityType::Viewport)
            continue;
        if (found)
            return 0;   // più finestre selezionate
        found = id;
    }
    return found;
}

void MainWindow::updateViewportStatus()
{
    const DrawingWidget* d = current();
    if (status_) {
        // Scala di annotazione: nello spazio modello (nei layout conta la scala di ogni finestra).
        if (d && d->shownSpace() == 0)
            status_->setAnnotation(true, QString::fromStdString(d->document().annotationScale()));
        else
            status_->setAnnotation(false);
    }
    const EntityId id = statusViewport();
    const Entity* e = d && id ? d->document().find(id) : nullptr;
    const auto* vp = e && e->type() == EntityType::Viewport ? static_cast<const ViewportEntity*>(e) : nullptr;
    if (!status_)
        return;
    if (!vp) {
        status_->setViewport(false);
        return;
    }
    status_->setViewport(true, scaleLabel(vp->scale()) + QStringLiteral(" ▾"), !vp->locked,
                         icon(vp->locked ? Icon::LockClosed : Icon::LockOpen),
                         vp->locked ? tr("Finestra bloccata: zoom e pan agiscono sul foglio. Clic per sbloccare")
                                    : tr("Blocca la finestra: la scala e la vista del modello non cambiano più"));
}

void MainWindow::setViewportScale(double scale)
{
    DrawingWidget* d = current();
    const EntityId id = statusViewport();
    if (!d || !id || scale <= 0.0)
        return;
    d->modifyEntities({id}, [scale](Entity& e) { static_cast<ViewportEntity&>(e).setScale(scale); });
    print(tr("Scala della finestra: %1").arg(scaleLabel(scale)));
}

} // namespace cartesio
