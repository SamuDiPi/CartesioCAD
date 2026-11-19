#pragma once

// Tavolozza Guida (comando GUIDA, F1), disegnata in QML (qml/GuidePanel.qml). Si apre solo a richiesta.
//  - Percorsi guidati (guide/percorsi.json): un passo alla volta; la finestra principale evidenzia il pulsante del
//    passo e controlla la sua condizione (oggetti nuovi, selezione, comando finito, scheda, tavolozza...): quando è
//    soddisfatta il passo è fatto e si passa al successivo.
//  - Manuale (docs/manuale/*.md nelle risorse): indice per capitoli, ricerca, pagina di un comando (F1 durante un
//    comando).

#include <QJsonArray>
#include <QJsonObject>
#include <QQuickWidget>
#include <QTimer>
#include <QVariant>

#include <vector>

namespace cartesio {

class GuidePanel : public QQuickWidget {
    Q_OBJECT
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    // Percorsi.
    Q_PROPERTY(QVariantList tours READ tours CONSTANT)
    Q_PROPERTY(int tour READ tour NOTIFY tourChanged)
    Q_PROPERTY(int step READ step NOTIFY tourChanged)
    Q_PROPERTY(int stepCount READ stepCount NOTIFY tourChanged)
    Q_PROPERTY(QString tourTitle READ tourTitle NOTIFY tourChanged)
    Q_PROPERTY(QString stepText READ stepText NOTIFY tourChanged)
    Q_PROPERTY(bool stepDone READ stepDone NOTIFY tourChanged)
    Q_PROPERTY(bool stepManual READ stepManual NOTIFY tourChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY tourChanged)
    // Manuale.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY manualChanged)
    Q_PROPERTY(QString query READ query NOTIFY manualChanged)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY manualChanged)
    Q_PROPERTY(QString pageText READ pageText NOTIFY manualChanged)

public:
    explicit GuidePanel(QWidget* parent = nullptr);

    int page() const { return page_; }
    void setPage(int page);

    QVariantList tours() const { return toursList_; }
    int tour() const { return tour_; }
    int step() const { return step_; }
    int stepCount() const;
    QString tourTitle() const;
    QString stepText() const;
    bool stepDone() const { return stepDone_; }
    bool stepManual() const;
    bool finished() const { return finished_; }
    // Il passo corrente (per la finestra principale: pulsante e condizione).
    QJsonObject currentStep() const;
    // La condizione del passo è soddisfatta: il passo è fatto, dopo un attimo si passa al successivo.
    void markDone();

    QVariantList sections() const { return shown_; }
    QString query() const { return query_; }
    QString pageTitle() const { return pageTitle_; }
    QString pageText() const { return pageText_; }
    // Pagina del manuale di un comando (o di un alias); false se non c'è.
    bool openCommand(const QString& command);

    // Dal QML.
    Q_INVOKABLE void startTour(int index);
    Q_INVOKABLE void next();
    Q_INVOKABLE void back();
    Q_INVOKABLE void stopTour();
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void openSection(int index);
    Q_INVOKABLE void closeSection();

signals:
    void pageChanged();
    void tourChanged();
    void manualChanged();
    // Passo nuovo (o percorso finito, con step < 0): la finestra principale evidenzia il pulsante e fotografa lo
    // stato da cui misurare la condizione.
    void stepEntered();

private:
    struct Section {
        QString chapter, title, markdown, plain;
        QStringList commands;   // comandi e alias nel titolo (tra apici inversi)
    };
    void loadTours();
    void loadManual();
    void enterStep(int index);

    int page_ = 0;
    QJsonArray toursJson_;
    QVariantList toursList_;
    int tour_ = -1;
    int step_ = 0;
    bool stepDone_ = false;
    bool finished_ = false;
    QTimer advance_;

    std::vector<Section> manual_;
    QVariantList shown_;
    std::vector<int> shownIndex_;
    QString query_;
    QString pageTitle_, pageText_;
};

} // namespace cartesio
