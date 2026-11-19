// Tavolozza Circuiti: schema unifilare, tabella dei circuiti, verifica, legenda, assegnazione e layer
// (modello: CircuitsPanel in C++).
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    component Button: Rectangle {
        id: b
        property string text
        property string tip
        signal clicked()
        width: label.implicitWidth + 24
        height: 26
        radius: 2
        color: area.pressed ? Theme.checked : (area.containsMouse ? "#56637a" : Theme.hover)
        border.color: Theme.hoverBorder
        Text { id: label; anchors.centerIn: parent; text: b.text; color: Theme.text; font.pixelSize: 12 }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { if (b.tip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, b.tip) } }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); b.clicked() }
        }
    }

    component Combo: Rectangle {
        id: c
        property var model
        width: model ? model.width : 120
        height: 24
        radius: 2
        color: Theme.field
        border.color: area.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
        Text {
            x: 6
            width: parent.width - 22
            anchors.verticalCenter: parent.verticalCenter
            text: c.model ? c.model.currentText : ""
            color: Theme.text
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.muted }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onClicked: { const p = c.mapToGlobal(0, c.height); c.model.popup(p.x, p.y) }
        }
    }

    component Field: Rectangle {
        id: f
        property alias text: input.text
        property string placeholder
        signal edited(string text)
        signal finished()
        height: 24
        radius: 2
        color: Theme.field
        border.color: input.activeFocus ? Theme.accent : Theme.fieldBorder
        TextInput {
            id: input
            x: 6
            width: parent.width - 12
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            selectionColor: Theme.accent
            selectByMouse: true
            clip: true
            font.pixelSize: 12
            onTextEdited: f.edited(text)
            onEditingFinished: f.finished()
            Text { visible: input.text.length === 0; text: f.placeholder; color: "#7d8796"; font: input.font }
        }
    }

    // Tabella semplice: intestazione fissa, righe con celle di larghezza data, scorrimento in due direzioni.
    component Table: ListView {
        id: t
        property var columns: []          // [[titolo, larghezza], ...]
        property int selected: -1
        property int editableColumn: -1
        signal rowClicked(int row)
        signal rowDoubleClicked(int row, int column)
        signal cellEdited(int row, string text)
        readonly property int tableWidth: {
            let w = 0
            for (let i = 0; i < columns.length; ++i) w += columns[i][1]
            return w
        }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalAndVerticalFlick
        contentWidth: tableWidth
        headerPositioning: ListView.OverlayHeader
        header: Rectangle {
            z: 2
            width: Math.max(t.width, t.tableWidth)
            height: 24
            color: "#333a46"
            Row {
                height: parent.height
                Repeater {
                    model: t.columns
                    delegate: Item {
                        required property var modelData
                        width: modelData[1]
                        height: parent.height
                        Text { x: 6; width: parent.width - 8; anchors.verticalCenter: parent.verticalCenter; text: modelData[0]; color: Theme.muted3; font.pixelSize: 11; elide: Text.ElideRight }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#262b34" }
                    }
                }
            }
        }
        delegate: Rectangle {
            id: rowItem
            required property var modelData
            required property int index
            width: Math.max(t.width, t.tableWidth)
            height: 24
            color: index === t.selected ? Theme.checked : (index % 2 ? "#2f3640" : "#2a303a")
            Row {
                height: parent.height
                Repeater {
                    model: t.columns.length
                    delegate: Item {
                        id: cell
                        required property int index
                        width: t.columns[index][1]
                        height: parent.height
                        Text {
                            x: 6
                            width: parent.width - 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !edit.visible
                            text: rowItem.modelData.cells[cell.index]
                            color: rowItem.modelData.attention && (cell.index === 0 || cell.index === 5) ? "#dc6e1e"
                                 : rowItem.modelData.dim ? "#8c919b" : Theme.text
                            font.pixelSize: 12
                            font.bold: rowItem.modelData.attention === true && (cell.index === 0 || cell.index === 5)
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            visible: edit.visible
                            color: Theme.field
                            border.color: Theme.accent
                            TextInput {
                                id: edit
                                visible: false
                                x: 4
                                width: parent.width - 8
                                anchors.verticalCenter: parent.verticalCenter
                                color: "#ffffff"
                                selectionColor: Theme.accent
                                font.pixelSize: 12
                                clip: true
                                onEditingFinished: { if (visible) { visible = false; t.cellEdited(rowItem.index, text) } }
                                Keys.onEscapePressed: visible = false
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                const tip = rowItem.modelData.tips ? rowItem.modelData.tips[cell.index] : rowItem.modelData.tip
                                if (tip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, tip) }
                            }
                            onExited: ui.hideToolTip()
                            onClicked: t.rowClicked(rowItem.index)
                            onDoubleClicked: {
                                if (cell.index === t.editableColumn) {
                                    edit.text = rowItem.modelData.cells[cell.index]
                                    edit.visible = true
                                    edit.selectAll()
                                    edit.forceActiveFocus()
                                } else {
                                    t.rowDoubleClicked(rowItem.index, cell.index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Column {
        id: content
        x: 6; y: 6
        width: parent.width - 12
        height: parent.height - 12
        spacing: 6

        Row {
            spacing: 6
            Button { text: qsTr("Importa schema PDF..."); tip: qsTr("Schema unifilare dei quadri in PDF: si leggono i circuiti"); onClicked: panel.chooseFile() }
            Button { text: qsTr("Verifica"); tip: qsTr("Confronta le sigle in pianta con lo schema"); onClicked: panel.verify() }
        }
        Text { width: parent.width; text: panel.statusText; color: Theme.muted2; font.pixelSize: 12; wrapMode: Text.WordWrap }
        Row {
            spacing: 6
            width: parent.width
            Combo { model: panel.boardCombo; width: Math.min(200, (content.width - 116) / 2) }
            Combo { model: panel.kindCombo }
            Field {
                width: content.width - x
                placeholder: qsTr("Cerca sigla o descrizione")
                onEdited: (text) => panel.setSearch(text)
            }
        }

        // Schede.
        Row {
            height: 26
            Repeater {
                model: [qsTr("Circuiti"), qsTr("Verifica"), qsTr("Legenda")]
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: tabLabel.implicitWidth + 24
                    height: 26
                    color: index === panel.currentTab ? "#2a303a" : (tabArea.containsMouse ? "#353c48" : "transparent")
                    Rectangle { visible: index === panel.currentTab; width: parent.width; height: 2; color: Theme.accent }
                    Text { id: tabLabel; anchors.centerIn: parent; text: modelData; color: index === panel.currentTab ? "#ffffff" : Theme.muted3; font.pixelSize: 12 }
                    MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: panel.setTab(index) }
                }
            }
        }

        Item {
            id: pages
            width: parent.width
            height: content.height - y - bottom.height - 6

            Table {
                anchors.fill: parent
                visible: panel.currentTab === 0
                model: panel.circuitRows
                selected: panel.selectedCircuit
                columns: [[qsTr("Sigla"), 120], [qsTr("Tipo"), 60], [qsTr("Descrizione"), 230], [qsTr("kW"), 60],
                          [qsTr("Fase"), 130], [qsTr("Interruttore"), 220], [qsTr("Cavo"), 220], [qsTr("In pianta"), 70]]
                onRowClicked: (row) => panel.selectCircuit(row)
                onRowDoubleClicked: (row, column) => panel.openCircuit(row)
            }

            ListView {
                anchors.fill: parent
                visible: panel.currentTab === 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: panel.checkGroups
                delegate: Column {
                    id: group
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    Rectangle {
                        width: parent.width
                        height: 26
                        color: "#333a46"
                        Text {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: (group.modelData.expanded ? "▾  " : "▸  ") + group.modelData.title + "   (" + group.modelData.count + ")"
                            color: "#ffffff"
                            font.pixelSize: 12
                            font.bold: true
                        }
                        MouseArea { anchors.fill: parent; onClicked: panel.toggleGroup(group.index) }
                    }
                    Repeater {
                        model: group.modelData.expanded ? group.modelData.items : []
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: group.width
                            height: 22
                            color: index % 2 ? "#2f3640" : "#2a303a"
                            Text { x: 24; width: parent.width * 0.5 - 24; anchors.verticalCenter: parent.verticalCenter; text: modelData.text; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { x: parent.width * 0.5; width: parent.width * 0.5 - 6; anchors.verticalCenter: parent.verticalCenter; text: modelData.detail; color: Theme.muted2; font.pixelSize: 12; elide: Text.ElideRight }
                            MouseArea { anchors.fill: parent; onDoubleClicked: panel.openCheckItem(group.index, index) }
                        }
                    }
                }
            }

            Table {
                anchors.fill: parent
                visible: panel.currentTab === 2
                model: panel.legendRows
                editableColumn: 2
                columns: [[qsTr("Lettera"), 60], [qsTr("Legenda"), 180], [qsTr("Parole chiave"), 160], [qsTr("Circuiti"), 110],
                          [qsTr("Punti"), 110], [qsTr("Note"), 340]]
                onRowDoubleClicked: (row, column) => panel.openLegend(row, column)
                onCellEdited: (row, text) => panel.editKeywords(row, text)
            }
        }

        Column {
            id: bottom
            width: parent.width
            spacing: 6
            Button {
                text: qsTr("Assegna la sigla ai punti selezionati")
                tip: qsTr("Scrive la sigla del circuito scelto accanto a ogni punto selezionato nel disegno (annullabile)")
                onClicked: panel.assignSelected()
            }
            Rectangle {
                width: parent.width
                height: layersGrid.height + 26
                color: "transparent"
                border.color: "#4a5567"
                radius: 2
                Text { x: 8; y: 4; text: qsTr("Layer"); color: Theme.muted2; font.pixelSize: 11 }
                Column {
                    id: layersGrid
                    x: 8; y: 20
                    width: parent.width - 16
                    spacing: 4
                    Repeater {
                        model: [
                            [qsTr("Punti:"), panel.pointLayers, qsTr("Prefissi dei layer dei punti, separati da ;")],
                            [qsTr("Lettere:"), panel.letterLayers, ""],
                            [qsTr("Sigle:"), panel.tagLayers, ""]
                        ]
                        delegate: Row {
                            required property var modelData
                            required property int index
                            spacing: 6
                            Text { width: 60; anchors.verticalCenter: parent.verticalCenter; text: modelData[0]; color: Theme.muted2; font.pixelSize: 12 }
                            Field {
                                id: field
                                width: layersGrid.width - 66
                                text: modelData[1]
                                onFinished: {
                                    const values = [panel.pointLayers, panel.letterLayers, panel.tagLayers]
                                    values[index] = field.text
                                    if (field.text !== modelData[1])
                                        panel.setLayers(values[0], values[1], values[2])
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
