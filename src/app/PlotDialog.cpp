// Dialogo Stampa / Imposta pagina, anteprima, editor delle tabelle stili,
// timbro di stampa e gestore delle impostazioni di pagina.
#include "app/PlotDialog.h"

#include "app/AppOptions.h"
#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/QmlUi.h"
#include "app/Theme.h"
#include "app/UiControls.h"
#include "core/io/Dxf.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickWidget>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace cartesio {

namespace {

constexpr int kPaperCustomCurrent = -2;   // misure del foglio attuale, non in elenco
constexpr int kPaperCustomNew = -3;       // "Personalizzato..."

QString mm(double v)
{
    return QString::number(v, 'f', v == std::round(v) ? 0 : 2);
}

QSettings store()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

} // namespace

// ====================================================================== dialogo Stampa

PlotDialog::PlotDialog(DrawingWidget& drawing, int space, bool pageSetupMode, QWidget* parent)
    : QDialog(parent), drawing_(drawing), space_(space), pageSetupMode_(pageSetupMode)
{
    QString where = tr("Modello");
    if (const Layout* l = drawing.document().findLayout(space))
        where = QString::fromStdString(l->name);
    setWindowTitle(pageSetupMode ? tr("Imposta pagina - %1").arg(where) : tr("Stampa - %1").arg(where));
    buildUi();
    PlotDialogState initial;
    initial.job = plotJobFor(drawing, space);
    setState(initial);
    setExpanded(store().value(QStringLiteral("Plot/expanded"), false).toBool(), false);
}

void PlotDialog::buildUi()
{
    // Controlli (mostrati da qml/PlotDialog.qml, disposti).
    setupCombo_ = new UiCombo({}, this);
    addSetupButton_ = new UiButton(tr("Aggiungi..."), this);
    addSetupButton_->setToolTip(tr("Salva queste impostazioni come impostazione di pagina con nome"));

    deviceCombo_ = new UiCombo({}, this);
    propertiesButton_ = new UiButton(tr("Proprietà..."), this);
    deviceInfo_ = new UiLabel({}, this);
    toFileCheck_ = new UiCheck(tr("Stampa su file"), this);
    toFileCheck_->setVisible(!pageSetupMode_);
    sheet_ = new UiSheet(this);
    sheetLabel_ = new UiLabel({}, this);

    paperCombo_ = new UiCombo({}, this);
    copiesSpin_ = new UiSpin(1, 999, 0, {}, this);

    whatCombo_ = new UiCombo({}, this);
    if (space_ != 0)
        whatCombo_->addItem(tr("Layout"), static_cast<int>(PlotType::Layout));
    whatCombo_->addItem(tr("Estensioni"), static_cast<int>(PlotType::Extents));
    if (space_ == 0)
        whatCombo_->addItem(tr("Limiti"), static_cast<int>(PlotType::Limits));
    whatCombo_->addItem(tr("Schermo"), static_cast<int>(PlotType::Display));
    whatCombo_->addItem(tr("Vista"), static_cast<int>(PlotType::View));
    whatCombo_->addItem(tr("Finestra"), static_cast<int>(PlotType::Window));
    // Il disegno non ha viste con nome: "Vista" non si può scegliere.
    const int viewIndex = whatCombo_->findData(static_cast<int>(PlotType::View));
    whatCombo_->setItemEnabled(viewIndex, false);
    whatCombo_->setItemToolTip(viewIndex, tr("Il disegno non ha viste con nome"));
    windowButton_ = new UiButton(tr("Finestra<"), this);
    windowButton_->setToolTip(tr("Chiude il dialogo per scegliere i due angoli della finestra nel disegno"));

    offsetX_ = new UiSpin(-10000, 10000, 2, QStringLiteral(" mm"), this);
    offsetY_ = new UiSpin(-10000, 10000, 2, QStringLiteral(" mm"), this);
    centerCheck_ = new UiCheck(tr("Centra stampa"), this);

    fitCheck_ = new UiCheck(tr("Adatta al foglio"), this);
    scaleCombo_ = new UiCombo({}, this);
    scalePaper_ = new UiSpin(0.000001, 1e9, 4, {}, this);
    unitsCombo_ = new UiCombo({}, this);
    unitsCombo_->addItem(tr("pollici"), 0);
    unitsCombo_->addItem(tr("mm"), 1);
    scaleDrawing_ = new UiSpin(0.000001, 1e9, 4, {}, this);
    lwScaleCheck_ = new UiCheck(tr("Scala spessori di linea"), this);

    styleCombo_ = new UiCombo({}, this);
    styleEdit_ = new UiButton(tr("Modifica..."), this);
    styleEdit_->setToolTip(tr("Modifica la tabella stili di stampa"));
    showStylesCheck_ = new UiCheck(tr("Mostra stili di stampa"), this);
    showStylesCheck_->setToolTip(tr("Salvato nel disegno; CartesioCAD usa gli stili solo in stampa e in anteprima"));
    showStylesCheck_->setVisible(pageSetupMode_);

    shadeCombo_ = new UiCombo({}, this);
    shadeCombo_->addItems({tr("Come visualizzato"), tr("Wireframe"), tr("Nascosto"), tr("Render")});
    shadeCombo_->setEnabled(false);
    shadeCombo_->setToolTip(tr("In un disegno 2D si stampa sempre come visualizzato"));
    qualityCombo_ = new UiCombo({}, this);
    qualityCombo_->addItems({tr("Bozza"), tr("Anteprima"), tr("Normale"), tr("Presentazione"), tr("Massima"),
                             tr("Personalizzata")});
    qualityCombo_->setToolTip(tr("Nel PDF fissa la risoluzione delle coordinate (il disegno resta vettoriale)"));
    dpiSpin_ = new UiSpin(72, 4800, 0, {}, this);

    backgroundCheck_ = new UiCheck(tr("Stampa in background"), this);
    backgroundCheck_->setEnabled(false);
    backgroundCheck_->setToolTip(tr("Non serve: la stampa di un disegno 2D dura pochi istanti"));
    lineweightsCheck_ = new UiCheck(tr("Stampa spessori di linea oggetti"), this);
    transparencyCheck_ = new UiCheck(tr("Stampa trasparenza"), this);
    transparencyCheck_->setToolTip(tr("Salvata nelle impostazioni del programma (il DXF 2000 non la prevede)"));
    stylesCheck_ = new UiCheck(tr("Stampa con stili di stampa"), this);
    paperLastCheck_ = new UiCheck(tr("Stampa spazio carta per ultimo"), this);
    hideCheck_ = new UiCheck(tr("Nascondi oggetti spazio carta"), this);
    hideCheck_->setEnabled(false);
    hideCheck_->setToolTip(tr("Riguarda solo gli oggetti 3D: in un disegno 2D non ha effetto"));
    stampCheck_ = new UiCheck(tr("Timbro di stampa attivato"), this);
    stampButton_ = new UiButton(QStringLiteral("..."), this);
    stampButton_->setToolTip(tr("Impostazioni del timbro di stampa"));
    saveCheck_ = new UiCheck(space_ == 0 ? tr("Salva modifiche nel modello") : tr("Salva modifiche nel layout"), this);
    if (space_ == 0) {
        paperLastCheck_->setEnabled(false);
        paperLastCheck_->setToolTip(tr("Vale solo per i layout"));
    }
    if (pageSetupMode_) {
        backgroundCheck_->hide();
        stampCheck_->hide();
        stampButton_->hide();
        saveCheck_->hide();
    }

    portraitRadio_ = new UiCheck(tr("Verticale"), this);
    landscapeRadio_ = new UiCheck(tr("Orizzontale"), this);
    UiCheck::makeGroup({portraitRadio_, landscapeRadio_});
    upsideDownCheck_ = new UiCheck(tr("Stampa capovolta"), this);

    previewButton_ = new UiButton(tr("Anteprima..."), this);
    applyButton_ = new UiButton(space_ == 0 ? tr("Applica al modello") : tr("Applica al layout"), this);
    applyButton_->setToolTip(tr("Salva queste impostazioni nel disegno senza stampare"));
    applyButton_->setVisible(!pageSetupMode_);

    fillDevices();
    fillScales();

    // --- Segnali
    const auto changed = [this] {
        if (!loading_)
            updateUi();
    };
    connect(setupCombo_, &UiCombo::activated, this, [this] { onPageSetupChosen(); });
    connect(addSetupButton_, &UiButton::clicked, this, [this] { addPageSetup(); });
    connect(deviceCombo_, &UiCombo::currentIndexChanged, this, [this] {
        if (!loading_)
            onDeviceChanged();
    });
    connect(propertiesButton_, &UiButton::clicked, this, [this] { deviceProperties(); });
    connect(paperCombo_, &UiCombo::currentIndexChanged, this, [this] {
        if (!loading_)
            onPaperChanged();
    });
    connect(whatCombo_, &UiCombo::currentIndexChanged, this, changed);
    connect(windowButton_, &UiButton::clicked, this, [this] {
        outcome_ = Outcome::PickWindow;
        accept();
    });
    for (UiSpin* s : {offsetX_, offsetY_})
        connect(s, &UiSpin::valueChanged, this, changed);
    for (UiCheck* c : {centerCheck_, fitCheck_, lwScaleCheck_, lineweightsCheck_, transparencyCheck_, stylesCheck_,
                       paperLastCheck_, stampCheck_, upsideDownCheck_, portraitRadio_})
        connect(c, &UiCheck::toggled, this, changed);
    connect(scaleCombo_, &UiCombo::activated, this, [this] { onScaleChoice(); });
    for (UiSpin* s : {scalePaper_, scaleDrawing_})
        connect(s, &UiSpin::valueChanged, this, [this] {
            if (!loading_)
                onScaleEdited();
        });
    connect(unitsCombo_, &UiCombo::currentIndexChanged, this, changed);
    connect(styleCombo_, &UiCombo::activated, this, [this] {
        if (styleCombo_->currentData().toString() == QLatin1String("*new*")) {
            const QString name = editPlotStyleTable(this, QString());
            fillStyles(name);
        } else if (styleCombo_->currentData().toString() == QLatin1String("*manage*")) {
            const QString before = QString::fromStdString(base_.styleSheet);
            showPlotStyleManager(this);
            fillStyles(before);
        }
        if (!styleCombo_->currentData().toString().isEmpty() && !stylesCheck_->isChecked())
            stylesCheck_->setChecked(true);
        updateUi();
    });
    connect(styleEdit_, &UiButton::clicked, this, [this] { editStyles(); });
    connect(qualityCombo_, &UiCombo::currentIndexChanged, this, changed);
    connect(stampButton_, &UiButton::clicked, this, [this] { editStamp(); });
    connect(previewButton_, &UiButton::clicked, this, [this] { preview(); });
    connect(applyButton_, &UiButton::clicked, this, [this] { applyToLayout(); });

    // --- Vista QML
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = makeQuickWidget(this, QColor(0x3b, 0x44, 0x53));
    view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    QQmlContext* context = view_->rootContext();
    context->setContextProperty(QStringLiteral("plot"), this);
    const std::pair<const char*, QObject*> controls[] = {
        {"setupCombo", setupCombo_}, {"addSetupButton", addSetupButton_}, {"deviceCombo", deviceCombo_},
        {"propertiesButton", propertiesButton_}, {"deviceInfo", deviceInfo_}, {"toFileCheck", toFileCheck_},
        {"sheet", sheet_}, {"sheetLabel", sheetLabel_}, {"paperCombo", paperCombo_}, {"copiesSpin", copiesSpin_},
        {"whatCombo", whatCombo_}, {"windowButton", windowButton_}, {"offsetX", offsetX_}, {"offsetY", offsetY_},
        {"centerCheck", centerCheck_}, {"fitCheck", fitCheck_}, {"scaleCombo", scaleCombo_},
        {"scalePaper", scalePaper_}, {"unitsCombo", unitsCombo_}, {"scaleDrawing", scaleDrawing_},
        {"lwScaleCheck", lwScaleCheck_}, {"styleCombo", styleCombo_}, {"styleEdit", styleEdit_},
        {"showStylesCheck", showStylesCheck_}, {"shadeCombo", shadeCombo_}, {"qualityCombo", qualityCombo_},
        {"dpiSpin", dpiSpin_}, {"backgroundCheck", backgroundCheck_}, {"lineweightsCheck", lineweightsCheck_},
        {"transparencyCheck", transparencyCheck_}, {"stylesCheck", stylesCheck_}, {"paperLastCheck", paperLastCheck_},
        {"hideCheck", hideCheck_}, {"stampCheck", stampCheck_}, {"stampButton", stampButton_}, {"saveCheck", saveCheck_},
        {"portraitRadio", portraitRadio_}, {"landscapeRadio", landscapeRadio_}, {"upsideDownCheck", upsideDownCheck_},
        {"previewButton", previewButton_}, {"applyButton", applyButton_}};
    for (const auto& [name, object] : controls)
        context->setContextProperty(QString::fromLatin1(name), object);
    view_->setSource(QUrl(QStringLiteral("qrc:/qml/PlotDialog.qml")));
    layout->addWidget(view_);
}

void PlotDialog::plotClicked()
{
    if (!pageSetupMode_ && isNoDevice(deviceCombo_->currentData().toString())) {
        QMessageBox::information(this, windowTitle(), tr("Nessuna stampante scelta: scegliere il PDF o una stampante."));
        return;
    }
    outcome_ = Outcome::Plot;
    accept();
}

void PlotDialog::setExpanded(bool on, bool remember)
{
    expanded_ = on;
    emit expandedChanged();
    if (remember)
        store().setValue(QStringLiteral("Plot/expanded"), on);
    resize(on ? 1000 : 660, 620);
}

void PlotDialog::fillDevices()
{
    deviceCombo_->clear();
    deviceCombo_->addItem(tr("Nessuno"), QString::fromLatin1(kNoDevice));
    deviceCombo_->addItem(tr("PDF (CartesioCAD)"), QString::fromLatin1(kPdfDevice));
    if (!appOptions().hideSystemPrinters)
        for (const QString& name : QPrinterInfo::availablePrinterNames())
            deviceCombo_->addItem(name, name);
}

void PlotDialog::fillPapers()
{
    const bool wasLoading = loading_;
    loading_ = true;
    paperCombo_->clear();
    printerPapers_.clear();
    const QString device = deviceCombo_->currentData().toString();
    if (!isPdfDevice(device) && !isNoDevice(device)) {
        printerPapers_ = printerPapers(device);
        for (std::size_t i = 0; i < printerPapers_.size(); ++i) {
            const PrinterPaper& p = printerPapers_[i];
            paperCombo_->addItem(QStringLiteral("%1 (%2 x %3 mm)").arg(p.name, mm(p.width), mm(p.height)),
                                 static_cast<int>(i));
        }
    } else {
        const auto& formats = plotMediaFormats();
        for (std::size_t i = 0; i < formats.size(); ++i) {
            const MediaFormat& m = formats[i];
            const QString size = m.inches ? QStringLiteral("%1 x %2\"").arg(m.width / 25.4).arg(m.height / 25.4)
                                          : QStringLiteral("%1 x %2 mm").arg(mm(m.width), mm(m.height));
            paperCombo_->addItem(QStringLiteral("%1 (%2)").arg(QString::fromLatin1(m.name), size), static_cast<int>(i));
        }
        paperCombo_->addItem(tr("Personalizzato..."), kPaperCustomNew);
    }
    // Il foglio attuale.
    int index = -1;
    const double w = base_.paperWidth, h = base_.paperHeight;
    for (int i = 0; i < paperCombo_->count() && index < 0; ++i) {
        const int data = paperCombo_->itemData(i).toInt();
        if (data < 0)
            continue;
        double pw = 0, ph = 0;
        if (!printerPapers_.empty()) {
            pw = printerPapers_[static_cast<std::size_t>(data)].width;
            ph = printerPapers_[static_cast<std::size_t>(data)].height;
        } else {
            pw = plotMediaFormats()[static_cast<std::size_t>(data)].width;
            ph = plotMediaFormats()[static_cast<std::size_t>(data)].height;
        }
        if (std::abs(std::max(pw, ph) - std::max(w, h)) <= 0.5 && std::abs(std::min(pw, ph) - std::min(w, h)) <= 0.5)
            index = i;
    }
    if (index < 0) {
        paperCombo_->insertItem(0, tr("Attuale (%1 x %2 mm)").arg(mm(std::max(w, h)), mm(std::min(w, h))),
                                kPaperCustomCurrent);
        index = 0;
    }
    paperCombo_->setCurrentIndex(index);
    loading_ = wasLoading;
}

void PlotDialog::fillScales()
{
    scales_.clear();
    scales_.push_back({tr("Personalizzata"), -1, 0, 0});
    const auto add = [this](double paper, double drawing) {
        const int type = standardScaleTypeFor(paper, drawing);
        const QString label = paper == 1.0 ? QStringLiteral("1:%1").arg(drawing) : QStringLiteral("%1:1").arg(paper);
        scales_.push_back({label, type, paper, drawing});
    };
    for (double d : {1.0, 2.0, 4.0, 5.0, 8.0, 10.0, 16.0, 20.0, 25.0, 30.0, 40.0, 50.0, 100.0, 200.0, 250.0, 500.0,
                     1000.0, 2000.0, 5000.0})
        add(1.0, d);
    for (double p : {2.0, 4.0, 8.0, 10.0, 100.0, 1000.0})
        add(p, 1.0);
    for (const StandardScale& s : standardScales())
        if (s.type >= 1 && s.type <= 15)
            scales_.push_back({QString::fromUtf8(s.name), s.type, s.paper, s.drawing});
    scaleCombo_->clear();
    for (const ScaleChoice& c : scales_)
        scaleCombo_->addItem(c.label);
}

void PlotDialog::fillStyles(const QString& selectPath)
{
    // Un percorso completo (spesso di un altro PC, salvato da un altro programma): conta il nome del file.
    const QString select = selectPath.isEmpty() ? QString() : QFileInfo(QDir::fromNativeSeparators(selectPath)).fileName();
    styleCombo_->clear();
    styleCombo_->addItem(tr("Nessuna"), QString());
    for (const QString& name : plotStyleTableNames())
        styleCombo_->addItem(name, name);
    if (!select.isEmpty() && styleCombo_->findDataNoCase(select) < 0)
        styleCombo_->addItem(tr("%1 (non trovata)").arg(select), select);
    styleCombo_->addItem(tr("Nuova..."), QStringLiteral("*new*"));
    styleCombo_->addItem(tr("Gestione tabelle..."), QStringLiteral("*manage*"));
    const int i = select.isEmpty() ? 0 : styleCombo_->findDataNoCase(select);
    styleCombo_->setCurrentIndex(std::max(0, i));
}

void PlotDialog::fillPageSetups(const QString& select)
{
    setupCombo_->clear();
    setupCombo_->addItem(tr("<Nessuna>"), QString());
    for (const PlotSettings& s : drawing_.document().pageSetups())
        if (s.flag(kPlotModelType) == (space_ == 0))
            setupCombo_->addItem(QString::fromStdString(s.pageSetupName), QString::fromStdString(s.pageSetupName));
    const int i = select.isEmpty() ? 0 : setupCombo_->findDataNoCase(select);
    setupCombo_->setCurrentIndex(std::max(0, i));
}

void PlotDialog::setState(const PlotDialogState& state)
{
    stamp_ = state.job.stampSettings;
    loading_ = true;
    copiesSpin_->setValue(state.job.copies);
    transparencyCheck_->setChecked(state.job.transparency);
    stampCheck_->setChecked(state.job.stamp);
    saveCheck_->setChecked(state.saveChanges);
    toFileCheck_->setChecked(state.plotToFile);
    fillPageSetups(state.pageSetup);
    loading_ = false;
    loadSettings(state.job.settings);
}

void PlotDialog::loadSettings(const PlotSettings& s)
{
    loading_ = true;
    base_ = s;
    const QString device = QString::fromStdString(s.device);
    int deviceIndex = deviceCombo_->findData(device);
    // Stampanti nascoste nelle Opzioni: quella del disegno resta comunque disponibile.
    if (deviceIndex < 0 && QPrinterInfo::availablePrinterNames().contains(device)) {
        deviceCombo_->addItem(device, device);
        deviceIndex = deviceCombo_->count() - 1;
    }
    QString note;
    if (deviceIndex < 0) {
        if (isNoDevice(device))
            deviceIndex = 0;
        else {
            deviceIndex = 1;   // PDF
            if (!isPdfDevice(device))
                note = tr("La stampante \"%1\" del disegno non è disponibile: si usa il PDF.").arg(device);
        }
    }
    deviceCombo_->setCurrentIndex(deviceIndex);
    deviceInfo_->setProperty("note", note);
    fillPapers();

    landscapeRadio_->setChecked(isLandscape(s));
    portraitRadio_->setChecked(!isLandscape(s));
    upsideDownCheck_->setChecked(isUpsideDown(s));
    whatCombo_->setCurrentIndex(std::max(0, whatCombo_->findData(static_cast<int>(s.plotType))));
    offsetX_->setValue(s.plotOrigin.x);
    offsetY_->setValue(s.plotOrigin.y);
    centerCheck_->setChecked(s.flag(kPlotCentered));

    fitCheck_->setChecked(isFitToPaper(s));
    unitsCombo_->setCurrentIndex(s.paperUnits == 0 ? 0 : 1);
    int choice = 0;
    double paper = s.customPaper, drawing = s.customDrawing;
    if (s.flag(kPlotUseStandardScale) && s.standardScaleType > 0) {
        if (const StandardScale* st = findStandardScale(s.standardScaleType)) {
            paper = st->paper;
            drawing = st->drawing;
        }
        for (std::size_t i = 1; i < scales_.size(); ++i)
            if (scales_[i].standardType == s.standardScaleType)
                choice = static_cast<int>(i);
    } else if (!s.flag(kPlotUseStandardScale) && paper > 0 && drawing > 0) {
        // Scala personalizzata: la voce metrica con lo stesso rapporto, se c'è.
        for (std::size_t i = 1; i < scales_.size(); ++i) {
            const bool imperial = scales_[i].standardType >= 1 && scales_[i].standardType <= 15;
            if (!imperial && std::abs(scales_[i].paper / scales_[i].drawing - paper / drawing) <= 1e-12 * (paper / drawing))
                choice = static_cast<int>(i);
        }
    }
    scaleCombo_->setCurrentIndex(choice);
    scalePaper_->setValue(paper > 0 ? paper : 1.0);
    scaleDrawing_->setValue(drawing > 0 ? drawing : 1.0);
    lwScaleCheck_->setChecked(s.flag(kPlotScaleLineweights));

    fillStyles(QString::fromStdString(s.styleSheet));
    if (showStylesCheck_)
        showStylesCheck_->setChecked(s.flag(kPlotShowPlotStyles));
    qualityCombo_->setCurrentIndex(std::clamp(s.shadeResolution, 0, 5));
    dpiSpin_->setValue(std::clamp(s.shadeDpi, 72, 4800));
    lineweightsCheck_->setChecked(s.flag(kPlotPrintLineweights));
    stylesCheck_->setChecked(s.flag(kPlotPlotStyles));
    paperLastCheck_->setChecked(s.flag(kPlotDrawViewportsFirst));
    hideCheck_->setChecked(s.flag(kPlotHidden));
    loading_ = false;
    updateUi();
}

PlotSettings PlotDialog::settings() const
{
    PlotSettings s = base_;
    s.device = deviceCombo_->currentData().toString().toStdString();
    const int paper = paperCombo_->currentData().toInt();
    if (paper >= 0 && !printerPapers_.empty()) {
        const PrinterPaper& p = printerPapers_[static_cast<std::size_t>(paper)];
        s.paperWidth = p.width;
        s.paperHeight = p.height;
        s.mediaName = p.key.toStdString();
        s.marginLeft = p.marginLeft;
        s.marginBottom = p.marginBottom;
        s.marginRight = p.marginRight;
        s.marginTop = p.marginTop;
    } else if (paper >= 0) {
        const MediaFormat& m = plotMediaFormats()[static_cast<std::size_t>(paper)];
        s.paperWidth = m.width;
        s.paperHeight = m.height;
        s.mediaName = canonicalMediaName(m);
    }
    setOrientation(s, landscapeRadio_->isChecked(), upsideDownCheck_->isChecked());
    s.plotType = static_cast<PlotType>(whatCombo_->currentData().toInt());
    s.plotOrigin = {offsetX_->value(), offsetY_->value()};
    s.setFlag(kPlotCentered, centerCheck_->isChecked());
    s.paperUnits = unitsCombo_->currentData().toInt();
    if (fitCheck_->isChecked()) {
        s.setFlag(kPlotUseStandardScale, true);
        s.standardScaleType = 0;
    } else {
        const int choice = scaleCombo_->currentIndex();
        const ScaleChoice* c = choice > 0 && choice < static_cast<int>(scales_.size())
                                   ? &scales_[static_cast<std::size_t>(choice)] : nullptr;
        s.customPaper = scalePaper_->value();
        s.customDrawing = scaleDrawing_->value();
        if (c && c->standardType > 0) {
            s.setFlag(kPlotUseStandardScale, true);
            s.standardScaleType = c->standardType;
        } else {
            s.setFlag(kPlotUseStandardScale, false);
        }
    }
    s.setFlag(kPlotScaleLineweights, lwScaleCheck_->isChecked());
    s.styleSheet = styleCombo_->currentData().toString().toStdString();
    if (s.styleSheet == "*new*" || s.styleSheet == "*manage*")
        s.styleSheet.clear();
    if (showStylesCheck_)
        s.setFlag(kPlotShowPlotStyles, showStylesCheck_->isChecked());
    s.shadeResolution = qualityCombo_->currentIndex();
    s.shadeDpi = dpiSpin_->value();
    s.setFlag(kPlotPrintLineweights, lineweightsCheck_->isChecked());
    s.setFlag(kPlotPlotStyles, stylesCheck_->isChecked());
    s.setFlag(kPlotDrawViewportsFirst, paperLastCheck_->isChecked());
    s.setFlag(kPlotModelType, space_ == 0);
    return s;
}

PlotJob PlotDialog::currentJob() const
{
    PlotJob job;
    job.settings = settings();
    job.space = space_;
    job.copies = copiesSpin_->value();
    job.transparency = transparencyCheck_->isChecked();
    job.stamp = !pageSetupMode_ && stampCheck_->isChecked();
    job.stampSettings = stamp_;
    return job;
}

PlotDialogState PlotDialog::state() const
{
    PlotDialogState st;
    st.job = currentJob();
    st.plotToFile = toFileCheck_->isChecked();
    st.saveChanges = saveCheck_->isChecked();
    st.pageSetup = setupCombo_->currentData().toString();
    return st;
}

void PlotDialog::updateUi()
{
    const PlotSettings s = settings();
    const QString device = deviceCombo_->currentData().toString();
    const bool pdf = isPdfDevice(device), none = isNoDevice(device);

    // Stampante.
    QString info;
    if (none)
        info = tr("Nessuna stampante: le impostazioni si possono salvare ma non stampare.");
    else if (pdf)
        info = tr("<b>Plotter:</b> PDF vettoriale di CartesioCAD<br><b>Dove:</b> file<br><b>Descrizione:</b> "
                  "un foglio per pagina, alla misura esatta");
    else {
        const QPrinterInfo pi = QPrinterInfo::printerInfo(device);
        info = tr("<b>Plotter:</b> %1<br><b>Dove:</b> %2<br><b>Descrizione:</b> %3")
                   .arg(pi.makeAndModel().toHtmlEscaped(), pi.location().toHtmlEscaped(), pi.description().toHtmlEscaped());
    }
    const QString note = deviceInfo_->property("note").toString();
    if (!note.isEmpty())
        info += QStringLiteral("<br><span style='color:#c03030'>%1</span>").arg(note.toHtmlEscaped());
    deviceInfo_->setText(info);
    toFileCheck_->setEnabled(!pdf && !none);
    if (pdf)
        toFileCheck_->setChecked(true);
    copiesSpin_->setEnabled(!pdf && !none);
    if (pdf)
        copiesSpin_->setValue(1);

    // Area e anteprima parziale.
    windowButton_->setVisible(s.plotType == PlotType::Window);
    offsetX_->setEnabled(!centerCheck_->isChecked());
    offsetY_->setEnabled(!centerCheck_->isChecked());
    QString error;
    const auto area = plotArea(drawing_, s, space_, &error);
    const SheetSize sheet = sheetSize(s);
    std::optional<BBox> placed;
    bool outside = false;
    double effective = 0.0;
    if (area) {
        const PlotFrame frame = computePlotFrame(s, *area);
        if (frame.valid) {
            placed = frame.placed;
            effective = frame.scale;
            // Un layout comprende i margini: si controlla che stia nel foglio.
            BBox pr = frame.printable;
            if (s.plotType == PlotType::Layout) {
                pr.min = {0.0, 0.0};
                pr.max = {frame.sheet.width, frame.sheet.height};
            }
            outside = frame.placed.min.x < pr.min.x - 1e-6 || frame.placed.min.y < pr.min.y - 1e-6
                      || frame.placed.max.x > pr.max.x + 1e-6 || frame.placed.max.y > pr.max.y + 1e-6;
        }
    }
    sheet_->setFrame(sheet, printableArea(s), placed, outside);
    QString label = QStringLiteral("%1 x %2 mm").arg(mm(sheet.width), mm(sheet.height));
    if (!area)
        label += QStringLiteral("\n") + error;
    else if (outside)
        label += tr("\nil disegno esce dall'area stampabile");
    sheetLabel_->setText(label);
    sheetLabel_->setError(!area || outside);

    // Scala: con "Adatta" si mostra quella risultante.
    const bool fit = fitCheck_->isChecked();
    scaleCombo_->setEnabled(!fit);
    scalePaper_->setEnabled(!fit);
    scaleDrawing_->setEnabled(!fit);
    unitsCombo_->setEnabled(!fit);
    if (fit) {
        loading_ = true;
        scaleCombo_->setCurrentIndex(0);
        const double unit = s.paperUnits == 0 ? 25.4 : 1.0;
        scalePaper_->setValue(1.0);
        scaleDrawing_->setValue(effective > 0 ? unit / effective : 1.0);
        loading_ = false;
    }

    // Opzioni.
    const bool styles = stylesCheck_->isChecked();
    lineweightsCheck_->setEnabled(!styles);
    if (styles)
        lineweightsCheck_->setChecked(true);
    styleEdit_->setEnabled(!styleCombo_->currentData().toString().isEmpty());
    // Le qualità predefinite hanno la loro risoluzione: il campo mostra quella usata.
    const int quality = qualityCombo_->currentIndex();
    dpiSpin_->setEnabled(quality == 5);
    if (quality != 5) {
        static const int dpi[] = {150, 300, 600, 1200, 2400};
        loading_ = true;
        dpiSpin_->setValue(dpi[std::clamp(quality, 0, 4)]);
        loading_ = false;
    }
    stampButton_->setEnabled(stampCheck_->isChecked());
}

void PlotDialog::onDeviceChanged()
{
    deviceInfo_->setProperty("note", QString());
    const QString device = deviceCombo_->currentData().toString();
    if (isPdfDevice(device) || isNoDevice(device)) {
        // Margini del PDF: quelli del disegno, oppure 10 mm se venivano da una stampante.
        if (base_.marginLeft == 0 && base_.marginTop == 0 && base_.marginRight == 0 && base_.marginBottom == 0)
            base_.marginLeft = base_.marginBottom = base_.marginRight = base_.marginTop = 10.0;
    }
    base_ = settings();
    fillPapers();
    if (!printerPapers_.empty())
        base_ = settings();   // margini della stampante
    updateUi();
}

void PlotDialog::onPaperChanged()
{
    if (paperCombo_->currentData().toInt() == kPaperCustomNew) {
        double width = std::max(base_.paperWidth, base_.paperHeight);
        double height = std::min(base_.paperWidth, base_.paperHeight);
        FormDialog dialog(tr("Formato personalizzato"), FormDialog::Ok | FormDialog::Cancel, this);
        dialog.number(tr("Larghezza:"), 10, 10000, 2, width, QStringLiteral(" mm"));
        dialog.number(tr("Altezza:"), 10, 10000, 2, height, QStringLiteral(" mm"));
        dialog.setSize(380, 170);
        if (dialog.exec() == QDialog::Accepted) {
            base_ = settings();
            base_.paperWidth = std::max(width, height);
            base_.paperHeight = std::min(width, height);
            base_.mediaName = QStringLiteral("Personalizzato_(%1_x_%2_MM)")
                                  .arg(base_.paperWidth, 0, 'f', 2).arg(base_.paperHeight, 0, 'f', 2).toStdString();
            base_.rotation = landscapeRadio_->isChecked() ? 0 : 1;
        }
        fillPapers();
    }
    updateUi();
}

void PlotDialog::onScaleChoice()
{
    const int choice = scaleCombo_->currentIndex();
    if (choice > 0 && choice < static_cast<int>(scales_.size())) {
        const ScaleChoice& c = scales_[static_cast<std::size_t>(choice)];
        loading_ = true;
        scalePaper_->setValue(c.paper);
        scaleDrawing_->setValue(c.drawing);
        if (c.standardType >= 1 && c.standardType <= 15)
            unitsCombo_->setCurrentIndex(0);   // scale in pollici
        loading_ = false;
    }
    updateUi();
}

void PlotDialog::onScaleEdited()
{
    // Valori scritti a mano: si sceglie la voce corrispondente, oppure "Personalizzata".
    const double ratio = scalePaper_->value() / scaleDrawing_->value();
    int choice = 0;
    for (std::size_t i = 1; i < scales_.size(); ++i)
        if (std::abs(scales_[i].paper / scales_[i].drawing - ratio) <= 1e-12 * ratio)
            choice = static_cast<int>(i);
    loading_ = true;
    scaleCombo_->setCurrentIndex(choice);
    loading_ = false;
    updateUi();
}

void PlotDialog::onPageSetupChosen()
{
    const QString name = setupCombo_->currentData().toString();
    if (name.isEmpty())
        return;
    if (const PlotSettings* s = drawing_.document().findPageSetup(name.toStdString())) {
        PlotSettings copy = *s;
        copy.pageSetupName = name.toStdString();
        loadSettings(copy);
    }
}

void PlotDialog::addPageSetup()
{
    QString name = tr("Impostazione%1").arg(drawing_.document().pageSetups().size() + 1);
    {
        FormDialog ask(tr("Aggiungi impostazione di pagina"), FormDialog::Ok | FormDialog::Cancel, this);
        ask.text(tr("Nome della nuova impostazione di pagina:"), name);
        ask.setSize(460, 150);
        if (ask.exec() != QDialog::Accepted || name.trimmed().isEmpty())
            return;
    }
    name = name.trimmed();
    PlotSettings s = settings();
    s.pageSetupName = name.toStdString();
    drawing_.document().setPageSetup(s);
    drawing_.notifyChanged();
    fillPageSetups(name);
}

void PlotDialog::editStyles()
{
    const QString name = styleCombo_->currentData().toString();
    if (name.isEmpty())
        return;
    const QString saved = editPlotStyleTable(this, name);
    if (!saved.isEmpty())
        fillStyles(saved);
    updateUi();
}

void PlotDialog::editStamp()
{
    editPlotStamp(this, stamp_);
}

void PlotDialog::deviceProperties()
{
    const QString device = deviceCombo_->currentData().toString();
    if (isNoDevice(device))
        return;
    if (isPdfDevice(device)) {
        // Come l'editor dei .pc3: l'area stampabile del formato.
        const PlotSettings s = settings();
        double values[4] = {s.marginTop, s.marginBottom, s.marginLeft, s.marginRight};
        const QString labels[4] = {tr("Superiore:"), tr("Inferiore:"), tr("Sinistro:"), tr("Destro:")};
        FormDialog dialog(tr("Proprietà PDF - area stampabile"), FormDialog::Ok | FormDialog::Cancel, this);
        dialog.group(tr("Margini non stampabili del foglio (nell'orientamento del formato)"));
        for (int i = 0; i < 4; ++i)
            dialog.number(labels[i], 0, 200, 2, values[i], QStringLiteral(" mm"));
        dialog.setSize(420, 260);
        if (dialog.exec() != QDialog::Accepted)
            return;
        base_ = settings();
        base_.marginTop = values[0];
        base_.marginBottom = values[1];
        base_.marginLeft = values[2];
        base_.marginRight = values[3];
        updateUi();
        return;
    }
    // Stampante: il dialogo di sistema; se ne riprendono formato, orientamento e copie.
    QPrinter printer(QPrinterInfo::printerInfo(device), QPrinter::HighResolution);
    printer.setCopyCount(copiesSpin_->value());
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Proprietà - %1").arg(device));
    if (dialog.exec() != QDialog::Accepted)
        return;
    copiesSpin_->setValue(std::max(1, printer.copyCount()));
    const QSizeF size = printer.pageLayout().pageSize().size(QPageSize::Millimeter);
    loading_ = true;
    for (int i = 0; i < paperCombo_->count(); ++i) {
        const int data = paperCombo_->itemData(i).toInt();
        if (data >= 0 && data < static_cast<int>(printerPapers_.size())
            && printerPapers_[static_cast<std::size_t>(data)].key == printer.pageLayout().pageSize().key())
            paperCombo_->setCurrentIndex(i);
    }
    const bool landscape = printer.pageLayout().orientation() == QPageLayout::Landscape;
    landscapeRadio_->setChecked(landscape);
    portraitRadio_->setChecked(!landscape);
    loading_ = false;
    Q_UNUSED(size);
    updateUi();
}

void PlotDialog::preview()
{
    if (showPlotPreview(this, drawing_, currentJob()) && !pageSetupMode_) {
        outcome_ = Outcome::Plot;
        accept();
    }
}

void PlotDialog::applyToLayout()
{
    Document& doc = drawing_.document();
    const PlotSettings s = settings();
    if (space_ == 0)
        doc.setModelPlot(s);
    else if (Layout* l = doc.findLayout(space_)) {
        applyPlotSettings(*l, s);
        doc.touch();
    }
    PlotPreferences prefs = PlotPreferences::load();
    prefs.transparency = transparencyCheck_->isChecked();
    prefs.stamp = stampCheck_->isChecked();
    prefs.stampSettings = stamp_;
    prefs.save();
    drawing_.notifyChanged();
}

// Anteprima, editor delle tabelle stili e timbro di stampa: PlotTools.cpp (in QML).

// ====================================================================== gestore impostazioni di pagina

void showPageSetupManager(QWidget* parent, DrawingWidget& drawing, int space)
{
    Document& doc = drawing.document();
    const Layout* layout = doc.findLayout(space);
    const QString current = layout ? QString::fromStdString(layout->name) : QObject::tr("Modello");
    FormDialog dialog(QObject::tr("Gestione impostazioni di pagina"), FormDialog::Close, parent);

    const QString currentKey = QStringLiteral("*current*");
    QStringList keys;
    int selected = 0;
    const auto settingsOf = [&](const QString& key) -> std::optional<PlotSettings> {
        if (key == currentKey) {
            if (space == 0)
                return doc.modelPlot();
            if (const Layout* l = doc.findLayout(space))
                return layoutPlotSettings(*l);
            return std::nullopt;
        }
        if (const PlotSettings* s = doc.findPageSetup(key.toStdString()))
            return *s;
        return std::nullopt;
    };
    const auto selectedKey = [&] { return selected >= 0 && selected < keys.size() ? keys[selected] : QString(); };

    dialog.group(QObject::tr("Impostazioni di pagina"));
    dialog.note(QObject::tr("Layout corrente: <b>%1</b>").arg(current.toHtmlEscaped()));
    int listField = -1, detailsField = -1;
    std::function<void()> showDetails;
    std::function<void(const QString&)> refill;
    std::function<void(const QString&)> edit;
    listField = dialog.list({}, 0, [&](int index) {
        selected = index;
        showDetails();
    }, [&](int index) {
        selected = index;
        edit(selectedKey());
    });

    showDetails = [&] {
        const auto s = settingsOf(selectedKey());
        if (!s) {
            dialog.setFieldText(detailsField, QString());
            return;
        }
        const SheetSize sheet = sheetSize(*s);
        dialog.setFieldText(detailsField,
                            QObject::tr("<b>Dispositivo:</b> %1<br><b>Formato:</b> %2 (%3 x %4 mm)<br><b>Area:</b> %5, "
                                        "<b>scala:</b> %6<br><b>Tabella stili:</b> %7")
                                .arg(deviceDisplayName(QString::fromStdString(s->device)).toHtmlEscaped(),
                                     QString::fromStdString(mediaDisplayName(sheet.width, sheet.height)))
                                .arg(sheet.width, 0, 'f', 0)
                                .arg(sheet.height, 0, 'f', 0)
                                .arg(QStringList{QObject::tr("Schermo"), QObject::tr("Estensioni"), QObject::tr("Limiti"),
                                                 QObject::tr("Vista"), QObject::tr("Finestra"),
                                                 QObject::tr("Layout")}[static_cast<int>(s->plotType)],
                                     QString::fromStdString(plotScaleText(*s)),
                                     s->styleSheet.empty() ? QObject::tr("nessuna") : QString::fromStdString(s->styleSheet)));
    };
    refill = [&](const QString& select) {
        keys.clear();
        QStringList names;
        keys << currentKey;
        names << QStringLiteral("*%1*").arg(current);
        for (const PlotSettings& s : doc.pageSetups())
            if (s.flag(kPlotModelType) == (space == 0)) {
                keys << QString::fromStdString(s.pageSetupName);
                names << QString::fromStdString(s.pageSetupName);
            }
        selected = std::max(0, static_cast<int>(keys.indexOf(select)));
        dialog.setItems(listField, names, selected);
        showDetails();
    };
    edit = [&](const QString& key) {
        const auto s = settingsOf(key);
        if (!s)
            return;
        PlotDialog editor(drawing, space, true, &dialog);
        PlotDialogState state;
        state.job = plotJobFor(drawing, space);
        state.job.settings = *s;
        editor.setState(state);
        if (key != currentKey)
            editor.setWindowTitle(QObject::tr("Imposta pagina - %1").arg(key));
        if (editor.exec() != QDialog::Accepted || editor.outcome() != PlotDialog::Outcome::Plot)
            return;
        PlotSettings result = editor.settings();
        if (key == currentKey) {
            if (space == 0)
                doc.setModelPlot(result);
            else if (Layout* l = doc.findLayout(space)) {
                applyPlotSettings(*l, result);
                doc.touch();
            }
        } else {
            result.pageSetupName = key.toStdString();
            doc.setPageSetup(result);
        }
        drawing.notifyChanged();
        showDetails();
    };
    const auto create = [&] {
        QString name = QObject::tr("Impostazione%1").arg(doc.pageSetups().size() + 1);
        {
            FormDialog ask(QObject::tr("Nuova impostazione di pagina"), FormDialog::Ok | FormDialog::Cancel, &dialog);
            ask.text(QObject::tr("Nome:"), name);
            ask.setSize(420, 150);
            if (ask.exec() != QDialog::Accepted || name.trimmed().isEmpty())
                return;
        }
        name = name.trimmed();
        auto base = settingsOf(currentKey);
        if (!base)
            return;
        base->pageSetupName = name.toStdString();
        base->setFlag(kPlotModelType, space == 0);
        doc.setPageSetup(*base);
        refill(name);
        edit(name);
        showDetails();
    };
    const auto setCurrent = [&] {
        const QString key = selectedKey();
        const auto s = settingsOf(key);
        if (!s || key == currentKey)
            return;
        PlotSettings applied = *s;
        applied.pageSetupName = key.toStdString();
        if (space == 0)
            doc.setModelPlot(applied);
        else if (Layout* l = doc.findLayout(space)) {
            applyPlotSettings(*l, applied);
            doc.touch();
        }
        drawing.notifyChanged();
        refill(currentKey);
    };
    const auto remove = [&] {
        const QString key = selectedKey();
        if (key.isEmpty() || key == currentKey)
            return;
        doc.removePageSetup(key.toStdString());
        drawing.notifyChanged();
        refill(currentKey);
    };
    const auto import = [&] {
        const QString path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Importa impostazioni di pagina"), QString(),
                                                          QObject::tr("Disegni (*.dxf *.dwg)"));
        if (path.isEmpty())
            return;
        Document source;
        QString error;
        if (!DrawingWidget::readFile(path, source, &error)) {
            QMessageBox::critical(&dialog, dialog.windowTitle(), error);
            return;
        }
        QList<QPair<QString, QVariant>> names;
        for (const PlotSettings& s : source.pageSetups())
            if (s.flag(kPlotModelType) == (space == 0))
                names.append({QString::fromStdString(s.pageSetupName), QString::fromStdString(s.pageSetupName)});
        if (names.isEmpty()) {
            QMessageBox::information(&dialog, dialog.windowTitle(),
                                     QObject::tr("Il disegno non ha impostazioni di pagina per %1.")
                                         .arg(space == 0 ? QObject::tr("il modello") : QObject::tr("i layout")));
            return;
        }
        QString name = names.front().first;
        FormDialog ask(dialog.windowTitle(), FormDialog::Ok | FormDialog::Cancel, &dialog);
        ask.choice(QObject::tr("Impostazione da importare:"), names, name, [&name](const QVariant& v) { name = v.toString(); });
        ask.setSize(460, 150);
        if (ask.exec() != QDialog::Accepted)
            return;
        doc.setPageSetup(*source.findPageSetup(name.toStdString()));
        drawing.notifyChanged();
        refill(name);
    };
    dialog.buttonsRow({{QObject::tr("Imposta corrente"), setCurrent},
                       {QObject::tr("Nuova..."), create},
                       {QObject::tr("Modifica..."), [&] { edit(selectedKey()); }},
                       {QObject::tr("Importa..."), import},
                       {QObject::tr("Elimina"), remove}});
    dialog.group(QObject::tr("Dettagli dell'impostazione selezionata"));
    detailsField = dialog.note(QString());
    refill(currentKey);
    dialog.setSize(580, 560);
    dialog.exec();
}

} // namespace cartesio
