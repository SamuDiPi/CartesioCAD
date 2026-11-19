#include "app/AppOptions.h"

#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace cartesio {

namespace {

QSettings store()
{
    return QSettings(QStringLiteral("CartesioCAD"), QStringLiteral("CartesioCAD"));
}

// Metri per unità, per i codici di $INSUNITS che hanno senso in un disegno tecnico.
double metres(int code)
{
    switch (code) {
    case 1: return 0.0254;     // pollici
    case 2: return 0.3048;     // piedi
    case 4: return 0.001;      // millimetri
    case 5: return 0.01;       // centimetri
    case 6: return 1.0;        // metri
    case 7: return 1000.0;     // chilometri
    case 10: return 0.9144;    // iarde
    case 13: return 1e-6;      // micron
    case 14: return 0.1;       // decimetri
    default: return 0.0;       // senza unità o non gestite
    }
}

} // namespace

double unitsFactor(int fromUnits, int toUnits)
{
    const double a = metres(fromUnits), b = metres(toUnits);
    return a > 0.0 && b > 0.0 ? a / b : 1.0;
}

QString unitsName(int code)
{
    switch (code) {
    case 0: return QObject::tr("Senza unità");
    case 1: return QObject::tr("Pollici");
    case 2: return QObject::tr("Piedi");
    case 4: return QObject::tr("Millimetri");
    case 5: return QObject::tr("Centimetri");
    case 6: return QObject::tr("Metri");
    case 7: return QObject::tr("Chilometri");
    case 10: return QObject::tr("Iarde");
    case 13: return QObject::tr("Micron");
    case 14: return QObject::tr("Decimetri");
    default: return QObject::tr("Unità %1").arg(code);
    }
}

AppOptions AppOptions::load()
{
    QSettings s = store();
    s.beginGroup(QStringLiteral("Options"));
    AppOptions o;
    const auto i = [&s](const char* key, int fallback) { return s.value(QLatin1String(key), fallback).toInt(); };
    const auto b = [&s](const char* key, bool fallback) { return s.value(QLatin1String(key), fallback).toBool(); };
    const auto str = [&s](const char* key, const QString& fallback) {
        return s.value(QLatin1String(key), fallback).toString();
    };
    const auto color = [&s](const char* key, const QColor& fallback) {
        const QColor c(s.value(QLatin1String(key), fallback.name()).toString());
        return c.isValid() ? c : fallback;
    };
    o.saveFormat = i("newDrawingFormat", o.saveFormat);
    o.autosave = b("autosave", o.autosave);
    o.autosaveMinutes = std::max(1, i("autosaveMinutes", o.autosaveMinutes));
    o.backup = b("backup", o.backup);
    o.recentFiles = std::clamp(i("recentFiles", o.recentFiles), 0, 50);
    o.startPageAtLaunch = b("startPageAtLaunch", o.startPageAtLaunch);
    o.dwgEngine = i("dwgEngine", o.dwgEngine);
    o.odaPath = str("odaPath", o.odaPath);
    o.odaDwgVersion = str("odaDwgVersion", o.odaDwgVersion);
    o.defaultDevice = str("defaultDevice", o.defaultDevice);
    o.defaultStyleSheet = str("defaultStyleSheet", o.defaultStyleSheet);
    o.plotStylesFolder = str("plotStylesFolder", o.plotStylesFolder);
    o.templatesFolder = str("templatesFolder", o.templatesFolder);
    o.fontFolders = str("fontFolders", o.fontFolders);
    o.fontsOfInstalledPrograms = b("fontsOfInstalledPrograms", o.fontsOfInstalledPrograms);
    o.refPathType = i("refPathType", o.refPathType);
    o.textRendering = i("textRendering", o.textRendering);
    o.uiMode = i("uiMode", o.uiMode);
    o.showLayoutTabs = b("showLayoutTabs", o.showLayoutTabs);
    o.showFileTabs = b("showFileTabs", o.showFileTabs);
    o.groupLayoutTabs = b("groupLayoutTabs", o.groupLayoutTabs);
    o.showUcsIcon = b("showUcsIcon", o.showUcsIcon);
    o.showNavigationBar = b("showNavigationBar", o.showNavigationBar);
    o.showPrintableArea = b("showPrintableArea", o.showPrintableArea);
    o.showPaperBackground = b("showPaperBackground", o.showPaperBackground);
    o.showPaperShadow = b("showPaperShadow", o.showPaperShadow);
    o.layoutBackground = color("layoutBackground", o.layoutBackground);
    o.modelBackground = color("modelBackground", o.modelBackground);
    o.paperColor = color("paperColor", o.paperColor);
    o.gridColor = color("gridColor", o.gridColor);
    o.pageSetupForNewLayouts = b("pageSetupForNewLayouts", o.pageSetupForNewLayouts);
    o.viewportInNewLayouts = b("viewportInNewLayouts", o.viewportInNewLayouts);
    o.newLayoutPaper = str("newLayoutPaper", o.newLayoutPaper);
    o.newLayoutLandscape = b("newLayoutLandscape", o.newLayoutLandscape);
    o.newLayoutMargin = std::clamp(s.value(QStringLiteral("newLayoutMargin"), o.newLayoutMargin).toDouble(), 0.0, 50.0);
    o.hideSystemPrinters = b("hideSystemPrinters", o.hideSystemPrinters);
    o.plotLineweights = b("plotLineweights", o.plotLineweights);
    o.plotWithStyles = b("plotWithStyles", o.plotWithStyles);
    o.plotPaperSpaceLast = b("plotPaperSpaceLast", o.plotPaperSpaceLast);
    o.plotHidePaperObjects = b("plotHidePaperObjects", o.plotHidePaperObjects);
    o.plotFolder = str("plotFolder", o.plotFolder);
    o.plotFileNameWithLayout = b("plotFileNameWithLayout", o.plotFileNameWithLayout);
    o.openAfterPlot = b("openAfterPlot", o.openAfterPlot);
    o.plotLog = b("plotLog", o.plotLog);
    o.plotLogPath = str("plotLogPath", o.plotLogPath);
    o.doubleClickEdit = b("doubleClickEdit", o.doubleClickEdit);
    o.rightClickMenu = b("rightClickMenu", o.rightClickMenu);
    o.insertSourceUnits = i("insertSourceUnits", o.insertSourceUnits);
    o.insertTargetUnits = i("insertTargetUnits", o.insertTargetUnits);
    o.canaloPath = str("canaloPath", o.canaloPath);
    o.canaloFolder = str("canaloFolder", o.canaloFolder);
    o.canaloWatch = b("canaloWatch", o.canaloWatch);
    o.snapMarker = b("snapMarker", o.snapMarker);
    o.snapMarkerSize = std::clamp(i("snapMarkerSize", o.snapMarkerSize), 2, 20);
    o.snapMarkerColor = color("snapMarkerColor", o.snapMarkerColor);
    o.snapAperture = std::clamp(i("snapAperture", o.snapAperture), 2, 50);
    o.showAperture = b("showAperture", o.showAperture);
    o.pickboxSize = std::clamp(i("pickboxSize", o.pickboxSize), 1, 20);
    o.systemCursor = b("systemCursor", o.systemCursor);
    o.gripSize = std::clamp(i("gripSize", o.gripSize), 1, 12);
    o.gripColor = color("gripColor", o.gripColor);
    o.pickFirst = b("pickFirst", o.pickFirst);
    o.pressAndDrag = b("pressAndDrag", o.pressAndDrag);
    o.quickProperties = b("quickPropertiesOnSelect", o.quickProperties);
    DraftSettings& d = o.draft;
    d.grid = b("grid", d.grid);
    d.snap = b("snap", d.snap);
    d.ortho = b("ortho", d.ortho);
    d.polar = b("polar", d.polar);
    d.osnap = b("osnap", d.osnap);
    d.osmode = i("osmode", d.osmode);
    d.dynInput = b("dynInput", d.dynInput);
    d.otrack = b("otrack", d.otrack);
    d.lwDisplay = b("lwDisplay", d.lwDisplay);
    d.polarAngleDeg = s.value(QStringLiteral("polarAngle"), d.polarAngleDeg).toDouble();
    return o;
}

void AppOptions::save() const
{
    QSettings s = store();
    s.beginGroup(QStringLiteral("Options"));
    s.setValue(QStringLiteral("newDrawingFormat"), saveFormat);
    s.setValue(QStringLiteral("autosave"), autosave);
    s.setValue(QStringLiteral("autosaveMinutes"), autosaveMinutes);
    s.setValue(QStringLiteral("backup"), backup);
    s.setValue(QStringLiteral("recentFiles"), recentFiles);
    s.setValue(QStringLiteral("startPageAtLaunch"), startPageAtLaunch);
    s.setValue(QStringLiteral("dwgEngine"), dwgEngine);
    s.setValue(QStringLiteral("odaPath"), odaPath);
    s.setValue(QStringLiteral("odaDwgVersion"), odaDwgVersion);
    s.setValue(QStringLiteral("defaultDevice"), defaultDevice);
    s.setValue(QStringLiteral("defaultStyleSheet"), defaultStyleSheet);
    s.setValue(QStringLiteral("plotStylesFolder"), plotStylesFolder);
    s.setValue(QStringLiteral("templatesFolder"), templatesFolder);
    s.setValue(QStringLiteral("fontFolders"), fontFolders);
    s.setValue(QStringLiteral("fontsOfInstalledPrograms"), fontsOfInstalledPrograms);
    s.setValue(QStringLiteral("refPathType"), refPathType);
    s.setValue(QStringLiteral("textRendering"), textRendering);
    s.setValue(QStringLiteral("uiMode"), uiMode);
    s.setValue(QStringLiteral("showLayoutTabs"), showLayoutTabs);
    s.setValue(QStringLiteral("showFileTabs"), showFileTabs);
    s.setValue(QStringLiteral("groupLayoutTabs"), groupLayoutTabs);
    s.setValue(QStringLiteral("showUcsIcon"), showUcsIcon);
    s.setValue(QStringLiteral("showNavigationBar"), showNavigationBar);
    s.setValue(QStringLiteral("showPrintableArea"), showPrintableArea);
    s.setValue(QStringLiteral("showPaperBackground"), showPaperBackground);
    s.setValue(QStringLiteral("showPaperShadow"), showPaperShadow);
    s.setValue(QStringLiteral("layoutBackground"), layoutBackground.name());
    s.setValue(QStringLiteral("modelBackground"), modelBackground.name());
    s.setValue(QStringLiteral("paperColor"), paperColor.name());
    s.setValue(QStringLiteral("gridColor"), gridColor.name());
    s.setValue(QStringLiteral("pageSetupForNewLayouts"), pageSetupForNewLayouts);
    s.setValue(QStringLiteral("viewportInNewLayouts"), viewportInNewLayouts);
    s.setValue(QStringLiteral("newLayoutPaper"), newLayoutPaper);
    s.setValue(QStringLiteral("newLayoutLandscape"), newLayoutLandscape);
    s.setValue(QStringLiteral("newLayoutMargin"), newLayoutMargin);
    s.setValue(QStringLiteral("hideSystemPrinters"), hideSystemPrinters);
    s.setValue(QStringLiteral("plotLineweights"), plotLineweights);
    s.setValue(QStringLiteral("plotWithStyles"), plotWithStyles);
    s.setValue(QStringLiteral("plotPaperSpaceLast"), plotPaperSpaceLast);
    s.setValue(QStringLiteral("plotHidePaperObjects"), plotHidePaperObjects);
    s.setValue(QStringLiteral("plotFolder"), plotFolder);
    s.setValue(QStringLiteral("plotFileNameWithLayout"), plotFileNameWithLayout);
    s.setValue(QStringLiteral("openAfterPlot"), openAfterPlot);
    s.setValue(QStringLiteral("plotLog"), plotLog);
    s.setValue(QStringLiteral("plotLogPath"), plotLogPath);
    s.setValue(QStringLiteral("doubleClickEdit"), doubleClickEdit);
    s.setValue(QStringLiteral("rightClickMenu"), rightClickMenu);
    s.setValue(QStringLiteral("insertSourceUnits"), insertSourceUnits);
    s.setValue(QStringLiteral("insertTargetUnits"), insertTargetUnits);
    s.setValue(QStringLiteral("canaloPath"), canaloPath);
    s.setValue(QStringLiteral("canaloFolder"), canaloFolder);
    s.setValue(QStringLiteral("canaloWatch"), canaloWatch);
    s.setValue(QStringLiteral("snapMarker"), snapMarker);
    s.setValue(QStringLiteral("snapMarkerSize"), snapMarkerSize);
    s.setValue(QStringLiteral("snapMarkerColor"), snapMarkerColor.name());
    s.setValue(QStringLiteral("snapAperture"), snapAperture);
    s.setValue(QStringLiteral("showAperture"), showAperture);
    s.setValue(QStringLiteral("pickboxSize"), pickboxSize);
    s.setValue(QStringLiteral("systemCursor"), systemCursor);
    s.setValue(QStringLiteral("gripSize"), gripSize);
    s.setValue(QStringLiteral("gripColor"), gripColor.name());
    s.setValue(QStringLiteral("pickFirst"), pickFirst);
    s.setValue(QStringLiteral("pressAndDrag"), pressAndDrag);
    s.setValue(QStringLiteral("quickPropertiesOnSelect"), quickProperties);
    s.remove(QStringLiteral("quickProperties"));
    s.setValue(QStringLiteral("grid"), draft.grid);
    s.setValue(QStringLiteral("snap"), draft.snap);
    s.setValue(QStringLiteral("ortho"), draft.ortho);
    s.setValue(QStringLiteral("polar"), draft.polar);
    s.setValue(QStringLiteral("osnap"), draft.osnap);
    s.setValue(QStringLiteral("osmode"), draft.osmode);
    s.setValue(QStringLiteral("dynInput"), draft.dynInput);
    s.setValue(QStringLiteral("otrack"), draft.otrack);
    s.setValue(QStringLiteral("lwDisplay"), draft.lwDisplay);
    s.setValue(QStringLiteral("polarAngle"), draft.polarAngleDeg);
}

AppOptions& appOptions()
{
    static AppOptions options = AppOptions::load();
    return options;
}

void applyPlotDefaults(PlotSettings& s)
{
    const AppOptions& o = appOptions();
    if (!o.defaultDevice.isEmpty())
        s.device = o.defaultDevice.toStdString();
    s.styleSheet = o.defaultStyleSheet.toStdString();
    s.setFlag(kPlotPrintLineweights, o.plotLineweights);
    s.setFlag(kPlotPlotStyles, o.plotWithStyles);
    s.setFlag(kPlotDrawViewportsFirst, o.plotPaperSpaceLast);
    s.setFlag(kPlotHidden, o.plotHidePaperObjects);
}

PlotSettings newLayoutPlotSettings()
{
    const AppOptions& o = appOptions();
    const MediaFormat* m = findMediaByName(o.newLayoutPaper.toStdString());
    const double w = m ? std::max(m->width, m->height) : 420.0;
    const double h = m ? std::min(m->width, m->height) : 297.0;
    PlotSettings s = PlotSettings::forLayout(w, h, o.newLayoutMargin);
    if (!o.newLayoutLandscape)
        setSheet(s, h, w);
    applyPlotDefaults(s);
    return s;
}

QString plotLogFile()
{
    const QString configured = appOptions().plotLogPath.trimmed();
    if (!configured.isEmpty())
        return configured;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Registro stampe.csv");
}

} // namespace cartesio
