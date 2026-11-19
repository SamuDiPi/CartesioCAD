#include "app/Theme.h"

#include "core/Layer.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace cartesio::theme {

QColor aciColor(int index)
{
    switch (index) {
    case 1: return {255, 0, 0};
    case 2: return {255, 255, 0};
    case 3: return {0, 255, 0};
    case 4: return {0, 255, 255};
    case 5: return {0, 0, 255};
    case 6: return {255, 0, 255};
    case 7: return {255, 255, 255};
    case 8: return {128, 128, 128};
    case 9: return {192, 192, 192};
    default: break;
    }
    if (index >= 10 && index <= 249) {
        // 24 tinte ogni 15°, ciascuna con 5 luminosità, piena e attenuata.
        const int hueStep = (index - 10) / 10;
        const int sub = (index - 10) % 10;
        static constexpr double values[] = {1.0, 0.8, 0.6, 0.5, 0.3};
        const double saturation = (sub % 2 == 0) ? 1.0 : 0.5;
        return QColor::fromHsvF(static_cast<float>(hueStep * 15 / 360.0), static_cast<float>(saturation),
                                static_cast<float>(values[sub / 2]));
    }
    if (index >= 250 && index <= 255) {
        static constexpr int grays[] = {51, 91, 132, 173, 214, 255};
        const int g = grays[index - 250];
        return {g, g, g};
    }
    return {255, 255, 255};
}

QString aciName(int index)
{
    switch (index) {
    case kColorByLayer: return QStringLiteral("DaLayer");
    case kColorByBlock: return QStringLiteral("DaBlocco");
    case 1: return QStringLiteral("Rosso");
    case 2: return QStringLiteral("Giallo");
    case 3: return QStringLiteral("Verde");
    case 4: return QStringLiteral("Ciano");
    case 5: return QStringLiteral("Blu");
    case 6: return QStringLiteral("Magenta");
    case 7: return QStringLiteral("Bianco");
    default: return QStringLiteral("Colore %1").arg(index);
    }
}

namespace {

const char* kStyleSheet = R"(
QMainWindow, QDialog { background: #3b4453; }
QWidget { color: #e6e9ee; }
QToolTip { background: #2a303b; color: #e6e9ee; border: 1px solid #5a6578; padding: 4px; }

/* --- Barra di accesso rapido e titolo --- */
#QuickAccessBar { background: #2c323c; border-bottom: 1px solid #1c2028; }
#QuickAccessBar QToolButton { padding: 2px; border-radius: 3px; }
#AppButton { background: #b03a2e; border: none; border-radius: 3px; }
#AppButton:hover { background: #c94a3d; }
#AppButton::menu-indicator { image: none; width: 0; }
#TitleLabel { color: #c7cdd6; }

/* --- Barra multifunzione --- */
#Ribbon { background: #3b4453; border-bottom: 1px solid #1c2028; }
#RibbonTabs { background: #2c323c; }
QTabBar#RibbonTabBar::tab {
    background: transparent; color: #c7cdd6; padding: 5px 14px; border: none; margin-right: 1px;
}
QTabBar#RibbonTabBar::tab:selected { background: #3b4453; color: #ffffff; border-top: 2px solid #3b8eea; }
QTabBar#RibbonTabBar::tab:hover:!selected { background: #353c48; }
#RibbonPage { background: #3b4453; }
#RibbonPanel { background: #3b4453; border-right: 1px solid #2a303a; }
#RibbonPanelTitle { background: #323946; color: #b8c0cc; padding: 2px 6px; font-size: 8pt; }
#RibbonPanel QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 1px 3px; }
#RibbonPanel QToolButton:hover { background: #4a5567; border: 1px solid #5d6b80; }
#RibbonPanel QToolButton:pressed, #RibbonPanel QToolButton:checked { background: #2f5d94; border: 1px solid #3b8eea; }
#RibbonPanel QToolButton:disabled { color: #6c7584; }

/* --- Schede dei file --- */
#FileTabsBar { background: #2c323c; border-bottom: 1px solid #1c2028; }
QTabBar#FileTabs::tab {
    background: #333a46; color: #c7cdd6; padding: 5px 12px; border: 1px solid #262b34; border-bottom: none;
    margin-right: 2px; min-width: 90px;
}
QTabBar#FileTabs::tab:selected { background: #212830; color: #ffffff; border-top: 2px solid #3b8eea; }
QTabBar#FileTabs::tab:hover:!selected { background: #3d4553; }
#NewTabButton { border: none; padding: 2px 8px; color: #c7cdd6; font-size: 13pt; }
#NewTabButton:hover { background: #3d4553; }

/* --- Riga di comando mobile --- */
#CommandHistory { background: rgba(28, 33, 41, 185); border: 1px solid rgba(80, 90, 105, 120);
                  border-bottom: none; padding: 3px 8px; color: #cfd5de; }
#CommandBar { background: rgba(46, 53, 64, 240); border: 1px solid #56627a; }
#CommandBar QLineEdit { background: transparent; border: none; color: #ffffff; selection-background-color: #3b8eea; }
#CommandBar QToolButton { border: none; padding: 2px; }
#CommandBar QToolButton:hover { background: #4a5567; }
#CommandPrompt { color: #9fd0ff; }

/* --- Barra di stato --- */
QStatusBar { background: #2c323c; border-top: 1px solid #1c2028; }
QStatusBar::item { border: none; }
#StatusToggle { border: 1px solid transparent; border-radius: 2px; padding: 2px; }
#StatusToggle:hover { background: #3d4553; }
#StatusToggle:checked { background: #2f5d94; border: 1px solid #3b8eea; }
#ModelTab { background: #212830; border: 1px solid #3b8eea; padding: 2px 10px; color: #e6e6e6; }
#ModelTab:hover { background: #2c3644; }
#CoordLabel { color: #c7cdd6; padding: 0 8px; }

/* --- Controlli --- */
QComboBox { background: #2a303a; border: 1px solid #56627a; border-radius: 2px; padding: 2px 6px; min-height: 18px; }
QComboBox:hover { border: 1px solid #7b8aa3; }
QComboBox QAbstractItemView { background: #2a303a; border: 1px solid #56627a; selection-background-color: #2f5d94; }
QLineEdit { background: #2a303a; border: 1px solid #56627a; border-radius: 2px; padding: 2px 4px; }
QLineEdit:focus { border: 1px solid #3b8eea; }
QLineEdit:read-only { background: #343b47; color: #b8c0cc; }
QPushButton { background: #4a5567; border: 1px solid #5d6b80; border-radius: 2px; padding: 4px 14px; }
QPushButton:hover { background: #56637a; }
QPushButton:pressed { background: #2f5d94; }
QPushButton:default { border: 1px solid #3b8eea; }

/* --- Menu --- */
QMenuBar { background: #2c323c; }
QMenu { background: #2f3540; border: 1px solid #56627a; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 24px; }
QMenu::item:selected { background: #2f5d94; }
QMenu::item:disabled { color: #6c7584; }
QMenu::separator { height: 1px; background: #4a5567; margin: 4px 8px; }

/* --- Tavolozze --- */
QDockWidget::title { background: #2c323c; padding: 5px 8px; text-align: left; border-bottom: 1px solid #1c2028; }
QTableWidget, QTreeWidget, QListWidget {
    background: #2a303a; alternate-background-color: #2f3640; border: 1px solid #1c2028;
    gridline-color: #3a424f; selection-background-color: #2f5d94; selection-color: #ffffff;
}
QHeaderView::section { background: #333a46; color: #c7cdd6; border: none; border-right: 1px solid #262b34;
                       border-bottom: 1px solid #262b34; padding: 4px 6px; }
#PaletteSection { background: #333a46; color: #ffffff; font-weight: bold; padding: 4px 6px; }
#PaletteLabel { color: #b8c0cc; }

/* --- Pagina iniziale --- */
#StartPage { background: #212830; }
#StartTitle { font-size: 26pt; font-weight: 300; color: #ffffff; }
#StartSubtitle { color: #9aa3b0; font-size: 10pt; }
#StartSection { color: #c7cdd6; font-size: 11pt; font-weight: bold; }
#StartButton { background: #2c3440; border: 1px solid #3d4757; border-radius: 4px; padding: 12px 18px;
               text-align: left; font-size: 11pt; }
#StartButton:hover { background: #34404f; border: 1px solid #3b8eea; }
#RecentList { background: transparent; border: none; font-size: 10pt; }
#RecentList::item { padding: 8px; border-bottom: 1px solid #2c3440; }
#RecentList::item:hover { background: #2c3440; }

/* --- Barre di scorrimento --- */
QScrollBar:vertical { background: #2a303a; width: 12px; }
QScrollBar::handle:vertical { background: #4a5567; min-height: 24px; border-radius: 4px; margin: 2px; }
QScrollBar:horizontal { background: #2a303a; height: 12px; }
QScrollBar::handle:horizontal { background: #4a5567; min-width: 24px; border-radius: 4px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
)";

} // namespace

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, QColor(42, 48, 58));
    p.setColor(QPalette::AlternateBase, QColor(47, 54, 64));
    p.setColor(QPalette::ToolTipBase, QColor(42, 48, 59));
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, QColor(74, 85, 103));
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::Highlight, QColor(47, 93, 148));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, kTextMuted);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(108, 117, 132));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(108, 117, 132));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(108, 117, 132));
    app.setPalette(p);

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(9.0);
    app.setFont(font);

    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

} // namespace cartesio::theme
