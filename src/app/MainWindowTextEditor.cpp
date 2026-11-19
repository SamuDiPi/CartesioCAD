// Scheda contestuale "Editor di testo" della barra multifunzione: compare mentre si scrive un testo multilinea
// (anche di direttrici e celle di tabella) e lo comanda: Stile, Formattazione, Paragrafo, Inserisci, Strumenti,
// Opzioni, Chiudi.

#include "app/MainWindow.h"

#include "app/ColorDialog.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/Icons.h"
#include "app/Ribbon.h"
#include "app/TextEditInPlace.h"
#include "app/Theme.h"

#include <QDate>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMenu>
#include <QPainter>
#include <QTime>

namespace cartesio {

namespace {

constexpr auto kTab = "Editor di testo";

// Icone di allineamento: righe di lunghezze diverse allineate a sinistra, al centro, a destra o giustificate.
QIcon alignIcon(int align)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QPen(QColor(222, 226, 232), 2.0));
    const int lengths[] = {22, 16, 22, 12};
    for (int i = 0; i < 4; ++i) {
        const int len = align >= 3 ? 22 : lengths[i];
        const int x = align == 1 ? 16 - len / 2 : align == 2 ? 27 - len : 5;
        const int y = 8 + i * 5;
        p.drawLine(x, y, x + len, y);
    }
    return QIcon(pm);
}

QIcon swatch(int aci)
{
    QPixmap pm(14, 14);
    pm.fill(aci <= 0 ? QColor(0, 0, 0, 0) : theme::aciColor(aci));
    QPainter p(&pm);
    p.setPen(QColor(150, 158, 170));
    p.drawRect(0, 0, 13, 13);
    return QIcon(pm);
}

RichTextControls* editor()
{
    return TextEditorHub::instance().active();
}

} // namespace

void MainWindow::setupTextEditorTab(Ribbon* ribbon)
{
    const QString tab = tr(kTab);
    // Azione che comanda l'editor attivo (e gli ridà il fuoco).
    const auto act = [this](const QString& text, const QIcon& ic, const QString& tip, std::function<void(RichTextControls&)> run) {
        QAction* a = makeAction(text, ic, {}, tip);
        connect(a, &QAction::triggered, this, [run] {
            if (RichTextControls* e = editor()) {
                run(*e);
                e->refocus();
            }
        });
        return a;
    };
    const auto withMenu = [this](QAction* main, QMenu* menu) {
        main->setMenu(menu);
        return main;
    };

    // ---------------------------------------------------------------- Stile
    RibbonPanel* stylePanel = ribbon->addPanel(tab, tr("Stile"));
    stylePanel->setPriority(8);
    textStyleCombo_ = new ComboModel(150, this);
    textStyleCombo_->setToolTip(tr("Stile di testo del testo multilinea"));
    textHeightCombo_ = new ComboModel(110, this);
    textHeightCombo_->setToolTip(tr("Altezza del testo selezionato (o di quello che si scrive)"));
    textAnnotativeAct_ = act(tr("Annotativo"), icon(Icon::TextMulti), tr("Testo annotativo: segue la scala di annotazione"),
                             [this](RichTextControls& e) { e.setAnnotative(textAnnotativeAct_->isChecked()); });
    textAnnotativeAct_->setCheckable(true);
    textMaskAct_ = act(tr("Maschera"), icon(Icon::WipeoutIcon), tr("Maschera di sfondo: copre quello che sta sotto il testo"),
                       [this](RichTextControls& e) { e.setMask(textMaskAct_->isChecked()); });
    textMaskAct_->setCheckable(true);
    stylePanel->addComboColumn({textStyleCombo_, textHeightCombo_}, {{textAnnotativeAct_, textMaskAct_}});
    connect(textStyleCombo_, &ComboModel::activated, this, [this](int index) {
        if (RichTextControls* e = editor(); e && !updatingTextTab_) {
            e->setStyle(textStyleCombo_->itemText(index));
            e->refocus();
        }
    });
    connect(textHeightCombo_, &ComboModel::activated, this, [this](int index) {
        RichTextControls* e = editor();
        if (!e || updatingTextTab_)
            return;
        double h = textHeightCombo_->itemData(index).toDouble();
        if (h <= 0.0) {   // "Altra altezza..."
            double value = e->state().height;
            FormDialog ask(tr("Altezza del testo"), FormDialog::Ok | FormDialog::Cancel, this);
            ask.number(tr("Altezza:"), 0.0001, 100000.0, 4, value);
            ask.setSize(320, 140);
            if (ask.exec() != QDialog::Accepted) {
                refreshTextEditorTab();
                e->refocus();
                return;
            }
            h = value;
        }
        e->setHeight(h);
        e->refocus();
    });

    // ---------------------------------------------------------------- Formattazione
    RibbonPanel* format = ribbon->addPanel(tab, tr("Formattazione"));
    format->setPriority(9);
    textBoldAct_ = act(tr("Grassetto"), glyphIcon(tr("G"), true, false), tr("Grassetto (Ctrl+B)"),
                       [](RichTextControls& e) { e.toggle(RichTextControls::Flag::Bold); });
    textItalicAct_ = act(tr("Corsivo"), glyphIcon(tr("C"), false, true), tr("Corsivo (Ctrl+I)"),
                         [](RichTextControls& e) { e.toggle(RichTextControls::Flag::Italic); });
    textUnderlineAct_ = act(tr("Sottolineato"), glyphIcon(tr("S"), false, false, 1), tr("Sottolineato"),
                            [](RichTextControls& e) { e.toggle(RichTextControls::Flag::Underline); });
    textOverlineAct_ = act(tr("Sopralineato"), glyphIcon(QStringLiteral("O"), false, false, 2), tr("Sopralineato"),
                           [](RichTextControls& e) { e.toggle(RichTextControls::Flag::Overline); });
    textStrikeAct_ = act(tr("Barrato"), glyphIcon(QStringLiteral("abc"), false, false, 3), tr("Barrato"),
                         [](RichTextControls& e) { e.toggle(RichTextControls::Flag::Strike); });
    for (QAction* a : {textBoldAct_, textItalicAct_, textUnderlineAct_, textOverlineAct_, textStrikeAct_})
        a->setCheckable(true);
    auto* caseMenu = new QMenu(this);
    caseMenu->addAction(tr("MAIUSCOLE"), this, [] { if (auto* e = editor()) { e->changeCase(true); e->refocus(); } });
    caseMenu->addAction(tr("minuscole"), this, [] { if (auto* e = editor()) { e->changeCase(false); e->refocus(); } });
    QAction* caseAct = withMenu(makeAction(tr("Maiuscole/minuscole"), glyphIcon(QStringLiteral("Aa"), false, false), {},
                                           tr("Cambia in maiuscole o minuscole il testo selezionato")), caseMenu);
    QAction* clearAct = act(tr("Cancella formattazione"), glyphIcon(QStringLiteral("A"), false, false, 3),
                            tr("Toglie la formattazione dal testo selezionato (torna allo stile)"),
                            [](RichTextControls& e) { e.clearFormatting(); });
    textFontCombo_ = new ComboModel(170, this);
    textFontCombo_->setToolTip(tr("Carattere del testo selezionato"));
    textColorCombo_ = new ComboModel(170, this);
    textColorCombo_->setToolTip(tr("Colore del testo selezionato"));
    format->addSmallColumn({textBoldAct_, textItalicAct_, textUnderlineAct_}, false);
    format->addSmallColumn({textOverlineAct_, textStrikeAct_, caseAct}, false);
    format->addComboColumn({textFontCombo_, textColorCombo_}, {{clearAct}});
    connect(textFontCombo_, &ComboModel::activated, this, [this](int index) {
        if (RichTextControls* e = editor(); e && !updatingTextTab_) {
            e->setFamily(textFontCombo_->itemText(index));
            e->refocus();
        }
    });
    connect(textColorCombo_, &ComboModel::activated, this, [this](int index) {
        RichTextControls* e = editor();
        if (!e || updatingTextTab_)
            return;
        int aci = textColorCombo_->itemData(index).toInt();
        if (aci == -2) {
            const auto picked = ColorDialog::pick(7, false, this);
            if (!picked) {
                refreshTextEditorTab();
                e->refocus();
                return;
            }
            aci = *picked;
        }
        e->setColor(std::max(0, aci), -1);
        e->refocus();
    });

    // ---------------------------------------------------------------- Paragrafo
    RibbonPanel* paragraph = ribbon->addPanel(tab, tr("Paragrafo"));
    paragraph->setPriority(6);
    auto* justifyMenu = new QMenu(this);
    const QStringList justify = {tr("In alto a sinistra (TS)"), tr("In alto al centro (TC)"), tr("In alto a destra (TD)"),
                                 tr("Al centro a sinistra (MS)"), tr("Al centro (MC)"), tr("Al centro a destra (MD)"),
                                 tr("In basso a sinistra (BS)"), tr("In basso al centro (BC)"), tr("In basso a destra (BD)")};
    for (int i = 0; i < justify.size(); ++i) {
        QAction* a = justifyMenu->addAction(justify[i]);
        a->setCheckable(true);
        a->setData(i + 1);
        connect(a, &QAction::triggered, this, [i] { if (auto* e = editor()) { e->setAttachment(i + 1); e->refocus(); } });
    }
    textJustifyMenu_ = justifyMenu;
    QAction* justifyAct = withMenu(makeAction(tr("Giustificazione"), glyphIcon(QStringLiteral("A"), true, false), {},
                                              tr("Punto del testo che resta fermo (giustificazione)")), justifyMenu);
    auto* listMenu = new QMenu(this);
    const QStringList lists = {tr("Disattivato"), tr("Puntato"), tr("Numerato"), tr("Lettere")};
    for (int i = 0; i < lists.size(); ++i)
        listMenu->addAction(lists[i], this, [i] { if (auto* e = editor()) { e->setList(i); e->refocus(); } });
    QAction* listAct = withMenu(makeAction(tr("Elenchi puntati e numerati"), glyphIcon(QStringLiteral("1."), false, false), {},
                                           tr("Elenco puntato, numerato o con lettere sui paragrafi selezionati")), listMenu);
    auto* spacingMenu = new QMenu(this);
    for (double f : {1.0, 1.5, 2.0, 2.5}) {
        QAction* a = spacingMenu->addAction(QStringLiteral("%1x").arg(f, 0, 'f', 1));
        a->setCheckable(true);
        a->setData(f);
        connect(a, &QAction::triggered, this, [f] { if (auto* e = editor()) { e->setLineSpacing(f); e->refocus(); } });
    }
    spacingMenu->addSeparator();
    spacingMenu->addAction(tr("Altro..."), this, [this] {
        RichTextControls* e = editor();
        if (!e)
            return;
        double value = e->state().lineSpacing;
        FormDialog ask(tr("Spaziatura linea"), FormDialog::Ok | FormDialog::Cancel, this);
        ask.number(tr("Fattore (1 = normale):"), 0.25, 4.0, 2, value);
        ask.setSize(320, 140);
        if (ask.exec() == QDialog::Accepted)
            e->setLineSpacing(value);
        e->refocus();
    });
    textSpacingMenu_ = spacingMenu;
    QAction* spacingAct = withMenu(makeAction(tr("Spaziatura linea"), alignIcon(3), {}, tr("Interlinea del testo multilinea")),
                                   spacingMenu);
    const QStringList alignNames = {tr("Allinea a sinistra"), tr("Centra"), tr("Allinea a destra"), tr("Giustifica"),
                                    tr("Distribuisci")};
    for (int i = 0; i < 5; ++i) {
        textAlignActs_[i] = act(alignNames[i], alignIcon(i), alignNames[i] + tr(" (paragrafi selezionati)"),
                                [i](RichTextControls& e) { e.setAlign(i); });
        textAlignActs_[i]->setCheckable(true);
    }
    paragraph->addLargeButton(justifyAct);
    paragraph->addSmallColumn({listAct, spacingAct});
    paragraph->addSmallColumn({textAlignActs_[0], textAlignActs_[1], textAlignActs_[2]}, false);
    paragraph->addSmallColumn({textAlignActs_[3], textAlignActs_[4]}, false);

    // ---------------------------------------------------------------- Inserisci
    RibbonPanel* insert = ribbon->addPanel(tab, tr("Inserisci"));
    insert->setPriority(4);
    auto* symbolMenu = new QMenu(this);
    const std::pair<QString, QString> symbols[] = {
        {tr("Gradi"), QStringLiteral("°")},       {tr("Più/meno"), QStringLiteral("±")},
        {tr("Diametro"), QStringLiteral("Ø")},    {tr("Circa uguale"), QStringLiteral("≈")},
        {tr("Angolo"), QStringLiteral("∠")},      {tr("Delta"), QStringLiteral("Δ")},
        {tr("Diverso"), QStringLiteral("≠")},     {tr("Ohm"), QStringLiteral("Ω")},
        {tr("Quadrato"), QStringLiteral("²")},    {tr("Cubo"), QStringLiteral("³")},
        {tr("Minore o uguale"), QStringLiteral("≤")}, {tr("Maggiore o uguale"), QStringLiteral("≥")},
        {tr("Micro"), QStringLiteral("µ")},       {tr("Linea d'asse"), QStringLiteral("℄")},
    };
    for (const auto& [name, ch] : symbols)
        symbolMenu->addAction(ch + QStringLiteral("   ") + name, this,
                              [ch = ch] { if (auto* e = editor()) { e->insertText(ch); e->refocus(); } });
    symbolMenu->addSeparator();
    symbolMenu->addAction(tr("Spazio non divisibile"), this,
                          [] { if (auto* e = editor()) { e->insertText(QStringLiteral(" ")); e->refocus(); } });
    insert->addLargeButton(withMenu(makeAction(tr("Simbolo"), glyphIcon(QStringLiteral("@"), false, false), {},
                                               tr("Inserisce un simbolo nel testo")), symbolMenu));
    auto* fieldMenu = new QMenu(this);
    fieldMenu->addAction(tr("Data di oggi"), this, [] {
        if (auto* e = editor()) { e->insertText(QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy"))); e->refocus(); }
    });
    fieldMenu->addAction(tr("Ora"), this, [] {
        if (auto* e = editor()) { e->insertText(QTime::currentTime().toString(QStringLiteral("HH:mm"))); e->refocus(); }
    });
    fieldMenu->addAction(tr("Nome del file"), this, [this] {
        if (auto* e = editor(); e && current()) {
            e->insertText(QFileInfo(current()->displayName()).completeBaseName());
            e->refocus();
        }
    });
    fieldMenu->addAction(tr("Percorso del file"), this, [this] {
        if (auto* e = editor(); e && current()) {
            e->insertText(QString::fromStdString(current()->document().filePath()));
            e->refocus();
        }
    });
    insert->addLargeButton(withMenu(makeAction(tr("Campo dati"), glyphIcon(QStringLiteral("[#]"), false, false), {},
                                               tr("Inserisce data, ora, nome o percorso del file")), fieldMenu));

    auto* columnsMenu = new QMenu(this);
    const QStringList columnNames = {tr("Nessuna colonna"), tr("Due colonne"), tr("Tre colonne"), tr("Quattro colonne")};
    for (int i = 0; i < columnNames.size(); ++i)
        columnsMenu->addAction(columnNames[i], this, [i] { if (auto* e = editor()) { e->setColumns(i + 1); e->refocus(); } });
    insert->addLargeButton(withMenu(makeAction(tr("Colonne"), glyphIcon(QStringLiteral("\u2016")), {},
                                               tr("Divide il testo multilinea in colonne")), columnsMenu));

    // ---------------------------------------------------------------- Controllo ortografia
    RibbonPanel* spell = ribbon->addPanel(tab, tr("Ortografia"));
    spell->setPriority(3);
    textSpellAct_ = act(tr("Controllo\nortografia"), glyphIcon(QStringLiteral("\u2713"), true),
                        tr("Sottolinea le parole sbagliate (correttore di Windows); tasto destro sulla parola per i suggerimenti"),
                        [this](RichTextControls& e) { e.setSpellCheck(textSpellAct_->isChecked()); });
    textSpellAct_->setCheckable(true);
    textSpellAct_->setChecked(true);
    spell->addLargeButton(textSpellAct_);

    // ---------------------------------------------------------------- Strumenti, Opzioni, Chiudi
    RibbonPanel* tools = ribbon->addPanel(tab, tr("Strumenti"));
    tools->setPriority(3);
    tools->addLargeButton(act(tr("Trova e\nsostituisci"), glyphIcon(QStringLiteral("a→b"), false, false), tr("Trova e sostituisce nel testo"),
                              [this](RichTextControls& e) {
                                  QString find, replace;
                                  bool matchCase = false;
                                  FormDialog ask(tr("Trova e sostituisci"), FormDialog::Ok | FormDialog::Cancel, this);
                                  ask.text(tr("Trova:"), find);
                                  ask.text(tr("Sostituisci con:"), replace);
                                  ask.check(tr("Maiuscole/minuscole"), matchCase);
                                  ask.setSize(380, 200);
                                  if (ask.exec() != QDialog::Accepted)
                                      return;
                                  const int n = e.replaceAll(find, replace, matchCase);
                                  print(tr("Sostituzioni nel testo: %1").arg(n));
                              }));
    RibbonPanel* options = ribbon->addPanel(tab, tr("Opzioni"));
    options->setPriority(2);
    textRulerAct_ = act(tr("Righello"), alignIcon(0), tr("Righello sopra il testo: la maniglia ne cambia la larghezza"),
                        [this](RichTextControls& e) { e.setRuler(textRulerAct_->isChecked()); });
    textRulerAct_->setCheckable(true);
    textRulerAct_->setChecked(true);
    options->addSmallColumn({textRulerAct_});
    RibbonPanel* closePanel = ribbon->addPanel(tab, tr("Chiudi"));
    closePanel->setPriority(10);   // sempre visibile
    closePanel->addLargeButton(act(tr("Chiudi editor\ndi testo"), glyphIcon(QStringLiteral("✓"), true, false), tr("Conferma il testo e chiude l'editor"),
                                   [](RichTextControls& e) { e.close(true); }));

    ribbon->setContextual(tab);
    connect(&TextEditorHub::instance(), &TextEditorHub::activeChanged, this, [this, ribbon, tab] {
        const bool on = editor() != nullptr;
        ribbon->showContextual(tab, on);
        if (on)
            refreshTextEditorTab(true);
    });
    connect(&TextEditorHub::instance(), &TextEditorHub::stateChanged, this, [this] { refreshTextEditorTab(); });
}

void MainWindow::refreshTextEditorTab(bool lists)
{
    RichTextControls* e = editor();
    if (!e || !textStyleCombo_)
        return;
    const RichTextControls::State s = e->state();
    updatingTextTab_ = true;
    DrawingWidget* d = current();
    if (lists || textStyleCombo_->count() == 0) {
        textStyleCombo_->clear();
        if (d)
            for (const TextStyle& st : d->document().textStyles())
                textStyleCombo_->addItem(QString::fromStdString(st.name));
        textFontCombo_->clear();
        for (const QString& family : QFontDatabase::families())
            textFontCombo_->addItem(family);
        textColorCombo_->clear();
        textColorCombo_->addItem(swatch(0), tr("Dell'oggetto"), 0);
        for (int c = 1; c <= 7; ++c)
            textColorCombo_->addItem(swatch(c), theme::aciName(c), c);
        textColorCombo_->addItem(tr("Altri colori..."), -2);
    }
    // Altezze: quelle comuni più quella del testo.
    textHeightCombo_->clear();
    QList<double> heights = {1.0, 1.5, 1.8, 2.0, 2.5, 3.0, 3.5, 5.0, 7.0, 10.0};
    if (s.height > 0.0 && !heights.contains(s.height))
        heights << s.height;
    std::sort(heights.begin(), heights.end());
    for (double h : heights)
        textHeightCombo_->addItem(QString::number(h, 'g', 6), h);
    textHeightCombo_->addItem(tr("Altra altezza..."), 0.0);
    textHeightCombo_->setCurrentIndex(std::max(0, textHeightCombo_->findData(s.height)));

    textStyleCombo_->setEnabled(s.entity);
    textStyleCombo_->setCurrentIndex(std::max(0, textStyleCombo_->findText(s.style)));
    textFontCombo_->setCurrentIndex(std::max(0, textFontCombo_->findText(s.family)));
    textColorCombo_->setCurrentIndex(std::max(0, textColorCombo_->findData(s.rgb >= 0 ? -3 : s.aci)));
    textAnnotativeAct_->setEnabled(s.entity);
    textAnnotativeAct_->setChecked(s.annotative);
    textMaskAct_->setEnabled(s.entity);
    textMaskAct_->setChecked(s.mask);
    textBoldAct_->setChecked(s.bold);
    textItalicAct_->setChecked(s.italic);
    textUnderlineAct_->setChecked(s.underline);
    textOverlineAct_->setChecked(s.overline);
    textStrikeAct_->setChecked(s.strike);
    const int align = s.align >= 0 ? s.align : (s.attachment - 1) % 3;
    for (int i = 0; i < 5; ++i)
        textAlignActs_[i]->setChecked(i == align);
    for (QAction* a : textJustifyMenu_->actions())
        a->setChecked(a->data().toInt() == s.attachment);
    for (QAction* a : textSpacingMenu_->actions())
        if (a->data().isValid())
            a->setChecked(std::abs(a->data().toDouble() - s.lineSpacing) < 1e-9);
    textRulerAct_->setChecked(s.ruler);
    textSpellAct_->setChecked(s.spell);
    textSpellAct_->setEnabled(s.spellAvailable);
    textRulerAct_->setEnabled(s.entity);
    updatingTextTab_ = false;
}

} // namespace cartesio
