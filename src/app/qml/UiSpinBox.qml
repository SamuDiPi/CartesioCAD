// Campo numerico per un UiSpin (rotellina per i passi).
import QtQuick
import CartesioCAD

Rectangle {
    id: spin
    property var model
    visible: model.visible
    implicitWidth: 110
    implicitHeight: 24
    radius: 2
    color: Theme.field
    border.color: input.activeFocus ? Theme.accent : Theme.fieldBorder
    opacity: model.enabled ? 1 : 0.45
    TextInput {
        id: input
        x: 6
        width: parent.width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: spin.model.display
        readOnly: !spin.model.enabled
        color: Theme.text
        selectionColor: Theme.accent
        selectByMouse: true
        clip: true
        font.pixelSize: 12
        onEditingFinished: { spin.model.commit(text); text = Qt.binding(() => spin.model.display) }
    }
    WheelHandler { onWheel: function(event) { spin.model.step(event.angleDelta.y > 0 ? 1 : -1) } }
}
