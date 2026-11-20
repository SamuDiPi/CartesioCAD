// Finestra principale tutta in QML (oggetto "main": MainView): barra multifunzione, schede dei file, tavolozze ai
// lati, disegno (CadCanvas) con la riga di comando sovrapposta, barra di stato. Una sola scena.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    // ------------------------------------------------------------ componenti

    // Pannello laterale con titolo e chiusura (al posto dei dock della finestra a widget).
    component Panel: Rectangle {
        id: panel
        property string title
        property url source
        signal closeRequested()
        color: Theme.window
        border.color: Theme.border
        Rectangle {
            id: header
            width: parent.width
            height: 26
            color: Theme.panelTitle
            Text {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                text: panel.title
                color: Theme.text
                font.pixelSize: 12
                font.bold: true
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 20; height: 20; radius: 2
                color: closeArea.containsMouse ? Theme.hover : "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: Theme.muted2; font.pixelSize: 11 }
                MouseArea { id: closeArea; anchors.fill: parent; hoverEnabled: true; onClicked: panel.closeRequested() }
            }
        }
        Loader {
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            source: panel.visible ? panel.source : ""
        }
    }

    // Divisore trascinabile fra un pannello e il disegno.
    component Splitter: Rectangle {
        id: splitter
        property bool leftSide: true
        signal dragged(real delta)
        width: 5
        color: splitArea.containsMouse || splitArea.pressed ? Theme.accent : Theme.separator
        MouseArea {
            id: splitArea
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor
            property real startX: 0
            onPressed: function(mouse) { startX = mapToItem(root, mouse.x, 0).x }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const x = mapToItem(root, mouse.x, 0).x
                splitter.dragged(x - startX)
                startX = x
            }
        }
    }

    // ------------------------------------------------------------ struttura

    Loader {
        id: ribbonView
        width: parent.width
        height: 166
        source: "qrc:/qml/Ribbon.qml"
    }

    Loader {
        id: tabsView
        anchors.top: ribbonView.bottom
        width: parent.width
        height: tabs.shown ? 32 : 0
        visible: tabs.shown
        source: "qrc:/qml/FileTabs.qml"
    }

    Loader {
        id: statusView
        anchors.bottom: parent.bottom
        width: parent.width
        height: 30
        source: "qrc:/qml/StatusBar.qml"
    }

    Item {
        id: body
        anchors.top: tabsView.bottom
        anchors.bottom: statusView.top
        width: parent.width

        // Tavolozze a sinistra: Proprietà e Gestore proprietà layer (una sopra l'altra se aperte entrambe).
        Item {
            id: leftArea
            // Sulla pagina Inizia niente tavolozze: tornano aprendo un disegno.
            visible: (main.propertiesVisible || main.layersVisible) && !main.startVisible
            width: visible ? main.leftWidth : 0
            height: parent.height
            Panel {
                id: propertiesPanel
                visible: main.propertiesVisible
                width: parent.width
                height: main.layersVisible ? parent.height * 0.5 : parent.height
                title: qsTr("Proprietà")
                source: "qrc:/qml/PropertiesPalette.qml"
                onCloseRequested: main.propertiesVisible = false
            }
            Panel {
                visible: main.layersVisible
                y: main.propertiesVisible ? propertiesPanel.height : 0
                width: parent.width
                height: parent.height - y
                title: qsTr("Gestore proprietà layer")
                source: "qrc:/qml/LayerManager.qml"
                onCloseRequested: main.layersVisible = false
            }
        }
        Splitter {
            id: leftSplitter
            visible: leftArea.visible
            x: leftArea.width
            height: parent.height
            onDragged: function(delta) { main.leftWidth = main.leftWidth + delta }
        }

        // Tavolozze a destra: Impianto, Circuiti e Blocchi (una sopra l'altra, in parti uguali, quelle aperte).
        Item {
            id: rightArea
            visible: (main.circuitsVisible || main.libraryVisible || main.plantVisible || main.guideVisible) && !main.startVisible
            anchors.right: parent.right
            width: visible ? main.rightWidth : 0
            height: parent.height
            readonly property int shown: (main.guideVisible ? 1 : 0) + (main.plantVisible ? 1 : 0) + (main.circuitsVisible ? 1 : 0)
                                         + (main.libraryVisible ? 1 : 0)
            readonly property real share: shown > 0 ? height / shown : height
            Panel {
                id: guidePanel
                visible: main.guideVisible
                width: parent.width
                height: visible ? rightArea.share : 0
                title: qsTr("Guida")
                source: "qrc:/qml/GuidePanel.qml"
                onCloseRequested: main.guideVisible = false
            }
            Panel {
                id: plantPanel
                visible: main.plantVisible
                y: guidePanel.height
                width: parent.width
                height: visible ? rightArea.share : 0
                title: qsTr("Impianto")
                source: "qrc:/qml/PlantPanel.qml"
                onCloseRequested: main.plantVisible = false
            }
            Panel {
                id: circuitsPanel
                visible: main.circuitsVisible
                y: guidePanel.height + plantPanel.height
                width: parent.width
                height: visible ? rightArea.share : 0
                title: qsTr("Circuiti")
                source: "qrc:/qml/CircuitsPanel.qml"
                onCloseRequested: main.circuitsVisible = false
            }
            Panel {
                visible: main.libraryVisible
                y: guidePanel.height + plantPanel.height + circuitsPanel.height
                width: parent.width
                height: parent.height - y
                title: qsTr("Blocchi")
                source: "qrc:/qml/BlockLibrary.qml"
                onCloseRequested: main.libraryVisible = false
            }
        }
        Splitter {
            visible: rightArea.visible
            anchors.right: rightArea.left
            height: parent.height
            leftSide: false
            onDragged: function(delta) { main.rightWidth = main.rightWidth - delta }
        }

        // Disegno, o la pagina Inizio.
        Item {
            id: center
            x: leftArea.visible ? leftArea.width + leftSplitter.width : 0
            width: parent.width - x - (rightArea.visible ? rightArea.width + 5 : 0)
            height: parent.height
            clip: true

            Loader {
                anchors.fill: parent
                active: main.startVisible
                visible: main.startVisible
                source: "qrc:/qml/StartPage.qml"
            }

            // Disegno: una vista, o più viste affiancate (finestre del modello) con la loro disposizione.
            Item {
                id: canvas
                anchors.fill: parent
                visible: !main.startVisible
                onVisibleChanged: if (visible) commandView.focusInput()
                readonly property int count: main.views.length > 0 ? main.views.length : 1
                // Riquadro della vista i: 0 una, 1 due affiancate, 2 due una sopra l'altra, 3 una grande a sinistra e
                // due a destra, 4 quattro.
                function tile(i) {
                    const w = width, h = height, g = 2
                    const hw = (w - g) / 2, hh = (h - g) / 2
                    switch (count > 1 ? main.tileMode : 0) {
                    case 1: return Qt.rect(i * (hw + g), 0, hw, h)
                    case 2: return Qt.rect(0, i * (hh + g), w, hh)
                    case 3: return i === 0 ? Qt.rect(0, 0, hw, h) : Qt.rect(hw + g, (i - 1) * (hh + g), hw, hh)
                    case 4: return Qt.rect((i % 2) * (hw + g), Math.floor(i / 2) * (hh + g), hw, hh)
                    default: return Qt.rect(0, 0, w, h)
                    }
                }
                Repeater {
                    model: main.views.length > 0 ? main.views : [main.view]
                    delegate: CadCanvas {
                        required property var modelData
                        required property int index
                        readonly property rect area: canvas.tile(index)
                        x: area.x; y: area.y; width: area.width; height: area.height
                        view: modelData
                    }
                }
            }

            // Riga di comando sovrapposta in basso, al centro.
            Loader {
                id: commandView
                visible: main.commandLineVisible && !main.startVisible
                width: Math.min(center.width - 40, Math.max(520, center.width * 0.62))
                height: item ? Math.max(30, item.implicitHeight) : 30
                x: (center.width - width) / 2
                y: center.height - height - 14
                source: "qrc:/qml/CommandLine.qml"
                function focusInput() { if (item) item.focusInput() }
                function report() {
                    const p = mapToItem(canvas, 0, height - 30)
                    main.setCommandBar(Math.round(p.x), Math.round(p.y), Math.round(width), 30)
                }
                onXChanged: report()
                onYChanged: report()
                onWidthChanged: report()
                onHeightChanged: report()
                onLoaded: { report(); focusInput() }
            }
        }
    }

    Connections {
        target: main
        function onFocusCommandLineRequested() { commandView.focusInput() }
    }
}
