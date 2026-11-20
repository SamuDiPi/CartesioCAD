// Tavolozza Canalina: database dei cavi sempre visibile, cavi della canalina selezionata e riepilogo dei calcoli di
// CanaloCAD, con la sezione e la tavola nel disegno (modello: CanaloPanel in C++, oggetto "canalo").
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    component Button: Rectangle {
        id: b
        property string text
        property string tip
        property bool enabled: true
        signal clicked()
        width: label.implicitWidth + 20
        height: 24
        radius: 2
        opacity: enabled ? 1.0 : 0.45
        color: area.pressed && b.enabled ? Theme.checked : (area.containsMouse && b.enabled ? "#56637a" : Theme.hover)
        border.color: Theme.hoverBorder
        Text { id: label; anchors.centerIn: parent; text: b.text; color: Theme.text; font.pixelSize: 12 }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { if (b.tip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, b.tip) } }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); if (b.enabled) b.clicked() }
        }
    }

    component Caption: Text {
        color: Theme.muted3
        font.pixelSize: 11
        font.bold: true
        font.capitalization: Font.AllUppercase
    }

    // Tabella: intestazione fissa, righe con celle di larghezza data; colore della riga per sistema (2 sicurezza in
    // rosso, 3 segnale e dati in azzurro, come le sezioni di CanaloCAD).
    component Table: ListView {
        id: t
        property var columns: []
        property int selected: -1
        signal rowClicked(int row)
        signal rowDoubleClicked(int row)
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
            height: 22
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
            height: 22
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
                            text: rowItem.modelData.cells[cell.index]
                            color: rowItem.modelData.level === 2 ? "#ef7a7a" : rowItem.modelData.level === 3 ? "#7ab8ef" : Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                const tip = rowItem.modelData.cells[cell.index]
                                if (tip && tip.length > 16) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, tip) }
                            }
                            onExited: ui.hideToolTip()
                            onClicked: t.rowClicked(rowItem.index)
                            onDoubleClicked: t.rowDoubleClicked(rowItem.index)
                        }
                    }
                }
            }
        }
    }

    // Campo di testo con il bordo.
    component Field: Rectangle {
        id: f
        property alias text: input.text
        property string placeholder
        property int inputWidth: 120
        signal edited(string text)
        width: inputWidth
        height: 24
        color: "#20252d"
        border.color: input.activeFocus ? Theme.accent : Theme.border
        radius: 2
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
            Text { visible: input.text.length === 0; text: f.placeholder; color: "#7d8796"; font: input.font }
        }
    }

    // Spazio per le tabelle oltre a pulsanti e titoli: con poco spazio la tavolozza scorre.
    readonly property real room: Math.max(0, height - 380)

    Flickable {
        id: scroller
        anchors.fill: parent
        contentHeight: content.height + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
    Column {
        id: content
        x: 6; y: 6
        width: scroller.width - 12
        spacing: 5

        // Canalina corrente.
        Text {
            width: parent.width
            text: canalo.hasTray ? canalo.trayTitle : qsTr("Nessuna canalina selezionata")
            color: canalo.hasTray ? "#ffffff" : Theme.muted2
            font.pixelSize: 13
            font.bold: canalo.hasTray
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: canalo.statusText
            color: Theme.muted2
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        Flow {
            id: trayButtons
            width: parent.width
            spacing: 5
            Button { text: qsTr("Nuova canalina"); tip: qsTr("Canalina nuova collegata agli oggetti selezionati: la sezione disegnata (misure e cavi letti dal disegno) o il percorso in pianta"); onClicked: canalo.newTray() }
            Button { text: qsTr("Collega selezione"); enabled: canalo.hasTray; tip: qsTr("Gli oggetti selezionati (es. il percorso in pianta) entrano nella canalina: selezionandoli si vede il suo riepilogo"); onClicked: canalo.linkSelection() }
            Button { text: qsTr("Importa selezione"); tip: qsTr("Misure e cavi dalla sezione selezionata (cerchi, bordo, sigle), con l'importazione di CanaloCAD"); onClicked: canalo.importSelection() }
            Button { text: qsTr("Importa DWG/DXF..."); tip: qsTr("Misure e cavi da un file DWG o DXF, come Importa DWG/DXF di CanaloCAD"); onClicked: canalo.importFile("") }
            Button { text: qsTr("Dati canalina..."); tip: qsTr("Misure, tipologia, materiale, coperchio, setti, tratto, staffaggio, tavola e cartiglio"); onClicked: canalo.editTray() }
            Button { text: qsTr("?"); tip: qsTr("Come si usa"); onClicked: canalo.showGuide() }
        }


        Caption { text: qsTr("Database cavi") }
        Flow {
            width: parent.width
            spacing: 4
            Repeater {
                model: canalo.categories
                delegate: Rectangle {
                    required property var modelData
                    width: chipLabel.implicitWidth + 16
                    height: 22
                    radius: 11
                    color: modelData.key === canalo.category ? Theme.accent : (chipArea.containsMouse ? "#353c48" : "#2a303a")
                    border.color: Theme.border
                    Text { id: chipLabel; anchors.centerIn: parent; text: modelData.label; color: "#ffffff"; font.pixelSize: 11 }
                    MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true; onClicked: canalo.setCategory(modelData.key) }
                }
            }
            Field { inputWidth: 150; placeholder: qsTr("Cerca (tipo, formazione...)"); onEdited: (t) => canalo.setSearch(t) }
        }
        Table {
            id: dbTable
            width: parent.width
            height: Math.max(160, root.room * 0.45)
            model: canalo.dbRows
            selected: canalo.dbSelected
            columns: [[qsTr("Tipo"), 110], [qsTr("Formazione"), 90], [qsTr("Ø mm"), 50], [qsTr("kg/m"), 50], [qsTr("Sistema"), 85], [qsTr("Descrizione"), 300]]
            onRowClicked: (row) => canalo.selectDb(row)
            onRowDoubleClicked: (row) => canalo.addFromDb(row, parseInt(qty.text) || 1, trefoil.checked)
        }
        Flow {
            width: parent.width
            spacing: 5
            Text { height: 24; verticalAlignment: Text.AlignVCenter; text: qsTr("Quantità"); color: Theme.text; font.pixelSize: 12 }
            Field { id: qty; inputWidth: 40; text: "1" }
            Rectangle {
                id: trefoil
                property bool checked: false
                width: trefoilLabel.implicitWidth + 26
                height: 24
                color: "transparent"
                Rectangle { x: 2; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; radius: 2; color: trefoil.checked ? Theme.accent : "#20252d"; border.color: Theme.border
                    Text { anchors.centerIn: parent; visible: trefoil.checked; text: "✓"; color: "#ffffff"; font.pixelSize: 11 } }
                Text { id: trefoilLabel; x: 22; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Terna a trifoglio"); color: Theme.text; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; onClicked: trefoil.checked = !trefoil.checked }
            }
            Button { text: qsTr("Aggiungi"); enabled: canalo.dbSelected >= 0; tip: qsTr("Aggiunge i cavi scelti alla canalina (doppio clic nella tabella fa lo stesso); senza canalina ne crea una nuova"); onClicked: canalo.addFromDb(canalo.dbSelected, parseInt(qty.text) || 1, trefoil.checked) }
            Button { text: qsTr("Nuovo cavo..."); tip: qsTr("Cavo personale (vale per tutti i disegni), partendo dai dati di quello scelto"); onClicked: canalo.newOwnCable() }
            Button { text: qsTr("Modifica..."); enabled: canalo.ownSelected; onClicked: canalo.editOwnCable() }
            Button { text: qsTr("Elimina"); enabled: canalo.ownSelected; onClicked: canalo.removeOwnCable() }
        }

        Caption { text: qsTr("Cavi nella canalina") }
        Table {
            id: cableTable
            width: parent.width
            height: Math.max(100, root.room * 0.25)
            model: canalo.cableRows
            selected: canalo.cableSelected
            columns: [[qsTr("N."), 36], [qsTr("Sigla"), 130], [qsTr("Tipo"), 100], [qsTr("Formazione"), 85], [qsTr("Ø mm"), 50], [qsTr("Sistema"), 85], [qsTr("Descrizione"), 240]]
            onRowClicked: (row) => canalo.selectCable(row)
            onRowDoubleClicked: (row) => canalo.editCable(row)
        }
        Flow {
            width: parent.width
            spacing: 5
            Button { text: qsTr("Modifica..."); enabled: canalo.cableSelected >= 0; tip: qsTr("Sigla, descrizione e corrente d'impiego (doppio clic fa lo stesso)"); onClicked: canalo.editCable(canalo.cableSelected) }
            Button { text: qsTr("Togli"); enabled: canalo.cableSelected >= 0; onClicked: canalo.removeCable(canalo.cableSelected) }
            Button { text: qsTr("Svuota"); enabled: canalo.hasTray; onClicked: canalo.clearCables() }
            Button { text: qsTr("Dimensiona..."); enabled: canalo.hasTray; tip: qsTr("Larghezza per la riserva voluta, come Dimensiona di CanaloCAD"); onClicked: canalo.autoSize() }
        }

        Caption { text: qsTr("Riepilogo") }
        Column {
            id: summaryList
            width: parent.width
            spacing: 1
            Repeater {
                model: canalo.summary
                delegate: Item {
                    required property var modelData
                    width: summaryList.width
                    height: Math.max(20, valueText.implicitHeight + 4)
                    Text {
                        width: 130
                        y: 2
                        text: modelData.label
                        color: modelData.level < 0 ? Theme.muted2 : Theme.muted3
                        font.pixelSize: modelData.level < 0 ? 11 : 12
                        elide: Text.ElideRight
                    }
                    Text {
                        id: valueText
                        x: 136
                        y: 2
                        width: parent.width - 136
                        text: modelData.value
                        color: modelData.level === 2 ? "#ef5a5a" : modelData.level === 1 ? "#5cc58a" : modelData.level < 0 ? Theme.muted2 : Theme.text
                        font.pixelSize: modelData.level < 0 ? 11 : 12
                        font.bold: modelData.level === 2
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        Flow {
            id: actions
            width: parent.width
            spacing: 5
            Button { text: qsTr("Disegna sezione"); enabled: canalo.hasTray; tip: qsTr("La sezione di CanaloCAD al vero nel disegno (profilo, cavi, bandierine, quote, staffaggio); se c'è già si aggiorna"); onClicked: canalo.drawSection() }
            Button { text: qsTr("Inserisci tavola"); enabled: canalo.hasTray; tip: qsTr("La tavola di CanaloCAD come blocco: la stessa che esporta CanaloCAD; se è già nel disegno si aggiorna"); onClicked: canalo.insertSheet() }
            Button { text: qsTr("Salva tavola DXF..."); enabled: canalo.hasTray; tip: qsTr("Il file DXF della tavola, identico a quello che esporta CanaloCAD"); onClicked: canalo.saveSheet() }
        }
    }
    }

    // Barra di scorrimento (la tavolozza scorre quando è più bassa del suo contenuto).
    Rectangle {
        visible: scroller.contentHeight > scroller.height
        anchors.right: parent.right
        anchors.rightMargin: 1
        width: 4
        radius: 2
        color: "#56637a"
        y: scroller.height * scroller.visibleArea.yPosition
        height: scroller.height * scroller.visibleArea.heightRatio
    }
}
