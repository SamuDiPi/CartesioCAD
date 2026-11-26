// Finestra di dialogo generica (modello: FormDialog): schede, gruppi e campi descritti dal C++.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window
    focus: true

    readonly property var page: form.pages.length > form.currentPage ? form.pages[form.currentPage] : ({ rows: [] })
    readonly property int labelWidth: Math.min(270, Math.round(width * 0.42))

    Keys.onEscapePressed: form.button(form.buttons.length === 1 ? 8 : 2)
    Keys.onReturnPressed: defaultButton()
    Keys.onEnterPressed: defaultButton()
    function defaultButton() {
        for (let b of form.buttons)
            if (b.id === 1 || b.id === 8) { form.button(b.id); return }
    }

    // ------------------------------------------------------------ componenti

    component FlatButton: Rectangle {
        id: fb
        property string label
        property bool primary: false
        signal clicked()
        signal doubleClicked()
        implicitWidth: Math.max(84, caption.implicitWidth + 24)
        implicitHeight: 26
        radius: 3
        color: !enabled ? Theme.field : primary ? (fbArea.pressed ? Theme.checked : fbArea.containsMouse ? "#4c9bf0" : Theme.accent)
                        : (fbArea.pressed ? Theme.checked : fbArea.containsMouse ? Theme.hover : Theme.field)
        border.color: primary ? Theme.accent : fbArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
        opacity: enabled ? 1 : 0.5
        Text { id: caption; anchors.centerIn: parent; text: fb.label; color: Theme.text; font.pixelSize: 12 }
        MouseArea { id: fbArea; anchors.fill: parent; hoverEnabled: true; onClicked: fb.clicked() }
    }

    component Field: Rectangle {
        property bool active: false
        implicitHeight: 24
        radius: 2
        color: Theme.field
        border.color: active ? Theme.accent : Theme.fieldBorder
    }

    component LineEdit: Field {
        id: le
        property alias text: input.text
        property string placeholder
        property alias validator: input.validator
        signal committed(string value)
        active: input.activeFocus
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
            onEditingFinished: le.committed(text)
            Text {
                visible: !input.text.length && !input.activeFocus
                text: le.placeholder
                color: Theme.muted
                font.pixelSize: 12
                font.italic: true
                elide: Text.ElideRight
                width: parent.width
            }
        }
    }

    // Un colore della tavolozza ACI (`aci`: il campo, con tavolozza, nomi e valore scelto).
    component Swatch: Rectangle {
        id: sw
        property var aci
        property int index
        property int size: 16
        width: size; height: size
        color: aci.palette[index]
        border.width: swatchArea.containsMouse || aci.value === index ? 2 : 1
        border.color: aci.value === index ? "white" : swatchArea.containsMouse ? Theme.text : Theme.border
        MouseArea {
            id: swatchArea
            anchors.fill: parent
            hoverEnabled: true
            onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, sw.aci.names[sw.index] + " (" + sw.index + ")") }
            onExited: ui.hideToolTip()
            onClicked: form.edit(sw.aci.id, sw.index)
            onDoubleClicked: { form.edit(sw.aci.id, sw.index); form.button(1) }
        }
    }

    // ------------------------------------------------------------ righe

    Component {
        id: groupRow
        Item {
            property var f
            height: 34
            Text {
                y: 12
                text: f.label
                color: Theme.text
                font.pixelSize: 12
                font.bold: true
            }
            Rectangle { x: groupTitle.width; y: 20; width: parent.width - x; height: 1; color: Theme.separator }
            Text { id: groupTitle; visible: false; text: f.label + "  "; font.pixelSize: 12; font.bold: true }
        }
    }

    Component {
        id: checkRow
        Item {
            id: cr
            property var f
            property bool on: f.value
            height: 24
            Row {
                x: (cr.f.indent || 0) * 22
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 15; height: 15; radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: cr.on ? Theme.accent : Theme.field
                    border.color: cr.on ? Theme.accent : checkArea.containsMouse ? Theme.hoverBorder2 : Theme.fieldBorder
                    Text { anchors.centerIn: parent; text: "✓"; visible: cr.on; color: "white"; font.pixelSize: 11; font.bold: true }
                }
                Text {
                    width: cr.width - 30 - (cr.f.indent || 0) * 22
                    text: cr.f.label
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                id: checkArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: { cr.on = !cr.on; form.edit(cr.f.id, cr.on) }
            }
        }
    }

    // Etichetta a sinistra e controllo a destra.
    component Labeled: Item {
        id: lab
        property var f
        default property alias content: holder.data
        height: Math.max(26, holder.childrenRect.height)
        Text {
            width: root.labelWidth - 10
            anchors.verticalCenter: parent.verticalCenter
            text: lab.f.label || ""
            color: Theme.muted3
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Item {
            id: holder
            x: root.labelWidth
            width: lab.width - root.labelWidth
            height: parent.height
        }
    }

    Component {
        id: numberRow
        Labeled {
            id: nr
            LineEdit {
                id: numberEdit
                width: Math.min(160, parent.width)
                anchors.verticalCenter: parent.verticalCenter
                text: Number(nr.f.value).toFixed(nr.f.decimals) + (nr.f.suffix || "")
                validator: RegularExpressionValidator { regularExpression: /^\s*-?[0-9]*[.,]?[0-9]*.*$/ }
                onCommitted: function(value) {
                    let v = parseFloat(value.replace(",", "."))
                    if (isNaN(v)) v = nr.f.value
                    v = Math.max(nr.f.min, Math.min(nr.f.max, v))
                    form.edit(nr.f.id, v)
                    nr.f = Object.assign({}, nr.f, { value: v })
                    text = Number(v).toFixed(nr.f.decimals) + (nr.f.suffix || "")
                }
                WheelHandler {
                    onWheel: function(event) {
                        const step = nr.f.decimals > 0 ? Math.pow(10, -Math.min(nr.f.decimals, 1)) : 1
                        let v = Math.max(nr.f.min, Math.min(nr.f.max, nr.f.value + (event.angleDelta.y > 0 ? step : -step)))
                        form.edit(nr.f.id, v)
                        nr.f = Object.assign({}, nr.f, { value: v })
                        numberEdit.text = Number(v).toFixed(nr.f.decimals) + (nr.f.suffix || "")
                    }
                }
            }
        }
    }

    Component {
        id: sliderRow
        Labeled {
            id: sr
            property int value: f.value
            Rectangle {
                id: track
                width: parent.width - 60
                height: 4; radius: 2
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.field
                border.color: Theme.fieldBorder
                Rectangle {
                    width: (sr.value - sr.f.min) / Math.max(1, sr.f.max - sr.f.min) * track.width
                    height: parent.height; radius: 2; color: Theme.accent
                }
                Rectangle {
                    x: (sr.value - sr.f.min) / Math.max(1, sr.f.max - sr.f.min) * track.width - 7
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14; height: 14; radius: 7
                    color: Theme.text
                    border.color: Theme.border
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    function update(mx) {
                        const t = Math.max(0, Math.min(1, (mx - 8) / track.width))
                        const v = Math.round(sr.f.min + t * (sr.f.max - sr.f.min))
                        if (v !== sr.value) { sr.value = v; form.edit(sr.f.id, v) }
                    }
                    onPressed: function(mouse) { update(mouse.x) }
                    onPositionChanged: function(mouse) { if (pressed) update(mouse.x) }
                }
            }
            Text {
                x: parent.width - 50
                anchors.verticalCenter: parent.verticalCenter
                text: sr.value + (sr.f.suffix || "")
                color: Theme.text
                font.pixelSize: 12
            }
        }
    }

    Component {
        id: colorRow
        Labeled {
            id: colr
            Field {
                width: 130
                anchors.verticalCenter: parent.verticalCenter
                active: colorArea.containsMouse
                Rectangle { x: 5; width: 16; height: 14; anchors.verticalCenter: parent.verticalCenter; color: colr.f.value; border.color: Theme.border }
                Text { x: 28; anchors.verticalCenter: parent.verticalCenter; text: String(colr.f.value).toUpperCase(); color: Theme.text; font.pixelSize: 12 }
                MouseArea { id: colorArea; anchors.fill: parent; hoverEnabled: true; onClicked: form.pickColor(colr.f.id) }
            }
        }
    }

    Component {
        id: choiceRow
        Labeled {
            id: ch
            Field {
                id: box
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
                active: choiceArea.containsMouse
                Text {
                    x: 6
                    width: parent.width - 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: ch.f.value
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.muted }
                MouseArea {
                    id: choiceArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { const p = box.mapToGlobal(0, box.height); form.popupChoice(ch.f.id, p.x, p.y, box.width) }
                }
            }
        }
    }

    Component {
        id: textRow
        Labeled {
            id: tr
            LineEdit {
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
                text: tr.f.value
                placeholder: tr.f.placeholder || ""
                onCommitted: function(value) { form.edit(tr.f.id, value) }
                onTextChanged: form.edit(tr.f.id, text)
            }
        }
    }

    // Testo su più righe: etichetta sopra, area che scorre; in sola lettura per le anteprime.
    Component {
        id: textAreaRow
        Column {
            id: ta
            property var f
            spacing: 4
            Text {
                visible: (ta.f.label || "").length > 0
                width: parent.width
                text: ta.f.label || ""
                color: Theme.muted3
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Field {
                width: parent.width
                height: (ta.f.lines || 5) * 16 + 10
                active: area.activeFocus
                color: ta.f.readOnly ? Theme.window : Theme.field
                Flickable {
                    id: flick
                    anchors.fill: parent
                    anchors.margins: 5
                    contentWidth: width
                    contentHeight: area.contentHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    function ensureVisible(r) {
                        if (contentY >= r.y)
                            contentY = r.y
                        else if (contentY + height <= r.y + r.height)
                            contentY = r.y + r.height - height
                    }
                    TextEdit {
                        id: area
                        width: flick.width
                        text: ta.f.value || ""
                        readOnly: !!ta.f.readOnly
                        wrapMode: TextEdit.Wrap
                        color: ta.f.readOnly ? Theme.muted3 : Theme.text
                        selectionColor: Theme.accent
                        selectByMouse: true
                        font.pixelSize: ta.f.readOnly ? 11 : 12
                        onCursorRectangleChanged: flick.ensureVisible(cursorRectangle)
                        onTextChanged: {
                            if (!readOnly)
                                form.edit(ta.f.id, text)
                            else
                                cursorPosition = 0   // anteprima: dall'inizio
                        }
                        Component.onCompleted: if (readOnly) cursorPosition = 0
                        Text {
                            visible: !area.text.length && !area.activeFocus
                            width: parent.width
                            text: ta.f.placeholder || ""
                            color: Theme.muted
                            font.pixelSize: 12
                            font.italic: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    Component {
        id: pathRow
        Item {
            id: pr
            property var f
            readonly property bool labeled: (f.label || "").length > 0 && f.mode !== 3
            height: 26
            Text {
                visible: pr.labeled
                width: root.labelWidth - 10
                anchors.verticalCenter: parent.verticalCenter
                text: pr.f.label || ""
                color: Theme.muted3
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            LineEdit {
                x: pr.labeled ? root.labelWidth : 0
                width: pr.width - x - browseButton.width - 6
                anchors.verticalCenter: parent.verticalCenter
                text: pr.f.value
                placeholder: pr.f.placeholder || ""
                onTextChanged: form.edit(pr.f.id, text)
            }
            FlatButton {
                id: browseButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                label: pr.f.browseText
                onClicked: form.browse(pr.f.id)
            }
        }
    }

    Component {
        id: noteRow
        Item {
            id: nt
            property var f
            height: noteText.implicitHeight + 6
            Text {
                id: noteText
                y: 3
                width: parent.width
                text: nt.f.label
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                linkColor: Theme.accent
                onLinkActivated: function(link) { form.openLink(link) }
            }
        }
    }

    Component {
        id: listRow
        Rectangle {
            id: lr
            property var f
            height: 220
            color: Theme.drawing
            border.color: Theme.fieldBorder
            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: lr.f.items
                currentIndex: lr.f.index
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: listView.width
                    height: 22
                    color: index === listView.currentIndex ? Theme.checked : itemArea.containsMouse ? Theme.tabHover : "transparent"
                    Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData; color: Theme.text; font.pixelSize: 12 }
                    MouseArea {
                        id: itemArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { listView.currentIndex = index; form.edit(lr.f.id, index) }
                        onDoubleClicked: { listView.currentIndex = index; form.edit(lr.f.id, index); form.activate(lr.f.id, index) }
                    }
                }
            }
        }
    }

    // Galleria: riquadri con anteprima e nome (motivi di tratteggio).
    Component {
        id: galleryRow
        Rectangle {
            id: gr
            property var f
            height: 330
            color: Theme.drawing
            border.color: Theme.fieldBorder
            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                cellWidth: 92
                cellHeight: 100
                model: gr.f.items
                currentIndex: gr.f.index
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: 88
                    height: 96
                    radius: 3
                    color: index === grid.currentIndex ? Theme.checked : tileArea.containsMouse ? Theme.tabHover : "transparent"
                    border.color: index === grid.currentIndex ? Theme.hoverBorder2 : "transparent"
                    Image {
                        x: 8; y: 4
                        width: 72; height: 72
                        source: gr.f.images[index]
                        sourceSize.width: 72
                        sourceSize.height: 72
                        smooth: false
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 78
                        width: parent.width - 4
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: parent.modelData
                        color: Theme.text
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: tileArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { grid.currentIndex = index; form.edit(gr.f.id, index) }
                        onDoubleClicked: { grid.currentIndex = index; form.edit(gr.f.id, index); form.activate(gr.f.id, index) }
                    }
                }
            }
        }
    }

    Component {
        id: buttonsRowComponent
        Item {
            id: br
            property var f
            height: 30
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: br.f.items
                    FlatButton {
                        required property string modelData
                        required property int index
                        label: modelData
                        onClicked: form.press(br.f.id, index)
                    }
                }
            }
        }
    }

    // Tavolozza dell'indice colore ACI: 24 tinte x 10 righe, colori standard e grigi, DaLayer/DaBlocco.
    Component {
        id: aciRow
        Column {
            id: ac
            property var f
            spacing: 8
            function pick(i) { form.edit(ac.f.id, i) }
            Text { text: qsTr("Indice colore (ACI):"); color: Theme.muted3; font.pixelSize: 12 }
            Grid {
                columns: 24
                spacing: 1
                flow: Grid.TopToBottom
                rows: 10
                Repeater { model: 240; Swatch { required property int modelData; aci: ac.f; index: 10 + modelData } }
            }
            Row {
                spacing: 2
                Repeater { model: 9; Swatch { required property int modelData; aci: ac.f; index: 1 + modelData; size: 22 } }
                Item { width: 16; height: 1 }
                Repeater { model: 6; Swatch { required property int modelData; aci: ac.f; index: 250 + modelData; size: 22 } }
            }
            Row {
                spacing: 8
                FlatButton { visible: ac.f.byLayer; label: qsTr("DaLayer"); onClicked: ac.pick(ac.f.byLayerValue) }
                FlatButton { visible: ac.f.byLayer; label: qsTr("DaBlocco"); onClicked: ac.pick(ac.f.byBlockValue) }
                Item { width: 20; height: 1 }
                Text { text: qsTr("Colore:"); color: Theme.muted3; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: 40; height: 22
                    anchors.verticalCenter: parent.verticalCenter
                    color: ac.f.value >= 1 && ac.f.value <= 255 ? ac.f.palette[ac.f.value] : "transparent"
                    border.color: Theme.border
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: ac.f.value === ac.f.byLayerValue ? qsTr("DaLayer") : ac.f.value === ac.f.byBlockValue ? qsTr("DaBlocco")
                                                                                                               : ac.f.names[ac.f.value]
                    color: Theme.text
                    font.pixelSize: 12
                }
            }
        }
    }

    // ------------------------------------------------------------ struttura

    // Linguette delle schede (solo con più di una).
    Row {
        id: tabs
        visible: form.pages.length > 1
        x: 10; y: 8
        height: visible ? 28 : 0
        spacing: 2
        Repeater {
            model: form.pages
            Rectangle {
                required property var modelData
                required property int index
                width: tabText.implicitWidth + 22
                height: 28
                radius: 3
                color: index === form.currentPage ? Theme.bar : tabArea.containsMouse ? Theme.tabHover : "transparent"
                border.color: index === form.currentPage ? Theme.fieldBorder : "transparent"
                Rectangle { visible: index === form.currentPage; anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent }
                Text { id: tabText; anchors.centerIn: parent; text: modelData.title; color: index === form.currentPage ? Theme.text : Theme.muted2; font.pixelSize: 12 }
                MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: form.currentPage = index }
            }
        }
    }

    Rectangle {
        id: body
        anchors.top: tabs.visible ? tabs.bottom : parent.top
        anchors.topMargin: tabs.visible ? 0 : 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        anchors.margins: 10
        color: Theme.bar
        border.color: Theme.separator
        radius: 3

        Flickable {
            id: flick
            anchors.fill: parent
            anchors.margins: 12
            contentHeight: rows.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: rows
                width: flick.width - (flick.contentHeight > flick.height ? 10 : 0)
                spacing: 6
                Repeater {
                    model: root.page.rows
                    Loader {
                        id: rowLoader
                        required property var modelData
                        width: rows.width
                        property var rowData: modelData
                        enabled: form.enabledFields[String(modelData.id)] !== false
                        opacity: enabled ? 1 : 0.45
                        sourceComponent: {
                            switch (modelData.kind) {
                            case "group": return groupRow
                            case "check": return checkRow
                            case "number": return numberRow
                            case "slider": return sliderRow
                            case "color": return colorRow
                            case "choice": return choiceRow
                            case "text": return textRow
                            case "textarea": return textAreaRow
                            case "path": return pathRow
                            case "note": return noteRow
                            case "list": return listRow
                            case "gallery": return galleryRow
                            case "buttons": return buttonsRowComponent
                            case "aci": return aciRow
                            }
                            return null
                        }
                        onLoaded: { item.f = rowLoader.rowData; item.width = Qt.binding(() => rowLoader.width) }
                        Connections {
                            target: form
                            function onFieldChanged(id, data) {
                                if (id === rowLoader.modelData.id && rowLoader.item) {
                                    rowLoader.rowData = data
                                    rowLoader.item.f = data
                                }
                            }
                        }
                    }
                }
            }
        }
        // Barra di scorrimento sottile.
        Rectangle {
            visible: flick.contentHeight > flick.height
            anchors.right: parent.right
            anchors.rightMargin: 3
            y: 4 + flick.visibleArea.yPosition * (parent.height - 8)
            width: 5
            height: flick.visibleArea.heightRatio * (parent.height - 8)
            radius: 2
            color: Theme.hoverBorder
        }
    }

    Row {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        height: 26
        spacing: 8
        Repeater {
            model: form.buttons
            FlatButton {
                required property var modelData
                label: modelData.text
                primary: modelData.id === 1 || (modelData.id === 8 && form.buttons.length === 1)
                onClicked: form.button(modelData.id)
            }
        }
    }
}
