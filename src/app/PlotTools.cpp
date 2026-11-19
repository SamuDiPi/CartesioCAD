// Anteprima di stampa, editor delle tabelle stili (.ctb) e timbro di stampa, in QML.
#include "app/PlotTools.h"

#include "app/DrawingWidget.h"
#include "app/FormDialog.h"
#include "app/PlotDialog.h"
#include "app/PlotEngine.h"
#include "app/QmlUi.h"
#include "app/Theme.h"
#include "app/UiControls.h"

#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QVBoxLayout>

#include <cmath>

namespace cartesio {

namespace {

QString mm(double v)
{
    return QString::number(v, 'f', v == std::round(v) ? 0 : 2);
}

// L'immagine dell'anteprima per il QML ("image://preview/foglio").
class PreviewProvider : public QQuickImageProvider {
public:
    explicit PreviewProvider(QImage image) : QQuickImageProvider(QQuickImageProvider::Image), image_(std::move(image)) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override
    {
        if (size)
            *size = image_.size();
        return image_;
    }

private:
    QImage image_;
};

QQuickWidget* hostQml(QDialog* dialog, const char* contextName, QObject* model, const QString& qml)
{
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    QQuickWidget* view = makeQuickWidget(dialog, theme::kWindow);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->rootContext()->setContextProperty(QString::fromLatin1(contextName), model);
    layout->addWidget(view);
    Q_UNUSED(qml);
    return view;
}

} // namespace

// ================================================================== anteprima

PreviewDialog::PreviewDialog(QWidget* parent, const QImage& image, const QString& title)
    : QDialog(parent), image_(image), source_(QStringLiteral("image://preview/foglio"))
{
    setWindowTitle(title);
    QQuickWidget* view = hostQml(this, "preview", this, {});
    view->engine()->addImageProvider(QStringLiteral("preview"), new PreviewProvider(image_));
    view->setSource(QUrl(QStringLiteral("qrc:/qml/PlotPreview.qml")));
    resize(1100, 800);
}

PreviewDialog::~PreviewDialog() = default;

bool showPlotPreview(QWidget* parent, DrawingWidget& drawing, const PlotJob& job)
{
    QString error;
    const SheetSize sheet = sheetSize(job.settings);
    // Circa 150 dpi, senza superare i 30 milioni di pixel.
    double dpmm = 150.0 / 25.4;
    const double pixels = sheet.width * sheet.height * dpmm * dpmm;
    if (pixels > 3e7)
        dpmm *= std::sqrt(3e7 / pixels);
    const QImage image = renderPlotPreview(drawing, job, dpmm, &error);
    if (image.isNull()) {
        QMessageBox::warning(parent, QObject::tr("Anteprima di stampa"), error);
        return false;
    }
    PreviewDialog dialog(parent, image,
                         QObject::tr("Anteprima di stampa - %1 x %2 mm").arg(mm(sheet.width), mm(sheet.height)));
    return dialog.exec() == QDialog::Accepted;
}

// ================================================================== editor stili

StyleEditor::StyleEditor(QWidget* parent, PlotStyleTable table, const QString& name)
    : QDialog(parent), table_(std::move(table)), name_(name)
{
    setWindowTitle(tr("Editor tabella stili di stampa - %1").arg(name));
    description_ = new UiText(QString::fromStdString(table_.description), {}, this);
    list_ = new UiSelectList({}, this);
    for (int aci = 1; aci <= 255; ++aci)
        list_->addItem(tr("Colore %1").arg(aci), theme::aciColor(aci), aci);
    color_ = new UiCombo({}, this);
    color_->addItem(tr("Usa colore oggetto"), -1);
    color_->addItem(tr("Nero"), 0x000000);
    color_->addItem(tr("Rosso"), 0xFF0000);
    color_->addItem(tr("Giallo"), 0xFFFF00);
    color_->addItem(tr("Verde"), 0x00FF00);
    color_->addItem(tr("Ciano"), 0x00FFFF);
    color_->addItem(tr("Blu"), 0x0000FF);
    color_->addItem(tr("Magenta"), 0xFF00FF);
    color_->addItem(tr("Seleziona colore..."), -2);
    dither_ = new UiCheck(tr("Retinatura (dithering)"), this);
    gray_ = new UiCheck(tr("Scala di grigi"), this);
    pen_ = new UiSpin(0, 32, 0, {}, this);
    pen_->setToolTip(tr("Solo per i plotter a penne (0 = automatica): salvata nel file, non usata da PDF e stampanti"));
    virtualPen_ = new UiSpin(0, 255, 0, {}, this);
    virtualPen_->setToolTip(tr("0 = automatica"));
    screen_ = new UiSpin(0, 100, 0, QStringLiteral(" %"), this);
    linetype_ = new UiCombo({}, this);
    linetype_->addItem(tr("Usa tipo di linea oggetto"));
    linetype_->setToolTip(tr("I tipi di linea delle tabelle stili non sono supportati: si stampa quello dell'oggetto"));
    linetype_->setEnabled(false);
    adaptive_ = new UiCheck(tr("Regolazione adattiva"), this);
    lineweight_ = new UiCombo({}, this);
    lineweight_->addItem(tr("Usa spessore oggetto"), 0);
    for (std::size_t i = 1; i < table_.lineweights.size(); ++i)
        lineweight_->addItem(QStringLiteral("%1 mm").arg(table_.lineweights[i], 0, 'f', 2), static_cast<int>(i));
    end_ = new UiCombo({}, this);
    end_->addItem(tr("Piatta"), 0);
    end_->addItem(tr("Quadrata"), 1);
    end_->addItem(tr("Arrotondata"), 2);
    end_->addItem(tr("Rombo"), 3);
    end_->addItem(tr("Usa stile estremità oggetto"), 4);
    join_ = new UiCombo({}, this);
    join_->addItem(tr("Spigolo vivo"), 0);
    join_->addItem(tr("Smussata"), 1);
    join_->addItem(tr("Arrotondata"), 2);
    join_->addItem(tr("Rombo"), 3);
    join_->addItem(tr("Usa stile giunzione oggetto"), 5);
    fill_ = new UiCombo({}, this);
    fill_->addItem(tr("Usa stile riempimento oggetto"), 73);
    fill_->addItem(tr("Pieno"), 64);
    fill_->setToolTip(tr("I motivi di riempimento delle tabelle stili non sono supportati"));
    styleDescription_ = new UiText({}, {}, this);
    save_ = new UiButton(tr("Salva e chiudi"), this);
    saveAs_ = new UiButton(tr("Salva con nome..."), this);
    cancel_ = new UiButton(tr("Annulla"), this);

    const auto edit = [this] {
        if (!loading_)
            storeStyle();
    };
    connect(list_, &UiSelectList::selectionChanged, this, [this] { showStyle(); });
    connect(color_, &UiCombo::activated, this, [this] {
        if (color_->currentData().toInt() == -2) {
            const QColor c = QColorDialog::getColor(Qt::black, this, tr("Colore di stampa"));
            if (c.isValid()) {
                const int rgb = static_cast<int>(c.rgb() & 0xFFFFFF);
                int i = color_->findData(rgb);
                if (i < 0) {
                    color_->insertItem(color_->count() - 1, QStringLiteral("RGB %1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue()), rgb);
                    i = color_->count() - 2;
                }
                color_->setCurrentIndex(i);
            } else {
                color_->setCurrentIndex(0);
            }
        }
        storeStyle();
    });
    for (UiCheck* c : {dither_, gray_, adaptive_})
        connect(c, &UiCheck::toggled, this, edit);
    for (UiSpin* s : {pen_, virtualPen_, screen_})
        connect(s, &UiSpin::valueChanged, this, edit);
    for (UiCombo* c : {lineweight_, end_, join_, fill_})
        connect(c, &UiCombo::currentIndexChanged, this, edit);
    connect(styleDescription_, &UiText::textEdited, this, edit);
    connect(save_, &UiButton::clicked, this, [this] {
        if (saveTo(name_))
            accept();
    });
    connect(saveAs_, &UiButton::clicked, this, [this] {
        QString n = name_;
        FormDialog ask(tr("Salva tabella stili"), FormDialog::Ok | FormDialog::Cancel, this);
        ask.text(tr("Nome del file:"), n);
        ask.setSize(420, 150);
        if (ask.exec() != QDialog::Accepted || n.trimmed().isEmpty())
            return;
        n = n.trimmed();
        if (!n.endsWith(QLatin1String(".ctb"), Qt::CaseInsensitive))
            n += QStringLiteral(".ctb");
        if (saveTo(n))
            accept();
    });
    connect(cancel_, &UiButton::clicked, this, &QDialog::reject);

    QQuickWidget* view = hostQml(this, "editor", this, {});
    const std::pair<const char*, QObject*> controls[] = {
        {"description", description_}, {"styles", list_}, {"styleColor", color_}, {"dither", dither_},
        {"gray", gray_}, {"pen", pen_}, {"virtualPen", virtualPen_}, {"screen", screen_}, {"linetype", linetype_},
        {"adaptive", adaptive_}, {"lineweight", lineweight_}, {"endStyle", end_}, {"joinStyle", join_},
        {"fillStyle", fill_}, {"styleDescription", styleDescription_}, {"saveButton", save_},
        {"saveAsButton", saveAs_}, {"cancelButton", cancel_}};
    for (const auto& [name, object] : controls)
        view->rootContext()->setContextProperty(QString::fromLatin1(name), object);
    view->setSource(QUrl(QStringLiteral("qrc:/qml/StyleEditor.qml")));
    list_->setCurrentRow(0);
    resize(760, 600);
}

void StyleEditor::showStyle()
{
    const auto rows = list_->selectedRows();
    if (rows.empty())
        return;
    const PlotStyle& s = table_.forAci(list_->itemData(rows.front()).toInt());
    loading_ = true;
    if (s.hasObjectColor()) {
        color_->setCurrentIndex(0);
    } else {
        const int rgb = static_cast<int>(s.rgb());
        int i = color_->findData(rgb);
        if (i < 0) {
            const QColor c = QColor::fromRgb(static_cast<QRgb>(rgb));
            color_->insertItem(color_->count() - 1, QStringLiteral("RGB %1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue()), rgb);
            i = color_->count() - 2;
        }
        color_->setCurrentIndex(i);
    }
    dither_->setChecked(s.dithering());
    gray_->setChecked(s.grayscale());
    pen_->setValue(s.physicalPen);
    virtualPen_->setValue(s.virtualPen);
    screen_->setValue(s.screen);
    adaptive_->setChecked(s.adaptiveLinetype);
    lineweight_->setCurrentIndex(std::max(0, lineweight_->findData(s.lineweight)));
    end_->setCurrentIndex(std::max(0, end_->findData(s.endStyle)));
    join_->setCurrentIndex(std::max(0, join_->findData(s.joinStyle)));
    fill_->setCurrentIndex(std::max(0, fill_->findData(s.fillStyle)));
    styleDescription_->setText(QString::fromStdString(s.description));
    loading_ = false;
}

// Le modifiche valgono per tutti i colori selezionati.
void StyleEditor::storeStyle()
{
    for (int row : list_->selectedRows()) {
        PlotStyle& s = table_.forAci(list_->itemData(row).toInt());
        const int color = color_->currentData().toInt();
        if (color < 0)
            s.setObjectColor();
        else
            s.setRgb(static_cast<std::uint32_t>(color));
        s.setDithering(dither_->isChecked());
        s.setGrayscale(gray_->isChecked());
        s.physicalPen = static_cast<int>(pen_->value());
        s.virtualPen = static_cast<int>(virtualPen_->value());
        s.screen = static_cast<int>(screen_->value());
        s.adaptiveLinetype = adaptive_->isChecked();
        s.lineweight = lineweight_->currentData().toInt();
        s.endStyle = end_->currentData().toInt();
        s.joinStyle = join_->currentData().toInt();
        s.fillStyle = fill_->currentData().toInt();
        s.description = styleDescription_->text().toStdString();
    }
}

bool StyleEditor::saveTo(const QString& name)
{
    table_.description = description_->text().toStdString();
    QString error;
    const QString path = plotStylesFolder() + QLatin1Char('/') + name;
    if (!savePlotStyleTable(table_, path, &error)) {
        QMessageBox::critical(this, windowTitle(), error);
        return false;
    }
    saved_ = name;
    return true;
}

QString editPlotStyleTable(QWidget* parent, const QString& name)
{
    QString n = name;
    std::shared_ptr<const PlotStyleTable> table;
    QString error;
    if (n.isEmpty()) {
        // Nuova tabella: nome e punto di partenza (una tabella esistente o un file .ctb da importare).
        n = QObject::tr("Nuova.ctb");
        QString base = QStringLiteral("Colori.ctb");
        QList<QPair<QString, QVariant>> bases{{QObject::tr("Importa un file .ctb..."), QStringLiteral("*import*")}};
        for (const QString& b : plotStyleTableNames())
            bases.append({b, b});
        FormDialog ask(QObject::tr("Nuova tabella stili di stampa"), FormDialog::Ok | FormDialog::Cancel, parent);
        ask.text(QObject::tr("Nome del file:"), n);
        ask.choice(QObject::tr("Partire da:"), bases, base, [&base](const QVariant& v) { base = v.toString(); });
        ask.setSize(460, 190);
        if (ask.exec() != QDialog::Accepted || n.trimmed().isEmpty())
            return {};
        n = n.trimmed();
        if (!n.endsWith(QLatin1String(".ctb"), Qt::CaseInsensitive))
            n += QStringLiteral(".ctb");
        if (base == QLatin1String("*import*")) {
            const QString path = QFileDialog::getOpenFileName(parent, QObject::tr("Importa tabella stili"), QString(),
                                                              QObject::tr("Tabelle stili di stampa (*.ctb)"));
            if (path.isEmpty())
                return {};
            if (auto imported = readCtbFile(path, &error))
                table = std::make_shared<const PlotStyleTable>(std::move(*imported));
        } else {
            table = loadPlotStyleTable(base, &error);
        }
    } else {
        table = loadPlotStyleTable(n, &error);
    }
    if (!table) {
        QMessageBox::critical(parent, QObject::tr("Tabella stili di stampa"), error);
        return {};
    }
    StyleEditor editor(parent, *table, n);
    if (editor.exec() != QDialog::Accepted)
        return {};
    return editor.savedName();
}

// ================================================================== timbro

bool editPlotStamp(QWidget* parent, PlotStampSettings& settings)
{
    PlotStampSettings s = settings;
    QString user1 = QString::fromStdString(s.user1), user2 = QString::fromStdString(s.user2);
    FormDialog dialog(QObject::tr("Timbro di stampa"), FormDialog::Ok | FormDialog::Cancel, parent);
    dialog.group(QObject::tr("Campi del timbro di stampa"));
    dialog.check(QObject::tr("Nome del disegno"), s.drawingName);
    dialog.check(QObject::tr("Nome del layout"), s.layoutName);
    dialog.check(QObject::tr("Data e ora"), s.dateTime);
    dialog.check(QObject::tr("Nome di accesso"), s.loginName);
    dialog.check(QObject::tr("Nome del dispositivo"), s.deviceName);
    dialog.check(QObject::tr("Formato carta"), s.paperSize);
    dialog.check(QObject::tr("Scala di stampa"), s.plotScale);
    dialog.group(QObject::tr("Campi definiti dall'utente"));
    dialog.text(QObject::tr("Campo 1:"), user1);
    dialog.text(QObject::tr("Campo 2:"), user2);
    dialog.group(QObject::tr("Posizione e testo"));
    dialog.choice(QObject::tr("Posizione:"),
                  {{QObject::tr("In basso a sinistra"), 0},
                   {QObject::tr("In alto a sinistra"), 1},
                   {QObject::tr("In alto a destra"), 2},
                   {QObject::tr("In basso a destra"), 3}},
                  std::clamp(s.corner, 0, 3), [&s](const QVariant& v) { s.corner = v.toInt(); });
    dialog.number(QObject::tr("Altezza testo:"), 0.5, 20, 2, s.textHeight, QStringLiteral(" mm"));
    dialog.number(QObject::tr("Distanza X dal bordo:"), 0, 200, 2, s.offsetX, QStringLiteral(" mm"));
    dialog.number(QObject::tr("Distanza Y dal bordo:"), 0, 200, 2, s.offsetY, QStringLiteral(" mm"));
    dialog.setSize(520, 620);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    s.user1 = user1.toStdString();
    s.user2 = user2.toStdString();
    settings = s;
    return true;
}

} // namespace cartesio
