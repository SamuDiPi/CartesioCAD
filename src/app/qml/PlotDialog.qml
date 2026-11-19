// Dialogo Stampa / Imposta pagina (modello: PlotDialog), disposto: a sinistra impostazione di pagina,
// stampante, formato, area, offset e scala; a destra (pulsante ">") stili, qualità, opzioni e orientamento.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window
    focus: true
    Keys.onEscapePressed: plot.cancelClicked()

    // ------------------------------------------------------------ controlli

    component Box: Rectangle {
        id: box
        property string title
        default property alias content: inner.data
        implicitHeight: inner.childrenRect.height + 34
        color: Theme.bar
        border.color: Theme.separator
        radius: 3
        Text { x: 10; y: 6; text: box.title; color: Theme.text; font.pixelSize: 12; font.bold: true }
        Item {
            id: inner
            x: 10; y: 26
            width: box.width - 20
            height: childrenRect.height
        }
    }

    component Caption: Text {
        color: Theme.muted3
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        height: 24
    }

    component UiCheckBox: Item {
        id: cb
        property var model
        visible: model.visible
        implicitWidth: row.implicitWidth
        implicitHeight: 22
        opacity: model.enabled ? 1 : 0.45
        Row {
            id: row
            spacing: 7
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                width: 15; height: 15
                radius: cb.model.radio ? 8 : 2
                anchors.verticalCenter: parent.verticalCenter
                color: cb.model.checked && !cb.model.radio ? Theme.accent : Theme.field
                border.color: cb.model.checked ? Theme.accent : area.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
                Text { anchors.centerIn: parent; visible: cb.model.checked && !cb.model.radio; text: "✓"; color: "white"; font.pixelSize: 11; font.bold: true }
                Rectangle { anchors.centerIn: parent; visible: cb.model.checked && cb.model.radio; width: 7; height: 7; radius: 4; color: Theme.accent }
            }
            Text { text: cb.model.text; color: Theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onEntered: if (cb.model.tooltip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, cb.model.tooltip) }
            onExited: ui.hideToolTip()
            onClicked: cb.model.toggle()
        }
    }

    component UiComboBox: Rectangle {
        id: combo
        property var model
        visible: model.visible
        implicitWidth: 200
        implicitHeight: 24
        radius: 2
        color: Theme.field
        border.color: comboArea.containsMouse && model.enabled ? Theme.hoverBorder2 : Theme.fieldBorder
        opacity: model.enabled ? 1 : 0.45
        Text {
            x: 6
            width: parent.width - 24
            anchors.verticalCenter: parent.verticalCenter
            text: combo.model.currentText
            color: Theme.text
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.muted }
        MouseArea {
            id: comboArea
            anchors.fill: parent
            hoverEnabled: true
            onEntered: if (combo.model.tooltip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, combo.model.tooltip) }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); const p = combo.mapToGlobal(0, combo.height); combo.model.popup(p.x, p.y, combo.width) }
        }
    }

    component UiSpinBox: Rectangle {
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

    component UiPushButton: Rectangle {
        id: pb
        property var model
        property bool primary: false
        property string label: model ? model.text : ""
        signal clicked()
        visible: model ? model.visible : true
        implicitWidth: Math.max(80, caption.implicitWidth + 22)
        implicitHeight: 26
        radius: 3
        readonly property bool active: model ? model.enabled : true
        opacity: active ? 1 : 0.45
        color: primary ? (pbArea.containsMouse ? "#4c9bf0" : Theme.accent) : (pbArea.pressed ? Theme.checked : pbArea.containsMouse ? Theme.hover : Theme.field)
        border.color: primary ? Theme.accent : pbArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
        Text { id: caption; anchors.centerIn: parent; text: pb.label; color: Theme.text; font.pixelSize: 12 }
        MouseArea {
            id: pbArea
            anchors.fill: parent
            hoverEnabled: true
            onEntered: if (pb.model && pb.model.tooltip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, pb.model.tooltip) }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); if (!pb.active) return; if (pb.model) pb.model.click(); pb.clicked() }
        }
    }

    // ------------------------------------------------------------ colonna sinistra

    Flickable {
        id: leftFlick
        x: 10; y: 10
        width: plot.expanded ? Math.round((root.width - 30) * 0.62) : root.width - 20
        height: root.height - 56
        contentHeight: leftColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: leftColumn
            width: leftFlick.width
            spacing: 8

            Box {
                visible: !plot.pageSetupMode
                width: parent.width
                title: qsTr("Impostazione di pagina")
                Row {
                    spacing: 8
                    Caption { text: qsTr("Nome:") }
                    UiComboBox { model: setupCombo; width: leftColumn.width - 200 }
                    UiPushButton { model: addSetupButton }
                }
            }

            Box {
                width: parent.width
                title: qsTr("Stampante/plotter")
                Item {
                    width: parent.width
                    height: Math.max(deviceColumn.height, sheetColumn.height)
                    Column {
                        id: deviceColumn
                        width: parent.width - sheetColumn.width - 12
                        spacing: 8
                        Row {
                            spacing: 8
                            Caption { text: qsTr("Nome:") }
                            UiComboBox { model: deviceCombo; width: deviceColumn.width - 140 }
                            UiPushButton { model: propertiesButton }
                        }
                        Text {
                            width: parent.width
                            text: deviceInfo.text
                            textFormat: Text.StyledText
                            wrapMode: Text.WordWrap
                            color: Theme.muted2
                            font.pixelSize: 11
                        }
                        UiCheckBox { model: toFileCheck }
                    }
                    // Anteprima parziale: foglio, area stampabile (tratteggio) e dove va il disegno.
                    Column {
                        id: sheetColumn
                        anchors.right: parent.right
                        width: 150
                        spacing: 4
                        Item {
                            id: sheetArea
                            width: 150; height: 110
                            readonly property real k: sheet.sheetWidth > 0 && sheet.sheetHeight > 0
                                                      ? Math.min((width - 16) / sheet.sheetWidth, (height - 16) / sheet.sheetHeight) : 0
                            readonly property real sx: (width - sheet.sheetWidth * k) / 2
                            readonly property real sy: (height - sheet.sheetHeight * k) / 2
                            function rx(v) { return sx + v * k }
                            function ry(v) { return sy + (sheet.sheetHeight - v) * k }
                            Rectangle { x: sheetArea.sx + 3; y: sheetArea.sy + 3; width: sheet.sheetWidth * sheetArea.k; height: sheet.sheetHeight * sheetArea.k; color: "#46000000" }
                            Rectangle { x: sheetArea.sx; y: sheetArea.sy; width: sheet.sheetWidth * sheetArea.k; height: sheet.sheetHeight * sheetArea.k; color: "white"; border.color: "#787878" }
                            Rectangle {
                                visible: sheet.placed.length === 4
                                x: visible ? sheetArea.rx(sheet.placed[0]) : 0
                                y: visible ? sheetArea.ry(sheet.placed[3]) : 0
                                width: visible ? (sheet.placed[2] - sheet.placed[0]) * sheetArea.k : 0
                                height: visible ? (sheet.placed[3] - sheet.placed[1]) * sheetArea.k : 0
                                color: "#6e4682dc"
                            }
                            // Area stampabile tratteggiata.
                            Repeater {
                                model: 4
                                Item {
                                    required property int index
                                    readonly property real x0: sheetArea.rx(sheet.printable[0])
                                    readonly property real x1: sheetArea.rx(sheet.printable[2])
                                    readonly property real y0: sheetArea.ry(sheet.printable[3])
                                    readonly property real y1: sheetArea.ry(sheet.printable[1])
                                    readonly property bool horizontal: index < 2
                                    x: horizontal ? x0 : (index === 2 ? x0 : x1)
                                    y: horizontal ? (index === 0 ? y0 : y1) : y0
                                    width: horizontal ? x1 - x0 : 1
                                    height: horizontal ? 1 : y1 - y0
                                    Repeater {
                                        model: Math.max(0, Math.floor((parent.horizontal ? parent.width : parent.height) / 6))
                                        Rectangle {
                                            required property int index
                                            x: parent.horizontal ? index * 6 : 0
                                            y: parent.horizontal ? 0 : index * 6
                                            width: parent.horizontal ? 3 : 1
                                            height: parent.horizontal ? 1 : 3
                                            color: sheet.outside ? "#dc2828" : "#3c3c3c"
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: sheetLabel.text
                            wrapMode: Text.WordWrap
                            color: sheetLabel.error ? "#e05050" : Theme.muted2
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8
                Box {
                    width: plot.pageSetupMode ? parent.width : parent.width - copiesBox.width - 8
                    title: qsTr("Formato carta")
                    UiComboBox { model: paperCombo; width: parent.width }
                }
                Box {
                    id: copiesBox
                    visible: !plot.pageSetupMode
                    width: 150
                    title: qsTr("Numero di copie")
                    UiSpinBox { model: copiesSpin; width: 120 }
                }
            }

            Row {
                width: parent.width
                spacing: 8
                Column {
                    width: (parent.width - 8) / 2
                    spacing: 8
                    Box {
                        width: parent.width
                        title: qsTr("Area di stampa")
                        Column {
                            spacing: 6
                            Caption { text: qsTr("Cosa stampare:") }
                            Row {
                                spacing: 8
                                UiComboBox { model: whatCombo; width: 150 }
                                UiPushButton { model: windowButton }
                            }
                        }
                    }
                    Box {
                        width: parent.width
                        title: qsTr("Offset di stampa (origine nell'area stampabile)")
                        Column {
                            spacing: 6
                            Row { spacing: 8; Caption { text: "X:"; width: 18 } UiSpinBox { model: offsetX } UiCheckBox { model: centerCheck; anchors.verticalCenter: parent.verticalCenter } }
                            Row { spacing: 8; Caption { text: "Y:"; width: 18 } UiSpinBox { model: offsetY } }
                        }
                    }
                }
                Box {
                    width: (parent.width - 8) / 2
                    title: qsTr("Scala di stampa")
                    Column {
                        width: parent.width
                        spacing: 6
                        UiCheckBox { model: fitCheck }
                        Row { spacing: 8; Caption { text: qsTr("Scala:") } UiComboBox { model: scaleCombo; width: 170 } }
                        Row { spacing: 8; UiSpinBox { model: scalePaper } UiComboBox { model: unitsCombo; width: 80 } Caption { text: "=" } }
                        Row { spacing: 8; UiSpinBox { model: scaleDrawing } Caption { text: qsTr("unità") } }
                        UiCheckBox { model: lwScaleCheck }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ colonna destra (altre opzioni)

    Flickable {
        id: rightFlick
        visible: plot.expanded
        x: leftFlick.x + leftFlick.width + 10
        y: 10
        width: root.width - x - 10
        height: root.height - 56
        contentHeight: rightColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: rightColumn
            width: rightFlick.width
            spacing: 8

            Box {
                width: parent.width
                title: qsTr("Tabella stili di stampa (assegnazioni penne)")
                Column {
                    spacing: 6
                    Row { spacing: 8; UiComboBox { model: styleCombo; width: rightColumn.width - 120 } UiPushButton { model: styleEdit } }
                    UiCheckBox { model: showStylesCheck }
                }
            }
            Box {
                width: parent.width
                title: qsTr("Opzioni finestra ombreggiata")
                Column {
                    spacing: 6
                    Row { spacing: 8; Caption { text: qsTr("Stampa ombreggiata:"); width: 120 } UiComboBox { model: shadeCombo; width: rightColumn.width - 150 } }
                    Row { spacing: 8; Caption { text: qsTr("Qualità:"); width: 120 } UiComboBox { model: qualityCombo; width: rightColumn.width - 150 } }
                    Row { spacing: 8; Caption { text: qsTr("DPI:"); width: 120 } UiSpinBox { model: dpiSpin } }
                }
            }
            Box {
                width: parent.width
                title: qsTr("Opzioni di stampa")
                Column {
                    spacing: 4
                    UiCheckBox { model: backgroundCheck }
                    UiCheckBox { model: lineweightsCheck }
                    UiCheckBox { model: transparencyCheck }
                    UiCheckBox { model: stylesCheck }
                    UiCheckBox { model: paperLastCheck }
                    UiCheckBox { model: hideCheck }
                    Row {
                        spacing: 8
                        UiCheckBox { model: stampCheck }
                        UiPushButton { model: stampButton; implicitWidth: 30; implicitHeight: 22 }
                    }
                    UiCheckBox { model: saveCheck }
                }
            }
            Box {
                width: parent.width
                title: qsTr("Orientamento disegno")
                Column {
                    spacing: 4
                    UiCheckBox { model: portraitRadio }
                    UiCheckBox { model: landscapeRadio }
                    UiCheckBox { model: upsideDownCheck }
                }
            }
        }
    }

    // ------------------------------------------------------------ pulsanti

    UiPushButton {
        x: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        model: previewButton
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        spacing: 8
        UiPushButton { model: applyButton }
        UiPushButton { label: plot.pageSetupMode ? qsTr("OK") : qsTr("Stampa"); primary: true; onClicked: plot.plotClicked() }
        UiPushButton { label: qsTr("Annulla"); onClicked: plot.cancelClicked() }
        UiPushButton {
            label: plot.expanded ? "<" : ">"
            implicitWidth: 32
            onClicked: plot.toggleExpanded()
        }
    }
}
