#pragma once

// Ponte fra la finestra principale (azioni Qt, comandi) e l'interfaccia in QML: barra multifunzione, schede,
// barra di stato e riga di comando sono componenti QML (cartella qml/) che mostrano questi modelli.
//
// - ActionItem: una QAction vista dal QML (testo, icona, attiva, spuntata, menu).
// - ComboModel: elenco a discesa con l'interfaccia di QComboBox (clear, addItem, setCurrentIndex...), così il
//   codice che lo riempie resta quello di prima; l'elenco si apre come menu (anche con le icone cliccabili
//   dei layer).
// - TabsModel: schede con l'interfaccia di QTabBar (addTab, removeTab, setCurrentIndex, tabData...).
// - IconProvider: le icone (QIcon disegnate in Icons.cpp) per il QML, come "image://cad/<n>".

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QVariant>

#include <functional>
#include <map>
#include <vector>


namespace cartesio {

// Icone per il QML: registrate una volta e servite al formato richiesto.
class IconProvider : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;
    // URL dell'icona per il QML ("" se nulla).
    static QString url(const QIcon& icon);
};

// Crea un QQuickWidget con il provider delle icone, il percorso dei moduli QML accanto all'exe e lo sfondo dato.
QQuickWidget* makeQuickWidget(QWidget* parent, const QColor& background);

class ActionItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString shortText READ shortText NOTIFY changed)
    Q_PROPERTY(QString tooltip READ tooltip NOTIFY changed)
    Q_PROPERTY(QString icon READ icon NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool checkable READ checkable NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(bool hasMenu READ hasMenu NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)

public:
    explicit ActionItem(QAction* action, QObject* parent = nullptr);
    QString text() const;
    QString shortText() const;   // senza "a capo"
    QString tooltip() const;
    QString icon() const;
    bool enabled() const { return action_ && action_->isEnabled(); }
    bool checkable() const { return action_ && action_->isCheckable(); }
    bool checked() const { return action_ && action_->isChecked(); }
    bool hasMenu() const { return action_ && action_->menu(); }
    bool visible() const { return action_ && action_->isVisible(); }
    QAction* action() const { return action_; }

    Q_INVOKABLE void trigger();
    // Apre il menu dell'azione nel punto dello schermo indicato.
    Q_INVOKABLE void popup(int globalX, int globalY);

signals:
    void changed();

private:
    QPointer<QAction> action_;
    mutable QString iconUrl_;
    mutable qint64 iconKey_ = -1;
};

class ComboModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentText READ currentText NOTIFY itemsChanged)
    Q_PROPERTY(QString currentIcon READ currentIcon NOTIFY itemsChanged)
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY itemsChanged)
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(QString tooltip READ tooltip CONSTANT)
    Q_PROPERTY(int iconWidth READ iconWidth CONSTANT)

public:
    ComboModel(int width, QObject* parent = nullptr);

    // Interfaccia di QComboBox usata dalla finestra principale.
    void clear();
    void addItem(const QIcon& icon, const QString& text, const QVariant& data = {});
    void addItem(const QString& text, const QVariant& data = {}) { addItem(QIcon(), text, data); }
    void insertItem(int index, const QString& text, const QVariant& data = {});
    void setItemIcon(int index, const QIcon& icon);
    int count() const { return static_cast<int>(items_.size()); }
    QVariant itemData(int index) const;
    int findData(const QVariant& data) const;
    QString itemText(int index) const
    {
        return index >= 0 && index < count() ? items_[static_cast<std::size_t>(index)].text : QString();
    }
    // Primo elemento con questo testo (senza distinzione fra maiuscole e minuscole), oppure -1.
    int findText(const QString& text) const
    {
        for (int i = 0; i < count(); ++i)
            if (items_[static_cast<std::size_t>(i)].text.compare(text, Qt::CaseInsensitive) == 0)
                return i;
        return -1;
    }
    QVariant currentData() const { return itemData(current_); }
    int currentIndex() const { return current_; }
    void setCurrentIndex(int index);
    void setEnabled(bool on);
    bool isEnabled() const { return enabled_; }
    void setToolTip(const QString& tip) { tooltip_ = tip; }
    QString tooltip() const { return tooltip_; }
    int width() const { return width_; }
    void setIconWidth(int w) { iconWidth_ = w; }
    int iconWidth() const { return iconWidth_; }
    QString currentText() const;
    QString currentIcon() const;

    // Elenco dei layer: le prime `count` icone (18 px l'una) sono pulsanti che non chiudono l'elenco.
    void setIconSlots(int count) { slots_ = count; }

    // Apre l'elenco sotto il punto dello schermo indicato.
    Q_INVOKABLE void popup(int globalX, int globalY);

signals:
    void activated(int index);
    // Clic su un'icona di una riga dell'elenco (0 = prima icona...).
    void slotClicked(int index, int slot);
    void popupClosed();
    void itemsChanged();

private:
    struct Item {
        QIcon icon;
        QString text;
        QVariant data;
    };
    void notify();
    std::vector<Item> items_;
    int current_ = -1;
    bool enabled_ = true;
    int width_;
    int iconWidth_ = 16;
    int slots_ = 0;
    QString tooltip_;
    bool pending_ = false;
    QPointer<QMenu> menu_;
};

class TabsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    // Schede mostrate (le schede dei file si possono nascondere dalla scheda Schermo).
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
    explicit TabsModel(QObject* parent = nullptr) : QObject(parent) {}
    bool shown() const { return shown_; }
    void setShown(bool on)
    {
        if (shown_ != on) {
            shown_ = on;
            emit shownChanged();
        }
    }

    // Interfaccia di QTabBar usata dalla finestra principale.
    int addTab(const QString& text, bool closable = true);
    void removeTab(int index);
    void setTabText(int index, const QString& text);
    QString tabText(int index) const;
    void setTabData(int index, const QVariant& data);
    QVariant tabData(int index) const;
    int count() const { return static_cast<int>(tabs_.size()); }
    int currentIndex() const { return current_; }
    void setCurrentIndex(int index);

    QVariantList tabs() const;

    Q_INVOKABLE void select(int index)
    {
        emit clicked(index);   // anche sulla scheda già scelta (gruppi di layout: apre il menu)
        setCurrentIndex(index);
    }
    Q_INVOKABLE void requestClose(int index) { emit tabCloseRequested(index); }
    Q_INVOKABLE void requestMenu(int index, int globalX, int globalY) { emit menuRequested(index, QPoint(globalX, globalY)); }
    Q_INVOKABLE void requestNew() { emit newRequested(); }

signals:
    void shownChanged();
    void clicked(int index);
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void menuRequested(int index, QPoint globalPos);
    void newRequested();
    void tabsChanged();

private:
    bool shown_ = true;
    struct Tab {
        QString text;
        QVariant data;
        bool closable = true;
    };
    std::vector<Tab> tabs_;
    int current_ = -1;
};

} // namespace cartesio

namespace cartesio {

// Servizi per tutti i componenti QML (oggetto "ui"): suggerimenti nativi, che possono uscire dal componente.
class UiHelper : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    Q_INVOKABLE void showToolTip(int globalX, int globalY, const QString& text);
    Q_INVOKABLE void hideToolTip();
};

// Prepara il motore QML di un componente: icone, moduli accanto all'exe, oggetto "ui".
void setupQuickEngine(QQuickWidget* widget);

// Interfaccia tutta in QML (una sola scena, MainView.qml) oppure, con CARTESIO_UI=widgets, quella con i
// componenti QML separati in una finestra a widget. Nella scena unica le classi Ribbon, CommandLine,
// StatusBar, tavolozze e pagina Inizio restano i modelli dei loro componenti ma non si mostrano.
bool qmlRootUi();

} // namespace cartesio

namespace cartesio {

// Barra di stato in QML (qml/StatusBar.qml): schede Modello/layout, coordinate, MODELLO/CARTA, scala e blocco
// della finestra di layout, interruttori degli aiuti al disegno.
class StatusBar : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QObject* layoutTabs READ layoutTabsObject CONSTANT)
    Q_PROPERTY(bool tabsVisible READ tabsVisible NOTIFY changed)
    Q_PROPERTY(QString coordinates READ coordinates NOTIFY coordinatesChanged)
    Q_PROPERTY(QString spaceText READ spaceText NOTIFY changed)
    Q_PROPERTY(bool spaceEnabled READ spaceEnabled NOTIFY changed)
    Q_PROPERTY(bool viewportVisible READ viewportVisible NOTIFY changed)
    Q_PROPERTY(QString viewportScale READ viewportScale NOTIFY changed)
    Q_PROPERTY(bool viewportScaleEnabled READ viewportScaleEnabled NOTIFY changed)
    Q_PROPERTY(QString lockIcon READ lockIcon NOTIFY changed)
    Q_PROPERTY(QString lockTip READ lockTip NOTIFY changed)
    Q_PROPERTY(bool annoVisible READ annoVisible NOTIFY changed)
    Q_PROPERTY(QString annoScale READ annoScale NOTIFY changed)
    Q_PROPERTY(QVariantList toggles READ toggles CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString loadingText READ loadingText NOTIFY loadingChanged)

public:
    explicit StatusBar(QWidget* parent = nullptr);
    void setToggles(const QList<QAction*>& actions);

    TabsModel* layoutTabs() const { return layoutTabs_; }
    void setTabsVisible(bool on) { if (tabsVisible_ != on) { tabsVisible_ = on; emit changed(); } }
    void setCoordinates(const QString& text) { if (coordinates_ != text) { coordinates_ = text; emit coordinatesChanged(); } }
    void setSpace(const QString& text, bool enabled);
    void setViewport(bool visible, const QString& scale = {}, bool scaleEnabled = true, const QIcon& lock = {},
                     const QString& lockTip = {});

    bool tabsVisible() const { return tabsVisible_; }
    QString coordinates() const { return coordinates_; }
    QString spaceText() const { return spaceText_; }
    bool spaceEnabled() const { return spaceEnabled_; }
    bool viewportVisible() const { return viewportVisible_; }
    QString viewportScale() const { return viewportScale_; }
    bool viewportScaleEnabled() const { return viewportScaleEnabled_; }
    QString lockIcon() const { return lockIcon_; }
    QString lockTip() const { return lockTip_; }
    // Scala di annotazione (CANNOSCALE): si vede nello spazio modello.
    void setAnnotation(bool visible, const QString& scale = {})
    {
        if (annoVisible_ == visible && annoScale_ == scale)
            return;
        annoVisible_ = visible;
        annoScale_ = scale;
        emit changed();
    }
    bool annoVisible() const { return annoVisible_; }
    QString annoScale() const { return annoScale_; }
    Q_INVOKABLE void clickAnnoScale(int globalX, int globalY) { emit annoScaleClicked(QPoint(globalX, globalY)); }
    Q_INVOKABLE void clickLayoutList(int globalX, int globalY) { emit layoutListClicked(QPoint(globalX, globalY)); }
    QVariantList toggles() const { return toggles_; }
    // Apertura di un disegno in corso: fase e nome del file (vuoto = nessuna).
    void setLoading(const QString& text) { if (loadingText_ != text) { loadingText_ = text; emit loadingChanged(); } }
    bool loading() const { return !loadingText_.isEmpty(); }
    QString loadingText() const { return loadingText_; }
    Q_INVOKABLE void cancelLoading() { emit loadingCancelled(); }

    Q_INVOKABLE void clickSpace() { emit spaceClicked(); }
    Q_INVOKABLE void clickViewportScale(int globalX, int globalY) { emit viewportScaleClicked(QPoint(globalX, globalY)); }
    Q_INVOKABLE void clickLock() { emit lockClicked(); }

signals:
    void changed();
    void coordinatesChanged();
    void spaceClicked();
    void viewportScaleClicked(QPoint globalPos);
    void lockClicked();
    void annoScaleClicked(QPoint globalPos);
    void layoutListClicked(QPoint globalPos);
    void loadingChanged();
    void loadingCancelled();

private:
    QObject* layoutTabsObject() const { return layoutTabs_; }
    TabsModel* layoutTabs_;
    bool tabsVisible_ = true;
    QString coordinates_;
    QString spaceText_;
    bool spaceEnabled_ = false;
    bool viewportVisible_ = false;
    QString viewportScale_;
    bool viewportScaleEnabled_ = true;
    QString lockIcon_;
    QString lockTip_;
    bool annoVisible_ = false;
    QString annoScale_;
    QVariantList toggles_;
    QString loadingText_;
};

} // namespace cartesio
