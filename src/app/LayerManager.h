#pragma once

#include <QQuickWidget>
#include <QVariant>

namespace cartesio {

class DrawingWidget;
class ViewportEntity;

// Tavolozza "Gestore proprietà layer", disegnata in QML (qml/LayerManager.qml): crea, elimina, rinomina i layer
// e ne cambia stato (acceso, congelato, congelato nella finestra, bloccato), colore, tipo e spessore di linea.
class LayerManager : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedRowChanged)
    Q_PROPERTY(bool hasDrawing READ hasDrawing NOTIFY rowsChanged)
    Q_PROPERTY(bool inViewport READ inViewport NOTIFY rowsChanged)
    Q_PROPERTY(QVariantMap icons READ icons CONSTANT)

public:
    explicit LayerManager(QWidget* parent = nullptr);

    void setDrawing(DrawingWidget* drawing);
    void refresh();

    QVariantList rows() const { return rows_; }
    int selectedRow() const { return selected_; }
    bool hasDrawing() const { return drawing_ != nullptr; }
    bool inViewport() const { return activeViewport() != nullptr; }
    QVariantMap icons() const;

    // Dal QML.
    Q_INVOKABLE void select(int row);
    // Colonne con un'icona: "on", "freeze", "vpfreeze", "lock".
    Q_INVOKABLE void toggle(int row, const QString& column);
    Q_INVOKABLE void pickColor(int row);
    Q_INVOKABLE void popupLinetype(int row, int globalX, int globalY);
    Q_INVOKABLE void popupLineweight(int row, int globalX, int globalY);
    Q_INVOKABLE void rename(int row, const QString& name);
    Q_INVOKABLE void newLayer();
    Q_INVOKABLE void deleteLayer();
    Q_INVOKABLE void makeCurrent();

signals:
    void rowsChanged();
    void selectedRowChanged();
    // Un layer nuovo da rinominare subito (il QML apre la modifica del nome).
    void editNameRequested(int row);

private:
    void rebuild();
    void changed();
    const ViewportEntity* activeViewport() const;

    DrawingWidget* drawing_ = nullptr;
    QVariantList rows_;
    int selected_ = -1;
    bool pending_ = false;
};

} // namespace cartesio
