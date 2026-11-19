// Elenco a discesa per un UiCombo (si apre come menu nativo).
import QtQuick
import CartesioCAD

Rectangle {
    id: combo
    property var model
    visible: model.visible
    implicitWidth: 200
    implicitHeight: 24
    radius: 2
    color: Theme.field
    border.color: comboArea.containsMouse && model.enabled ? Theme.hoverBorder2 : Theme.fieldBorder
    opacity: model.enabled ? 1 : 0.45
    Text {
        x: 6
        width: parent.width - 24
        anchors.verticalCenter: parent.verticalCenter
        text: combo.model.currentText
        color: Theme.text
        font.pixelSize: 12
        elide: Text.ElideRight
    }
    Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.muted }
    MouseArea {
        id: comboArea
        anchors.fill: parent
        hoverEnabled: true
        onEntered: if (combo.model.tooltip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, combo.model.tooltip) }
        onExited: ui.hideToolTip()
        onClicked: { ui.hideToolTip(); const p = combo.mapToGlobal(0, combo.height); combo.model.popup(p.x, p.y, combo.width) }
    }
}
