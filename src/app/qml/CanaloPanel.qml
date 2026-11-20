// Finestra Canalina: scheda Cavi (database a sinistra, cavi della canalina a destra) e scheda Riepilogo (calcoli di
// CanaloCAD, sezione e tavola nel disegno). Modello: CanaloPanel in C++, oggetto "canalo".
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window
    property int tab: 0

    component Button: Rectangle {
        id: b
        property string text
        property string tip
        property bool enabled: true
        property bool primary: false
        signal clicked()
        width: label.implicitWidth + 24
        height: 28
        radius: 3
        opacity: enabled ? 1.0 : 0.45
        color: primary ? (area.pressed && b.enabled ? "#2f6fc0" : (area.containsMouse && b.enabled ? "#4a8be0" : Theme.accent))
                       : (area.pressed && b.enabled ? Theme.checked : (area.containsMouse && b.enabled ? "#56637a" : Theme.hover))
        border.color: primary ? Theme.accent : Theme.hoverBorder
        Text { id: label; anchors.centerIn: parent; text: b.text; color: "#ffffff"; font.pixelSize: 13 }
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
        font.pixelSize: 12
        font.bold: true
        font.capitalization: Font.AllUppercase
    }

    component Check: Rectangle {
        id: c
        property bool checked: false
        property string text
        property string tip
        signal toggled(bool on)
        width: checkLabel.implicitWidth + 26
        height: 28
        color: "transparent"
        Rectangle { x: 2; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; radius: 2; color: c.checked ? Theme.accent : "#20252d"; border.color: Theme.border
            Text { anchors.centerIn: parent; visible: c.checked; text: "✓"; color: "#ffffff"; font.pixelSize: 12 } }
        Text { id: checkLabel; x: 24; anchors.verticalCenter: parent.verticalCenter; text: c.text; color: Theme.text; font.pixelSize: 13 }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { if (c.tip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, c.tip) } }
            onExited: ui.hideToolTip()
            onClicked: { c.checked = !c.checked; c.toggled(c.checked) }
        }
    }

    // Tabella: intestazione fissa, righe con celle di larghezza data (l'ultima colonna prende lo spazio che resta);
    // colore della riga per sistema (2 sicurezza in rosso, 3 segnale e dati in azzurro, come in CanaloCAD).
    component Table: ListView {
        id: t
        property var columns: []
        property int selected: -1
        property string emptyText
        signal rowClicked(int row)
        signal rowDoubleClicked(int row)
        readonly property int fixedWidth: {
            let w = 0
            for (let i = 0; i < columns.length - 1; ++i) w += columns[i][1]
            return w
        }
        function colWidth(i) { return i < columns.length - 1 ? columns[i][1] : Math.max(columns[i][1], t.width - fixedWidth) }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        headerPositioning: ListView.OverlayHeader
        header: Rectangle {
            z: 2
            width: t.width
            height: 26
            color: "#333a46"
            Row {
                height: parent.height
                Repeater {
                    model: t.columns.length
                    delegate: Item {
                        required property int index
                        width: t.colWidth(index)
                        height: parent.height
                        Text { x: 8; width: parent.width - 10; anchors.verticalCenter: parent.verticalCenter; text: t.columns[index][0]; color: Theme.muted3; font.pixelSize: 12; elide: Text.ElideRight }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#262b34" }
                    }
                }
            }
        }
        delegate: Rectangle {
            id: rowItem
            required property var modelData
            required property int index
            width: t.width
            height: 26
            color: index === t.selected ? Theme.checked : (index % 2 ? "#2f3640" : "#2a303a")
            Row {
                height: parent.height
                Repeater {
                    model: t.columns.length
                    delegate: Item {
                        id: cell
                        required property int index
                        width: t.colWidth(index)
                        height: parent.height
                        Text {
                            x: 8
                            width: parent.width - 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: rowItem.modelData.cells[cell.index]
                            color: rowItem.modelData.level === 2 ? "#ef7a7a" : rowItem.modelData.level === 3 ? "#7ab8ef" : Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: t.rowClicked(rowItem.index)
                onDoubleClicked: t.rowDoubleClicked(rowItem.index)
            }
        }
        Text {
            anchors.centerIn: parent
            visible: t.count === 0
            text: t.emptyText
            color: Theme.muted2
            font.pixelSize: 13
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    // Campo di testo con il bordo.
    component Field: Rectangle {
        id: f
        property alias text: input.text
        property string placeholder
        property int inputWidth: 120
        signal edited(string text)
        signal finished(string text)
        width: inputWidth
        height: 28
        color: "#20252d"
        border.color: input.activeFocus ? Theme.accent : Theme.border
        radius: 3
        TextInput {
            id: input
            x: 8
            width: parent.width - 16
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            selectionColor: Theme.accent
            selectByMouse: true
            clip: true
            font.pixelSize: 13
            onTextEdited: f.edited(text)
            onEditingFinished: f.finished(text)
            Text { visible: input.text.length === 0; text: f.placeholder; color: "#7d8796"; font: input.font }
        }
    }

    // ------------------------------------------------------------ intestazione

    Rectangle {
        id: header
        width: parent.width
        height: headerColumn.height + 12
        color: "#262b34"
        Column {
            id: headerColumn
            x: 12; y: 8
            width: parent.width - 24
            spacing: 6
            Item {
                width: parent.width
                height: 28
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - headerButtons.width - 12
                    text: canalo.hasTray ? canalo.trayTitle : qsTr("Nessuna canalina")
                    color: canalo.hasTray ? "#ffffff" : Theme.muted2
                    font.pixelSize: 16
                    font.bold: true
                    elide: Text.ElideRight
                }
                Row {
                    id: headerButtons
                    anchors.right: parent.right
                    spacing: 6
                    Button { text: qsTr("Usa selezione"); primary: true; tip: qsTr("Gli oggetti selezionati nel disegno: una sezione L×H (anche con i cavi) diventa una canalina con le sue misure, ridisegnata da CanaloCAD al suo posto; un percorso in pianta si collega alla canalina; un oggetto di una canalina la mostra"); onClicked: canalo.useSelection() }
                    Button { text: qsTr("Nuova"); tip: qsTr("Una canalina nuova, con la misura scelta dai cavi"); onClicked: canalo.newTray() }
                    Button { text: qsTr("Dati canalina..."); tip: qsTr("Misure, tipologia, materiale, coperchio, setti, tratto, staffaggio, tavola e cartiglio"); onClicked: canalo.editTray() }
                    Button { text: qsTr("Importa DWG/DXF..."); tip: qsTr("Misure e cavi da un file, come Importa DWG/DXF di CanaloCAD"); onClicked: canalo.importFile("") }
                    Button { text: "?"; tip: qsTr("Come si usa"); onClicked: canalo.showGuide() }
                }
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: canalo.statusText
                color: Theme.muted2
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            // Schede.
            Row {
                height: 30
                Repeater {
                    model: [qsTr("Cavi"), qsTr("Riepilogo e tavola")]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: tabLabel.implicitWidth + 32
                        height: 30
                        color: index === root.tab ? Theme.window : (tabArea.containsMouse ? "#353c48" : "transparent")
                        Rectangle { visible: index === root.tab; width: parent.width; height: 2; color: Theme.accent }
                        Text { id: tabLabel; anchors.centerIn: parent; text: modelData; color: index === root.tab ? "#ffffff" : Theme.muted3; font.pixelSize: 13; font.bold: index === root.tab }
                        MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: root.tab = index }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ scheda Cavi

    Item {
        id: cablesTab
        visible: root.tab === 0
        anchors.top: header.bottom
        anchors.bottom: quickBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12

        // Database (sinistra).
        Item {
            id: dbSide
            width: parent.width * 0.56
            height: parent.height
            Column {
                id: dbTop
                width: parent.width
                spacing: 6
                Caption { text: qsTr("Database cavi") }
                Flow {
                    width: parent.width
                    spacing: 5
                    Repeater {
                        model: canalo.categories
                        delegate: Rectangle {
                            required property var modelData
                            width: chipLabel.implicitWidth + 20
                            height: 26
                            radius: 13
                            color: modelData.key === canalo.category ? Theme.accent : (chipArea.containsMouse ? "#353c48" : "#2a303a")
                            border.color: Theme.border
                            Text { id: chipLabel; anchors.centerIn: parent; text: modelData.label; color: "#ffffff"; font.pixelSize: 12 }
                            MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true; onClicked: canalo.setCategory(modelData.key) }
                        }
                    }
                }
                Field { inputWidth: dbTop.width; placeholder: qsTr("Cerca: tipo, formazione, descrizione (es. 3G2,5, FTG18, Cat.6)"); onEdited: (t) => canalo.setSearch(t) }
            }
            Table {
                anchors.top: dbTop.bottom
                anchors.topMargin: 6
                anchors.bottom: dbBottom.top
                anchors.bottomMargin: 6
                width: parent.width
                model: canalo.dbRows
                selected: canalo.dbSelected
                columns: [[qsTr("Tipo"), 120], [qsTr("Formazione"), 100], [qsTr("Ø mm"), 60], [qsTr("kg/m"), 60], [qsTr("Sistema"), 100], [qsTr("Descrizione"), 160]]
                emptyText: qsTr("Nessun cavo in questa categoria.")
                onRowClicked: (row) => canalo.selectDb(row)
                onRowDoubleClicked: (row) => canalo.addFromDb(row, parseInt(qty.text) || 1, trefoil.checked)
            }
            Flow {
                id: dbBottom
                anchors.bottom: parent.bottom
                width: parent.width
                spacing: 6
                Text { height: 28; verticalAlignment: Text.AlignVCenter; text: qsTr("Quantità"); color: Theme.text; font.pixelSize: 13 }
                Field { id: qty; inputWidth: 46; text: "1" }
                Check { id: trefoil; text: qsTr("Terna a trifoglio"); tip: qsTr("Per gli unipolari: tre conduttori L1, L2, L3 a trifoglio") }
                Button { text: qsTr("Aggiungi  →"); primary: true; enabled: canalo.dbSelected >= 0; tip: qsTr("Nella canalina (anche con un doppio clic sulla riga); senza canalina ne nasce una"); onClicked: canalo.addFromDb(canalo.dbSelected, parseInt(qty.text) || 1, trefoil.checked) }
                Button { text: qsTr("Nuovo cavo..."); tip: qsTr("Cavo personale (vale per tutti i disegni), partendo dai dati di quello scelto"); onClicked: canalo.newOwnCable() }
                Button { visible: canalo.ownSelected; text: qsTr("Modifica..."); onClicked: canalo.editOwnCable() }
                Button { visible: canalo.ownSelected; text: qsTr("Elimina"); onClicked: canalo.removeOwnCable() }
            }
        }

        // Cavi della canalina (destra).
        Item {
            anchors.left: dbSide.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            height: parent.height
            Caption { id: cablesCaption; text: qsTr("Cavi nella canalina") }
            Table {
                anchors.top: cablesCaption.bottom
                anchors.topMargin: 8
                anchors.bottom: cablesBottom.top
                anchors.bottomMargin: 6
                width: parent.width
                model: canalo.cableRows
                selected: canalo.cableSelected
                columns: [[qsTr("N."), 40], [qsTr("Sigla"), 130], [qsTr("Tipo"), 95], [qsTr("Formazione"), 85], [qsTr("Ø mm"), 50], [qsTr("Descrizione"), 100]]
                emptyText: canalo.hasTray ? qsTr("Nessun cavo: scegli nel database e premi Aggiungi.")
                                          : qsTr("Scegli i cavi nel database: la canalina nasce con la misura adatta. Oppure disegna la canalina, selezionala e premi Usa selezione.")
                onRowClicked: (row) => canalo.selectCable(row)
                onRowDoubleClicked: (row) => canalo.editCable(row)
            }
            Column {
                id: cablesBottom
                anchors.bottom: parent.bottom
                width: parent.width
                spacing: 6
                Flow {
                    width: parent.width
                    spacing: 6
                    Button { text: qsTr("Modifica..."); enabled: canalo.cableSelected >= 0; tip: qsTr("Sigla, descrizione e corrente d'impiego (anche con un doppio clic)"); onClicked: canalo.editCable(canalo.cableSelected) }
                    Button { text: qsTr("Togli"); enabled: canalo.cableSelected >= 0; onClicked: canalo.removeCable(canalo.cableSelected) }
                    Button { text: qsTr("Svuota"); enabled: canalo.hasTray; onClicked: canalo.clearCables() }
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    visible: canalo.hasTray
                    Check {
                        text: qsTr("Misura automatica dai cavi")
                        checked: canalo.autoSize
                        tip: qsTr("La larghezza più stretta (non meno dell'altezza) che lascia la riserva e tiene i cavi nell'altezza, come Dimensiona di CanaloCAD; senza, la misura è quella disegnata o scritta in Dati canalina")
                        onToggled: (on) => canalo.setAutoSize(on)
                    }
                    Text { height: 28; verticalAlignment: Text.AlignVCenter; text: qsTr("riserva"); color: Theme.text; font.pixelSize: 13 }
                    Field { inputWidth: 46; text: Math.round(canalo.reserve); onFinished: (t) => canalo.setReserve(parseFloat(t) || 30) }
                    Text { height: 28; verticalAlignment: Text.AlignVCenter; text: "%"; color: Theme.text; font.pixelSize: 13 }
                }
            }
        }
    }

    // Riga dei risultati principali, sempre visibile nella scheda Cavi.
    Rectangle {
        id: quickBar
        visible: root.tab === 0
        anchors.bottom: parent.bottom
        width: parent.width
        height: canalo.hasTray ? 34 : 0
        color: canalo.quickLevel === 2 ? "#4a2a2e" : "#262b34"
        Text {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 150
            text: canalo.quickSummary
            color: canalo.quickLevel === 2 ? "#ff9a9a" : Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Riepilogo  ›")
            color: Theme.accent
            font.pixelSize: 13
            MouseArea { anchors.fill: parent; onClicked: root.tab = 1 }
        }
    }

    // ------------------------------------------------------------ scheda Riepilogo e tavola

    Item {
        visible: root.tab === 1
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12

        Flickable {
            id: summaryScroll
            anchors.top: parent.top
            anchors.bottom: actions.top
            anchors.bottomMargin: 8
            width: parent.width
            contentHeight: summaryColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: summaryColumn
                width: summaryScroll.width - 8
                spacing: 2
                Text {
                    visible: !canalo.hasTray
                    text: qsTr("Nessuna canalina: scegline una o creala dalla scheda Cavi.")
                    color: Theme.muted2
                    font.pixelSize: 13
                }
                Repeater {
                    model: canalo.summary
                    delegate: Item {
                        required property var modelData
                        width: summaryColumn.width
                        height: Math.max(24, valueText.implicitHeight + 6)
                        Rectangle { anchors.fill: parent; color: modelData.level === 2 ? "#3a2528" : "transparent"; radius: 2 }
                        Text {
                            x: 6
                            width: 190
                            y: 4
                            text: modelData.label
                            color: modelData.level < 0 ? Theme.muted2 : Theme.muted3
                            font.pixelSize: modelData.level < 0 ? 12 : 13
                            elide: Text.ElideRight
                        }
                        Text {
                            id: valueText
                            x: 200
                            y: 4
                            width: parent.width - 206
                            text: modelData.value
                            color: modelData.level === 2 ? "#ff8080" : modelData.level === 1 ? "#5cc58a" : modelData.level < 0 ? Theme.muted2 : Theme.text
                            font.pixelSize: modelData.level < 0 ? 12 : 13
                            font.bold: modelData.level === 2
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
        Flow {
            id: actions
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: 8
            Button { text: canalo.sectionPlaced ? qsTr("Mostra la sezione") : qsTr("Disegna sezione"); primary: true; enabled: canalo.hasTray; tip: qsTr("La sezione di CanaloCAD al vero nel disegno (profilo, cavi, bandierine, quote, staffaggio): si aggiorna da sola a ogni cambio"); onClicked: canalo.drawSection() }
            Button { text: qsTr("Inserisci tavola"); enabled: canalo.hasTray; tip: qsTr("La tavola di CanaloCAD come blocco (la stessa che esporta CanaloCAD); se è già nel disegno si aggiorna"); onClicked: canalo.insertSheet() }
            Button { text: qsTr("Salva tavola DXF..."); enabled: canalo.hasTray; tip: qsTr("Il file DXF della tavola, identico a quello che esporta CanaloCAD"); onClicked: canalo.saveSheet() }
        }
    }
}
