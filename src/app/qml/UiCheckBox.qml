// Casella di spunta o pulsante di opzione per un UiCheck.
import QtQuick
import CartesioCAD

Item {
    id: cb
    property var model
    visible: model.visible
    implicitWidth: row.implicitWidth
    implicitHeight: 22
    opacity: model.enabled ? 1 : 0.45
    Row {
        id: row
        spacing: 7
        anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            width: 15; height: 15
            radius: cb.model.radio ? 8 : 2
            anchors.verticalCenter: parent.verticalCenter
            color: cb.model.checked && !cb.model.radio ? Theme.accent : Theme.field
            border.color: cb.model.checked ? Theme.accent : area.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
            Text { anchors.centerIn: parent; visible: cb.model.checked && !cb.model.radio; text: "✓"; color: "white"; font.pixelSize: 11; font.bold: true }
            Rectangle { anchors.centerIn: parent; visible: cb.model.checked && cb.model.radio; width: 7; height: 7; radius: 4; color: Theme.accent }
        }
        Text { text: cb.model.text; color: Theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onEntered: if (cb.model.tooltip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, cb.model.tooltip) }
        onExited: ui.hideToolTip()
        onClicked: cb.model.toggle()
    }
}
