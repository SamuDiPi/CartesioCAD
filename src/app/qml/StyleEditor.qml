// Editor di una tabella stili di stampa .ctb (modello: StyleEditor): colori a sinistra, proprietà a destra.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window
    focus: true
    Keys.onEscapePressed: cancelButton.click()

    component Caption: Text {
        width: 150
        height: 24
        verticalAlignment: Text.AlignVCenter
        color: Theme.muted3
        font.pixelSize: 12
        elide: Text.ElideRight
    }

    Row {
        id: top
        x: 10; y: 10
        spacing: 8
        Caption { text: qsTr("Descrizione:"); width: 90 }
        UiLineEdit { model: description; width: root.width - 120 }
    }

    // Colori: clic, Ctrl+clic e Maiusc+clic per modificarne più di uno insieme.
    Rectangle {
        id: listBox
        anchors.top: top.bottom
        anchors.topMargin: 10
        anchors.bottom: buttons.top
        anchors.bottomMargin: 10
        x: 10
        width: 200
        color: Theme.drawing
        border.color: Theme.fieldBorder
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: styles.items
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: list.width
                height: 22
                color: styles.selected[index] ? Theme.checked : rowArea.containsMouse ? Theme.tabHover : "transparent"
                Rectangle { x: 8; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter; color: parent.modelData.color; border.color: Theme.border }
                Text { x: 30; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.text; color: Theme.text; font.pixelSize: 12 }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: function(mouse) { styles.click(index, mouse.modifiers) }
                }
            }
        }
        Text {
            anchors.top: parent.bottom
            anchors.topMargin: 2
            text: qsTr("Ctrl/Maiusc: più colori")
            color: Theme.muted
            font.pixelSize: 10
            visible: false
        }
    }

    UiBox {
        title: qsTr("Proprietà (per i colori selezionati)")
        anchors.top: top.bottom
        anchors.topMargin: 10
        anchors.left: listBox.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        Column {
            width: parent.width
            spacing: 6
            Row { spacing: 8; Caption { text: qsTr("Colore:") } UiComboBox { model: styleColor; width: 230 } }
            UiCheckBox { model: dither }
            UiCheckBox { model: gray }
            Row { spacing: 8; Caption { text: qsTr("Penna n. (0 = automatica):") } UiSpinBox { model: pen } }
            Row { spacing: 8; Caption { text: qsTr("Penna virtuale n.:") } UiSpinBox { model: virtualPen } }
            Row { spacing: 8; Caption { text: qsTr("Intensità:") } UiSpinBox { model: screen } }
            Row { spacing: 8; Caption { text: qsTr("Tipo di linea:") } UiComboBox { model: linetype; width: 230 } }
            UiCheckBox { model: adaptive }
            Row { spacing: 8; Caption { text: qsTr("Spessore linea:") } UiComboBox { model: lineweight; width: 230 } }
            Row { spacing: 8; Caption { text: qsTr("Stile estremità linea:") } UiComboBox { model: endStyle; width: 230 } }
            Row { spacing: 8; Caption { text: qsTr("Stile giunzione linea:") } UiComboBox { model: joinStyle; width: 230 } }
            Row { spacing: 8; Caption { text: qsTr("Stile riempimento:") } UiComboBox { model: fillStyle; width: 230 } }
            Row { spacing: 8; Caption { text: qsTr("Descrizione stile:") } UiLineEdit { model: styleDescription; width: 230 } }
        }
    }

    Row {
        id: buttons
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        x: 10
        width: parent.width - 20
        spacing: 8
        UiPushButton { model: saveAsButton }
    }
    Row {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        spacing: 8
        UiPushButton { model: saveButton; primary: true }
        UiPushButton { model: cancelButton }
    }
}
