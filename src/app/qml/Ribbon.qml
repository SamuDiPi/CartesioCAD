// Barra multifunzione di CartesioCAD: accesso rapido, schede e pannelli (modello: Ribbon in C++).
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window
    property int currentTab: 0
    onCurrentTabChanged: scheduleFit()

    // Pannelli che non ci stanno: si chiudono prima quelli con priorità più bassa, a parità quelli più a destra.
    function scheduleFit() { fitTimer.restart() }
    Timer {
        id: fitTimer
        interval: 0
        onTriggered: root.fitPanels()
    }
    function fitPanels() {
        const items = []
        for (let i = 0; i < panelRepeater.count; ++i) {
            const it = panelRepeater.itemAt(i)
            if (it)
                items.push(it)
        }
        let total = 0
        for (const it of items)
            total += it.fullWidth
        const available = panels.width - 4
        const order = items.map((it, i) => ({ it: it, i: i, p: it.model ? it.model.priority : 0 }))
        order.sort((a, b) => a.p !== b.p ? a.p - b.p : b.i - a.i)
        const close = new Set()
        for (const o of order) {
            if (total <= available)
                break
            close.add(o.it)
            total -= o.it.fullWidth - o.it.collapsedWidth
        }
        // Secondo giro: si riaprono i pannelli chiusi più importanti che ci stanno nello spazio rimasto.
        for (let k = order.length - 1; k >= 0; --k) {
            const o = order[k]
            if (!close.has(o.it))
                continue
            const extra = o.it.fullWidth - o.it.collapsedWidth
            if (total + extra <= available) {
                close.delete(o.it)
                total += extra
            }
        }
        for (const it of items)
            it.collapsed = close.has(it)
    }
    // Schede contestuali (Editor di testo): la barra le seleziona quando compaiono.
    Connections {
        target: ribbon
        function onSelectedTabChanged() { root.currentTab = ribbon.selectedTab }
    }

    // ---------------------------------------------------------------- componenti

    // Suggerimento nativo dopo una breve sosta del mouse.
    component TipArea: MouseArea {
        id: area
        property string tip: ""
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        Timer {
            id: tipTimer
            interval: 650
            onTriggered: {
                const p = area.mapToGlobal(area.mouseX + 12, area.mouseY + 18)
                ui.showToolTip(p.x, p.y, area.tip)
            }
        }
        onEntered: tipTimer.restart()
        onExited: { tipTimer.stop(); ui.hideToolTip() }
        onPressed: { tipTimer.stop(); ui.hideToolTip() }
    }

    // Sfondo dei pulsanti: evidenziato al passaggio, blu se premuto o spuntato.
    component ButtonFrame: Rectangle {
        property bool hovered: false
        property bool down: false
        property bool checked: false
        radius: 2
        color: down || checked ? Theme.checked : (hovered ? Theme.hover : "transparent")
        border.color: down || checked ? Theme.accent : (hovered ? Theme.hoverBorder : "transparent")
    }

    // Cornice arancione dei percorsi guidati sul pulsante da usare.
    component GuideFrame: Rectangle {
        property var action
        anchors.fill: parent
        anchors.margins: -2
        radius: 3
        color: "transparent"
        border.color: "#ff9a1f"
        border.width: 2.5
        visible: ribbon.highlight !== "" && action && action.shortText === ribbon.highlight
        SequentialAnimation on opacity {
            running: visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 550 }
            NumberAnimation { to: 1.0; duration: 550 }
        }
    }

    // Pulsante grande: icona 32 px e testo sotto; con un menu, la parte bassa apre le varianti.
    component LargeButton: Item {
        id: big
        property var action
        width: Math.max(54, label.implicitWidth + 10)
        height: 84
        opacity: action && action.enabled ? 1 : 0.4
        ButtonFrame {
            anchors.fill: parent
            hovered: iconArea.containsMouse || menuArea.containsMouse
            down: iconArea.pressed
            checked: big.action && big.action.checked
        }
        GuideFrame { action: big.action; z: 5 }
        Image {
            id: iconImage
            source: big.action ? big.action.icon : ""
            sourceSize: Qt.size(32, 32)
            width: 32; height: 32
            anchors.horizontalCenter: parent.horizontalCenter
            y: 6
        }
        Text {
            id: label
            text: big.action ? big.action.text + (big.action.hasMenu ? " ▾" : "") : ""
            color: Theme.text
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            y: 42
            lineHeight: 0.95
        }
        TipArea {
            id: iconArea
            anchors.fill: parent
            anchors.bottomMargin: big.action && big.action.hasMenu ? parent.height - 40 : 0
            tip: big.action ? big.action.tooltip : ""
            onClicked: if (big.action) big.action.trigger()
        }
        TipArea {
            id: menuArea
            visible: big.action && big.action.hasMenu
            anchors.fill: parent
            anchors.topMargin: 40
            tip: big.action ? big.action.tooltip : ""
            onClicked: {
                const p = big.mapToGlobal(0, big.height)
                big.action.popup(p.x, p.y)
            }
        }
    }

    // Pulsante piccolo: icona 16 px, testo facoltativo, freccia del menu.
    component SmallButton: Item {
        id: small
        property var action
        property bool showText: true
        width: row.implicitWidth + 8
        height: 26
        visible: action && action.visible
        opacity: action && action.enabled ? 1 : 0.4
        ButtonFrame {
            anchors.fill: parent
            hovered: area.containsMouse
            down: area.pressed
            checked: small.action && small.action.checked
        }
        GuideFrame { action: small.action; z: 5 }
        Row {
            id: row
            x: 4
            spacing: 5
            anchors.verticalCenter: parent.verticalCenter
            Image {
                source: small.action ? small.action.icon : ""
                sourceSize: Qt.size(16, 16)
                width: 16; height: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: small.showText
                text: small.action ? small.action.shortText : ""
                color: Theme.text
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: small.action && small.action.hasMenu
                text: "▾"
                color: Theme.muted
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        TipArea {
            id: area
            anchors.fill: parent
            tip: small.action ? small.action.tooltip : ""
            onClicked: (mouse) => {
                if (!small.action)
                    return
                // Con un menu: la freccia lo apre, il resto esegue il comando.
                if (small.action.hasMenu && mouse.x > small.width - 14) {
                    const p = small.mapToGlobal(0, small.height)
                    small.action.popup(p.x, p.y)
                } else {
                    small.action.trigger()
                }
            }
        }
    }

    // Elenco a discesa (layer, colore, tipo e spessore di linea): si apre come menu.
    component Combo: Item {
        id: combo
        property var model
        width: model ? model.width : 160
        height: 24
        opacity: model && model.enabled ? 1 : 0.45
        Rectangle {
            anchors.fill: parent
            radius: 2
            color: Theme.field
            border.color: comboArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
        }
        Row {
            x: 5
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 22
            clip: true
            Image {
                visible: source != ""
                source: combo.model ? combo.model.currentIcon : ""
                sourceSize: Qt.size(combo.model ? combo.model.iconWidth : 16, 16)
                width: combo.model ? combo.model.iconWidth : 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: combo.model ? combo.model.currentText : ""
                color: Theme.text
                font.pixelSize: 11
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            text: "▾"
            color: Theme.muted
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
        }
        TipArea {
            id: comboArea
            anchors.fill: parent
            tip: combo.model ? combo.model.tooltip : ""
            onClicked: {
                const p = combo.mapToGlobal(0, combo.height)
                combo.model.popup(p.x, p.y)
            }
        }
    }

    // Un pannello: colonne di pulsanti e titolo in basso.
    component Panel: Rectangle {
        id: panel
        property var model
        // Chiuso quando non c'è posto: un pulsante con il nome che apre un menu con i comandi del pannello.
        property bool collapsed: false
        readonly property int fullWidth: content.implicitWidth + 12
        readonly property int collapsedWidth: Math.max(64, collapsedLabel.implicitWidth + 16)
        width: collapsed ? collapsedWidth : fullWidth
        height: parent ? parent.height : 104
        color: Theme.window
        onFullWidthChanged: root.scheduleFit()
        Item {
            id: collapsedButton
            visible: panel.collapsed
            x: 3; y: 3
            width: parent.width - 6
            height: 84
            ButtonFrame {
                anchors.fill: parent
                hovered: collapsedArea.containsMouse
                down: collapsedArea.pressed
            }
            Image {
                source: panel.model ? panel.model.icon : ""
                sourceSize: Qt.size(32, 32)
                width: 32; height: 32
                anchors.horizontalCenter: parent.horizontalCenter
                y: 6
            }
            Text {
                id: collapsedLabel
                text: (panel.model ? panel.model.title : "") + " \u25BE"
                color: Theme.text
                font.pixelSize: 11
                anchors.horizontalCenter: parent.horizontalCenter
                y: 44
            }
            TipArea {
                id: collapsedArea
                anchors.fill: parent
                tip: panel.model ? panel.model.title : ""
                onClicked: {
                    const p = collapsedButton.mapToGlobal(0, collapsedButton.height)
                    panel.model.popup(p.x, p.y)
                }
            }
        }
        Row {
            id: content
            visible: !panel.collapsed
            x: 6
            y: 3
            spacing: 3
            height: 86
            Repeater {
                model: panel.model ? panel.model.columns : []
                delegate: Loader {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: modelData.kind === "large" ? largeColumn
                                   : modelData.kind === "small" ? smallColumn : comboColumn
                    Component {
                        id: largeColumn
                        LargeButton { action: modelData.action }
                    }
                    Component {
                        id: smallColumn
                        Column {
                            id: smallCol
                            spacing: 2
                            property bool withText: modelData.showText
                            Repeater {
                                model: modelData.actions
                                delegate: SmallButton {
                                    required property var modelData
                                    action: modelData
                                    showText: smallCol.withText
                                }
                            }
                        }
                    }
                    Component {
                        id: comboColumn
                        Column {
                            spacing: 3
                            Repeater {
                                model: modelData.combos
                                delegate: Combo {
                                    required property var modelData
                                    model: modelData
                                }
                            }
                            Repeater {
                                model: modelData.rows
                                delegate: Row {
                                    required property var modelData
                                    spacing: 1
                                    Repeater {
                                        model: modelData
                                        delegate: SmallButton {
                                            required property var modelData
                                            action: modelData
                                            showText: false
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 17
            color: Theme.panelTitle
            Text {
                anchors.centerIn: parent
                text: panel.model ? panel.model.title : ""
                color: Theme.muted2
                font.pixelSize: 10
            }
        }
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.separator
        }
    }

    // ---------------------------------------------------------------- barra di accesso rapido

    Rectangle {
        id: quickBar
        width: parent.width
        height: 34
        color: Theme.bar
        Row {
            x: 4
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                width: 34; height: 28
                radius: 3
                color: appArea.containsMouse ? "#c94a3d" : "#b03a2e"
                Image {
                    anchors.centerIn: parent
                    source: ribbon.appAction ? ribbon.appAction.icon : ""
                    sourceSize: Qt.size(22, 22)
                }
                TipArea {
                    id: appArea
                    anchors.fill: parent
                    tip: qsTr("Menu dell'applicazione")
                    onClicked: {
                        const p = parent.mapToGlobal(0, parent.height)
                        ribbon.appAction.popup(p.x, p.y)
                    }
                }
            }
            Item { width: 6; height: 1 }
            Repeater {
                model: ribbon.quickActions
                delegate: SmallButton {
                    required property var modelData
                    action: modelData
                    showText: false
                }
            }
        }
        Text {
            anchors.centerIn: parent
            text: ribbon.title
            color: Theme.muted3
            font.pixelSize: 12
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    // ---------------------------------------------------------------- schede

    Rectangle {
        id: tabBar
        y: quickBar.height
        width: parent.width
        height: 28
        color: Theme.bar
        // Guida (F1): sempre lì, si apre solo se la si chiede.
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: helpRow.implicitWidth + 14
            height: 22
            radius: 11
            color: helpArea.containsMouse ? Theme.tabHover : "transparent"
            border.color: helpArea.containsMouse ? Theme.accent : "#4a5567"
            Row {
                id: helpRow
                anchors.centerIn: parent
                spacing: 5
                Text { text: "?"; color: Theme.accent; font.pixelSize: 13; font.bold: true }
                Text { text: qsTr("Guida"); color: Theme.muted3; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                id: helpArea
                anchors.fill: parent
                hoverEnabled: true
                onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, qsTr("Percorsi guidati e manuale di tutte le funzioni (F1)")) }
                onExited: ui.hideToolTip()
                onClicked: { ui.hideToolTip(); ribbon.requestHelp() }
            }
        }
        Row {
            x: 4
            height: parent.height
            Repeater {
                model: ribbon.tabs
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: tabText.implicitWidth + 28
                    height: parent.height
                    color: index === root.currentTab ? Theme.window : (tabArea.containsMouse ? Theme.tabHover : "transparent")
                    Rectangle {
                        visible: index === root.currentTab
                        width: parent.width; height: 2
                        color: Theme.accent
                    }
                    // Scheda contestuale: sfondo azzurro come la scheda di un editor aperto.
                    Rectangle {
                        visible: modelData.contextual === true
                        anchors.fill: parent
                        anchors.topMargin: 3
                        color: Theme.accent
                        opacity: index === root.currentTab ? 0.85 : 0.55
                        radius: 1
                    }
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: modelData.title
                        color: index === root.currentTab || modelData.contextual === true ? "#ffffff" : Theme.muted3
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: tabArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { root.currentTab = index; ribbon.tabClicked(index) }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- pannelli

    Flickable {
        id: panels
        y: tabBar.y + tabBar.height
        width: parent.width
        height: parent.height - y - 1
        contentWidth: panelRow.width
        contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: panelRow
            height: panels.height
            Repeater {
                id: panelRepeater
                model: ribbon.tabs.length > root.currentTab ? ribbon.tabs[root.currentTab].panels : []
                delegate: Panel {
                    required property var modelData
                    model: modelData
                }
                onCountChanged: root.scheduleFit()
            }
        }
        onWidthChanged: root.scheduleFit()
        // Rotella: scorre i pannelli quando non ci stanno.
        WheelHandler {
            orientation: Qt.Vertical
            property: "contentX"
            rotationScale: -3
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
}
