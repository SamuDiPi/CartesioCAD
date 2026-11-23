// Schede della barra multifunzione oltre a Disegno: Inserimenti, Testi e quote, Schermo, Strumenti, Stampa e PDF e
// la scheda Tavola (contestuale: solo nei layout). L'ordine finale lo dà Ribbon::arrange in MainWindow.cpp.

#include "app/MainWindow.h"

#include "app/AppOptions.h"
#include "app/CadView.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/Icons.h"
#include "app/Ribbon.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>

namespace cartesio {

void MainWindow::setupRibbonTabs(Ribbon* ribbon)
{
    const auto withMenu = [this](QAction* main, std::initializer_list<QAction*> items) {
        auto* menu = new QMenu(this);
        for (QAction* a : items)
            menu->addAction(a);
        main->setMenu(menu);
        return main;
    };
    const auto cmd = [this](const QString& text, const QIcon& ic, const char* command) {
        return makeCommand(text, ic, QString::fromLatin1(command));
    };

    // ================================================================ Inserisci
    RibbonPanel* blockPanel = ribbon->addPanel(tr("Inserimenti"), tr("Blocchi"));
    blockPanel->addLargeButton(insertAct_);
    QAction* eattedit = cmd(tr("Edita\nattributo"), icon(Icon::AttributeDef), "EATTEDIT");
    blockPanel->addLargeButton(withMenu(eattedit, {cmd(tr("Edita attributo"), icon(Icon::AttributeDef), "EATTEDIT"),
                                                   cmd(tr("Sincronizza attributi"), icon(Icon::AttributeDef), "ATTSYNC")}));
    RibbonPanel* blockDef = ribbon->addPanel(tr("Inserimenti"), tr("Crea blocchi"));
    blockDef->addLargeButton(blockAct_);
    blockDef->addLargeButton(attdefAct_);
    blockDef->addLargeButton(cmd(tr("Gestisci\nattributi"), icon(Icon::AttributeDef), "BATTMAN"));
    blockDef->addLargeButton(cmd(tr("Editor\nblocchi"), icon(Icon::BlockCreate), "BEDIT"));
    RibbonPanel* refPanel = ribbon->addPanel(tr("Inserimenti"), tr("File collegati"));
    QAction* attach = cmd(tr("Allega"), icon(Icon::BlockInsert), "XATTACH");
    refPanel->addLargeButton(withMenu(attach, {cmd(tr("Riferimento DWG/DXF (XATTACH)..."), icon(Icon::BlockInsert), "XATTACH"),
                                               cmd(tr("Immagine raster (IMAGEATTACH)..."), icon(Icon::BlockInsert), "IMAGEATTACH")}));
    refPanel->addLargeButton(cmd(tr("Ritaglia"), icon(Icon::Trim), "XCLIP"));
    // Cornici delle coperture (WIPEOUTFRAME): nascoste, visibili e stampate, visibili e non stampate.
    auto* frameMenu = new QMenu(this);
    const QStringList frames = {tr("Cornici nascoste"), tr("Cornici visibili e stampate"), tr("Cornici visibili, non stampate")};
    for (int i = 0; i < 3; ++i)
        frameMenu->addAction(frames[i], this, [this, i] {
            if (DrawingWidget* d = current()) {
                d->submit(QStringLiteral("WIPEOUTFRAME"));
                d->submit(QString::number(i));
            }
        });
    QAction* frameAct = makeAction(tr("Variante cornice"), icon(Icon::WipeoutIcon), {}, tr("Cornici delle coperture (WIPEOUTFRAME)"));
    frameAct->setMenu(frameMenu);
    refPanel->addSmallColumn({cmd(tr("Riferimenti esterni"), icon(Icon::BlockInsert), "XREF"), frameAct,
                              cmd(tr("Inserisci da file"), icon(Icon::Open), "INSERTFILE")});

    // ================================================================ Annota
    RibbonPanel* textPanel = ribbon->addPanel(tr("Testi e quote"), tr("Testo"));
    textPanel->addLargeButton(withMenu(cmd(tr("Testo\nmultilinea"), icon(Icon::TextMulti), "MTEXT"), {mtextAct_, textAct_}));
    annoTextStyleCombo_ = new ComboModel(170, this);
    annoTextStyleCombo_->setToolTip(tr("Stile di testo corrente"));
    annoTextHeightCombo_ = new ComboModel(170, this);
    annoTextHeightCombo_->setToolTip(tr("Altezza del testo (TEXTSIZE)"));
    textPanel->addComboColumn({annoTextStyleCombo_, annoTextHeightCombo_},
                              {{cmd(tr("Trova testo"), glyphIcon(QStringLiteral("a→b")), "FIND"),
                                cmd(tr("Stili di testo"), icon(Icon::TextSingle), "STYLE")}});
    connect(annoTextStyleCombo_, &ComboModel::activated, this, [this](int index) {
        if (DrawingWidget* d = current(); d && !updatingCombos_) {
            d->submit(QStringLiteral("TEXTSTYLE"));
            d->submit(annoTextStyleCombo_->itemText(index));
        }
    });
    connect(annoTextHeightCombo_, &ComboModel::activated, this, [this](int index) {
        DrawingWidget* d = current();
        if (!d || updatingCombos_)
            return;
        double h = annoTextHeightCombo_->itemData(index).toDouble();
        if (h <= 0.0) {
            h = 2.5;
            FormDialog ask(tr("Altezza del testo"), FormDialog::Ok | FormDialog::Cancel, this);
            ask.number(tr("Altezza:"), 0.0001, 100000.0, 4, h);
            ask.setSize(320, 140);
            if (ask.exec() != QDialog::Accepted) {
                updateAnnotateCombos();
                return;
            }
        }
        d->submit(QStringLiteral("TEXTSIZE"));
        d->submit(QString::number(h, 'g', 10));
        updateAnnotateCombos();
    });

    RibbonPanel* dimPanel = ribbon->addPanel(tr("Testi e quote"), tr("Quote"));
    QAction* dimAct = cmd(tr("Quota"), icon(Icon::DimLinear), "DIMLINEAR");
    dimPanel->addLargeButton(withMenu(dimAct, {dimLinearAct_, dimAlignedAct_, dimAngularAct_, dimRadiusAct_, dimDiameterAct_,
                                               dimOrdinateAct_}));
    annoDimStyleCombo_ = new ComboModel(170, this);
    annoDimStyleCombo_->setToolTip(tr("Stile di quota corrente"));
    dimPanel->addComboColumn({annoDimStyleCombo_}, {{cmd(tr("Modifica quote"), icon(Icon::DimAligned), "DIMEDIT"),
                                                      cmd(tr("Sposta testo di quota"), icon(Icon::DimAligned), "DIMTEDIT"),
                                                      dimStyleAct_}});
    connect(annoDimStyleCombo_, &ComboModel::activated, this, [this](int index) {
        if (DrawingWidget* d = current(); d && !updatingCombos_) {
            d->submit(QStringLiteral("-DIMSTYLE"));
            d->submit(annoDimStyleCombo_->itemText(index));
        }
    });
    QAction* linear = makeAction(tr("Lineare"), icon(Icon::DimLinear), {}, tr("Quote lineari e altre"));
    connect(linear, &QAction::triggered, dimLinearAct_, &QAction::trigger);
    QAction* cont = makeAction(tr("Continua"), icon(Icon::DimLinear), {}, tr("Quote continue o dalla linea di base"));
    connect(cont, &QAction::triggered, dimContinueAct_, &QAction::trigger);
    dimPanel->addSmallColumn({withMenu(linear, {dimLinearAct_, dimAlignedAct_, dimAngularAct_, dimRadiusAct_, dimDiameterAct_,
                                                dimOrdinateAct_}),
                              qdimAct_, withMenu(cont, {dimContinueAct_, dimBaselineAct_})});

    RibbonPanel* centers = ribbon->addPanel(tr("Testi e quote"), tr("Assi"));
    centers->addLargeButton(cmd(tr("Centro"), glyphIcon(QStringLiteral("⊕")), "CENTERMARK"));
    centers->addLargeButton(cmd(tr("Linea d'asse"), glyphIcon(QStringLiteral("—·—")), "CENTERLINE"));

    RibbonPanel* leaders = ribbon->addPanel(tr("Testi e quote"), tr("Direttrici"));
    leaders->addLargeButton(leaderAct_);
    annoLeaderStyleCombo_ = new ComboModel(170, this);
    annoLeaderStyleCombo_->setToolTip(tr("Stile di direttrice corrente"));
    leaders->addComboColumn({annoLeaderStyleCombo_}, {{cmd(tr("Stili direttrici"), icon(Icon::Leader), "MLEADERSTYLE")}});
    leaders->addSmallColumn({cmd(tr("Aggiungi direttrice"), glyphIcon(QStringLiteral("+↗")), "MLEADERADD"),
                             cmd(tr("Rimuovi direttrice"), glyphIcon(QStringLiteral("−↗")), "MLEADERREMOVE")});
    leaders->addSmallColumn({cmd(tr("Allinea"), glyphIcon(QStringLiteral("≡")), "MLEADERALIGN"),
                             cmd(tr("Raccogli"), glyphIcon(QStringLiteral("⋀")), "MLEADERCOLLECT")});
    connect(annoLeaderStyleCombo_, &ComboModel::activated, this, [this](int index) {
        DrawingWidget* d = current();
        if (!d || updatingCombos_)
            return;
        const QString name = annoLeaderStyleCombo_->itemData(index).toString();
        d->document().setCurrentMLeaderStyle(name.toStdString());
        d->notifyChanged();
    });

    RibbonPanel* tables = ribbon->addPanel(tr("Testi e quote"), tr("Tabelle"));
    tables->addLargeButton(cmd(tr("Tabella"), icon(Icon::TableIcon), "TABLE"));

    RibbonPanel* revision = ribbon->addPanel(tr("Testi e quote"), tr("Revisione"));
    revision->addLargeButton(cmd(tr("Maschera"), icon(Icon::WipeoutIcon), "WIPEOUT"));
    revision->addLargeButton(cmd(tr("Nuvola di\nrevisione"), icon(Icon::Revcloud), "REVCLOUD"));

    RibbonPanel* scales = ribbon->addPanel(tr("Testi e quote"), tr("Scale"));
    QAction* addCurrent = makeAction(tr("Aggiungi\nscala corrente"), glyphIcon(QStringLiteral("1:n+")), {},
                                     tr("Aggiunge la scala di annotazione corrente agli oggetti annotativi selezionati"));
    connect(addCurrent, &QAction::triggered, this, [this] {
        DrawingWidget* d = current();
        if (!d)
            return;
        if (d->selection().empty()) {
            print(tr("Scegli prima gli oggetti annotativi."));
            return;
        }
        const int n = d->document().addObjectScale(d->selection(), d->document().annotationScale());
        print(tr("Scala %1 aggiunta a %2 oggetti.").arg(QString::fromStdString(d->document().annotationScale())).arg(n));
        d->notifyChanged();
    });
    scales->addLargeButton(addCurrent);
    scales->addSmallColumn({cmd(tr("Elenco scale"), glyphIcon(QStringLiteral("1:n")), "SCALELISTEDIT"),
                            cmd(tr("Aggiungi/Elimina scale"), glyphIcon(QStringLiteral("±")), "OBJECTSCALE")});

    // ================================================================ Vista
    RibbonPanel* viewTools = ribbon->addPanel(tr("Schermo"), tr("Elementi"));
    const auto optionToggle = [this](const QString& text, const QIcon& ic, const QString& tip, bool AppOptions::*field) {
        QAction* a = makeAction(text, ic, {}, tip);
        a->setCheckable(true);
        a->setChecked(appOptions().*field);
        connect(a, &QAction::toggled, this, [this, field](bool on) {
            appOptions().*field = on;
            appOptions().save();
            if (field == &AppOptions::showFileTabs)
                fileTabs_->setShown(on);
            refreshLayoutTabs();
            if (DrawingWidget* d = current())
                d->view()->update();
        });
        return a;
    };
    viewTools->addLargeButton(optionToggle(tr("Icona\ndegli assi"), glyphIcon(QStringLiteral("XY")), tr("Icona degli assi nell'angolo (UCSICON)"),
                                           &AppOptions::showUcsIcon));
    viewTools->addLargeButton(optionToggle(tr("Pulsanti di\nnavigazione"), icon(Icon::Pan),
                                           tr("Pulsanti di navigazione sul disegno: pan e zoom"), &AppOptions::showNavigationBar));

    RibbonPanel* views = ribbon->addPanel(tr("Schermo"), tr("Viste salvate"));
    viewsCombo_ = new ComboModel(180, this);
    viewsCombo_->setToolTip(tr("Viste salvate del disegno: sceglierne una per ripristinarla"));
    QAction* newView = makeAction(tr("Nuova vista"), icon(Icon::ZoomExtents), {}, tr("Salva con un nome quello che si vede ora"));
    connect(newView, &QAction::triggered, this, [this] {
        DrawingWidget* d = current();
        if (!d)
            return;
        QString name = tr("Vista %1").arg(d->document().namedViews().size() + 1);
        FormDialog ask(tr("Nuova vista"), FormDialog::Ok | FormDialog::Cancel, this);
        ask.text(tr("Nome:"), name);
        ask.setSize(340, 140);
        if (ask.exec() != QDialog::Accepted || name.trimmed().isEmpty())
            return;
        const BBox area = d->view()->visibleArea();
        d->document().setNamedView({name.trimmed().toStdString(), area.center(), area.width(), area.height(), d->shownSpace()});
        print(tr("Vista \"%1\" salvata.").arg(name.trimmed()));
        d->notifyChanged();
    });
    QAction* manageViews = makeAction(tr("Gestione viste"), icon(Icon::ListIcon), {}, tr("Ripristina, rinomina o elimina le viste con nome"));
    connect(manageViews, &QAction::triggered, this, [this] { manageNamedViews(); });
    views->addComboColumn({viewsCombo_}, {{newView, manageViews}});
    connect(viewsCombo_, &ComboModel::activated, this, [this](int index) {
        if (!updatingCombos_)
            restoreNamedView(viewsCombo_->itemText(index));
    });

    // Finestre del modello affiancate (VPORTS).
    RibbonPanel* tilesPanel = ribbon->addPanel(tr("Schermo"), tr("Finestre del modello"));
    auto* tilesMenu = new QMenu(this);
    const QStringList tileNames = {tr("Una"), tr("Due: verticali"), tr("Due: orizzontali"), tr("Tre: una grande a sinistra"),
                                   tr("Quattro")};
    for (int i = 0; i < tileNames.size(); ++i)
        tilesMenu->addAction(tileNames[i], this, [this, i] {
            if (DrawingWidget* d = current())
                d->setTiles(static_cast<DrawingWidget::Tiles>(i));
        });
    QAction* tilesAct = makeAction(tr("Configurazione\nfinestre"), glyphIcon(QStringLiteral("\u229E")), {},
                                   tr("Divide lo spazio modello in più viste (VPORTS): un clic su una vista la rende attiva"));
    connect(tilesAct, &QAction::triggered, this, [this] { chooseTiles(); });
    tilesAct->setMenu(tilesMenu);
    tilesPanel->addLargeButton(tilesAct);
    QAction* joinTiles = makeAction(tr("Unisci"), glyphIcon(QStringLiteral("\u25A1")), {}, tr("Torna a una vista sola"));
    connect(joinTiles, &QAction::triggered, this, [this] {
        if (DrawingWidget* d = current())
            d->setTiles(DrawingWidget::Tiles::One);
    });
    tilesPanel->addSmallColumn({joinTiles});

    RibbonPanel* palettes = ribbon->addPanel(tr("Schermo"), tr("Pannelli"));
    palettes->addLargeButton(propertiesAct_);
    palettes->addLargeButton(libraryAct_);
    palettes->addLargeButton(layerPropsAct_);
    palettes->addSmallColumn({commandLineAct_, historyAct_}, false);

    RibbonPanel* uiPanel = ribbon->addPanel(tr("Schermo"), tr("Interfaccia"));
    auto* switchMenu = new QMenu(this);
    connect(switchMenu, &QMenu::aboutToShow, this, [this, switchMenu] {
        switchMenu->clear();
        for (int i = 0; i < fileTabs_->count(); ++i) {
            QAction* a = switchMenu->addAction(fileTabs_->tabText(i));
            a->setCheckable(true);
            a->setChecked(i == fileTabs_->currentIndex());
            connect(a, &QAction::triggered, this, [this, i] { fileTabs_->setCurrentIndex(i); });
        }
    });
    QAction* switchAct = makeAction(tr("Scambia\nfinestre"), icon(Icon::LayoutNew), {}, tr("Passa a un altro disegno aperto"));
    switchAct->setMenu(switchMenu);
    uiPanel->addLargeButton(switchAct);
    uiPanel->addLargeButton(optionToggle(tr("Schede\nFile"), icon(Icon::New), tr("Schede dei disegni aperti"), &AppOptions::showFileTabs));
    uiPanel->addLargeButton(optionToggle(tr("Schede\nLayout"), icon(Icon::LayoutNew), tr("Schede Modello e Layout nella barra di stato"),
                                         &AppOptions::showLayoutTabs));

    // ================================================================ Gestisci
    RibbonPanel* custom = ribbon->addPanel(tr("Strumenti"), tr("Personalizzazione"));
    QAction* optionsAct = makeAction(tr("Opzioni"), icon(Icon::Properties), {}, tr("Opzioni del programma"));
    connect(optionsAct, &QAction::triggered, this, [this] { showOptions(); });
    custom->addLargeButton(optionsAct);
    custom->addLargeButton(scriptAct_);
    RibbonPanel* cleanup = ribbon->addPanel(tr("Strumenti"), tr("Pulizia"));
    cleanup->addSmallColumn({cmd(tr("Elimina"), icon(Icon::Erase), "PURGE"), cmd(tr("Elimina duplicati"), icon(Icon::Erase), "OVERKILL"),
                             cmd(tr("Verifica"), glyphIcon(QStringLiteral("✓"), true), "AUDIT")});
    cleanup->addSmallColumn({cmd(tr("Recupera"), icon(Icon::Open), "RECOVER"), cmd(tr("Seleziona simili"), icon(Icon::MatchProp), "SELECTSIMILAR")});

    // ================================================================ Output
    RibbonPanel* plotPanel = ribbon->addPanel(tr("Stampa e PDF"), tr("Stampa"));
    plotPanel->addLargeButton(plotAct_);
    plotPanel->addLargeButton(publishAct_);
    plotPanel->addLargeButton(previewAct_);
    QAction* plotLog = makeAction(tr("Visualizza dettagli"), icon(Icon::ListIcon), {}, tr("Apre il registro delle stampe"));
    connect(plotLog, &QAction::triggered, this, [this] {
        const QString file = plotLogFile();
        if (!QFileInfo::exists(file)) {
            print(tr("Registro delle stampe ancora vuoto (si attiva nelle Opzioni, Stampa e pubblicazione)."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(file));
    });
    QAction* plotters = makeAction(tr("Gestione plotter"), icon(Icon::PlotIcon), {}, tr("Stampanti e tabelle stili di stampa (Opzioni)"));
    connect(plotters, &QAction::triggered, this, [this] { showOptions(2); });
    plotPanel->addSmallColumn({pageSetupAct_, plotLog, plotters});
    RibbonPanel* exportPanel = ribbon->addPanel(tr("Stampa e PDF"), tr("PDF"));
    exportPanel->addLargeButton(pdfAct_);

    // ================================================================ Layout (contestuale)
    RibbonPanel* layoutPanel = ribbon->addPanel(tr("Tavola"), tr("Layout"));
    layoutPanel->addLargeButton(layoutNewAct_);
    layoutPanel->addLargeButton(pageSetupAct_);
    RibbonPanel* viewportsPanel = ribbon->addPanel(tr("Tavola"), tr("Finestre"));
    viewportsPanel->addLargeButton(mviewAct_);
    viewportsPanel->addSmallColumn({vpclipAct_});
    RibbonPanel* titlePanel = ribbon->addPanel(tr("Tavola"), tr("Cartiglio"));
    titlePanel->addLargeButton(titleBlockAct_);
    titlePanel->addSmallColumn({titleUpdateAct_});
    ribbon->setContextual(tr("Tavola"));
}

void MainWindow::chooseTiles()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    int choice = static_cast<int>(d->tiles());
    FormDialog dialog(tr("Finestre del modello"), FormDialog::Ok | FormDialog::Cancel, this);
    dialog.choice(tr("Disposizione:"),
                  {{tr("Una"), 0}, {tr("Due: verticali"), 1}, {tr("Due: orizzontali"), 2}, {tr("Tre: una grande a sinistra"), 3},
                   {tr("Quattro"), 4}},
                  choice, [&choice](const QVariant& v) { choice = v.toInt(); });
    dialog.note(tr("Ogni vista ha il suo zoom; un clic su una vista la rende attiva. Valgono nello spazio modello."));
    dialog.setSize(380, 200);
    if (dialog.exec() == QDialog::Accepted)
        d->setTiles(static_cast<DrawingWidget::Tiles>(choice));
}

void MainWindow::updateAnnotateCombos()
{
    if (!annoTextStyleCombo_)
        return;
    updatingCombos_ = true;
    DrawingWidget* d = current();
    for (ComboModel* c : {annoTextStyleCombo_, annoTextHeightCombo_, annoDimStyleCombo_, annoLeaderStyleCombo_, viewsCombo_})
        c->clear();
    if (d) {
        const Document& doc = d->document();
        for (const TextStyle& s : doc.textStyles())
            annoTextStyleCombo_->addItem(QString::fromStdString(s.name));
        annoTextStyleCombo_->setCurrentIndex(std::max(0, annoTextStyleCombo_->findText(QString::fromStdString(doc.currentTextStyle()))));
        const double size = textSizeSetting();
        QList<double> heights = {1.0, 1.5, 1.8, 2.0, 2.5, 3.0, 3.5, 5.0, 7.0, 10.0};
        if (!heights.contains(size))
            heights << size;
        std::sort(heights.begin(), heights.end());
        for (double h : heights)
            annoTextHeightCombo_->addItem(QString::number(h, 'g', 6), h);
        annoTextHeightCombo_->addItem(tr("Altra altezza..."), 0.0);
        annoTextHeightCombo_->setCurrentIndex(std::max(0, annoTextHeightCombo_->findData(size)));
        for (const DimStyle& s : doc.dimStyles())
            annoDimStyleCombo_->addItem(QString::fromStdString(s.name));
        annoDimStyleCombo_->setCurrentIndex(std::max(0, annoDimStyleCombo_->findText(QString::fromStdString(doc.currentDimStyle().name))));
        annoLeaderStyleCombo_->addItem(tr("(dallo stile di quota)"), QString());
        for (const auto& s : doc.mleaderStyles())
            annoLeaderStyleCombo_->addItem(QString::fromStdString(s.name), QString::fromStdString(s.name));
        annoLeaderStyleCombo_->setCurrentIndex(std::max(0, annoLeaderStyleCombo_->findData(QString::fromStdString(doc.currentMLeaderStyle()))));
        viewsCombo_->addItem(tr("Vista non salvata"));
        for (const auto& v : doc.namedViews())
            viewsCombo_->addItem(QString::fromStdString(v.name));
        viewsCombo_->setCurrentIndex(0);
    }
    updatingCombos_ = false;
}

void MainWindow::restoreNamedView(const QString& name)
{
    DrawingWidget* d = current();
    if (!d)
        return;
    const Document::NamedView* v = d->document().findNamedView(name.toStdString());
    if (!v)
        return;
    // Vista di un altro spazio: prima si passa a quello (le viste dello spazio carta lette dal file: il primo layout).
    int space = v->space;
    if (space < 0 && !d->document().layouts().empty())
        space = d->document().layouts().front().id;
    if (space >= 0 && space != d->shownSpace())
        d->setSpace(space);
    BBox box;
    box.expand(v->center - Vec2{v->width / 2.0, v->height / 2.0});
    box.expand(v->center + Vec2{v->width / 2.0, v->height / 2.0});
    d->view()->zoomWindow(box);
    print(tr("Vista \"%1\" ripristinata.").arg(name));
}

void MainWindow::manageNamedViews()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    QStringList names;
    for (const auto& v : d->document().namedViews())
        names << QString::fromStdString(v.name);
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Gestione viste"), tr("Nessuna vista con nome: salvarne una con \"Nuova vista\"."));
        return;
    }
    int selected = 0;
    FormDialog dialog(tr("Gestione viste"), FormDialog::Close, this);
    dialog.group(tr("Viste del disegno"));
    const int list = dialog.list(names, 0, [&selected](int i) { selected = i; }, [&](int i) {
        restoreNamedView(names.value(i));
        dialog.accept();
    });
    dialog.buttonsRow({{tr("Ripristina"), [&] {
                            restoreNamedView(names.value(selected));
                            dialog.accept();
                        }},
                       {tr("Rinomina..."), [&] {
                            const QString old = names.value(selected);
                            QString name = old;
                            FormDialog ask(tr("Rinomina vista"), FormDialog::Ok | FormDialog::Cancel, &dialog);
                            ask.text(tr("Nome:"), name);
                            ask.setSize(340, 140);
                            if (ask.exec() != QDialog::Accepted || name.trimmed().isEmpty() || name == old)
                                return;
                            if (const auto* v = d->document().findNamedView(old.toStdString())) {
                                Document::NamedView copy = *v;
                                d->document().removeNamedView(old.toStdString());
                                copy.name = name.trimmed().toStdString();
                                d->document().setNamedView(copy);
                                names[selected] = name.trimmed();
                                dialog.setItems(list, names, selected);
                            }
                        }},
                       {tr("Elimina"), [&] {
                            if (selected < 0 || selected >= names.size())
                                return;
                            d->document().removeNamedView(names[selected].toStdString());
                            names.removeAt(selected);
                            selected = std::min(selected, static_cast<int>(names.size()) - 1);
                            dialog.setItems(list, names, selected);
                        }}});
    dialog.setSize(420, 380);
    dialog.exec();
    d->notifyChanged();
}

} // namespace cartesio
