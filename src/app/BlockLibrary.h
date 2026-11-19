#pragma once

// Tavolozza Blocchi (libreria simboli), in QML
// (qml/BlockLibrary.qml): blocchi del disegno aperto, di un file libreria (DWG/DXF) o di una cartella (ogni file
// è un simbolo), e gli ultimi inseriti. Un clic avvia INSERT; un blocco di un altro file entra nel disegno con i
// suoi layer e i blocchi annidati.

#include <QHash>
#include <QPointer>
#include <QQuickWidget>
#include <QStringList>
#include <QVariant>

#include <memory>

class QThreadPool;

namespace cartesio {

class Document;
class DrawingWidget;
struct BlockDef;

// Anteprima di un blocco (linee, archi, testi, riempimenti, blocchi annidati) su fondo scuro.
QImage renderBlockPreview(const Document& doc, const BlockDef& def, int size);

class BlockLibrary : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
    enum Tab { Current = 0, Library = 1, Recent = 2 };

    explicit BlockLibrary(QWidget* parent = nullptr);
    ~BlockLibrary() override;

    void setDrawing(DrawingWidget* drawing);
    // Da chiamare quando il disegno cambia: si rifà l'elenco solo se cambiano i blocchi.
    void refresh();

    int tab() const { return tab_; }
    void setTab(int tab);
    QVariantList items() const { return items_; }
    QString filter() const { return filter_; }
    void setFilter(const QString& text);
    QString sourceText() const;
    QString statusText() const { return status_; }

    // File (DWG/DXF) o cartella della libreria; vuoto = nessuna. Si ricorda fra le sessioni.
    QString librarySource() const { return source_; }
    void setLibrarySource(const QString& path);
    // Disegno della libreria già letto (nullptr se `path` non è quello caricato).
    std::shared_ptr<const Document> loadedDocument(const QString& path) const;

    Q_INVOKABLE void activate(int index);
    // Dati del trascinamento di un simbolo sul disegno: "tipo\tsorgente\tnome".
    Q_INVOKABLE QString dragPayload(int index) const;
    Q_INVOKABLE void popupMenu(int index, int x, int y);
    Q_INVOKABLE void chooseFolder();
    Q_INVOKABLE void chooseFile();
    // Nome e sorgente di un inserimento appena fatto, per i Recenti.
    void addRecent(const QString& source, const QString& name);

signals:
    void changed();
    void itemsChanged();
    // `source` vuoto = blocco del disegno aperto; altrimenti il file della libreria (già letto).
    void insertBlockRequested(const QString& source, const QString& name);
    void insertFileRequested(const QString& path);
    void editBlockRequested(const QString& name);
    void openFileRequested(const QString& path);
    void message(const QString& text);

private:
    void rebuild();
    void loadSource(const QString& path, const QString& thenInsert = {});
    QString previewFor(const QString& source, const Document& doc, const BlockDef& def);
    void loadFileThumbnail(const QString& path);
    QVariantMap item(const QString& kind, const QString& name, const QString& source, const QString& image) const;

    QPointer<DrawingWidget> drawing_;
    int tab_ = Current;
    QString filter_;
    QString status_;
    QString source_;
    QVariantList items_;
    quint64 signature_ = 0;
    std::shared_ptr<const Document> sourceDoc_;
    QString sourceDocPath_;
    bool loading_ = false;
    QHash<QString, QString> previews_;      // chiave -> url dell'anteprima
    QHash<QString, QString> fileThumbs_;    // file -> url della miniatura
    QStringList recent_;                    // "sorgente\tnome" (nome vuoto = file intero)
    QThreadPool* pool_ = nullptr;
};

} // namespace cartesio
