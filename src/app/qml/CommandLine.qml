// Riga di comando: pulsanti, richiesta del comando, campo di testo e suggerimenti (modello: CommandLine in C++).
import QtQuick
import CartesioCAD

Item {
    id: root
    // Cresce verso l'alto quando ci sono suggerimenti.
    implicitHeight: bar.height + (list.visible ? list.height + 2 : 0)
    property int selected: -1

    function focusInput() { input.forceActiveFocus() }

    Connections {
        target: cmd.input
        function onTextReset() { if (input.text !== cmd.input.text) input.text = cmd.input.text }
        function onFocusRequested() { input.forceActiveFocus() }
    }
    Connections {
        target: cmd
        function onMatchesChanged() { root.selected = -1 }
    }

    // Suggerimenti (sopra la barra).
    Rectangle {
        id: list
        visible: cmd.matches.length > 0 && input.activeFocus
        x: prompt.visible ? prompt.x + prompt.width + 40 : 52
        width: 260
        height: Math.min(cmd.matches.length, 10) * 21 + 4
        anchors.bottom: bar.top
        anchors.bottomMargin: 2
        color: "#2f3540"
        border.color: Theme.fieldBorder
        Column {
            x: 2; y: 2
            width: parent.width - 4
            Repeater {
                model: cmd.matches
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: parent.width
                    height: 21
                    color: index === root.selected || itemArea.containsMouse ? Theme.checked : "transparent"
                    Text {
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: Theme.text
                        font.family: "Consolas"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: itemArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: cmd.choose(modelData)
                    }
                }
            }
        }
    }

    Rectangle {
        id: bar
        anchors.bottom: parent.bottom
        width: parent.width
        height: 30
        color: Theme.commandBar
        border.color: Theme.fieldBorder

        Row {
            id: buttons
            x: 4
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: [
                    { icon: cmd.closeIcon, size: 12, tip: qsTr("Nascondi la riga di comando (Ctrl+9)"), close: true },
                    { icon: cmd.historyIcon, size: 16, tip: qsTr("Cronologia dei comandi (F2)"), close: false }
                ]
                delegate: Rectangle {
                    required property var modelData
                    width: 22; height: 22
                    radius: 2
                    color: area.containsMouse ? Theme.hover : "transparent"
                    Image {
                        anchors.centerIn: parent
                        source: modelData.icon
                        sourceSize: Qt.size(modelData.size, modelData.size)
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: { const p = mapToGlobal(10, 26); ui.showToolTip(p.x, p.y, modelData.tip) }
                        onExited: ui.hideToolTip()
                        onClicked: modelData.close ? cmd.requestClose() : cmd.requestHistory()
                    }
                }
            }
        }

        Text {
            id: prompt
            visible: text.length > 0
            anchors.left: buttons.right
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: cmd.prompt
            color: "#9fd0ff"
            font.family: "Consolas"
            font.pixelSize: 13
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, bar.width * 0.6)
        }

        TextInput {
            id: input
            anchors.left: prompt.visible ? prompt.right : buttons.right
            anchors.leftMargin: 6
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            color: "#ffffff"
            selectionColor: Theme.accent
            selectByMouse: true
            focus: true
            clip: true
            font.family: "Consolas"
            font.pixelSize: 13
            onTextEdited: cmd.input.edited(text)
            Text {
                visible: input.text.length === 0
                text: cmd.input.placeholder
                color: "#7d8796"
                font: input.font
            }
            Keys.onPressed: (event) => {
                const n = cmd.matches.length
                if (list.visible && (event.key === Qt.Key_Down || event.key === Qt.Key_Up)) {
                    root.selected = event.key === Qt.Key_Down ? Math.min(n - 1, root.selected + 1)
                                                              : Math.max(-1, root.selected - 1)
                    event.accepted = true
                    return
                }
                if (list.visible && root.selected >= 0
                        && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab)) {
                    cmd.choose(cmd.matches[root.selected])
                    event.accepted = true
                    return
                }
                if (list.visible && event.key === Qt.Key_Tab && n > 0) {
                    cmd.choose(cmd.matches[0])
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    cmd.input.edited(text)
                    cmd.input.submit()
                    event.accepted = true
                    return
                }
                event.accepted = cmd.handleKey(event.key, event.modifiers, event.text)
            }
        }
    }
}
