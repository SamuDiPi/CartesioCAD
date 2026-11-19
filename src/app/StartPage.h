#pragma once

#include <QDateTime>
#include <QHash>
#include <QQuickWidget>
#include <QSet>
#include <QStringList>
#include <QVariant>

class QThreadPool;

namespace cartesio {

// Cartella dei modelli (.dwt/.dwg/.dxf) proposta per i disegni nuovi: quella delle Opzioni o, se vuota,
// "Modelli" nei dati del programma (mai una cartella di rete per default).
QString defaultTemplatesFolder();
QString templatesFolder();
QStringList templateFiles();

// Scheda "Inizia" in QML (qml/StartPage.qml): a sinistra "Inizia disegno"
// con l'elenco dei modelli e i comandi di apertura, al centro i documenti recenti (miniature, bloccati,
// raggruppati per data, tre modi di vista), a destra i collegamenti; in basso le schede CREA e APPRENDI.
class StartPage : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    Q_PROPERTY(QVariantMap thumbs READ thumbs NOTIFY thumbsChanged)
    Q_PROPERTY(QString templateName READ templateName NOTIFY templateChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit StartPage(QWidget* parent = nullptr);
    ~StartPage() override;

    // Documenti recenti (il primo è il più recente), quelli bloccati e l'ora dell'ultima apertura di ognuno.
    void setRecentFiles(const QStringList& files, const QStringList& pinned = {},
                        const QHash<QString, QDateTime>& opened = {});

    QVariantList recent() const { return recent_; }
    QVariantMap thumbs() const { return thumbs_; }
    QString templateName() const;
    // Modello scelto (percorso; vuoto = nessun modello).
    QString templatePath() const;
    int viewMode() const { return viewMode_; }
    void setViewMode(int mode);
    int page() const { return page_; }
    void setPage(int page);
    QString version() const;

    Q_INVOKABLE void requestNew();
    Q_INVOKABLE void requestOpen() { emit openRequested(); }
    Q_INVOKABLE void requestOpenMany() { emit openManyRequested(); }
    Q_INVOKABLE void requestRecent(const QString& path) { emit recentRequested(path); }
    Q_INVOKABLE void togglePin(const QString& path) { emit pinToggled(path); }
    // Menu dei modelli sotto "Inizia disegno" (coordinate globali).
    Q_INVOKABLE void popupTemplates(int x, int y, int width);
    // Menu del tasto destro su un documento recente.
    Q_INVOKABLE void popupRecentMenu(const QString& path, int x, int y);
    Q_INVOKABLE void openTemplatesFolder();
    Q_INVOKABLE void openLink(const QString& url);
    Q_INVOKABLE void runCommand(const QString& name) { emit commandRequested(name); }

signals:
    void newRequested();
    void templateRequested(const QString& path);
    void openRequested();
    void openManyRequested();
    void recentRequested(const QString& path);
    void pinToggled(const QString& path);
    void removeRequested(const QString& path);
    void commandRequested(const QString& name);
    void recentChanged();
    void thumbsChanged();
    void templateChanged();
    void viewModeChanged();
    void pageChanged();

private:
    void loadThumbnail(const QString& path);

    QVariantList recent_;
    QVariantMap thumbs_;
    QSet<QString> loading_;
    QThreadPool* pool_ = nullptr;
    int viewMode_ = 0;
    int page_ = 0;
};

} // namespace cartesio
