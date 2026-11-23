#pragma once

// Barra multifunzione, disegnata in QML (qml/Ribbon.qml): barra di accesso rapido con il
// pulsante dell'applicazione e il titolo, schede e pannelli con pulsanti grandi, colonne di pulsanti piccoli e
// colonne di elenchi a discesa (layer, colore, tipo e spessore di linea). I pulsanti sono le QAction della
// finestra principale (testo, icona, stato e menu restano sincronizzati).

#include "app/QmlUi.h"

#include <QList>
#include <QString>
#include <QVariant>

#include <initializer_list>
#include <vector>

namespace cartesio {

class RibbonPanel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY changed)
    Q_PROPERTY(int priority READ priority CONSTANT)
    // Icona del pannello chiuso (quella del suo primo comando).
    Q_PROPERTY(QString icon READ iconUrl NOTIFY changed)

public:
    RibbonPanel(const QString& title, QObject* parent) : QObject(parent), title_(title) {}

    // Pulsante grande: icona 32 px con il testo sotto.
    void addLargeButton(QAction* action);
    // Colonna di pulsanti piccoli (fino a 3): icona 16 px e testo a destra.
    void addSmallColumn(std::initializer_list<QAction*> actions, bool showText = true);
    // Colonna di elenchi a discesa, con sotto (facoltative) file di pulsanti piccoli senza testo.
    void addComboColumn(std::initializer_list<ComboModel*> combos, std::vector<std::vector<QAction*>> rows = {});

    // Priorità: i pannelli con priorità più alta si comprimono per ultimi.
    void setPriority(int priority) { priority_ = priority; }
    int priority() const { return priority_; }
    QString title() const { return title_; }
    QVariantList columns() const { return columns_; }
    QString iconUrl() const;
    // Pannello chiuso (non c'è posto nella barra): un menu con tutti i suoi comandi e i suoi elenchi.
    Q_INVOKABLE void popup(int globalX, int globalY);
    // Pulsanti del pannello per colonna (anche quelli delle colonne con gli elenchi).
    const std::vector<std::vector<QAction*>>& actionColumns() const { return actionColumns_; }

signals:
    void changed();

private:
    QObject* item(QAction* action);
    std::vector<std::vector<QAction*>> actionColumns_;   // per il menu del pannello chiuso
    std::vector<std::vector<ComboModel*>> comboColumns_;
    QString title_;
    QVariantList columns_;
    int priority_ = 0;
};

class Ribbon : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY changed)
    Q_PROPERTY(QObject* appAction READ appAction NOTIFY changed)
    Q_PROPERTY(QVariantList quickActions READ quickActions NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    // Scheda da selezionare (una contestuale appena mostrata, o quella di prima quando si nasconde).
    Q_PROPERTY(int selectedTab READ selectedTab NOTIFY selectedTabChanged)
    // Pulsante evidenziato dai percorsi guidati (testo del pulsante, vuoto = nessuno).
    Q_PROPERTY(QString highlight READ highlight NOTIFY highlightChanged)

public:
    explicit Ribbon(QWidget* parent = nullptr);

    // Crea il pannello nella scheda indicata (la scheda viene creata se non esiste).
    RibbonPanel* addPanel(const QString& tab, const QString& title);
    // Pulsante dell'applicazione (con il suo menu) e pulsanti della barra di accesso rapido.
    void setQuickAccess(QAction* app, const QList<QAction*>& actions);
    void setTitle(const QString& title);
    // Scheda contestuale (es. "Editor di testo"): nascosta finché non serve; mostrata, diventa quella selezionata,
    // nascosta torna la scheda di prima.
    void setContextual(const QString& tab);
    // Ordine delle schede e, per ognuna, dei suoi pannelli (quelli non elencati restano in fondo, nell'ordine in cui
    // sono stati creati).
    void arrange(const QStringList& tabs, const QList<QPair<QString, QStringList>>& panels);
    void showContextual(const QString& tab, bool on, bool select = true);
    int selectedTab() const { return selectedTab_; }
    // Seleziona la scheda con questo nome (prove senza schermo).
    void selectTab(const QString& title)
    {
        const int index = visibleIndex(title);
        if (index < 0)
            return;
        selectedTab_ = currentShown_ = index;
        lastUserTab_ = title;
        emit selectedTabChanged();
    }
    // Scheda scelta dall'utente (dal QML): serve a tornarci quando si chiude quella contestuale.
    Q_INVOKABLE void tabClicked(int index)
    {
        lastUserTab_ = visibleTitle(index);
        currentShown_ = index;
    }

    QVariantList tabs() const;
    // Struttura della barra: una riga "Scheda > Pannello > Pulsante [COMANDO]" per pulsante e voce di menu
    // (manuale e guida).
    QStringList describe() const;
    // Evidenzia il pulsante con questo testo (o quello che lo ha nel suo menu) e seleziona la sua scheda;
    // vuoto = nessuno. False se non c'è.
    bool setHighlight(const QString& label);
    QString highlight() const { return highlight_; }
    // Pulsante "? Guida" in fondo alla riga delle schede.
    Q_INVOKABLE void requestHelp() { emit helpRequested(); }
    // Titolo della scheda selezionata ora.
    QString currentTabTitle() const { return visibleTitle(currentShown_); }
    QObject* appAction() const { return app_; }
    QVariantList quickActions() const { return quick_; }
    QString title() const { return title_; }

signals:
    void changed();
    void titleChanged();
    void selectedTabChanged();
    void highlightChanged();
    void helpRequested();

private:
    struct Tab {
        QString title;
        QList<RibbonPanel*> panels;
        bool contextual = false;
        bool shown = true;
    };
    int visibleIndex(const QString& title) const;
    QString visibleTitle(int index) const;
    int selectedTab_ = 0;
    int currentShown_ = 0;   // scheda selezionata ora (indice fra quelle mostrate)
    QString lastUserTab_;
    std::vector<Tab> tabs_;
    QObject* app_ = nullptr;
    QVariantList quick_;
    QString title_;
    QString highlight_;
};

} // namespace cartesio
