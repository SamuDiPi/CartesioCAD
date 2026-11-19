// Tavolozza Guida: percorsi guidati e manuale (modello: GuidePanel in C++). Si apre solo a richiesta (GUIDA, F1).
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    component Button: Rectangle {
        id: b
        property string text
        property bool enabled: true
        property bool primary: false
        signal clicked()
        width: label.implicitWidth + 22
        height: 26
        radius: 2
        opacity: enabled ? 1.0 : 0.4
        color: b.primary ? (area.pressed ? "#1f6fc0" : (area.containsMouse ? "#3a8ee6" : Theme.accent))
                         : (area.pressed ? Theme.checked : (area.containsMouse ? "#56637a" : Theme.hover))
        border.color: b.primary ? Theme.accent : Theme.hoverBorder
        Text { id: label; anchors.centerIn: parent; text: b.text; color: "#ffffff"; font.pixelSize: 12 }
        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: if (b.enabled) b.clicked() }
    }

    // Schede: Percorsi, Manuale.
    Row {
        id: tabs
        x: 6; y: 6
        height: 28
        Repeater {
            model: [qsTr("Percorsi guidati"), qsTr("Manuale")]
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: tabLabel.implicitWidth + 26
                height: 28
                color: index === guide.page ? "#2a303a" : (tabArea.containsMouse ? "#353c48" : "transparent")
                Rectangle { visible: index === guide.page; width: parent.width; height: 2; color: Theme.accent }
                Text { id: tabLabel; anchors.centerIn: parent; text: modelData; color: index === guide.page ? "#ffffff" : Theme.muted3; font.pixelSize: 12; font.bold: index === guide.page }
                MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: guide.page = index }
            }
        }
    }

    // ================================================================ Percorsi
    Item {
        anchors { left: parent.left; right: parent.right; top: tabs.bottom; bottom: parent.bottom; margins: 8 }
        visible: guide.page === 0

        // Elenco dei percorsi.
        Flickable {
            anchors.fill: parent
            visible: guide.tour < 0
            contentHeight: tourList.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: tourList
                width: parent.width
                spacing: 8
                Text {
                    width: parent.width
                    text: qsTr("Scegli un percorso: CartesioCAD ti propone un passo alla volta, evidenzia in arancione il pulsante da usare e si accorge da solo quando hai fatto. Puoi uscire quando vuoi.")
                    color: Theme.muted2; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
                Repeater {
                    model: guide.tours
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: tourList.width
                        height: card.height + 16
                        radius: 3
                        color: cardArea.containsMouse ? "#323a48" : "#2a303a"
                        border.color: "#3d4656"
                        MouseArea { id: cardArea; anchors.fill: parent; hoverEnabled: true; onClicked: guide.startTour(index) }
                        Column {
                            id: card
                            x: 10; y: 8
                            width: parent.width - 20
                            spacing: 4
                            Text { text: modelData.title; color: "#ffffff"; font.pixelSize: 13; font.bold: true }
                            Text { width: parent.width; text: modelData.description; color: Theme.muted3; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Row {
                                spacing: 10
                                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("%1 passi, circa %2 minuti").arg(modelData.steps).arg(modelData.minutes); color: Theme.muted2; font.pixelSize: 11 }
                                Button { text: qsTr("Inizia ›"); primary: true; onClicked: guide.startTour(index) }
                            }
                        }
                    }
                }
            }
        }

        // Percorso in corso.
        Column {
            anchors.fill: parent
            visible: guide.tour >= 0
            spacing: 10
            Text { width: parent.width; text: guide.tourTitle; color: "#ffffff"; font.pixelSize: 14; font.bold: true; wrapMode: Text.WordWrap }
            Column {
                width: parent.width
                spacing: 4
                Text { text: guide.finished ? qsTr("Percorso completato") : qsTr("Passo %1 di %2").arg(guide.step + 1).arg(guide.stepCount); color: Theme.muted3; font.pixelSize: 11 }
                Rectangle {
                    width: parent.width; height: 6; radius: 3; color: "#2a303a"
                    Rectangle {
                        width: parent.width * (guide.finished ? 1 : (guide.step + (guide.stepDone ? 1 : 0)) / Math.max(1, guide.stepCount))
                        height: parent.height; radius: 3; color: guide.finished ? "#5cc58a" : Theme.accent
                        Behavior on width { NumberAnimation { duration: 250 } }
                    }
                }
            }
            Rectangle {
                width: parent.width
                height: stepColumn.height + 24
                radius: 4
                color: guide.stepDone ? "#20382c" : "#2a303a"
                border.color: guide.stepDone ? "#5cc58a" : Theme.accent
                border.width: 1.5
                Column {
                    id: stepColumn
                    x: 12; y: 12
                    width: parent.width - 24
                    spacing: 10
                    Text {
                        width: parent.width
                        text: guide.finished ? qsTr("Fatto tutto. Puoi rifare il percorso o sceglierne un altro; il manuale (scheda accanto) spiega ogni comando nei dettagli.")
                                             : guide.stepText
                        color: Theme.text; font.pixelSize: 13; wrapMode: Text.WordWrap; lineHeight: 1.2
                    }
                    Row {
                        spacing: 8
                        visible: !guide.finished
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: guide.stepDone ? "#5cc58a" : "#e3913a"
                            SequentialAnimation on opacity {
                                running: !guide.stepDone && guide.tour >= 0
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.25; duration: 600 }
                                NumberAnimation { to: 1.0; duration: 600 }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: guide.stepDone ? qsTr("Fatto!") : (guide.stepManual ? qsTr("Quando hai finito, premi Avanti.") : qsTr("In attesa: fai il passo nel disegno..."))
                            color: guide.stepDone ? "#5cc58a" : Theme.muted2
                            font.pixelSize: 12; font.bold: guide.stepDone
                        }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                Button { visible: !guide.finished; text: qsTr("‹ Indietro"); enabled: guide.step > 0; onClicked: guide.back() }
                Button { visible: !guide.finished && guide.stepManual && !guide.stepDone; text: qsTr("Avanti ›"); primary: true; onClicked: guide.next() }
                Button { visible: !guide.finished && !guide.stepManual && !guide.stepDone; text: qsTr("Salta il passo"); onClicked: guide.next() }
                Button { visible: guide.finished; text: qsTr("Altri percorsi"); primary: true; onClicked: guide.stopTour() }
                Button { visible: !guide.finished; text: qsTr("Esci dal percorso"); onClicked: guide.stopTour() }
            }
        }
    }

    // ================================================================ Manuale
    Item {
        anchors { left: parent.left; right: parent.right; top: tabs.bottom; bottom: parent.bottom; margins: 8 }
        visible: guide.page === 1

        // Indice e ricerca.
        Column {
            anchors.fill: parent
            visible: guide.pageText === ""
            spacing: 6
            Rectangle {
                id: searchBox
                width: parent.width
                height: 28
                radius: 2
                color: Theme.field
                border.color: input.activeFocus ? Theme.accent : Theme.fieldBorder
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
                    text: guide.query
                    onTextEdited: guide.search(text)
                    Text { visible: input.text.length === 0; text: qsTr("Cerca: comando (TRIM, TR), argomento (tratteggio, stampa...)"); color: "#7d8796"; font: input.font }
                }
            }
            ListView {
                id: sectionList
                width: parent.width
                height: parent.height - searchBox.height - 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: guide.sections
                section.property: guide.query === "" ? "chapter" : ""
                section.delegate: Rectangle {
                    required property string section
                    width: sectionList.width
                    height: 26
                    color: "transparent"
                    Text { x: 2; anchors.bottom: parent.bottom; anchors.bottomMargin: 3; text: section; color: Theme.accent; font.pixelSize: 12; font.bold: true }
                }
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 24
                    color: rowArea.containsMouse ? "#353c48" : "transparent"
                    Text {
                        x: 12
                        width: parent.width - 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: guide.query === "" ? modelData.title : modelData.title + "   <font color=\"#7d8796\">" + modelData.chapter + "</font>"
                        textFormat: Text.StyledText
                        color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; onClicked: guide.openSection(index) }
                }
                Text {
                    visible: sectionList.count === 0
                    width: parent.width
                    text: qsTr("Niente trovato per \"%1\".").arg(guide.query)
                    color: Theme.muted2; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
            }
        }

        // Pagina.
        Column {
            anchors.fill: parent
            visible: guide.pageText !== ""
            spacing: 6
            Row {
                id: pageBar
                spacing: 8
                Button { text: guide.query === "" ? qsTr("‹ Indice") : qsTr("‹ Risultati"); onClicked: guide.closeSection() }
                Text { anchors.verticalCenter: parent.verticalCenter; text: guide.pageTitle; color: Theme.muted3; font.pixelSize: 12 }
            }
            Flickable {
                id: pageFlick
                width: parent.width
                height: parent.height - pageBar.height - 6
                contentHeight: pageBody.height + 20
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Text {
                    id: pageBody
                    width: pageFlick.width - 8
                    text: guide.pageText
                    textFormat: Text.MarkdownText
                    color: Theme.text
                    linkColor: Theme.accent
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.15
                    onTextChanged: pageFlick.contentY = 0
                }
            }
        }
    }
}
