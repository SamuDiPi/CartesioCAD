#pragma once

#include <QIcon>

namespace cartesio {

// Icone dell'interfaccia, disegnate via codice (vettoriali, nitide a ogni dimensione).
enum class Icon {
    AppLogo,
    New, Open, Save, SaveAs, Undo, Redo, Close,
    Line, Circle, Arc, Polyline, Rectangle, Ellipse, PointIcon, Hatch,
    TextSingle, TextMulti, DimLinear, DimAligned, DimRadius, DimDiameter,
    BlockCreate, BlockInsert, Explode, AttributeDef,
    LayoutNew, ViewportIcon, PageSetup, Pdf, Script, TitleBlock, PlotIcon, PreviewIcon,
    Rotate, ScaleIcon, Mirror, ArrayIcon, Extend, Fillet, Chamfer, Stretch, Break, Join, Leader, Polygon, XLineIcon,
    Revcloud, TableIcon, GroupIcon, UngroupIcon, WipeoutIcon, MlineIcon, Measure, AreaIcon, ListIcon, MatchProp, Paste,
    Move, Copy, Offset, Trim, Erase,
    LayerProperties, LayerMakeCurrent, LayerAllOn, LayerNew, LayerDelete, LayerCurrentMark,
    BulbOn, BulbOff, Sun, Snowflake, LockOpen, LockClosed,
    ZoomExtents, ZoomIn, ZoomOut, Pan,
    Properties, CommandLine, History,
    Grid, Snap, Ortho, Polar, Osnap, OTrack, DynInput, Lineweight,
};

QIcon icon(Icon id);
// Icona fatta di lettere (pulsanti di formattazione e comandi senza disegno): decorazione 1 sottolineato,
// 2 sopralineato, 3 barrato.
QIcon glyphIcon(const QString& text, bool bold = false, bool italic = false, int decoration = 0);

} // namespace cartesio
