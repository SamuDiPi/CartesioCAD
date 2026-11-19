// Pulsante, per un UiButton (`model`) o libero (`label` e segnale clicked).
import QtQuick
import CartesioCAD

Rectangle {
    id: pb
    property var model
    property bool primary: false
    property string label: model ? model.text : ""
    signal clicked()
    visible: model ? model.visible : true
    implicitWidth: Math.max(80, caption.implicitWidth + 22)
    implicitHeight: 26
    radius: 3
    readonly property bool active: model ? model.enabled : true
    opacity: active ? 1 : 0.45
    color: primary ? (pbArea.containsMouse ? "#4c9bf0" : Theme.accent) : (pbArea.pressed ? Theme.checked : pbArea.containsMouse ? Theme.hover : Theme.field)
    border.color: primary ? Theme.accent : pbArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
    Text { id: caption; anchors.centerIn: parent; text: pb.label; color: Theme.text; font.pixelSize: 12 }
    MouseArea {
        id: pbArea
        anchors.fill: parent
        hoverEnabled: true
        onEntered: if (pb.model && pb.model.tooltip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, pb.model.tooltip) }
        onExited: ui.hideToolTip()
        onClicked: { ui.hideToolTip(); if (!pb.active) return; if (pb.model) pb.model.click(); pb.clicked() }
    }
}
