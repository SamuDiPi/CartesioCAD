// Tavolozza Blocchi (libreria simboli): blocchi del disegno, di un file o cartella libreria, recenti
// (modello: BlockLibrary in C++). Clic = inserisci, tasto destro = menu.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    component TabButton: Rectangle {
        id: tb
        property string text
        property int index
        signal clicked()
        height: 28
        width: (root.width - 16) / 3
        color: library.tab === index ? Theme.drawing : tabArea.containsMouse ? Theme.tabHover : "transparent"
        Rectangle { visible: library.tab === tb.index; anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent }
        Text { anchors.centerIn: parent; text: tb.text; color: library.tab === tb.index ? "white" : Theme.muted2; font.pixelSize: 12; font.bold: library.tab === tb.index }
        MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: library.tab = tb.index }
    }

    component SmallButton: Rectangle {
        id: sb
        property string text
        property string tip
        signal clicked()
        width: label.implicitWidth + 18
        height: 24
        radius: 2
        color: sbArea.pressed ? Theme.checked : (sbArea.containsMouse ? "#56637a" : Theme.hover)
        border.color: Theme.hoverBorder
        Text { id: label; anchors.centerIn: parent; text: sb.text; color: Theme.text; font.pixelSize: 12 }
        MouseArea {
            id: sbArea; anchors.fill: parent; hoverEnabled: true
            onEntered: if (sb.tip) { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, sb.tip) }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); sb.clicked() }
        }
    }

    Column {
        id: top
        x: 8; y: 6
        width: root.width - 16
        spacing: 6
        Row {
            TabButton { text: qsTr("Disegno"); index: 0 }
            TabButton { text: qsTr("Libreria"); index: 1 }
            TabButton { text: qsTr("Recenti"); index: 2 }
        }
        // Ricerca per nome.
        Rectangle {
            width: parent.width; height: 26; radius: 2
            color: Theme.field
            border.color: search.activeFocus ? Theme.accent : Theme.fieldBorder
            TextInput {
                id: search
                x: 8; width: parent.width - 16
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text; selectionColor: Theme.accent; selectByMouse: true; clip: true
                font.pixelSize: 12
                text: library.filter
                onTextEdited: library.filter = text
                Text {
                    visible: !search.text.length && !search.activeFocus
                    text: qsTr("Cerca per nome...")
                    color: Theme.muted; font.pixelSize: 12; font.italic: true
                }
            }
        }
        // Sorgente della libreria.
        Column {
            visible: library.tab === 1
            width: parent.width
            spacing: 4
            Text {
                width: parent.width
                text: library.sourceText
                color: Theme.muted2; font.pixelSize: 11
                elide: Text.ElideMiddle
            }
            Row {
                spacing: 6
                SmallButton { text: qsTr("File..."); tip: qsTr("Un disegno DWG/DXF: i suoi blocchi sono i simboli"); onClicked: library.chooseFile() }
                SmallButton { text: qsTr("Cartella..."); tip: qsTr("Una cartella di disegni: ogni file è un simbolo, inserito come blocco"); onClicked: library.chooseFolder() }
            }
        }
        Text {
            visible: library.statusText !== ""
            width: parent.width
            text: library.statusText
            color: Theme.muted2; font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    GridView {
        id: grid
        anchors.top: top.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property int columns: Math.max(1, Math.floor(width / 110))
        cellWidth: Math.floor(width / columns)
        cellHeight: 134
        model: library.items
        delegate: Item {
            id: cell
            required property var modelData
            required property int index
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: 3
                color: cellArea.containsMouse ? Theme.tabHover : "transparent"
                border.color: cellArea.containsMouse ? Theme.accent : "transparent"
                Rectangle {
                    id: thumb
                    width: 96; height: 96
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4
                    color: "#1b2027"
                    border.color: "#3e4757"
                    Image {
                        anchors.fill: parent; anchors.margins: 1
                        source: cell.modelData.image
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        smooth: true; mipmap: true
                    }
                    Text {
                        visible: cell.modelData.image === ""
                        anchors.centerIn: parent
                        text: cell.modelData.kind === "file" ? qsTr("DWG") : "?"
                        color: Theme.muted; font.pixelSize: 14; font.bold: true
                    }
                }
                Text {
                    anchors.top: thumb.bottom; anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 6
                    horizontalAlignment: Text.AlignHCenter
                    text: cell.modelData.name
                    color: Theme.text; font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
            // Trascinamento sul disegno: si rilascia dove va il simbolo (scala 1, rotazione 0).
            Item {
                id: dragProxy
                width: 1; height: 1
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction
                Drag.mimeData: ({ "application/x-cartesio-block": library.dragPayload(cell.index) })
                Drag.imageSource: cell.modelData.image
            }
            MouseArea {
                id: cellArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                property point pressAt
                property bool dragging: false
                onPressed: (mouse) => { pressAt = Qt.point(mouse.x, mouse.y); dragging = false }
                onPositionChanged: (mouse) => {
                    if (!pressed || dragging || !(pressedButtons & Qt.LeftButton))
                        return;
                    if (Math.abs(mouse.x - pressAt.x) + Math.abs(mouse.y - pressAt.y) > 8) {
                        dragging = true;
                        ui.hideToolTip();
                        dragProxy.Drag.active = true;
                    }
                }
                onReleased: dragProxy.Drag.active = false
                onEntered: { const p = mapToGlobal(mouseX + 12, mouseY + 18); ui.showToolTip(p.x, p.y, cell.modelData.tip) }
                onExited: ui.hideToolTip()
                onClicked: (mouse) => {
                    ui.hideToolTip();
                    if (dragging)
                        return;
                    if (mouse.button === Qt.RightButton) {
                        const p = mapToGlobal(mouse.x, mouse.y);
                        library.popupMenu(cell.index, p.x, p.y);
                    } else {
                        library.activate(cell.index);
                    }
                }
            }
        }
        Rectangle {
            visible: grid.contentHeight > grid.height
            anchors.right: parent.right
            y: grid.visibleArea.yPosition * grid.height
            width: 5; radius: 2
            height: Math.max(30, grid.visibleArea.heightRatio * grid.height)
            color: "#7a8494"
        }
    }
}
