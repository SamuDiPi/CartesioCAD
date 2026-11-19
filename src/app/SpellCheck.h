#pragma once

// Controllo ortografico con il correttore di Windows (lo stesso di Office ed Edge): italiano se c'è, altrimenti la
// lingua del sistema. Senza correttore (altri sistemi, Windows senza la lingua) non segnala niente.

#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace cartesio {

class SpellChecker {
public:
    static SpellChecker& instance();
    bool available() const { return checker_ != nullptr; }
    QString language() const { return language_; }
    // Parole sbagliate nel testo: inizio e lunghezza (in caratteri).
    std::vector<std::pair<int, int>> errors(const QString& text) const;
    QStringList suggestions(const QString& word) const;
    // Aggiunge la parola al dizionario dell'utente (vale per tutti i programmi che usano il correttore).
    void addWord(const QString& word);
    // Ignora la parola fino alla chiusura del programma.
    void ignoreWord(const QString& word);

    // Controllo attivo (scheda Editor di testo, Controllo ortografia).
    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

private:
    SpellChecker();
    ~SpellChecker();
    void* checker_ = nullptr;   // ISpellChecker*
    QString language_;
    bool enabled_ = true;
};

} // namespace cartesio
