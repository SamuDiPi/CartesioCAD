#include "app/PropertiesPalette.h"

#include "app/CadView.h"
#include "app/ColorDialog.h"
#include "app/DrawingWidget.h"
#include "app/AppOptions.h"
#include "app/PlotEngine.h"
#include "app/ImageLibrary.h"
#include "app/QmlUi.h"
#include "app/Theme.h"
#include "core/Document.h"

#include <QDir>
#include <QMenu>
#include <QQmlContext>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QTimer>

#include <cmath>
#include <map>
#include <set>

namespace cartesio {

namespace {

const QString kVaries = QStringLiteral("*VARIA*");

QString typeName(EntityType type)
{
    switch (type) {
    case EntityType::Line: return QObject::tr("Linea");
    case EntityType::Circle: return QObject::tr("Cerchio");
    case EntityType::Arc: return QObject::tr("Arco");
    case EntityType::Point: return QObject::tr("Punto");
    case EntityType::Polyline: return QObject::tr("Polilinea");
    case EntityType::Ellipse: return QObject::tr("Ellisse");
    case EntityType::Spline: return QObject::tr("Spline");
    case EntityType::Text: return QObject::tr("Testo");
    case EntityType::MText: return QObject::tr("Testo multilinea");
    case EntityType::Solid: return QObject::tr("Solido 2D");
    case EntityType::Hatch: return QObject::tr("Tratteggio");
    case EntityType::Insert: return QObject::tr("Riferimento di blocco");   // anche XREF
    case EntityType::Dimension: return QObject::tr("Quota");
    case EntityType::AttDef: return QObject::tr("Definizione di attributo");
    case EntityType::Viewport: return QObject::tr("Finestra");
    case EntityType::Leader: return QObject::tr("Direttrice");
    case EntityType::XLine: return QObject::tr("Linea di costruzione");
    case EntityType::Wipeout: return QObject::tr("Copertura");
    case EntityType::Image: return QObject::tr("Immagine raster");
    case EntityType::Mline: return QObject::tr("Multilinea");
    case EntityType::Table: return QObject::tr("Tabella");
    }
    return {};
}

QString fmt(double v, int decimals = 4)
{
    return QString::number(v, 'f', decimals);
}

QIcon swatch(int aci)
{
    QPixmap pm(14, 14);
    if (aci == kColorByLayer || aci == kColorByBlock)
        pm.fill(Qt::transparent);
    else
        pm.fill(theme::aciColor(aci));
    QPainter p(&pm);
    p.setPen(QColor(150, 158, 170));
    p.drawRect(0, 0, 13, 13);
    return QIcon(pm);
}

template <class T>
const T& as(const Entity& e)
{
    return static_cast<const T&>(e);
}

template <class T>
T& as(Entity& e)
{
    return static_cast<T&>(e);
}

// Trasparenza (codice 440): -1 DaLayer, 0x01000000 DaBlocco, 0x02000000 | alfa.
int transparencyCode(int percent)
{
    const int alpha = static_cast<int>(std::lround(255.0 * (1.0 - percent / 100.0)));
    return 0x02000000 | std::clamp(alpha, 0, 255);
}

std::vector<std::pair<QString, int>> transparencyOptions()
{
    std::vector<std::pair<QString, int>> out = {{QObject::tr("DaLayer"), -1}, {QObject::tr("DaBlocco"), 0x01000000}};
    for (int p = 0; p <= 90; p += 10)
        out.emplace_back(QString::number(p), transparencyCode(p));
    return out;
}

// Testo multilinea in una riga della tavolozza: senza codici, gli a capo come "\P".
std::string editableMText(const std::string& text)
{
    std::string plain = mtextPlainText(text), out;
    for (char c : plain) {
        if (c == '\n')
            out += "\\P";
        else
            out += c;
    }
    return out;
}

std::string fromEditableMText(const std::string& original, const std::string& edited)
{
    if (edited == editableMText(original))
        return original;   // niente di cambiato: la formattazione interna resta
    std::string plain;
    for (std::size_t i = 0; i < edited.size(); ++i) {
        if (edited[i] == '\\' && i + 1 < edited.size() && (edited[i + 1] == 'P' || edited[i + 1] == 'p')) {
            plain += '\n';
            ++i;
        } else {
            plain += edited[i];
        }
    }
    return mtextWithPlainText(original, plain);
}

std::vector<std::pair<QString, int>> lineweightOptions()
{
    std::vector<std::pair<QString, int>> out = {{QObject::tr("DaLayer"), kLineweightByLayer},
                                                {QObject::tr("DaBlocco"), kLineweightByBlock},
                                                {QObject::tr("Default"), kLineweightDefault}};
    for (int lw : standardLineweights())
        out.emplace_back(QStringLiteral("%1 mm").arg(lw / 100.0, 0, 'f', 2), lw);
    return out;
}

} // namespace

PropertiesPalette::PropertiesPalette(QWidget* parent, bool compact) : QQuickWidget(parent), compact_(compact)
{
    setupQuickEngine(this);
    setClearColor(theme::kWindow);
    rootContext()->setContextProperty(QStringLiteral("props"), this);
    if (!qmlRootUi() || compact_)   // nella scena unica la tavolozza la carica MainView.qml
        setSource(QUrl(QStringLiteral("qrc:/qml/PropertiesPalette.qml")));
    setMinimumWidth(240);
    rebuild();
}

void PropertiesPalette::setDrawing(DrawingWidget* drawing)
{
    drawing_ = drawing;
    filter_.reset();
    refresh();
}

// Ricostruzione differita: la tavolozza si aggiorna una volta sola anche dopo molte notifiche di fila, e mai
// dentro una chiamata che arriva dal QML stesso.
void PropertiesPalette::refresh()
{
    if (pending_)
        return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] {
        pending_ = false;
        rebuild();
    });
}

// --------------------------------------------------------------- dal QML

void PropertiesPalette::chooseSummary(int index)
{
    if (index < 0 || index >= static_cast<int>(summaryTypes_.size()))
        return;
    const int type = summaryTypes_[static_cast<std::size_t>(index)];
    filter_ = type < 0 ? std::nullopt : std::optional<EntityType>(static_cast<EntityType>(type));
    refresh();
}

void PropertiesPalette::popupSummary(int globalX, int globalY)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (int i = 0; i < summary_.size(); ++i) {
        QAction* a = menu->addAction(summary_[i]);
        a->setCheckable(true);
        a->setChecked(i == summaryIndex_);
        connect(a, &QAction::triggered, this, [this, i] { chooseSummary(i); });
    }
    menu->popup(QPoint(globalX, globalY));
}

void PropertiesPalette::toggleSection(const QString& title)
{
    if (collapsed_.contains(title))
        collapsed_.remove(title);
    else
        collapsed_.insert(title);
    refresh();
}

void PropertiesPalette::commit(int row, const QString& value)
{
    if (row >= 0 && row < static_cast<int>(actions_.size()) && actions_[static_cast<std::size_t>(row)].text)
        actions_[static_cast<std::size_t>(row)].text(value);
}

void PropertiesPalette::popupChoice(int row, int globalX, int globalY)
{
    if (row < 0 || row >= static_cast<int>(actions_.size()))
        return;
    const RowAction& action = actions_[static_cast<std::size_t>(row)];
    if (!action.choose)
        return;
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString current = rows_[row].toMap().value(QStringLiteral("value")).toString();
    for (std::size_t i = 0; i < action.options.size(); ++i) {
        QAction* a = menu->addAction(action.options[i]);
        a->setCheckable(true);
        a->setChecked(action.options[i] == current);
        const auto choose = action.choose;
        const int index = static_cast<int>(i);
        connect(a, &QAction::triggered, this, [choose, index] { choose(index); });
    }
    menu->popup(QPoint(globalX, globalY));
}

void PropertiesPalette::press(int row)
{
    if (row >= 0 && row < static_cast<int>(actions_.size()) && actions_[static_cast<std::size_t>(row)].press) {
        // Il dialogo del colore si apre dopo il ritorno al QML.
        const auto action = actions_[static_cast<std::size_t>(row)].press;
        QTimer::singleShot(0, this, action);
    }
}

void PropertiesPalette::step(int row, int delta)
{
    if (row >= 0 && row < static_cast<int>(actions_.size()) && actions_[static_cast<std::size_t>(row)].step)
        actions_[static_cast<std::size_t>(row)].step(delta);
}

void PropertiesPalette::selectSimilar()
{
    if (drawing_)
        drawing_->runCommand(QStringLiteral("SELECTSIMILAR"));
}

void PropertiesPalette::selectAllInSpace()
{
    if (!drawing_)
        return;
    const Document& doc = drawing_->document();
    std::vector<EntityId> ids;
    for (const auto& e : doc.entities())
        if (doc.resolveSpace(e->space()) == doc.activeSpace() && doc.isSelectable(*e))
            ids.push_back(e->id());
    drawing_->setSelection(std::move(ids));
}

void PropertiesPalette::selectNone()
{
    if (drawing_)
        drawing_->setSelection({});
}

// --------------------------------------------------------------- costruzione delle righe

void PropertiesPalette::addSection(const QString& title)
{
    section_ = title;
    rows_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("section")},
                             {QStringLiteral("label"), title},
                             {QStringLiteral("collapsed"), collapsed_.contains(title)}});
    actions_.emplace_back();
}

void PropertiesPalette::addReadOnly(const QString& label, const QString& value)
{
    if (!sectionOpen())
        return;
    rows_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("readonly")},
                             {QStringLiteral("label"), label},
                             {QStringLiteral("value"), value}});
    actions_.emplace_back();
}

void PropertiesPalette::addEdit(const QString& label, const QString& value, std::function<void(const QString&)> commit)
{
    if (!sectionOpen())
        return;
    rows_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("edit")},
                             {QStringLiteral("label"), label},
                             {QStringLiteral("value"), value}});
    RowAction a;
    a.text = std::move(commit);
    actions_.push_back(std::move(a));
}

void PropertiesPalette::addList(const QString& label, const QString& shown, const QString& icon,
                                std::vector<QString> options, std::function<void(int)> choose)
{
    if (!sectionOpen())
        return;
    rows_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("list")},
                             {QStringLiteral("label"), label},
                             {QStringLiteral("value"), shown},
                             {QStringLiteral("icon"), icon}});
    RowAction a;
    a.options = std::move(options);
    a.choose = std::move(choose);
    actions_.push_back(std::move(a));
}

void PropertiesPalette::addButton(const QString& label, const QString& text, const QString& icon, std::function<void()> press)
{
    if (!sectionOpen())
        return;
    rows_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("button")},
                             {QStringLiteral("label"), label},
                             {QStringLiteral("value"), text},
                             {QStringLiteral("icon"), icon}});
    RowAction a;
    a.press = std::move(press);
    actions_.push_back(std::move(a));
}

void PropertiesPalette::addCounter(const QString& label, const QString& text, std::function<void(int)> step)
{
    if (!sectionOpen())
        return;
    rows_.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("counter")},
                             {QStringLiteral("label"), label},
                             {QStringLiteral("value"), text}});
    RowAction a;
    a.step = std::move(step);
    actions_.push_back(std::move(a));
}

void PropertiesPalette::addInfo(const QString& label, const std::function<QString(const Entity&)>& get)
{
    QString value;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const QString v = get(*items_[i]);
        if (i == 0)
            value = v;
        else if (v != value)
            value = kVaries;
    }
    addReadOnly(label, value);
}

void PropertiesPalette::modify(const std::function<void(Entity&)>& change)
{
    QPointer<DrawingWidget> drawing = drawing_;
    const std::vector<EntityId> ids = ids_;
    QTimer::singleShot(0, this, [drawing, ids, change] {
        if (drawing)
            drawing->modifyEntities(ids, change);
    });
}

void PropertiesPalette::later(std::function<void(Document&)> change)
{
    QPointer<DrawingWidget> drawing = drawing_;
    QTimer::singleShot(0, this, [drawing, change] {
        if (!drawing)
            return;
        change(drawing->document());
        drawing->document().touch();
        drawing->notifyChanged();
    });
}

void PropertiesPalette::addNumber(const QString& label, const NumGet& get, const NumSet& set, int decimals)
{
    std::optional<double> common;
    bool varies = false;
    for (const Entity* e : items_) {
        const double v = get(*e);
        if (!common)
            common = v;
        else if (std::abs(*common - v) > 1e-9 * std::max(1.0, std::abs(v)))
            varies = true;
    }
    const QString shown = varies ? kVaries : (common ? fmt(*common, decimals) : QString());
    addEdit(label, shown, [this, shown, set](const QString& text) {
        if (text == shown)
            return;
        bool ok = false;
        const double v = QString(text).trimmed().replace(',', '.').toDouble(&ok);
        if (ok)
            modify([set, v](Entity& e) { set(e, v); });
    });
}

void PropertiesPalette::addText(const QString& label, const TextGet& get, const TextSet& set)
{
    std::optional<std::string> common;
    bool varies = false;
    for (const Entity* e : items_) {
        const std::string v = get(*e);
        if (!common)
            common = v;
        else if (*common != v)
            varies = true;
    }
    const QString shown = varies ? kVaries : QString::fromStdString(common.value_or(std::string()));
    addEdit(label, shown, [this, shown, set](const QString& text) {
        if (text == shown)
            return;
        const std::string value = text.toStdString();
        modify([set, value](Entity& e) { set(e, value); });
    });
}

void PropertiesPalette::addChoice(const QString& label, const std::vector<std::pair<QString, int>>& options,
                                  const std::function<int(const Entity&)>& get,
                                  const std::function<void(Entity&, int)>& set)
{
    std::optional<int> common;
    bool varies = false;
    for (const Entity* e : items_) {
        const int v = get(*e);
        if (!common)
            common = v;
        else if (*common != v)
            varies = true;
    }
    QString shown = varies ? kVaries : QString();
    if (!varies && common) {
        shown = tr("Personalizzato");
        for (const auto& [text, value] : options)
            if (value == *common)
                shown = text;
    }
    std::vector<QString> names;
    std::vector<int> values;
    for (const auto& [text, value] : options) {
        names.push_back(text);
        values.push_back(value);
    }
    addList(label, shown, QString(), std::move(names), [this, values, set](int index) {
        if (index < 0 || index >= static_cast<int>(values.size()))
            return;
        const int v = values[static_cast<std::size_t>(index)];
        modify([set, v](Entity& e) { set(e, v); });
    });
}

// --------------------------------------------------------------- sezioni

void PropertiesPalette::addGeneral()
{
    Document& doc = drawing_->document();

    // Colore.
    std::set<int> colors;
    for (const Entity* e : items_)
        colors.insert(e->color());
    const int color = colors.size() == 1 ? *colors.begin() : -1;
    addButton(tr("Colore"), color < 0 ? kVaries : theme::aciName(color), color >= 0 ? IconProvider::url(swatch(color)) : QString(),
              [this, color] {
                  const auto picked = ColorDialog::pick(color < 0 ? kColorByLayer : color, true, this);
                  if (picked)
                      modify([c = *picked](Entity& e) { e.setColor(c); });
              });

    std::vector<std::pair<QString, int>> layers;
    for (std::size_t i = 0; i < doc.layers().size(); ++i)
        layers.emplace_back(QString::fromStdString(doc.layers()[i].name), static_cast<int>(i));
    addChoice(tr("Layer"), layers, [](const Entity& e) { return e.layer(); },
              [](Entity& e, int v) { e.setLayer(v); });

    // Tipo di linea: DaLayer, DaBlocco e quelli del disegno (per indice nell'elenco).
    std::vector<std::string> names = {"ByLayer", "ByBlock"};
    for (const Linetype& lt : doc.linetypes())
        names.push_back(lt.name);
    std::vector<std::pair<QString, int>> linetypes;
    for (std::size_t i = 0; i < names.size(); ++i)
        linetypes.emplace_back(i == 0 ? tr("DaLayer") : i == 1 ? tr("DaBlocco") : QString::fromStdString(names[i]),
                               static_cast<int>(i));
    addChoice(tr("Tipo di linea"), linetypes,
              [names](const Entity& e) {
                  const std::string lt = e.linetype().empty() ? std::string("ByLayer") : e.linetype();
                  for (std::size_t i = 0; i < names.size(); ++i)
                      if (QString::fromStdString(names[i]).compare(QString::fromStdString(lt), Qt::CaseInsensitive) == 0)
                          return static_cast<int>(i);
                  return -100;
              },
              [names](Entity& e, int v) {
                  if (v >= 0 && v < static_cast<int>(names.size()))
                      e.setLinetype(names[static_cast<std::size_t>(v)]);
              });
    addNumber(tr("Scala tipo di linea"), [](const Entity& e) { return e.linetypeScale(); },
              [](Entity& e, double v) { e.setLinetypeScale(v); });
    addChoice(tr("Spessore di linea"), lineweightOptions(),
              [](const Entity& e) {
                  return e.lineweight() == kLineweightUnset ? kLineweightByLayer : e.lineweight();
              },
              [](Entity& e, int v) { e.setLineweight(v); });
    addChoice(tr("Trasparenza"), transparencyOptions(),
              [](const Entity& e) { return e.transparency() < 0 ? -1 : e.transparency(); },
              [](Entity& e, int v) { e.setTransparency(v); });
}

// Senza selezione: le proprietà correnti, per i nuovi oggetti.
void PropertiesPalette::addCurrentGeneral()
{
    Document& doc = drawing_->document();

    const int color = doc.currentColor();
    addButton(tr("Colore"), theme::aciName(color), IconProvider::url(swatch(color)), [this, color] {
        if (const auto picked = ColorDialog::pick(color, true, this))
            later([c = *picked](Document& d) { d.setCurrentColor(c); });
    });

    std::vector<QString> layers;
    for (const Layer& l : doc.layers())
        layers.push_back(QString::fromStdString(l.name));
    const QString currentLayer = doc.currentLayer() >= 0 && doc.currentLayer() < static_cast<int>(layers.size())
                                     ? layers[static_cast<std::size_t>(doc.currentLayer())]
                                     : QString();
    addList(tr("Layer"), currentLayer,
            IconProvider::url(swatch(doc.layers()[static_cast<std::size_t>(std::max(0, doc.currentLayer()))].color)), layers,
            [this](int i) { later([i](Document& d) { d.setCurrentLayer(i); }); });

    std::vector<QString> linetypeNames = {QStringLiteral("ByLayer"), QStringLiteral("ByBlock")};
    std::vector<QString> linetypeLabels = {tr("DaLayer"), tr("DaBlocco")};
    for (const Linetype& lt : doc.linetypes()) {
        linetypeNames.push_back(QString::fromStdString(lt.name));
        linetypeLabels.push_back(QString::fromStdString(lt.name));
    }
    QString currentLinetype = QString::fromStdString(doc.currentLinetype());
    for (std::size_t i = 0; i < linetypeNames.size(); ++i)
        if (linetypeNames[i].compare(currentLinetype, Qt::CaseInsensitive) == 0)
            currentLinetype = linetypeLabels[i];
    addList(tr("Tipo di linea"), currentLinetype, QString(), linetypeLabels, [this, linetypeNames](int i) {
        const std::string name = linetypeNames[static_cast<std::size_t>(i)].toStdString();
        later([name](Document& d) { d.setCurrentLinetype(name); });
    });

    std::vector<QString> weightLabels;
    std::vector<int> weights;
    QString currentWeight;
    for (const auto& [text, value] : lineweightOptions()) {
        weightLabels.push_back(text);
        weights.push_back(value);
        if (value == doc.currentLineweight())
            currentWeight = text;
    }
    addList(tr("Spessore di linea"), currentWeight, QString(), weightLabels, [this, weights](int i) {
        const int lw = weights[static_cast<std::size_t>(i)];
        later([lw](Document& d) { d.setCurrentLineweight(lw); });
    });
}

void PropertiesPalette::addPlotStyle(bool selection)
{
    addSection(tr("Stile di stampa"));
    addReadOnly(tr("Stile di stampa"), tr("DaColore"));
    if (selection)
        return;
    // Tabella stili dello spazio mostrato (la stessa del dialogo Stampa).
    Document& doc = drawing_->document();
    const int space = drawing_->shownSpace();
    const Layout* layout = doc.findLayout(space);
    const PlotSettings settings = layout ? layoutPlotSettings(*layout) : doc.modelPlot();
    std::vector<QString> labels = {tr("Nessuna")};
    std::vector<QString> names = {QString()};
    for (const QString& name : plotStyleTableNames()) {
        labels.push_back(name);
        names.push_back(name);
    }
    const QString current = QString::fromStdString(settings.styleSheet);
    QString shown = current.isEmpty() ? tr("Nessuna") : tr("%1 (non trovata)").arg(current);
    for (const QString& n : names)
        if (!current.isEmpty() && n.compare(current, Qt::CaseInsensitive) == 0)
            shown = n;
    addList(tr("Tabella stili di stampa"), shown, QString(), labels, [this, names, space](int i) {
        const std::string name = names[static_cast<std::size_t>(i)].toStdString();
        later([space, name](Document& d) {
            if (Layout* l = d.findLayout(space)) {
                PlotSettings s = layoutPlotSettings(*l);
                s.styleSheet = name;
                s.setFlag(kPlotPlotStyles, true);
                applyPlotSettings(*l, s);
            } else {
                PlotSettings s = d.modelPlot();
                s.styleSheet = name;
                s.setFlag(kPlotPlotStyles, true);
                d.setModelPlot(s);
            }
        });
    });
    addReadOnly(tr("Tabella collegata a"), layout ? QString::fromStdString(layout->name) : tr("Modello"));
    addReadOnly(tr("Tipo tabella stili"), tr("Dipendente dal colore (.ctb)"));
}

void PropertiesPalette::addViewAndMisc()
{
    Document& doc = drawing_->document();
    const BBox view = drawing_->view()->visibleArea();
    addSection(tr("Vista"));
    addReadOnly(tr("X centro"), fmt(view.center().x));
    addReadOnly(tr("Y centro"), fmt(view.center().y));
    addReadOnly(tr("Altezza"), fmt(view.height()));
    addReadOnly(tr("Larghezza"), fmt(view.width()));

    addSection(tr("Varie"));
    const std::vector<int> codes = {0, 4, 5, 14, 6, 7, 1, 2, 10};
    std::vector<QString> unitLabels;
    for (int code : codes)
        unitLabels.push_back(unitsName(code));
    addList(tr("Unità di inserimento"), unitsName(doc.insUnits()), QString(), unitLabels, [this, codes](int i) {
        const int code = codes[static_cast<std::size_t>(i)];
        later([code](Document& d) { d.setInsUnits(code); });
    });
    addEdit(tr("Scala tipo di linea globale"), fmt(doc.ltscale()), [this](const QString& text) {
        bool ok = false;
        const double v = QString(text).trimmed().replace(',', '.').toDouble(&ok);
        if (ok && v > 0.0)
            later([v](Document& d) { d.setLtscale(v); });
    });
    const Layout* layout = doc.findLayout(drawing_->shownSpace());
    addReadOnly(tr("Spazio"), layout ? tr("Layout %1").arg(QString::fromStdString(layout->name)) : tr("Modello"));
}

void PropertiesPalette::addGeometry(EntityType type)
{
    const auto deg = [](double rad) { return normalizeAngle(rad) * 180.0 / kPi; };
    switch (type) {
    case EntityType::Image:
        addSection(tr("Immagine"));
        addText(tr("File"), [](const Entity& e) { return as<ImageEntity>(e).path; },
                [](Entity& e, const std::string& s) { as<ImageEntity>(e).path = s; });
        addInfo(tr("Trovato in"), [this](const Entity& e) {
            if (as<ImageEntity>(e).isOle())   // logo incollato nel disegno: i dati sono nel disegno
                return tr("incorporato nel disegno (OLE: %1)").arg(QString::fromStdString(as<ImageEntity>(e).oleType));
            const QString found = drawing_ ? ImageLibrary::instance().find(drawing_->document(), as<ImageEntity>(e).path) : QString();
            return found.isEmpty() ? tr("(non trovato)") : QDir::toNativeSeparators(found);
        });
        addNumber(tr("Posizione X"), [](const Entity& e) { return as<ImageEntity>(e).insertion.x; },
                  [](Entity& e, double v) { as<ImageEntity>(e).insertion.x = v; });
        addNumber(tr("Posizione Y"), [](const Entity& e) { return as<ImageEntity>(e).insertion.y; },
                  [](Entity& e, double v) { as<ImageEntity>(e).insertion.y = v; });
        addNumber(tr("Larghezza"), [](const Entity& e) { return as<ImageEntity>(e).u.length() * as<ImageEntity>(e).width; },
                  [](Entity& e, double w) {
                      auto& im = as<ImageEntity>(e);
                      const double now = im.u.length() * im.width;
                      if (w > 0.0 && now > 0.0) {   // in proporzione, come la scala
                          im.u = im.u * (w / now);
                          im.v = im.v * (w / now);
                      }
                  });
        addInfo(tr("Altezza"), [](const Entity& e) { return fmt(as<ImageEntity>(e).v.length() * as<ImageEntity>(e).height); });
        addInfo(tr("Pixel"), [](const Entity& e) {
            return QStringLiteral("%1 x %2").arg(as<ImageEntity>(e).width).arg(as<ImageEntity>(e).height);
        });
        addNumber(tr("Luminosità"), [](const Entity& e) { return as<ImageEntity>(e).brightness; },
                  [](Entity& e, double v) { as<ImageEntity>(e).brightness = std::clamp(static_cast<int>(v), 0, 100); }, 0);
        addNumber(tr("Contrasto"), [](const Entity& e) { return as<ImageEntity>(e).contrast; },
                  [](Entity& e, double v) { as<ImageEntity>(e).contrast = std::clamp(static_cast<int>(v), 0, 100); }, 0);
        addNumber(tr("Dissolvenza"), [](const Entity& e) { return as<ImageEntity>(e).fade; },
                  [](Entity& e, double v) { as<ImageEntity>(e).fade = std::clamp(static_cast<int>(v), 0, 100); }, 0);
        addChoice(tr("Ritaglio"), {{tr("No"), 0}, {tr("Sì"), 1}},
                  [](const Entity& e) { return as<ImageEntity>(e).clipping ? 1 : 0; },
                  [](Entity& e, int v) { as<ImageEntity>(e).clipping = v == 1; });
        break;
    case EntityType::Line:
        addSection(tr("Geometria"));
        addNumber(tr("Inizio X"), [](const Entity& e) { return as<LineEntity>(e).start.x; },
                  [](Entity& e, double v) { as<LineEntity>(e).start.x = v; });
        addNumber(tr("Inizio Y"), [](const Entity& e) { return as<LineEntity>(e).start.y; },
                  [](Entity& e, double v) { as<LineEntity>(e).start.y = v; });
        addNumber(tr("Fine X"), [](const Entity& e) { return as<LineEntity>(e).end.x; },
                  [](Entity& e, double v) { as<LineEntity>(e).end.x = v; });
        addNumber(tr("Fine Y"), [](const Entity& e) { return as<LineEntity>(e).end.y; },
                  [](Entity& e, double v) { as<LineEntity>(e).end.y = v; });
        addInfo(tr("Delta X"), [](const Entity& e) { return fmt((as<LineEntity>(e).end - as<LineEntity>(e).start).x); });
        addInfo(tr("Delta Y"), [](const Entity& e) { return fmt((as<LineEntity>(e).end - as<LineEntity>(e).start).y); });
        addNumber(tr("Lunghezza"), [](const Entity& e) { return (as<LineEntity>(e).end - as<LineEntity>(e).start).length(); },
                  [](Entity& e, double v) {
                      auto& l = as<LineEntity>(e);
                      const Vec2 dir = l.end - l.start;
                      if (dir.length() > 0.0 && v > 0.0)
                          l.end = l.start + dir * (v / dir.length());
                  });
        addNumber(tr("Angolo"), [deg](const Entity& e) { return deg(angleOf(as<LineEntity>(e).end - as<LineEntity>(e).start)); },
                  [](Entity& e, double v) {
                      auto& l = as<LineEntity>(e);
                      l.end = l.start + Vec2::polar((l.end - l.start).length(), degToRad(v));
                  }, 2);
        break;
    case EntityType::Circle:
        addSection(tr("Geometria"));
        addNumber(tr("Centro X"), [](const Entity& e) { return as<CircleEntity>(e).center.x; },
                  [](Entity& e, double v) { as<CircleEntity>(e).center.x = v; });
        addNumber(tr("Centro Y"), [](const Entity& e) { return as<CircleEntity>(e).center.y; },
                  [](Entity& e, double v) { as<CircleEntity>(e).center.y = v; });
        addNumber(tr("Raggio"), [](const Entity& e) { return as<CircleEntity>(e).radius; },
                  [](Entity& e, double v) { if (v > 0.0) as<CircleEntity>(e).radius = v; });
        addNumber(tr("Diametro"), [](const Entity& e) { return 2.0 * as<CircleEntity>(e).radius; },
                  [](Entity& e, double v) { if (v > 0.0) as<CircleEntity>(e).radius = v / 2.0; });
        addNumber(tr("Circonferenza"), [](const Entity& e) { return 2.0 * kPi * as<CircleEntity>(e).radius; },
                  [](Entity& e, double v) { if (v > 0.0) as<CircleEntity>(e).radius = v / (2.0 * kPi); });
        addNumber(tr("Area"), [](const Entity& e) { const double r = as<CircleEntity>(e).radius; return kPi * r * r; },
                  [](Entity& e, double v) { if (v > 0.0) as<CircleEntity>(e).radius = std::sqrt(v / kPi); });
        break;
    case EntityType::Arc:
        addSection(tr("Geometria"));
        addNumber(tr("Centro X"), [](const Entity& e) { return as<ArcEntity>(e).center.x; },
                  [](Entity& e, double v) { as<ArcEntity>(e).center.x = v; });
        addNumber(tr("Centro Y"), [](const Entity& e) { return as<ArcEntity>(e).center.y; },
                  [](Entity& e, double v) { as<ArcEntity>(e).center.y = v; });
        addNumber(tr("Raggio"), [](const Entity& e) { return as<ArcEntity>(e).radius; },
                  [](Entity& e, double v) { if (v > 0.0) as<ArcEntity>(e).radius = v; });
        addNumber(tr("Angolo iniziale"), [deg](const Entity& e) { return deg(as<ArcEntity>(e).startAngle); },
                  [](Entity& e, double v) { as<ArcEntity>(e).startAngle = degToRad(v); }, 2);
        addNumber(tr("Angolo finale"), [deg](const Entity& e) { return deg(as<ArcEntity>(e).endAngle); },
                  [](Entity& e, double v) { as<ArcEntity>(e).endAngle = degToRad(v); }, 2);
        addInfo(tr("Angolo totale"), [](const Entity& e) { return fmt(as<ArcEntity>(e).sweep() * 180.0 / kPi, 2); });
        addInfo(tr("Lunghezza arco"), [](const Entity& e) { return fmt(as<ArcEntity>(e).sweep() * as<ArcEntity>(e).radius); });
        break;
    case EntityType::Point:
        addSection(tr("Geometria"));
        addNumber(tr("Posizione X"), [](const Entity& e) { return as<PointEntity>(e).position.x; },
                  [](Entity& e, double v) { as<PointEntity>(e).position.x = v; });
        addNumber(tr("Posizione Y"), [](const Entity& e) { return as<PointEntity>(e).position.y; },
                  [](Entity& e, double v) { as<PointEntity>(e).position.y = v; });
        break;
    case EntityType::Polyline: {
        addSection(tr("Geometria"));
        // Un vertice alla volta (con una sola polilinea).
        if (items_.size() == 1) {
            const auto& pl = as<PolylineEntity>(*items_.front());
            const int count = static_cast<int>(pl.vertices.size());
            vertex_ = std::clamp(vertex_, 0, std::max(0, count - 1));
            addCounter(tr("Vertice"), tr("%1 di %2").arg(vertex_ + 1).arg(count), [this, count](int delta) {
                vertex_ = std::clamp(vertex_ + delta, 0, std::max(0, count - 1));
                refresh();
            });
            const std::size_t i = static_cast<std::size_t>(vertex_);
            if (i < pl.vertices.size()) {
                addNumber(tr("Vertice X"), [i](const Entity& e) { return as<PolylineEntity>(e).vertices[i].point.x; },
                          [i](Entity& e, double v) { as<PolylineEntity>(e).vertices[i].point.x = v; });
                addNumber(tr("Vertice Y"), [i](const Entity& e) { return as<PolylineEntity>(e).vertices[i].point.y; },
                          [i](Entity& e, double v) { as<PolylineEntity>(e).vertices[i].point.y = v; });
                addNumber(tr("Curvatura (bulge)"), [i](const Entity& e) { return as<PolylineEntity>(e).vertices[i].bulge; },
                          [i](Entity& e, double v) { as<PolylineEntity>(e).vertices[i].bulge = v; });
                addNumber(tr("Larghezza iniziale segmento"),
                          [i](const Entity& e) { return as<PolylineEntity>(e).vertices[i].startWidth; },
                          [i](Entity& e, double v) { as<PolylineEntity>(e).vertices[i].startWidth = std::max(0.0, v); });
                addNumber(tr("Larghezza finale segmento"),
                          [i](const Entity& e) { return as<PolylineEntity>(e).vertices[i].endWidth; },
                          [i](Entity& e, double v) { as<PolylineEntity>(e).vertices[i].endWidth = std::max(0.0, v); });
            }
        } else {
            addInfo(tr("Vertici"), [](const Entity& e) { return QString::number(as<PolylineEntity>(e).vertices.size()); });
        }
        // Larghezza globale: la stessa su tutti i tratti; scriverla la impone a tutti.
        addNumber(tr("Larghezza globale"), [](const Entity& e) { return as<PolylineEntity>(e).maxWidth(); },
                  [](Entity& e, double v) { as<PolylineEntity>(e).setWidth(v); });
        addChoice(tr("Chiusa"), {{tr("No"), 0}, {tr("Sì"), 1}},
                  [](const Entity& e) { return as<PolylineEntity>(e).closed ? 1 : 0; },
                  [](Entity& e, int v) { as<PolylineEntity>(e).closed = v == 1; });
        addInfo(tr("Lunghezza"), [](const Entity& e) { return fmt(as<PolylineEntity>(e).length()); });
        addInfo(tr("Area"), [](const Entity& e) {
            const auto& p = as<PolylineEntity>(e);
            return p.closed ? fmt(std::abs(p.area())) : QStringLiteral("-");
        });
        break;
    }
    case EntityType::Ellipse:
        addSection(tr("Geometria"));
        addNumber(tr("Centro X"), [](const Entity& e) { return as<EllipseEntity>(e).center.x; },
                  [](Entity& e, double v) { as<EllipseEntity>(e).center.x = v; });
        addNumber(tr("Centro Y"), [](const Entity& e) { return as<EllipseEntity>(e).center.y; },
                  [](Entity& e, double v) { as<EllipseEntity>(e).center.y = v; });
        addNumber(tr("Raggio maggiore"), [](const Entity& e) { return as<EllipseEntity>(e).majorAxis.length(); },
                  [](Entity& e, double v) {
                      auto& el = as<EllipseEntity>(e);
                      if (v > 0.0 && el.majorAxis.length() > 0.0)
                          el.majorAxis = el.majorAxis * (v / el.majorAxis.length());
                  });
        addNumber(tr("Rapporto assi"), [](const Entity& e) { return as<EllipseEntity>(e).ratio; },
                  [](Entity& e, double v) { if (v > 0.0 && v <= 1.0) as<EllipseEntity>(e).ratio = v; });
        break;
    case EntityType::Text:
        addSection(tr("Testo"));
        addTextStyleChoice();
        addAnnotative();
        addText(tr("Contenuto"), [](const Entity& e) { return as<TextEntity>(e).text; },
                [](Entity& e, const std::string& s) { as<TextEntity>(e).text = s; });
        addNumber(tr("Altezza"), [](const Entity& e) { return as<TextEntity>(e).height; },
                  [](Entity& e, double v) { if (v > 0.0) as<TextEntity>(e).height = v; });
        addNumber(tr("Rotazione"), [](const Entity& e) { return radToDeg(as<TextEntity>(e).rotation); },
                  [](Entity& e, double v) { as<TextEntity>(e).rotation = degToRad(v); }, 2);
        addNumber(tr("Fattore larghezza"), [](const Entity& e) { return as<TextEntity>(e).widthFactor; },
                  [](Entity& e, double v) { if (v > 0.0) as<TextEntity>(e).widthFactor = v; }, 2);
        addNumber(tr("Angolo obliquo"), [](const Entity& e) { return radToDeg(as<TextEntity>(e).oblique); },
                  [](Entity& e, double v) { if (std::abs(v) < 85.0) as<TextEntity>(e).oblique = degToRad(v); }, 2);
        break;
    case EntityType::MText:
        addSection(tr("Testo"));
        addTextStyleChoice();
        addAnnotative();
        // Senza i codici di formattazione; "\P" = a capo. Modificando si tiene la formattazione d'insieme.
        addText(tr("Contenuto"), [](const Entity& e) { return editableMText(as<MTextEntity>(e).text); },
                [](Entity& e, const std::string& s) { as<MTextEntity>(e).text = fromEditableMText(as<MTextEntity>(e).text, s); });
        addNumber(tr("Altezza"), [](const Entity& e) { return as<MTextEntity>(e).height; },
                  [](Entity& e, double v) { if (v > 0.0) as<MTextEntity>(e).height = v; });
        addNumber(tr("Larghezza"), [](const Entity& e) { return as<MTextEntity>(e).width; },
                  [](Entity& e, double v) { if (v >= 0.0) as<MTextEntity>(e).width = v; });
        addNumber(tr("Rotazione"), [](const Entity& e) { return radToDeg(as<MTextEntity>(e).rotation); },
                  [](Entity& e, double v) { as<MTextEntity>(e).rotation = degToRad(v); }, 2);
        break;
    case EntityType::Table:
        addSection(tr("Tabella"));
        addInfo(tr("Righe x colonne"), [](const Entity& e) {
            return QStringLiteral("%1 x %2").arg(as<TableEntity>(e).rows).arg(as<TableEntity>(e).columns);
        });
        addNumber(tr("Posizione X"), [](const Entity& e) { return as<TableEntity>(e).position.x; },
                  [](Entity& e, double v) { as<TableEntity>(e).position.x = v; });
        addNumber(tr("Posizione Y"), [](const Entity& e) { return as<TableEntity>(e).position.y; },
                  [](Entity& e, double v) { as<TableEntity>(e).position.y = v; });
        addNumber(tr("Altezza testo"), [](const Entity& e) { return as<TableEntity>(e).textHeight; },
                  [](Entity& e, double v) { if (v > 0.0) as<TableEntity>(e).textHeight = v; });
        addReadOnly(tr("Celle"), tr("doppio clic sulla tabella o TABLEDIT"));
        break;
    case EntityType::Mline:
        addSection(tr("Geometria"));
        addInfo(tr("Vertici"), [](const Entity& e) { return QString::number(as<MlineEntity>(e).vertices.size()); });
        addNumber(tr("Scala"), [](const Entity& e) { return as<MlineEntity>(e).scale; },
                  [](Entity& e, double v) { if (v != 0.0) as<MlineEntity>(e).scale = v; });
        addChoice(tr("Giustificazione"), {{tr("Superiore"), 0}, {tr("Zero"), 1}, {tr("Inferiore"), 2}},
                  [](const Entity& e) { return as<MlineEntity>(e).justification; },
                  [](Entity& e, int v) { as<MlineEntity>(e).justification = v; });
        addChoice(tr("Chiusa"), {{tr("No"), 0}, {tr("Sì"), 1}},
                  [](const Entity& e) { return as<MlineEntity>(e).closed ? 1 : 0; },
                  [](Entity& e, int v) { as<MlineEntity>(e).closed = v == 1; });
        break;
    case EntityType::Wipeout:
        addSection(tr("Geometria"));
        addInfo(tr("Vertici"), [](const Entity& e) { return QString::number(as<WipeoutEntity>(e).outline.size()); });
        break;
    case EntityType::XLine:
        addSection(tr("Geometria"));
        addInfo(tr("Tipo"), [](const Entity& e) {
            return as<XLineEntity>(e).ray ? QObject::tr("Semiretta") : QObject::tr("Linea di costruzione");
        });
        addNumber(tr("Punto base X"), [](const Entity& e) { return as<XLineEntity>(e).base.x; },
                  [](Entity& e, double v) { as<XLineEntity>(e).base.x = v; });
        addNumber(tr("Punto base Y"), [](const Entity& e) { return as<XLineEntity>(e).base.y; },
                  [](Entity& e, double v) { as<XLineEntity>(e).base.y = v; });
        addNumber(tr("Angolo"), [](const Entity& e) { return radToDeg(angleOf(as<XLineEntity>(e).direction)); },
                  [](Entity& e, double v) { as<XLineEntity>(e).direction = Vec2::polar(1.0, degToRad(v)); }, 2);
        break;
    case EntityType::Leader:
        addSection(tr("Direttrice"));
        addAnnotative();
        addText(tr("Testo"), [](const Entity& e) { return editableMText(as<LeaderEntity>(e).text); },
                [](Entity& e, const std::string& s) { as<LeaderEntity>(e).text = fromEditableMText(as<LeaderEntity>(e).text, s); });
        addNumber(tr("Altezza testo"), [](const Entity& e) { return as<LeaderEntity>(e).textHeight; },
                  [](Entity& e, double v) { if (v > 0.0) as<LeaderEntity>(e).textHeight = v; });
        addNumber(tr("Dimensione freccia"), [](const Entity& e) { return as<LeaderEntity>(e).arrowSize; },
                  [](Entity& e, double v) { if (v >= 0.0) as<LeaderEntity>(e).arrowSize = v; });
        addInfo(tr("Vertici"), [](const Entity& e) { return QString::number(as<LeaderEntity>(e).points.size()); });
        break;
    case EntityType::Insert: {
        addSection(tr("Geometria"));
        addInfo(tr("Nome"), [](const Entity& e) {
            // Stato di un blocco dinamico: il nome del blocco dinamico, con quello dello stato salvato.
            const auto& insert = as<InsertEntity>(e);
            if (insert.effectiveName() != insert.blockName)
                return tr("%1 (blocco dinamico, stato %2)")
                    .arg(QString::fromStdString(insert.effectiveName()), QString::fromStdString(insert.blockName));
            return QString::fromStdString(insert.blockName);
        });
        addNumber(tr("Posizione X"), [](const Entity& e) { return as<InsertEntity>(e).position.x; },
                  [](Entity& e, double v) { as<InsertEntity>(e).position.x = v; });
        addNumber(tr("Posizione Y"), [](const Entity& e) { return as<InsertEntity>(e).position.y; },
                  [](Entity& e, double v) { as<InsertEntity>(e).position.y = v; });
        addNumber(tr("Scala X"), [](const Entity& e) { return as<InsertEntity>(e).scaleX; },
                  [](Entity& e, double v) { if (v != 0.0) as<InsertEntity>(e).scaleX = v; });
        addNumber(tr("Scala Y"), [](const Entity& e) { return as<InsertEntity>(e).scaleY; },
                  [](Entity& e, double v) { if (v != 0.0) as<InsertEntity>(e).scaleY = v; });
        addNumber(tr("Rotazione"), [](const Entity& e) { return radToDeg(as<InsertEntity>(e).rotation); },
                  [](Entity& e, double v) { as<InsertEntity>(e).rotation = degToRad(v); }, 2);
        // Riferimento esterno: percorso salvato e file effettivamente caricato.
        if (const auto& block = as<InsertEntity>(*items_.front()).block; block && block->isXref()) {
            addSection(tr("Riferimento esterno"));
            addInfo(tr("Percorso salvato"), [](const Entity& e) {
                const auto& b = as<InsertEntity>(e).block;
                return b ? QDir::toNativeSeparators(QString::fromStdString(b->xrefPath)) : QString();
            });
            addInfo(tr("Trovato in"), [this](const Entity& e) {
                const auto& b = as<InsertEntity>(e).block;
                return b && b->xrefLoaded ? QDir::toNativeSeparators(QString::fromStdString(b->xrefFound))
                                          : tr("(non trovato)");
            });
            addInfo(tr("Tipo"), [this](const Entity& e) {
                const auto& b = as<InsertEntity>(e).block;
                return b && b->xrefOverlay ? tr("Sovrapposizione") : tr("Collegamento");
            });
        }
        // Attributi: quelli del primo blocco (con più blocchi uguali si modificano insieme).
        const auto& first = as<InsertEntity>(*items_.front());
        if (!first.attributes.empty()) {
            addSection(tr("Attributi"));
            for (const Attribute& a : first.attributes) {
                const std::string tag = a.tag;
                addText(QString::fromStdString(tag),
                        [tag](const Entity& e) {
                            for (const Attribute& att : as<InsertEntity>(e).attributes)
                                if (att.tag == tag)
                                    return att.text.text;
                            return std::string();
                        },
                        [tag](Entity& e, const std::string& s) {
                            for (Attribute& att : as<InsertEntity>(e).attributes)
                                if (att.tag == tag)
                                    att.text.text = s;
                        });
            }
        }
        break;
    }
    case EntityType::Dimension:
        addSection(tr("Quota"));
        addAnnotative();
        addInfo(tr("Misura"), [](const Entity& e) { return fmt(as<DimensionEntity>(e).measurement()); });
        addText(tr("Testo sostitutivo"), [](const Entity& e) { return as<DimensionEntity>(e).textOverride; },
                [](Entity& e, const std::string& s) {
                    auto& d = as<DimensionEntity>(e);
                    d.textOverride = s;
                    d.block.reset();   // il blocco della quota (dal file) si ricostruisce con il testo nuovo
                    d.blockName.clear();
                });
        break;
    case EntityType::Hatch:
        addSection(tr("Motivo"));
        addText(tr("Motivo"), [](const Entity& e) { return as<HatchEntity>(e).pattern; },
                [](Entity& e, const std::string& s) {
                    auto& h = as<HatchEntity>(e);
                    std::string name = QString::fromStdString(s).trimmed().toUpper().toStdString();
                    if (name == "SOLIDO")
                        name = "SOLID";
                    h.setPattern(name, h.patternScale, h.patternAngle);   // nome sconosciuto: resta com'era
                });
        addNumber(tr("Scala"), [](const Entity& e) { return as<HatchEntity>(e).patternScale; },
                  [](Entity& e, double v) {
                      auto& h = as<HatchEntity>(e);
                      if (v > 0.0)
                          h.setPattern(h.pattern, v, h.patternAngle);
                  });
        addNumber(tr("Angolo"), [](const Entity& e) { return radToDeg(as<HatchEntity>(e).patternAngle); },
                  [](Entity& e, double v) {
                      auto& h = as<HatchEntity>(e);
                      h.setPattern(h.pattern, h.patternScale, degToRad(v));
                  });
        addInfo(tr("Contorni"), [](const Entity& e) { return QString::number(as<HatchEntity>(e).loops.size()); });
        break;
    case EntityType::Spline:
        addSection(tr("Geometria"));
        addInfo(tr("Grado"), [](const Entity& e) { return QString::number(as<SplineEntity>(e).degree); });
        addInfo(tr("Punti di controllo"), [](const Entity& e) { return QString::number(as<SplineEntity>(e).control.size()); });
        addInfo(tr("Punti di interpolazione"), [](const Entity& e) { return QString::number(as<SplineEntity>(e).fit.size()); });
        break;
    case EntityType::AttDef:
        addSection(tr("Attributo"));
        addText(tr("Etichetta"), [](const Entity& e) { return as<AttDefEntity>(e).tag; },
                [](Entity& e, const std::string& s) { as<AttDefEntity>(e).tag = s; });
        addText(tr("Richiesta"), [](const Entity& e) { return as<AttDefEntity>(e).prompt; },
                [](Entity& e, const std::string& s) { as<AttDefEntity>(e).prompt = s; });
        addText(tr("Predefinito"), [](const Entity& e) { return as<AttDefEntity>(e).text.text; },
                [](Entity& e, const std::string& s) { as<AttDefEntity>(e).text.text = s; });
        addNumber(tr("Altezza"), [](const Entity& e) { return as<AttDefEntity>(e).text.height; },
                  [](Entity& e, double v) { if (v > 0.0) as<AttDefEntity>(e).text.height = v; });
        break;
    case EntityType::Viewport: {
        addSection(tr("Finestra"));
        // Scala: 1:100 significa 1 mm sul foglio = 100 unità del modello.
        std::vector<std::pair<QString, int>> scales;
        const std::vector<std::pair<QString, double>> values = {
            {"1:1", 1.0}, {"1:2", 0.5}, {"1:5", 0.2}, {"1:10", 0.1}, {"1:20", 0.05}, {"1:25", 0.04},
            {"1:50", 0.02}, {"1:100", 0.01}, {"1:200", 0.005}, {"1:500", 0.002}, {"1:1000", 0.001},
            {"2:1", 2.0}, {"5:1", 5.0}, {"10:1", 10.0}};
        for (std::size_t i = 0; i < values.size(); ++i)
            scales.emplace_back(values[i].first, static_cast<int>(i));
        addChoice(tr("Scala standard"), scales,
                  [values](const Entity& e) {
                      const double s = as<ViewportEntity>(e).scale();
                      for (std::size_t i = 0; i < values.size(); ++i)
                          if (std::abs(s - values[i].second) < 1e-9 * values[i].second)
                              return static_cast<int>(i);
                      return -100;
                  },
                  [values](Entity& e, int i) {
                      if (i >= 0 && i < static_cast<int>(values.size()))
                          as<ViewportEntity>(e).setScale(values[static_cast<std::size_t>(i)].second);
                  });
        addInfo(tr("Scala personalizzata"), [](const Entity& e) {
            const double s = as<ViewportEntity>(e).scale();
            return s >= 1.0 ? QStringLiteral("%1:1").arg(s, 0, 'g', 6) : QStringLiteral("1:%1").arg(1.0 / s, 0, 'g', 6);
        });
        addNumber(tr("Centro X"), [](const Entity& e) { return as<ViewportEntity>(e).center.x; },
                  [](Entity& e, double v) { as<ViewportEntity>(e).center.x = v; });
        addNumber(tr("Centro Y"), [](const Entity& e) { return as<ViewportEntity>(e).center.y; },
                  [](Entity& e, double v) { as<ViewportEntity>(e).center.y = v; });
        addNumber(tr("Larghezza"), [](const Entity& e) { return as<ViewportEntity>(e).width; },
                  [](Entity& e, double v) {
                      auto& vp = as<ViewportEntity>(e);
                      if (v > 0.0) {
                          const double s = vp.scale();
                          vp.width = v;
                          vp.setScale(s);
                      }
                  });
        addNumber(tr("Altezza"), [](const Entity& e) { return as<ViewportEntity>(e).height; },
                  [](Entity& e, double v) {
                      auto& vp = as<ViewportEntity>(e);
                      if (v > 0.0) {
                          const double s = vp.scale();
                          vp.height = v;
                          vp.setScale(s);
                      }
                  });
        addChoice(tr("Vista bloccata"), {{tr("No"), 0}, {tr("Sì"), 1}},
                  [](const Entity& e) { return as<ViewportEntity>(e).locked ? 1 : 0; },
                  [](Entity& e, int v) { as<ViewportEntity>(e).locked = v == 1; });
        break;
    }
    case EntityType::Solid:
        break;
    }
}

// --------------------------------------------------------------- ricostruzione

void PropertiesPalette::fillSummary()
{
    summary_.clear();
    summaryTypes_.clear();
    summaryIndex_ = 0;
    if (!drawing_) {
        summary_ << tr("Nessun disegno aperto");
        summaryTypes_.push_back(-1);
        return;
    }
    const Document& doc = drawing_->document();
    std::vector<const Entity*> all;
    for (EntityId id : drawing_->selection())
        if (const Entity* e = doc.find(id))
            all.push_back(e);
    if (all.empty()) {
        summary_ << tr("Nessuna selezione");
        summaryTypes_.push_back(-1);
        filter_.reset();
        return;
    }
    std::map<int, int> counts;
    for (const Entity* e : all)
        ++counts[static_cast<int>(e->type())];
    if (counts.size() > 1) {
        summary_ << tr("Tutto (%1)").arg(all.size());
        summaryTypes_.push_back(-1);
    }
    if (filter_ && !counts.count(static_cast<int>(*filter_)))
        filter_.reset();
    for (const auto& [type, count] : counts) {
        const QString name = typeName(static_cast<EntityType>(type));
        summary_ << (count == 1 ? name : QStringLiteral("%1 (%2)").arg(name).arg(count));
        summaryTypes_.push_back(type);
        if (filter_ && static_cast<int>(*filter_) == type)
            summaryIndex_ = static_cast<int>(summaryTypes_.size()) - 1;
    }
}

void PropertiesPalette::rebuild()
{
    fillSummary();
    rows_.clear();
    actions_.clear();
    items_.clear();
    ids_.clear();

    if (drawing_) {
        const Document& doc = drawing_->document();
        for (EntityId id : drawing_->selection())
            if (const Entity* e = doc.find(id); e && (!filter_ || e->type() == *filter_)) {
                items_.push_back(e);
                ids_.push_back(id);
            }

        addSection(tr("Generale"));
        if (sectionOpen()) {
            if (items_.empty())
                addCurrentGeneral();
            else
                addGeneral();
        }
        if (items_.empty()) {
            addPlotStyle(false);
            addViewAndMisc();
        } else {
            addPlotStyle(true);
            // Geometria quando gli oggetti mostrati sono tutti dello stesso tipo.
            const EntityType type = items_.front()->type();
            const bool same = std::all_of(items_.begin(), items_.end(), [type](const Entity* e) { return e->type() == type; });
            if (same)
                addGeometry(type);
        }
    }
    if (compact_)
        compactRows();
    emit rowsChanged();
}

// Proprietà rapide: colore, layer, tipo di linea e le prime righe della geometria o del testo.
void PropertiesPalette::compactRows()
{
    static const QStringList general = {tr("Colore"), tr("Layer"), tr("Tipo di linea")};
    constexpr int kMaxRows = 8;
    QVariantList rows;
    std::vector<RowAction> actions;
    QString section;
    for (int i = 0; i < rows_.size() && static_cast<int>(rows.size()) < kMaxRows; ++i) {
        const QVariantMap row = rows_[i].toMap();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        const QString label = row.value(QStringLiteral("label")).toString();
        if (kind == QLatin1String("section")) {
            section = label;
            continue;
        }
        if (section == tr("Generale") ? !general.contains(label) : section == tr("Stile di stampa"))
            continue;
        rows.append(row);
        actions.push_back(std::move(actions_[static_cast<std::size_t>(i)]));
    }
    rows_ = rows;
    actions_ = std::move(actions);
}

} // namespace cartesio

namespace cartesio {

void PropertiesPalette::addAnnotative()
{
    if (!drawing_)
        return;
    const Document& doc = drawing_->document();
    // Sì: l'oggetto com'è vale per la scala di annotazione corrente. No: resta com'è, senza scale.
    addChoice(
        tr("Annotativo"), {{tr("No"), 0}, {tr("Sì"), 1}}, [](const Entity& e) { return e.annotative() ? 1 : 0; },
        [&doc](Entity& e, int v) {
            if (v == 0) {
                e.setAnnoScales({});
                e.setAnnoFactor(1.0);
            } else if (!e.annotative()) {
                doc.markAnnotative(e);
            }
        });
    bool any = false;
    for (const Entity* e : items_)
        any = any || e->annotative();
    if (any)
        addInfo(tr("Scale di annotazione"), [](const Entity& e) {
            QStringList names;
            for (const std::string& s : e.annoScales())
                names << QString::fromStdString(s);
            return names.join(QStringLiteral(", "));
        });
}

void PropertiesPalette::addTextStyleChoice()
{
    if (!drawing_)
        return;
    const Document& doc = drawing_->document();
    std::vector<std::pair<QString, int>> options;
    for (std::size_t i = 0; i < doc.textStyles().size(); ++i)
        options.emplace_back(QString::fromStdString(doc.textStyles()[i].name), static_cast<int>(i));
    const auto styleOf = [](const Entity& e) -> std::string {
        if (e.type() == EntityType::MText)
            return static_cast<const MTextEntity&>(e).style;
        if (e.type() == EntityType::Text)
            return static_cast<const TextEntity&>(e).style;
        return {};
    };
    addChoice(
        tr("Stile"), options,
        [&doc, styleOf](const Entity& e) {
            const std::string name = styleOf(e);
            const auto& styles = doc.textStyles();
            for (std::size_t i = 0; i < styles.size(); ++i)
                if (QString::fromStdString(styles[i].name).compare(QString::fromStdString(name), Qt::CaseInsensitive) == 0)
                    return static_cast<int>(i);
            return 0;
        },
        [&doc](Entity& e, int index) {
            if (index < 0 || index >= static_cast<int>(doc.textStyles().size()))
                return;
            const std::string name = doc.textStyles()[static_cast<std::size_t>(index)].name;
            if (e.type() == EntityType::MText)
                static_cast<MTextEntity&>(e).style = name;
            else if (e.type() == EntityType::Text)
                static_cast<TextEntity&>(e).style = name;
        });
}

} // namespace cartesio
