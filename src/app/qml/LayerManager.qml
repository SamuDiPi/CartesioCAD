// Gestore proprietà layer: barra degli strumenti e tabella dei layer (modello: LayerManager in C++).
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window
    enabled: layers.hasDrawing
    opacity: enabled ? 1 : 0.5

    // Larghezze delle colonne.
    readonly property var widths: ({ status: 30, name: 150, on: 46, freeze: 62, vp: 70, lock: 46, color: 110, linetype: 110, weight: 100 })
    readonly property int tableWidth: widths.status + widths.name + widths.on + widths.freeze + widths.vp + widths.lock
                                      + widths.color + widths.linetype + widths.weight

    component ToolButton: Rectangle {
        id: tb
        property string icon
        property string tip
        signal clicked()
        width: 30; height: 30
        radius: 2
        color: area.containsMouse ? Theme.hover : "transparent"
        Image { anchors.centerIn: parent; source: tb.icon; sourceSize: Qt.size(22, 22) }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, tb.tip) }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); tb.clicked() }
        }
    }

    // Cella con un'icona che si clicca (acceso, congelato, bloccato).
    component IconCell: Item {
        id: cell
        property string icon
        property string tip
        signal clicked()
        height: 26
        Image { anchors.centerIn: parent; source: cell.icon; sourceSize: Qt.size(16, 16); visible: cell.icon !== "" }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, cell.tip) }
            onExited: ui.hideToolTip()
            onClicked: cell.clicked()
        }
    }

    Row {
        id: toolbar
        x: 4; y: 4
        spacing: 2
        ToolButton { icon: layers.icons.new; tip: qsTr("Nuovo layer"); onClicked: layers.newLayer() }
        ToolButton { icon: layers.icons.delete; tip: qsTr("Elimina layer"); onClicked: layers.deleteLayer() }
        ToolButton { icon: layers.icons.makeCurrent; tip: qsTr("Imposta corrente"); onClicked: layers.makeCurrent() }
    }

    ListView {
        id: table
        anchors.top: toolbar.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalAndVerticalFlick
        contentWidth: root.tableWidth
        model: layers.rows
        headerPositioning: ListView.OverlayHeader
        header: Rectangle {
            z: 2
            width: Math.max(table.width, root.tableWidth)
            height: 26
            color: "#333a46"
            Row {
                height: parent.height
                Repeater {
                    model: [
                        [qsTr("Stato"), root.widths.status], [qsTr("Nome"), root.widths.name], [qsTr("Acceso"), root.widths.on],
                        [qsTr("Congelato"), root.widths.freeze], [qsTr("Cong. finestra"), root.widths.vp],
                        [qsTr("Blocca"), root.widths.lock], [qsTr("Colore"), root.widths.color],
                        [qsTr("Tipo di linea"), root.widths.linetype], [qsTr("Spessore"), root.widths.weight]
                    ]
                    delegate: Item {
                        required property var modelData
                        width: modelData[1]
                        height: parent.height
                        Text {
                            x: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 8
                            text: modelData[0]
                            color: Theme.muted3
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#262b34" }
                    }
                }
            }
        }

        Connections {
            target: layers
            function onEditNameRequested(row) {
                table.positionViewAtIndex(row, ListView.Contain)
                const item = table.itemAtIndex(row)
                if (item)
                    item.startEdit()
            }
        }

        delegate: Rectangle {
            id: rowItem
            required property var modelData
            required property int index
            width: Math.max(table.width, root.tableWidth)
            height: 26
            color: index === layers.selectedRow ? Theme.checked : (index % 2 ? "#2f3640" : "#2a303a")

            function startEdit() { nameEdit.visible = true; nameEdit.text = modelData.name; nameEdit.selectAll(); nameEdit.forceActiveFocus() }

            MouseArea {
                anchors.fill: parent
                onClicked: layers.select(rowItem.index)
                onDoubleClicked: (mouse) => {
                    if (mouse.x < root.widths.status)
                        layers.makeCurrent()
                    else if (mouse.x < root.widths.status + root.widths.name && rowItem.modelData.renamable)
                        rowItem.startEdit()
                }
            }
            Row {
                height: parent.height
                Item {
                    width: root.widths.status; height: parent.height
                    Image { anchors.centerIn: parent; visible: rowItem.modelData.current; source: layers.icons.current; sourceSize: Qt.size(16, 16) }
                }
                Item {
                    width: root.widths.name; height: parent.height
                    Text {
                        x: 6
                        width: parent.width - 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !nameEdit.visible
                        text: rowItem.modelData.name
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        visible: nameEdit.visible
                        color: Theme.field
                        border.color: Theme.accent
                        TextInput {
                            id: nameEdit
                            visible: false
                            x: 4
                            width: parent.width - 8
                            anchors.verticalCenter: parent.verticalCenter
                            color: "#ffffff"
                            selectionColor: Theme.accent
                            font.pixelSize: 12
                            clip: true
                            onEditingFinished: { if (visible) { visible = false; layers.rename(rowItem.index, text) } }
                            Keys.onEscapePressed: visible = false
                        }
                    }
                }
                IconCell {
                    width: root.widths.on
                    icon: rowItem.modelData.on ? layers.icons.on : layers.icons.off
                    tip: rowItem.modelData.on ? qsTr("Acceso: clic per spegnere") : qsTr("Spento: clic per accendere")
                    onClicked: layers.toggle(rowItem.index, "on")
                }
                IconCell {
                    width: root.widths.freeze
                    icon: rowItem.modelData.frozen ? layers.icons.snow : layers.icons.sun
                    tip: rowItem.modelData.frozen ? qsTr("Congelato: clic per scongelare") : qsTr("Clic per congelare")
                    onClicked: layers.toggle(rowItem.index, "freeze")
                }
                IconCell {
                    width: root.widths.vp
                    icon: rowItem.modelData.vpFrozen < 0 ? "" : (rowItem.modelData.vpFrozen ? layers.icons.snow : layers.icons.sun)
                    tip: rowItem.modelData.vpFrozen < 0 ? qsTr("Disponibile lavorando in una finestra di layout (doppio clic nella finestra)")
                       : (rowItem.modelData.vpFrozen ? qsTr("Congelato in questa finestra: clic per scongelare")
                                                     : qsTr("Clic per congelare solo in questa finestra"))
                    onClicked: layers.toggle(rowItem.index, "vpfreeze")
                }
                IconCell {
                    width: root.widths.lock
                    icon: rowItem.modelData.locked ? layers.icons.lockClosed : layers.icons.lockOpen
                    tip: rowItem.modelData.locked ? qsTr("Bloccato: clic per sbloccare") : qsTr("Clic per bloccare")
                    onClicked: layers.toggle(rowItem.index, "lock")
                }
                Item {
                    width: root.widths.color; height: parent.height
                    Row {
                        x: 6
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle { width: 14; height: 14; color: rowItem.modelData.color; border.color: "#141820"; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: rowItem.modelData.colorName; color: Theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { layers.select(rowItem.index); layers.pickColor(rowItem.index) } }
                }
                Item {
                    id: linetypeCell
                    width: root.widths.linetype; height: parent.height
                    Text { x: 6; width: parent.width - 8; anchors.verticalCenter: parent.verticalCenter; text: rowItem.modelData.linetype; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            layers.select(rowItem.index)
                            const p = linetypeCell.mapToGlobal(0, linetypeCell.height)
                            layers.popupLinetype(rowItem.index, p.x, p.y)
                        }
                    }
                }
                Item {
                    id: weightCell
                    width: root.widths.weight; height: parent.height
                    Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: rowItem.modelData.lineweight; color: Theme.text; font.pixelSize: 12 }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            layers.select(rowItem.index)
                            const p = weightCell.mapToGlobal(0, weightCell.height)
                            layers.popupLineweight(rowItem.index, p.x, p.y)
                        }
                    }
                }
            }
        }
    }
}
