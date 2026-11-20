#pragma once

// Finestra principale tutta in QML (qml/MainView.qml): una sola scena con barra multifunzione, schede dei file,
// tavolozze laterali, disegno (CadCanvas), riga di comando sovrapposta e barra di stato. Un solo QQuickWidget
// (una sola scena e un solo fotogramma) al posto dei componenti QML separati.
//
// L'oggetto "main" della scena: la vista del disegno corrente, la pagina Inizio, i pannelli e la riga di comando.

#include <QObject>
#include "app/CadView.h"

#include <QPointer>
#include <QQuickWidget>
#include <QImage>
#include <QRect>

#include <functional>
#include <map>

namespace cartesio {

class CadView;

class MainView : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QObject* view READ viewObject NOTIFY viewChanged)
    // Finestre del modello affiancate: le viste del disegno e la disposizione (0 una, 1 due verticali,
    // 2 due orizzontali, 3 tre, 4 quattro).
    Q_PROPERTY(QVariantList views READ views NOTIFY viewChanged)
    Q_PROPERTY(int tileMode READ tileMode NOTIFY viewChanged)
    Q_PROPERTY(bool startVisible READ startVisible NOTIFY viewChanged)
    Q_PROPERTY(bool commandLineVisible READ commandLineVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool propertiesVisible READ propertiesVisible WRITE setPropertiesVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool layersVisible READ layersVisible WRITE setLayersVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool circuitsVisible READ circuitsVisible WRITE setCircuitsVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool libraryVisible READ libraryVisible WRITE setLibraryVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool plantVisible READ plantVisible WRITE setPlantVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool guideVisible READ guideVisible WRITE setGuideVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool canaloVisible READ canaloVisible WRITE setCanaloVisible NOTIFY panelsChanged)
    Q_PROPERTY(int leftWidth READ leftWidth WRITE setLeftWidth NOTIFY panelsChanged)
    Q_PROPERTY(int rightWidth READ rightWidth WRITE setRightWidth NOTIFY panelsChanged)

public:
    enum Panel { Properties, Layers, Circuits, Library, Plant, Guide, Canalo };

    // `models`: oggetti dei componenti (ribbon, tabs, cmd, sbar, props, layers, panel, start).
    MainView(const std::map<QString, QObject*>& models, QWidget* parent = nullptr);

    void setView(CadView* view, bool start);
    void setViews(const std::vector<CadView*>& views, int tileMode);
    QVariantList views() const { return views_; }
    int tileMode() const { return tileMode_; }
    CadView* view() const { return view_; }
    QObject* viewObject() const;
    bool startVisible() const { return start_; }

    void setCommandLineVisible(bool on);
    bool commandLineVisible() const { return commandLine_; }
    void setPanelVisible(Panel panel, bool on);
    bool panelVisible(Panel panel) const;
    bool propertiesVisible() const { return panelVisible(Properties); }
    bool layersVisible() const { return panelVisible(Layers); }
    bool circuitsVisible() const { return panelVisible(Circuits); }
    void setPropertiesVisible(bool on) { setPanelVisible(Properties, on); }
    void setLayersVisible(bool on) { setPanelVisible(Layers, on); }
    void setCircuitsVisible(bool on) { setPanelVisible(Circuits, on); }
    bool libraryVisible() const { return panelVisible(Library); }
    void setLibraryVisible(bool on) { setPanelVisible(Library, on); }
    bool plantVisible() const { return panelVisible(Plant); }
    void setPlantVisible(bool on) { setPanelVisible(Plant, on); }
    bool guideVisible() const { return panelVisible(Guide); }
    void setGuideVisible(bool on) { setPanelVisible(Guide, on); }
    bool canaloVisible() const { return panelVisible(Canalo); }
    void setCanaloVisible(bool on) { setPanelVisible(Canalo, on); }
    int leftWidth() const { return leftWidth_; }
    int rightWidth() const { return rightWidth_; }
    void setLeftWidth(int w);
    void setRightWidth(int w);

    // Barra della riga di comando nelle coordinate della vista (per le righe di cronologia sopra di essa).
    QRect commandBar() const { return commandBar_; }
    Q_INVOKABLE void setCommandBar(int x, int y, int w, int h);

    // Fuoco al campo della riga di comando.
    void focusCommandLine();

    // Diagnosi senza schermo: la scena principale con Direct3D 11 (il disegno su GPU, come a schermo).
    // `frames` fotogrammi in più dopo i primi due, ognuno preceduto da step(i): msPerFrame riceve il tempo medio
    // (prove automatiche della reattività).
    QImage renderOffscreenGpu(QSize size, QString* error = nullptr, const std::function<void(int)>& step = {},
                              int frames = 0, double* msPerFrame = nullptr);

    // Stato dei pannelli fra una sessione e l'altra.
    QByteArray saveState() const;
    void restoreState(const QByteArray& state);

signals:
    void viewChanged();
    void panelsChanged();
    void commandBarChanged();
    void panelVisibilityChanged(int panel, bool visible);
    void focusCommandLineRequested();

private:
    QPointer<CadView> view_;
    QVariantList views_;
    int tileMode_ = 0;
    bool start_ = true;
    bool commandLine_ = true;
    bool panels_[7] = {false, false, false, false, false, false, false};
    int leftWidth_ = 320;
    int rightWidth_ = 440;
    QRect commandBar_;
};

} // namespace cartesio
