// Tavolozza Impianto: file di progetto, circuiti, tratte (tubi, canali, passerelle) e cavi propri, con le
// verifiche delle tratte (modello: PlantPanel in C++).
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
        width: label.implicitWidth + 24
        height: 26
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

    // Tabella: intestazione fissa, righe con celle di larghezza data; la colonna dell'esito colorata per livello
    // (0 verde, 1 arancione, 2 rosso).
    component Table: ListView {
        id: t
        property var columns: []
        property int selected: -1
        property int levelColumn: -1
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
                        readonly property bool levelCell: cell.index === t.levelColumn || (t.levelColumn < 0 && cell.index === 0)
                        Text {
                            x: 6
                            width: parent.width - 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: rowItem.modelData.cells[cell.index]
                            color: cell.levelCell && rowItem.modelData.level === 2 ? "#ef5a5a"
                                 : cell.levelCell && rowItem.modelData.level === 1 ? "#e3913a"
                                 : cell.levelCell && t.levelColumn >= 0 ? "#5cc58a" : Theme.text
                            font.pixelSize: 12
                            font.bold: cell.levelCell && rowItem.modelData.level > 0
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                const tip = rowItem.modelData.cells[cell.index]
                                if (tip && tip.length > 18) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, tip) }
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

    Column {
        id: content
        x: 6; y: 6
        width: parent.width - 12
        height: parent.height - 12
        spacing: 6

        Row {
            spacing: 6
            Button { text: qsTr("Nuovo..."); tip: qsTr("Nuovo file di progetto dell'impianto (.cdb), collegato a questo disegno"); onClicked: plant.newProjectDialog() }
            Button { text: qsTr("Apri..."); tip: qsTr("Apre un file di progetto e ci collega questo disegno"); onClicked: plant.openProjectDialog() }
            Button { text: qsTr("Importa schema PDF..."); enabled: plant.hasProject; tip: qsTr("Schema unifilare dei quadri in PDF: i circuiti entrano nel progetto (quelli già presenti si aggiornano)"); onClicked: plant.importSchemaDialog() }
            Button { text: qsTr("Verifica"); enabled: plant.hasProject; tip: qsTr("Verifica tutte le tratte: riempimento, segregazione, staffaggio (risultati anche nella riga di comando)"); onClicked: plant.verify() }
            Button { text: qsTr("?"); tip: qsTr("Come si usa: la guida passo per passo dell'Impianto"); onClicked: plant.showGuide() }
        }
        Text {
            width: parent.width
            visible: plant.hasProject
            text: "<b>" + plant.projectName + "</b> &nbsp; " + plant.summary
            textFormat: Text.StyledText
            color: Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, plant.projectFile) }
                onExited: ui.hideToolTip()
            }
        }
        Text {
            width: parent.width
            text: plant.statusText
            color: Theme.muted2
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Row {
            height: 26
            visible: plant.hasProject
            Repeater {
                model: [qsTr("Circuiti"), qsTr("Tratte"), qsTr("Cavi propri")]
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: tabLabel.implicitWidth + 24
                    height: 26
                    color: index === plant.currentTab ? "#2a303a" : (tabArea.containsMouse ? "#353c48" : "transparent")
                    Rectangle { visible: index === plant.currentTab; width: parent.width; height: 2; color: Theme.accent }
                    Text { id: tabLabel; anchors.centerIn: parent; text: modelData; color: index === plant.currentTab ? "#ffffff" : Theme.muted3; font.pixelSize: 12 }
                    MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: plant.setTab(index) }
                }
            }
        }

        Item {
            width: parent.width
            height: content.height - y - buttons.height - 6
            visible: plant.hasProject

            // Avviso sempre visibile sotto la tabella dei circuiti.
            Text {
                id: ampNote
                visible: plant.currentTab === 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                wrapMode: Text.WordWrap
                color: "#f0b040"
                font.pixelSize: 11
                text: qsTr("Portate e cadute di tensione sono indicative: le tabelle di CartesioCAD non sono ancora state confrontate con il testo della norma (CEI 64-8:2024 Cap. 52 = IEC 60364-5-52:2009; CEI-UNEL 35024/1). Verificarle prima di usarle in un progetto.")
            }
            Table {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: ampNote.top
                anchors.bottomMargin: 4
                visible: plant.currentTab === 0
                model: plant.currentTab === 0 ? plant.rows : []
                selected: plant.selectedRow
                levelColumn: 1
                columns: [[qsTr("Sigla"), 100], [qsTr("Esito"), 105], [qsTr("Sistema"), 65], [qsTr("Descrizione"), 160], [qsTr("Cavo"), 140],
                          [qsTr("Diametro"), 75], [qsTr("Ib (A)"), 50], [qsTr("In (A)"), 50], [qsTr("Iz' (A)"), 55], [qsTr("ΔV %"), 50],
                          [qsTr("Tratte"), 80], [qsTr("Note"), 380]]
                onRowClicked: (row) => plant.selectRow(row)
                onRowDoubleClicked: (row) => plant.editRow(row)
            }
            Table {
                anchors.fill: parent
                visible: plant.currentTab === 1
                model: plant.currentTab === 1 ? plant.rows : []
                selected: plant.selectedRow
                levelColumn: 1
                columns: [[qsTr("Tratta"), 50], [qsTr("Esito"), 105], [qsTr("Tipo"), 80], [qsTr("Misura"), 85], [qsTr("Riempim."), 70],
                          [qsTr("Lungh. (m)"), 70], [qsTr("Circuiti"), 180], [qsTr("Dettaglio"), 460]]
                onRowClicked: (row) => plant.selectRow(row)
                onRowDoubleClicked: (row) => plant.editRow(row)
            }
            Table {
                anchors.fill: parent
                visible: plant.currentTab === 2
                model: plant.currentTab === 2 ? plant.rows : []
                selected: plant.selectedRow
                columns: [[qsTr("Codice"), 140], [qsTr("Tipo"), 100], [qsTr("Formazione"), 90], [qsTr("Ø (mm)"), 60],
                          [qsTr("Peso (kg/m)"), 80], [qsTr("Sezione"), 65], [qsTr("Sistema"), 80]]
                onRowClicked: (row) => plant.selectRow(row)
                onRowDoubleClicked: (row) => plant.editRow(row)
            }
        }

        Flow {
            id: buttons
            width: parent.width
            spacing: 6
            visible: plant.hasProject
            Button {
                text: plant.currentTab === 1 ? qsTr("Nuova tratta dalla selezione...") : qsTr("Aggiungi...")
                tip: plant.currentTab === 1 ? qsTr("Scegli nel disegno le linee o polilinee del percorso, poi indica tubo, canale o passerella e i circuiti")
                                            : ""
                onClicked: plant.addItem()
            }
            Button { text: qsTr("Modifica..."); enabled: plant.selectedRow >= 0; onClicked: plant.editRow(plant.selectedRow) }
            Button { text: qsTr("Elimina"); enabled: plant.selectedRow >= 0; onClicked: plant.removeRow(plant.selectedRow) }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Aggiungi la selezione")
                enabled: plant.selectedRow >= 0
                tip: qsTr("Le linee selezionate nel disegno entrano nella tratta scelta")
                onClicked: plant.addSelectionToRun(plant.selectedRow)
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Disegna sezione")
                enabled: plant.selectedRow >= 0
                tip: qsTr("Disegna accanto al percorso la sezione della tratta con i cavi, l'esito e la legenda (comando SEZIONETRATTA)")
                onClicked: plant.section(plant.selectedRow)
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Distinta...")
                tip: qsTr("Distinta materiali dalle tratte di questo disegno: in Excel (CSV) e/o come tabella nel disegno (comando DISTINTAIMPIANTO)")
                onClicked: plant.bomDialog()
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Tratte da layer")
                tip: qsTr("Le linee dei layer delle canalizzazioni diventano tratte, con tipo e misura dalla legenda (comando TRATTEDALAYER)")
                onClicked: plant.runsFromLayersDialog()
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Percorsi automatici")
                tip: qsTr("Assegna ogni circuito alle tratte dal quadro agli utilizzatori con la sua sigla e ne calcola la lunghezza (comando PERCORSIIMPIANTO)")
                onClicked: plant.routesDialog()
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Quadro dalla selezione")
                tip: qsTr("Segna gli oggetti selezionati come il quadro da cui partono i suoi circuiti")
                onClicked: plant.boardFromSelection()
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Etichette in pianta")
                tip: qsTr("Scrive o aggiorna accanto a ogni tratta tipo, misura, circuiti, riempimento ed esito (comando ETICHETTETRATTE)")
                onClicked: plant.labels()
            }
            Button {
                visible: plant.currentTab === 1
                text: qsTr("Mostra")
                enabled: plant.selectedRow >= 0
                tip: qsTr("Seleziona e inquadra la tratta nel disegno")
                onClicked: plant.showRun(plant.selectedRow)
            }
        }
    }
}
