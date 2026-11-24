#pragma once

// Superficie della scheda CanaloCAD: una finestra nativa con dentro la pagina di CanaloCAD (WebView2Host). Nella
// scena unica in QML sta sopra la vista principale, nell'area che riporta MainView.qml; con l'interfaccia a widget è
// la pagina dello stack dei disegni.

#include <QWidget>

class QUrl;

namespace cartesio {

class WebView2Host;

class CanaloTab : public QWidget {
    Q_OBJECT

public:
    explicit CanaloTab(QWidget* parent = nullptr);
    WebView2Host* host() const { return host_; }
    void start(const QUrl& page, const QString& dataFolder);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private:
    void updateBounds();
    WebView2Host* host_ = nullptr;
};

} // namespace cartesio
