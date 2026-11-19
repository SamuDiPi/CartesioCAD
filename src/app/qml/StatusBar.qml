// Barra di stato: schede Modello/layout, coordinate, MODELLO/CARTA, finestra di layout, aiuti al disegno.
import QtQuick
import CartesioCAD

Rectangle {
    color: Theme.bar
    Rectangle { width: parent.width; height: 1; color: Theme.border }

    component Tip: MouseArea {
        property string tip: ""
        hoverEnabled: true
        onEntered: { const p = mapToGlobal(0, -28); ui.showToolTip(p.x, p.y, tip) }
        onExited: ui.hideToolTip()
        onPressed: ui.hideToolTip()
    }

    // Schede Modello / layout (in basso a sinistra): se sono tante scorrono con la rotella.
    // Elenco di tutti i layout (per gruppo): sempre a portata, anche quando le schede non ci stanno.
    Rectangle {
        id: listButton
        visible: sbar.tabsVisible
        x: 4
        y: 3
        width: 24
        height: parent.height - 3
        color: listArea.containsMouse ? "#3d4553" : "#333a46"
        border.color: "#262b34"
        Text { anchors.centerIn: parent; text: "\u2261"; color: Theme.muted3; font.pixelSize: 15 }
        Tip {
            id: listArea
            anchors.fill: parent
            tip: qsTr("Tutti i layout, per gruppo; raggruppa le schede")
            onClicked: {
                const p = listButton.mapToGlobal(0, 0)
                sbar.clickLayoutList(p.x, p.y)
            }
        }
    }
    Flickable {
        id: tabsArea
        visible: sbar.tabsVisible
        x: listButton.x + listButton.width + 2
        y: 3
        width: Math.max(0, (loadingBox.visible ? loadingBox.x : rightGroup.x) - x - 12)
        height: parent.height - 3
        contentWidth: tabsRow.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        WheelHandler { orientation: Qt.Vertical; property: "contentX"; rotationScale: -2 }
    Row {
        id: tabsRow
        height: tabsArea.height
        spacing: 1
        Repeater {
            model: sbar.layoutTabs.tabs
            delegate: Rectangle {
                id: tab
                required property var modelData
                required property int index
                readonly property bool current: index === sbar.layoutTabs.currentIndex
                width: label.implicitWidth + 24
                height: parent.height
                color: current ? Theme.drawing : (area.containsMouse ? "#3d4553" : "#333a46")
                border.color: current ? Theme.accent : "#262b34"
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: tab.modelData.text
                    color: tab.current ? "#ffffff" : Theme.muted3
                    font.pixelSize: 12
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            const p = mapToGlobal(mouse.x, mouse.y)
                            sbar.layoutTabs.requestMenu(tab.index, p.x, p.y)
                        } else {
                            sbar.layoutTabs.select(tab.index)
                        }
                    }
                }
            }
        }
        Rectangle {
            width: 26
            height: parent.height
            color: plus.containsMouse ? "#3d4553" : "transparent"
            Text { anchors.centerIn: parent; text: "+"; color: Theme.muted3; font.pixelSize: 16 }
            Tip {
                id: plus
                anchors.fill: parent
                tip: qsTr("Nuovo layout")
                onClicked: sbar.layoutTabs.requestNew()
            }
        }
    }
    }


    // Apertura di un disegno in corso: barra animata, fase e Annulla (a sinistra degli interruttori).
    Rectangle {
        id: loadingBox
        visible: sbar.loading
        anchors.right: rightGroup.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(220, Math.min(460, rightGroup.x - 20))
        height: 22
        radius: 3
        color: "#262c35"
        border.color: Theme.accent
        clip: true
        // Barra indeterminata: un tratto che scorre.
        Rectangle {
            id: track
            x: 8
            width: 70
            height: 6
            radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: "#3a4352"
            clip: true
            Rectangle {
                width: 36
                height: parent.height
                radius: 3
                color: Theme.accent
                NumberAnimation on x {
                    running: loadingBox.visible
                    from: -36; to: 70
                    duration: 1100
                    loops: Animation.Infinite
                }
            }
        }
        Text {
            anchors.left: track.right
            anchors.leftMargin: 8
            anchors.right: cancel.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: sbar.loadingText
            color: Theme.text
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }
        Text {
            id: cancel
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Annulla")
            color: cancelArea.containsMouse ? "#ffffff" : "#9fd0ff"
            font.pixelSize: 12
            font.underline: cancelArea.containsMouse
            MouseArea { id: cancelArea; anchors.fill: parent; hoverEnabled: true; onClicked: sbar.cancelLoading() }
        }
    }
    // A destra: coordinate, spazio, finestra di layout, interruttori.
    Row {
        id: rightGroup
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Text {
            width: 250
            anchors.verticalCenter: parent.verticalCenter
            text: sbar.coordinates
            color: Theme.muted3
            font.family: "Consolas"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            rightPadding: 12
            elide: Text.ElideLeft
        }

        Rectangle {
            width: spaceLabel.implicitWidth + 22
            height: 22
            color: spaceArea.containsMouse && sbar.spaceEnabled ? "#2c3644" : Theme.drawing
            border.color: Theme.accent
            opacity: sbar.spaceEnabled ? 1 : 0.6
            Text { id: spaceLabel; anchors.centerIn: parent; text: sbar.spaceText; color: "#e6e6e6"; font.pixelSize: 12 }
            Tip {
                id: spaceArea
                anchors.fill: parent
                tip: qsTr("Nei layout: passa dallo spazio carta al modello nella finestra (MSPACE/PSPACE)")
                onClicked: if (sbar.spaceEnabled) sbar.clickSpace()
            }
        }
        Rectangle {
            visible: sbar.viewportVisible
            width: scaleLabel.implicitWidth + 16
            height: 22
            radius: 2
            color: scaleArea.containsMouse ? "#3d4553" : "transparent"
            opacity: sbar.viewportScaleEnabled ? 1 : 0.5
            Text { id: scaleLabel; anchors.centerIn: parent; text: sbar.viewportScale; color: Theme.text; font.pixelSize: 12 }
            Tip {
                id: scaleArea
                anchors.fill: parent
                tip: qsTr("Scala della finestra")
                onClicked: {
                    if (!sbar.viewportScaleEnabled)
                        return
                    const p = parent.mapToGlobal(0, 0)
                    sbar.clickViewportScale(p.x, p.y - 4)
                }
            }
        }
        Rectangle {
            visible: sbar.viewportVisible
            width: 26; height: 24
            radius: 2
            color: lockArea.containsMouse ? "#3d4553" : "transparent"
            Image { anchors.centerIn: parent; source: sbar.lockIcon; sourceSize: Qt.size(18, 18) }
            Tip { id: lockArea; anchors.fill: parent; tip: sbar.lockTip; onClicked: sbar.clickLock() }
        }
        // Scala di annotazione (CANNOSCALE), nello spazio modello.
        Rectangle {
            visible: sbar.annoVisible
            width: annoLabel.implicitWidth + 16
            height: 22
            radius: 2
            color: annoArea.containsMouse ? "#3d4553" : "transparent"
            Text { id: annoLabel; anchors.centerIn: parent; text: sbar.annoScale + " ▾"; color: Theme.text; font.pixelSize: 12 }
            Tip {
                id: annoArea
                anchors.fill: parent
                tip: qsTr("Scala di annotazione: testi, quote e direttrici annotativi si disegnano a questa scala")
                onClicked: {
                    const p = parent.mapToGlobal(0, 0)
                    sbar.clickAnnoScale(p.x, p.y - 4)
                }
            }
        }
        Item { width: 6; height: 1 }

        Repeater {
            model: sbar.toggles
            delegate: Rectangle {
                id: toggle
                required property var modelData
                width: modelData.hasMenu ? 38 : 28
                height: 24
                radius: 2
                color: modelData.checked ? Theme.checked : (toggleArea.containsMouse ? "#3d4553" : "transparent")
                border.color: modelData.checked ? Theme.accent : "transparent"
                Image {
                    x: 5
                    anchors.verticalCenter: parent.verticalCenter
                    source: toggle.modelData.icon
                    sourceSize: Qt.size(18, 18)
                }
                Text {
                    visible: toggle.modelData.hasMenu
                    anchors.right: parent.right
                    anchors.rightMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: Theme.muted
                    font.pixelSize: 10
                }
                Tip {
                    id: toggleArea
                    anchors.fill: parent
                    tip: toggle.modelData.tooltip
                    onClicked: (mouse) => {
                        // Con un menu (osnap, polare): la freccia apre le opzioni, il resto accende/spegne.
                        if (toggle.modelData.hasMenu && mouse.x > toggle.width - 12) {
                            const p = toggle.mapToGlobal(0, 0)
                            toggle.modelData.popup(p.x, p.y - 4)
                        } else {
                            toggle.modelData.trigger()
                        }
                    }
                }
            }
        }
    }
}
