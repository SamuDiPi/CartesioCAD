// Schede dei disegni aperti, con la scheda Disegno e il pulsante "+" (modello: TabsModel in C++).
import QtQuick
import CartesioCAD

Rectangle {
    color: Theme.bar
    Row {
        x: 4
        y: 4
        height: parent.height - 4
        spacing: 1
        Repeater {
            model: tabs.tabs
            delegate: Rectangle {
                id: tab
                required property var modelData
                required property int index
                readonly property bool current: index === tabs.currentIndex
                width: label.implicitWidth + (modelData.closable ? 46 : 28)
                height: parent.height
                color: current ? Theme.drawing : (area.containsMouse ? "#3d4553" : "#333a46")
                border.color: "#262b34"
                Rectangle { visible: tab.current; width: parent.width; height: 2; color: Theme.accent }
                Text {
                    id: label
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.text
                    color: tab.current ? "#ffffff" : Theme.muted3
                    font.pixelSize: 12
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton && tab.modelData.closable)
                            tabs.requestClose(tab.index)
                        else
                            tabs.select(tab.index)
                    }
                }
                Rectangle {
                    visible: tab.modelData.closable
                    width: 16; height: 16
                    radius: 2
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: closeArea.containsMouse ? "#b03a2e" : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: closeArea.containsMouse || tab.current ? "#ffffff" : Theme.muted
                        font.pixelSize: 14
                    }
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: tabs.requestClose(tab.index)
                    }
                }
            }
        }
        Rectangle {
            width: 30
            height: parent.height
            color: plusArea.containsMouse ? "#3d4553" : "transparent"
            Text {
                anchors.centerIn: parent
                text: "+"
                color: Theme.muted3
                font.pixelSize: 18
            }
            MouseArea {
                id: plusArea
                anchors.fill: parent
                hoverEnabled: true
                onEntered: { const p = mapToGlobal(0, height + 4); ui.showToolTip(p.x, p.y, qsTr("Nuovo disegno")) }
                onExited: ui.hideToolTip()
                onClicked: tabs.requestNew()
            }
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
}
