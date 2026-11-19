// Tavolozza Proprietà: selezione per tipo, sezioni comprimibili e righe modificabili (modello: PropertiesPalette).
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    component SmallButton: Rectangle {
        id: b
        property string label
        property string tip
        signal clicked()
        width: 26; height: 24
        radius: 2
        color: area.containsMouse ? Theme.hover : Theme.field
        border.color: Theme.fieldBorder
        Text { anchors.centerIn: parent; text: b.label; color: Theme.text; font.pixelSize: 13 }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, b.tip) }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); b.clicked() }
        }
    }

    // In alto: selezione divisa per tipo e pulsanti di selezione.
    Row {
        id: top
        x: 4; y: 4
        width: parent.width - 8
        spacing: 4
        Rectangle {
            id: summary
            width: top.width - 3 * 30
            height: 24
            radius: 2
            color: Theme.field
            border.color: summaryArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
            opacity: props.summary.length > 1 ? 1 : 0.85
            Text {
                x: 6
                width: parent.width - 24
                anchors.verticalCenter: parent.verticalCenter
                text: props.summary.length > props.summaryIndex ? props.summary[props.summaryIndex] : ""
                color: Theme.text
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.muted }
            MouseArea {
                id: summaryArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: { const p = summary.mapToGlobal(0, summary.height); props.popupSummary(p.x, p.y) }
            }
        }
        SmallButton { label: "≈"; tip: qsTr("Seleziona simili (SELECTSIMILAR)"); onClicked: props.selectSimilar() }
        SmallButton { label: "▣"; tip: qsTr("Seleziona tutto nello spazio corrente"); onClicked: props.selectAllInSpace() }
        SmallButton { label: "✕"; tip: qsTr("Deseleziona tutto"); onClicked: props.selectNone() }
    }

    ListView {
        id: list
        anchors.top: top.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: props.rowCount
        spacing: 2
        delegate: Loader {
            required property int index
            width: list.width
            property var row: (props && props.rows[index]) || ({})
            property int rowIndex: index
            sourceComponent: row.kind === "section" ? sectionRow : valueRow
        }

        // Barra di scorrimento semplice.
        Rectangle {
            visible: list.contentHeight > list.height
            anchors.right: parent.right
            width: 6
            radius: 3
            color: Theme.hover
            y: list.visibleArea.yPosition * list.height
            height: Math.max(20, list.visibleArea.heightRatio * list.height)
        }
    }

    Component {
        id: sectionRow
        Rectangle {
            height: 26
            color: "#333a46"
            Text {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                text: (row.collapsed ? "▸  " : "▾  ") + row.label
                color: "#ffffff"
                font.pixelSize: 12
                font.bold: true
            }
            MouseArea { anchors.fill: parent; onClicked: props.toggleSection(row.label) }
        }
    }

    Component {
        id: valueRow
        Item {
            height: 26
            // Il campo modificabile prende il valore nuovo solo se non ci si sta scrivendo.
            readonly property string rowValue: row.value === undefined ? "" : String(row.value)
            onRowValueChanged: if (!input.activeFocus) input.text = rowValue
            Component.onCompleted: input.text = rowValue
            Text {
                id: label
                x: 8
                width: parent.width * 0.44 - 8
                anchors.verticalCenter: parent.verticalCenter
                text: row.label
                color: Theme.muted2
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Rectangle {
                id: editor
                x: parent.width * 0.44
                width: parent.width - x - 10
                height: 24
                anchors.verticalCenter: parent.verticalCenter
                radius: 2
                color: row.kind === "readonly" ? "#343b47" : Theme.field
                border.color: input.activeFocus ? Theme.accent : (editorArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder)

                // Testo o numero modificabile: si conferma con Invio o uscendo dal campo.
                TextInput {
                    id: input
                    visible: row.kind === "edit"
                    x: 6
                    width: parent.width - 12
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.text
                    selectionColor: Theme.accent
                    selectByMouse: true
                    clip: true
                    font.pixelSize: 12
                    onEditingFinished: if (text !== row.value) props.commit(rowIndex, text)
                    Keys.onEscapePressed: { text = row.value; focus = false }
                }
                Row {
                    visible: row.kind !== "edit"
                    x: 6
                    spacing: 6
                    width: parent.width - (row.kind === "list" ? 24 : 12)
                    anchors.verticalCenter: parent.verticalCenter
                    clip: true
                    Image {
                        visible: (row.icon || "") !== ""
                        source: row.icon || ""
                        sourceSize: Qt.size(14, 14)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: row.kind === "counter" ? "◂  " + row.value + "  ▸" : row.value
                        color: row.kind === "readonly" ? Theme.muted2 : Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    visible: row.kind === "list"
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: Theme.muted
                }
                MouseArea {
                    id: editorArea
                    anchors.fill: parent
                    visible: row.kind !== "edit" && row.kind !== "readonly"
                    hoverEnabled: true
                    onClicked: (mouse) => {
                        if (row.kind === "list") {
                            const p = editor.mapToGlobal(0, editor.height)
                            props.popupChoice(rowIndex, p.x, p.y)
                        } else if (row.kind === "button") {
                            props.press(rowIndex)
                        } else if (row.kind === "counter") {
                            props.step(rowIndex, mouse.x < editor.width / 2 ? -1 : 1)
                        }
                    }
                }
            }
        }
    }
}
