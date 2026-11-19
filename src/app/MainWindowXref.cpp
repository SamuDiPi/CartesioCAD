// Riferimenti esterni (XRIF): disegni collegati e immagini
// raster, con stato e percorso; cambia percorso, ricarica, apri, stacca; collega un disegno o un'immagine.
#include "app/MainWindow.h"

#include "app/DrawingWidget.h"
#include "app/ExternalRefs.h"
#include "app/AppOptions.h"
#include "app/FormDialog.h"
#include "app/ImageLibrary.h"
#include "core/Document.h"
#include "core/Import.h"
#include "core/commands/Command.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>

#include <algorithm>
#include <cmath>

namespace cartesio {

namespace {

// IMAGEATTACH: punto di inserimento (angolo in basso a sinistra), poi larghezza nel disegno.
class PlaceImageCommand : public Command {
public:
    PlaceImageCommand(std::string path, QSize pixels) : path_(std::move(path)), pixels_(pixels) {}

    std::string prompt() const override
    {
        if (!corner_)
            return "Punto di inserimento:";
        return "Larghezza dell'immagine <" + std::to_string(pixels_.width()) + ">:";
    }

    CommandStatus onPoint(CommandContext& ctx, Vec2 p) override
    {
        if (!corner_) {
            corner_ = p;
            return CommandStatus::Continue;
        }
        // Secondo punto: la larghezza è la distanza orizzontale (con il segno non conta).
        return place(ctx, std::max(1e-9, std::abs(p.x - corner_->x)));
    }

    CommandStatus onValue(CommandContext& ctx, double v) override
    {
        if (!corner_ || v <= 0.0) {
            ctx.message("Valore non valido.");
            return CommandStatus::Continue;
        }
        return place(ctx, v);
    }

    CommandStatus onEnter(CommandContext& ctx) override
    {
        if (!corner_)
            return CommandStatus::Finished;
        return place(ctx, pixels_.width());
    }

    std::vector<std::unique_ptr<Entity>> preview(Vec2 cursor) const override
    {
        std::vector<std::unique_ptr<Entity>> out;
        if (corner_)
            out.push_back(make(*corner_, std::max(1e-9, std::abs(cursor.x - corner_->x))));
        else
            out.push_back(make(cursor, pixels_.width()));
        return out;
    }

private:
    std::unique_ptr<ImageEntity> make(Vec2 at, double width) const
    {
        auto image = std::make_unique<ImageEntity>();
        image->insertion = at;
        image->width = pixels_.width();
        image->height = pixels_.height();
        const double unit = width / pixels_.width();
        image->u = {unit, 0.0};
        image->v = {0.0, unit};
        image->path = path_;
        return image;
    }

    CommandStatus place(CommandContext& ctx, double width)
    {
        std::vector<std::unique_ptr<Entity>> added;
        added.push_back(make(*corner_, width));
        ctx.document.commit(std::move(added));
        return CommandStatus::Finished;
    }

    std::string path_;
    QSize pixels_;
    std::optional<Vec2> corner_;
};

} // namespace

// Collega un'immagine raster (IMAGEATTACH) e la posiziona.
std::optional<int> MainWindow::askRefPathType(const QString& file)
{
    int type = appOptions().refPathType;
    FormDialog ask(tr("Tipo di percorso"), FormDialog::Ok | FormDialog::Cancel, this);
    ask.note(tr("File: <b>%1</b>").arg(QDir::toNativeSeparators(file).toHtmlEscaped()));
    ask.choice(tr("Percorso salvato nel disegno:"),
               {{refPathTypeName(RefPathRelative), RefPathRelative},
                {refPathTypeName(RefPathFull), RefPathFull},
                {refPathTypeName(RefPathNone), RefPathNone}},
               type, [&type](const QVariant& v) { type = v.toInt(); });
    ask.note(tr("Relativo: il disegno e i file collegati si possono spostare insieme (es. su un altro disco). Completo: "
                "il file resta dov'è. Nessun percorso: si cerca per nome nella cartella del disegno e in quelle delle "
                "Opzioni."));
    ask.setSize(520, 250);
    if (ask.exec() != QDialog::Accepted)
        return std::nullopt;
    return type;
}

bool MainWindow::attachImage(const QString& path, int pathType)
{
    DrawingWidget* d = current();
    if (!d || path.isEmpty())
        return false;
    QImageReader reader(path);
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty()) {
        print(tr("Immagine non leggibile: %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const int type = pathType >= 0 ? pathType : appOptions().refPathType;
    d->startCommand(std::make_unique<PlaceImageCommand>(
                        refPath(QFileInfo(path).absoluteFilePath(), QString::fromStdString(d->document().filePath()), type),
                        size),
                    QStringLiteral("IMAGEATTACH"));
    return true;
}

void MainWindow::showExternalReferences()
{
    DrawingWidget* d = current();
    if (!d)
        return;
    Document& doc = d->document();
    FormDialog dialog(tr("Riferimenti esterni"), FormDialog::Close, this);

    std::vector<ExternalRef> refs;
    int selected = 0;
    int listField = -1, detailsField = -1;
    const auto label = [](const ExternalRef& r) {
        return QStringLiteral("%1   %2   —   %3")
            .arg(r.kind == ExternalRef::Drawing ? tr("[DWG]") : tr("[Immagine]"), r.name, externalRefStatus(r));
    };
    const auto current = [&]() -> const ExternalRef* {
        return selected >= 0 && selected < static_cast<int>(refs.size()) ? &refs[static_cast<std::size_t>(selected)] : nullptr;
    };
    const auto showDetails = [&] {
        const ExternalRef* r = current();
        if (!r) {
            dialog.setFieldText(detailsField, refs.empty() ? tr("Il disegno non ha riferimenti esterni: si possono collegare "
                                                                "disegni DWG/DXF e immagini con i pulsanti qui sopra.")
                                                           : QString());
            return;
        }
        QString text = tr("<b>Nome:</b> %1<br><b>Tipo:</b> %2<br><b>Stato:</b> %3<br><b>Percorso salvato:</b> %4<br>"
                          "<b>Trovato in:</b> %5<br><b>Usato:</b> %6 volte<br><b>Tipo di percorso:</b> %7")
                           .arg(r->name.toHtmlEscaped(),
                                r->kind == ExternalRef::Drawing
                                    ? (r->overlay ? tr("disegno in sovrapposizione") : tr("disegno collegato"))
                                    : tr("immagine raster"),
                                externalRefStatus(*r),
                                QDir::toNativeSeparators(QString::fromStdString(r->saved)).toHtmlEscaped(),
                                r->found.isEmpty() ? tr("—") : QDir::toNativeSeparators(r->found).toHtmlEscaped())
                           .arg(r->uses)
                           .arg(refPathTypeName(refPathTypeOf(r->saved)));
        if (r->found.isEmpty())
            text += tr("<br><br>Il file non è al percorso salvato né nella cartella del disegno (e nelle sue sottocartelle) "
                       "né nelle cartelle delle Opzioni. Con <b>Cambia percorso...</b> si indica dove si trova.");
        dialog.setFieldText(detailsField, text);
    };
    const auto refill = [&](int select) {
        refs = externalReferences(doc);
        QStringList items;
        for (const ExternalRef& r : refs)
            items << label(r);
        selected = refs.empty() ? -1 : std::clamp(select, 0, static_cast<int>(refs.size()) - 1);
        dialog.setItems(listField, items, selected);
        showDetails();
    };
    // Ricarica i riferimenti a disegni non caricati (o appena cambiati) e ridisegna.
    const auto reload = [&] {
        DxfReadResult result;
        DrawingWidget::loadXrefs(QString::fromStdString(doc.filePath()), doc, result);
        for (const std::string& note : result.notes)
            print(QString::fromStdString(note));
        ImageLibrary::instance().reset();
        doc.touch();
        d->notifyChanged();
    };

    dialog.group(tr("Riferimenti del disegno"));
    listField = dialog.list({}, 0, [&](int index) {
        selected = index;
        showDetails();
    }, [&](int) {
        if (const ExternalRef* r = current(); r && !r->found.isEmpty() && r->kind == ExternalRef::Drawing) {
            const QString file = r->found;
            dialog.accept();
            openFile(file);
        }
    });
    dialog.buttonsRow({
        {tr("Cambia percorso..."), [&] {
             const ExternalRef* r = current();
             if (!r)
                 return;
             const QString filter = r->kind == ExternalRef::Drawing
                                        ? tr("Disegni (*.dwg *.dxf)")
                                        : tr("Immagini (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)");
             const QString start = r->found.isEmpty() ? QFileInfo(QString::fromStdString(doc.filePath())).absolutePath() : r->found;
             const QString file = QFileDialog::getOpenFileName(&dialog, tr("Nuovo percorso di %1").arg(r->name), start, filter);
             if (file.isEmpty())
                 return;
             setExternalRefPath(doc, *r, QDir::toNativeSeparators(file).toStdString());
             const int keep = selected;
             reload();
             refill(keep);
         }},
        {tr("Tipo di percorso..."), [&] {
             const ExternalRef* r = current();
             if (!r)
                 return;
             if (r->found.isEmpty()) {
                 QMessageBox::information(&dialog, dialog.windowTitle(),
                                          tr("Il file non è stato trovato: indicarlo prima con Cambia percorso..."));
                 return;
             }
             int type = refPathTypeOf(r->saved);
             FormDialog ask(tr("Tipo di percorso"), FormDialog::Ok | FormDialog::Cancel, &dialog);
             ask.choice(tr("Percorso salvato nel disegno:"),
                        {{refPathTypeName(RefPathRelative), RefPathRelative},
                         {refPathTypeName(RefPathFull), RefPathFull},
                         {refPathTypeName(RefPathNone), RefPathNone}},
                        type, [&type](const QVariant& v) { type = v.toInt(); });
             ask.setSize(480, 150);
             if (ask.exec() != QDialog::Accepted)
                 return;
             if (type == RefPathRelative && doc.filePath().empty()) {
                 QMessageBox::information(&dialog, dialog.windowTitle(),
                                          tr("Per un percorso relativo il disegno va prima salvato."));
                 return;
             }
             setExternalRefPathType(doc, *r, type);
             d->notifyChanged();
             const int keep = selected;
             refill(keep);
         }},
        {tr("Ricarica"), [&] {
             const ExternalRef* r = current();
             if (r && r->kind == ExternalRef::Drawing)
                 setExternalRefPath(doc, *r, r->saved);   // svuota il blocco: si rilegge il file
             const int keep = selected;
             reload();
             refill(keep);
         }},
        {tr("Apri"), [&] {
             const ExternalRef* r = current();
             if (!r || r->found.isEmpty())
                 return;
             if (r->kind == ExternalRef::Drawing) {
                 const QString file = r->found;
                 dialog.accept();
                 openFile(file);
             } else {
                 QDesktopServices::openUrl(QUrl::fromLocalFile(r->found));
             }
         }},
        {tr("Stacca"), [&] {
             const ExternalRef* r = current();
             if (!r)
                 return;
             if (QMessageBox::question(&dialog, dialog.windowTitle(),
                                       tr("Staccare \"%1\"? Si eliminano anche i suoi %2 inserimenti nel disegno.")
                                           .arg(r->name)
                                           .arg(r->uses))
                 != QMessageBox::Yes)
                 return;
             detachExternalRef(doc, *r);
             d->notifyChanged();
             refill(selected);
         }},
        {tr("Associa..."), [&] {
             // BIND: il riferimento diventa un blocco del disegno (non serve più il file esterno).
             const ExternalRef* r = current();
             if (!r || r->kind != ExternalRef::Drawing)
                 return;
             QMessageBox box(QMessageBox::Question, dialog.windowTitle(),
                             tr("Associare \"%1\" al disegno? Diventa un blocco normale e non dipende più dal file.\n\n"
                                "Associa: i layer si chiamano %1$0$nome.\n"
                                "Inserisci: i layer prendono il loro nome e si uniscono a quelli del disegno con lo "
                                "stesso nome.")
                                 .arg(r->name),
                             QMessageBox::NoButton, &dialog);
             QPushButton* bind = box.addButton(tr("Associa"), QMessageBox::AcceptRole);
             QPushButton* insert = box.addButton(tr("Inserisci"), QMessageBox::AcceptRole);
             box.addButton(tr("Annulla"), QMessageBox::RejectRole);
             box.exec();
             if (box.clickedButton() != bind && box.clickedButton() != insert)
                 return;
             std::string error;
             if (!bindXref(doc, r->name.toStdString(), box.clickedButton() == insert, &error)) {
                 QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromStdString(error));
                 return;
             }
             d->notifyChanged();
             refill(selected);
         }},
    });
    dialog.buttonsRow({
        {tr("Collega DWG/DXF..."), [&] {
             const QString file = QFileDialog::getOpenFileName(&dialog, tr("Riferimento esterno"), QString(),
                                                               tr("Disegni (*.dwg *.dxf)"));
             if (file.isEmpty())
                 return;
             dialog.accept();
             const auto type = askRefPathType(file);
             if (!type)
                 return;
             QString error;
             if (!d->attachXref(file, &error, *type))
                 QMessageBox::critical(this, tr("Riferimento esterno"), error);
         }},
        {tr("Collega immagine..."), [&] {
             const QString file = QFileDialog::getOpenFileName(&dialog, tr("Collega immagine"), QString(),
                                                               tr("Immagini (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)"));
             if (file.isEmpty())
                 return;
             dialog.accept();
             if (const auto type = askRefPathType(file))
                 attachImage(file, *type);
         }},
    });
    dialog.group(tr("Dettagli"));
    detailsField = dialog.note(QString());
    refill(0);
    dialog.setSize(640, 600);
    if (!dialogGrab_.isEmpty()) {   // -DIALOGPNG RIFERIMENTI
        dialog.show();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        dialog.grab().save(dialogGrab_);
        dialogGrab_.clear();
        return;
    }
    dialog.exec();
}

} // namespace cartesio
