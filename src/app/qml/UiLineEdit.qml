// Campo di testo per un UiText.
import QtQuick
import CartesioCAD

Rectangle {
    id: le
    property var model
    visible: model.visible
    implicitWidth: 200
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
        text: le.model.text
        readOnly: !le.model.enabled
        color: Theme.text
        selectionColor: Theme.accent
        selectByMouse: true
        clip: true
        font.pixelSize: 12
        onTextEdited: le.model.edit(text)
        Text {
            visible: !input.text.length && !input.activeFocus
            text: le.model.placeholder
            color: Theme.muted
            font.pixelSize: 12
            font.italic: true
        }
    }
}
