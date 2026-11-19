// Scheda "Inizia" (modello: StartPage), con la disposizione della pagina iniziale:
// CREA = Inizia disegno e modelli | Documenti recenti | Connetti; APPRENDI = comandi principali da provare.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: Theme.window

    readonly property color cardLight: "#d6d9de"
    readonly property color cardText: "#20252d"
    readonly property color blue: "#0a6ed1"
    readonly property color blueHover: "#2485e6"

    // ---------------------------------------------------------------- icone disegnate (niente immagini esterne)
    component VecIcon: Canvas {
        id: ic
        property string kind
        property color stroke: Theme.muted3
        width: 28; height: 28
        onKindChanged: requestPaint()
        onStrokeChanged: requestPaint()
        onPaint: {
            const c = getContext("2d");
            c.reset();
            c.strokeStyle = ic.stroke;
            c.fillStyle = ic.stroke;
            c.lineWidth = 1.5;
            c.lineJoin = "round";
            const w = width, h = height;
            if (kind === "folder") {
                c.beginPath();
                c.moveTo(w * .08, h * .25); c.lineTo(w * .38, h * .25); c.lineTo(w * .46, h * .35);
                c.lineTo(w * .86, h * .35); c.lineTo(w * .86, h * .80); c.lineTo(w * .08, h * .80); c.closePath();
                c.globalAlpha = .35; c.fill(); c.globalAlpha = 1; c.stroke();
                c.beginPath(); c.moveTo(w * .08, h * .80); c.lineTo(w * .22, h * .48); c.lineTo(w * .98, h * .48); c.lineTo(w * .86, h * .80); c.stroke();
            } else if (kind === "files") {
                for (let i = 2; i >= 0; --i) {
                    const o = i * w * .1;
                    c.beginPath(); c.rect(w * .12 + o, h * .18 + o * .6, w * .52, h * .56);
                    c.globalAlpha = i === 0 ? .35 : .15; c.fill(); c.globalAlpha = 1; c.stroke();
                }
            } else if (kind === "template") {
                c.beginPath(); c.rect(w * .16, h * .12, w * .6, h * .76); c.stroke();
                c.beginPath(); c.moveTo(w * .26, h * .3); c.lineTo(w * .66, h * .3); c.moveTo(w * .26, h * .44); c.lineTo(w * .66, h * .44);
                c.moveTo(w * .26, h * .58); c.lineTo(w * .52, h * .58); c.stroke();
                c.beginPath(); c.rect(w * .46, h * .66, w * .3, h * .22); c.globalAlpha = .5; c.fill(); c.globalAlpha = 1; c.stroke();
            } else if (kind === "pin" || kind === "pinOn") {
                c.save(); c.translate(w / 2, h / 2); c.rotate(kind === "pin" ? Math.PI / 2 : Math.PI / 4);
                c.beginPath(); c.rect(-w * .12, -h * .36, w * .24, h * .34);
                if (kind === "pinOn") c.fill(); c.stroke();
                c.beginPath(); c.moveTo(-w * .26, -h * .02); c.lineTo(w * .26, -h * .02); c.stroke();
                c.beginPath(); c.moveTo(0, -h * .02); c.lineTo(0, h * .38); c.stroke();
                c.restore();
            } else if (kind === "viewLarge") {
                c.beginPath(); c.rect(w * .2, h * .12, w * .6, h * .34); c.rect(w * .2, h * .54, w * .6, h * .34); c.fill();
            } else if (kind === "viewSmall") {
                for (let k = 0; k < 3; ++k) {
                    c.beginPath(); c.rect(w * .14, h * (.12 + k * .28), w * .24, h * .2); c.fill();
                    c.beginPath(); c.rect(w * .46, h * (.17 + k * .28), w * .4, h * .1); c.fill();
                }
            } else if (kind === "viewList") {
                for (let k = 0; k < 5; ++k) { c.beginPath(); c.rect(w * .2, h * (.14 + k * .16), w * .6, h * .07); c.fill(); }
            } else if (kind === "bug") {
                c.beginPath(); c.arc(w / 2, h * .56, w * .22, 0, Math.PI * 2); c.stroke();
                c.beginPath(); c.moveTo(w / 2, h * .34); c.lineTo(w / 2, h * .78);
                c.moveTo(w * .1, h * .5); c.lineTo(w * .28, h * .52); c.moveTo(w * .9, h * .5); c.lineTo(w * .72, h * .52);
                c.moveTo(w * .14, h * .8); c.lineTo(w * .3, h * .7); c.moveTo(w * .86, h * .8); c.lineTo(w * .7, h * .7);
                c.moveTo(w * .4, h * .22); c.lineTo(w * .46, h * .34); c.moveTo(w * .6, h * .22); c.lineTo(w * .54, h * .34); c.stroke();
            } else if (kind === "code") {
                c.beginPath(); c.moveTo(w * .34, h * .26); c.lineTo(w * .12, h * .5); c.lineTo(w * .34, h * .74);
                c.moveTo(w * .66, h * .26); c.lineTo(w * .88, h * .5); c.lineTo(w * .66, h * .74);
                c.moveTo(w * .56, h * .2); c.lineTo(w * .44, h * .8); c.stroke();
            }
        }
    }

    component LinkRow: Item {
        id: lr
        property string label
        property string icon
        property string tip
        signal clicked()
        width: 300; height: 38
        VecIcon { id: lrIcon; x: 8; anchors.verticalCenter: parent.verticalCenter; kind: lr.icon; stroke: lrArea.containsMouse ? "white" : Theme.muted3 }
        Text {
            x: 50; anchors.verticalCenter: parent.verticalCenter
            text: lr.label; font.pixelSize: 15
            color: lrArea.containsMouse ? "white" : Theme.text
            font.underline: lrArea.containsMouse
        }
        MouseArea {
            id: lrArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onEntered: if (lr.tip) { const p = mapToGlobal(50, height); ui.showToolTip(p.x, p.y, lr.tip) }
            onExited: ui.hideToolTip()
            onClicked: { ui.hideToolTip(); lr.clicked() }
        }
    }

    component BlueButton: Rectangle {
        id: bb
        property string label
        signal clicked()
        width: parent ? parent.width : 260; height: 34; radius: 2
        color: bbArea.pressed ? Qt.darker(root.blue, 1.15) : bbArea.containsMouse ? root.blueHover : root.blue
        border.color: "#0b4f96"
        Text { anchors.centerIn: parent; text: bb.label; color: "white"; font.pixelSize: 14; font.bold: true }
        MouseArea { id: bbArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bb.clicked() }
    }

    // ---------------------------------------------------------------- CREA
    Item {
        id: createPage
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.bottom: bottomBar.top
        visible: start.page === 0

        // Finestra larga: margini per la freccia APPRENDI e tre colonne. Più stretta: margini piccoli, senza
        // Connetti; molto stretta: solo la colonna Inizia (i recenti restano nel menu dell'applicazione).
        readonly property bool wide: width >= 1250
        readonly property real margin: wide ? 170 : 28
        readonly property real leftW: Math.min(330, Math.max(240, width * 0.3))
        readonly property bool showMid: width >= 2 * margin + leftW + 24 + 260
        readonly property bool showRight: width >= 2 * margin + leftW + 360 + 310 + 2 * 24
        readonly property real rightW: showRight ? 310 : 0
        readonly property real midW: !showMid ? 0 : showRight ? Math.min(520, Math.max(360, width * 0.3))
                                                    : Math.min(560, width - 2 * margin - leftW - 40)
        readonly property int gaps: showRight ? 2 : showMid ? 1 : 0
        readonly property real gap: gaps === 0 ? 0 : Math.max(24, Math.min(170, (width - leftW - midW - rightW - 2 * margin) / gaps))
        readonly property real x0: Math.max(margin, (width - leftW - midW - rightW - gaps * gap) / 2)
        readonly property real topY: Math.max(30, Math.min(110, height * 0.1))

        // Freccia per APPRENDI sul bordo sinistro.
        Item {
            visible: createPage.wide
            x: 22; width: 110; height: 30
            anchors.verticalCenter: parent.verticalCenter
            Row {
                spacing: 12; anchors.verticalCenter: parent.verticalCenter
                Text { text: "◀"; color: learnArea.containsMouse ? "white" : Theme.muted2; font.pixelSize: 13 }
                Text { text: qsTr("APPRENDI"); color: learnArea.containsMouse ? "white" : Theme.muted2; font.pixelSize: 13; font.bold: true }
            }
            MouseArea { id: learnArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: start.page = 1 }
        }

        // --- Colonna Inizia
        Column {
            id: leftCol
            x: createPage.x0; y: createPage.topY
            width: createPage.leftW
            spacing: 12
            Text { text: qsTr("Inizia"); color: Theme.text; font.pixelSize: 21; font.bold: true }

            Rectangle {
                id: tile
                width: createPage.leftW; height: Math.round(createPage.leftW * 0.84)
                color: "#f2f3f5"
                border.color: tileArea.containsMouse ? root.blue : "#aab1bb"
                border.width: tileArea.containsMouse ? 2 : 1

                // Illustrazione: edifici e pianta, disegnati qui.
                Canvas {
                    id: art
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    anchors.fill: parent; anchors.margins: 1
                    anchors.bottomMargin: 41
                    onPaint: {
                        const c = getContext("2d");
                        c.reset();
                        const g = c.createLinearGradient(0, 0, 0, height);
                        g.addColorStop(0, "#fbfbfc"); g.addColorStop(1, "#c9ced6");
                        c.fillStyle = g; c.fillRect(0, 0, width, height);
                        // Pianta in alto a destra.
                        c.strokeStyle = "#9aa2ad"; c.lineWidth = 1;
                        const px = width * .52, py = 16, pw = width * .44, ph = height * .42;
                        c.strokeRect(px, py, pw, ph);
                        c.strokeRect(px + 3, py + 3, pw - 6, ph - 6);
                        c.beginPath();
                        c.moveTo(px + pw * .38, py); c.lineTo(px + pw * .38, py + ph * .62);
                        c.moveTo(px, py + ph * .62); c.lineTo(px + pw * .7, py + ph * .62);
                        c.moveTo(px + pw * .7, py + ph * .3); c.lineTo(px + pw, py + ph * .3);
                        c.moveTo(px + pw * .7, py + ph * .3); c.lineTo(px + pw * .7, py + ph);
                        c.moveTo(px + pw * .2, py + ph * .62); c.lineTo(px + pw * .2, py + ph);
                        c.stroke();
                        c.beginPath(); c.arc(px + pw * .38, py + ph * .62, pw * .12, -Math.PI / 2, 0); c.stroke();
                        // Edifici in assonometria.
                        function box(x, y, w, d, hgt, rows, cols) {
                            const cx = Math.cos(Math.PI / 6), sy = 0.5;
                            const A = [x, y], B = [x + w * cx, y + w * sy], C = [x + w * cx - d * cx, y + w * sy + d * sy], D = [x - d * cx, y + d * sy];
                            function face(pts, fill) {
                                c.beginPath(); c.moveTo(pts[0][0], pts[0][1]);
                                for (let i = 1; i < pts.length; ++i) c.lineTo(pts[i][0], pts[i][1]);
                                c.closePath(); c.fillStyle = fill; c.fill(); c.strokeStyle = "#5e6672"; c.stroke();
                            }
                            face([[A[0], A[1] - hgt], [B[0], B[1] - hgt], [C[0], C[1] - hgt], [D[0], D[1] - hgt]], "#f4f5f7");
                            face([[D[0], D[1] - hgt], [C[0], C[1] - hgt], C, D], "#e3e6ea");
                            face([[C[0], C[1] - hgt], [B[0], B[1] - hgt], B, C], "#cfd4da");
                            c.strokeStyle = "#7d8592";
                            for (let r = 1; r < rows; ++r) {
                                const t = hgt * r / rows;
                                c.beginPath(); c.moveTo(D[0], D[1] - t); c.lineTo(C[0], C[1] - t); c.lineTo(B[0], B[1] - t); c.stroke();
                            }
                            for (let k = 1; k < cols; ++k) {
                                const f = k / cols;
                                c.beginPath();
                                c.moveTo(D[0] + (C[0] - D[0]) * f, D[1] + (C[1] - D[1]) * f - hgt);
                                c.lineTo(D[0] + (C[0] - D[0]) * f, D[1] + (C[1] - D[1]) * f);
                                c.moveTo(C[0] + (B[0] - C[0]) * f, C[1] + (B[1] - C[1]) * f - hgt);
                                c.lineTo(C[0] + (B[0] - C[0]) * f, C[1] + (B[1] - C[1]) * f);
                                c.stroke();
                            }
                        }
                        // Basamento
                        c.fillStyle = "#dfe3e8"; c.strokeStyle = "#8a929e";
                        c.beginPath(); c.moveTo(14, height * .78); c.lineTo(width * .3, height * .62); c.lineTo(width * .52, height * .74);
                        c.lineTo(width * .24, height * .92); c.closePath(); c.fill(); c.stroke();
                        const k = width / 328;   // misure pensate per il riquadro largo 330
                        box(width * .12, height * .5, 34 * k, 30 * k, 64 * k, 6, 3);
                        box(width * .26, height * .34, 30 * k, 26 * k, 104 * k, 10, 3);
                        box(width * .38, height * .52, 26 * k, 22 * k, 52 * k, 5, 2);
                    }
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 14
                    anchors.bottom: templateBar.top; anchors.bottomMargin: 12
                    text: qsTr("Inizia disegno")
                    color: tileArea.containsMouse ? "#5f6b7a" : "#8b939e"
                    font.pixelSize: Math.round(31 * tile.width / 330); font.bold: true
                    style: Text.Raised; styleColor: "white"
                }
                MouseArea {
                    id: tileArea
                    anchors.fill: parent; anchors.bottomMargin: 40
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onEntered: { const p = mapToGlobal(0, height); ui.showToolTip(p.x, p.y, qsTr("Disegno nuovo dal modello scelto qui sotto")) }
                    onExited: ui.hideToolTip()
                    onClicked: { ui.hideToolTip(); start.requestNew() }
                }
                // Modello del disegno nuovo.
                Rectangle {
                    id: templateBar
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 1
                    height: 39
                    color: tplArea.containsMouse ? "#e4e8ee" : "#eceef1"
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#b9bfc8" }
                    VecIcon { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 22; height: 22; kind: "template"; stroke: "#5c6470" }
                    Text {
                        x: 36; width: parent.width - 70; anchors.verticalCenter: parent.verticalCenter
                        text: start.templateName; color: "#4a525e"; font.pixelSize: 14; elide: Text.ElideRight
                    }
                    Text { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: "▼"; color: "#4a525e"; font.pixelSize: 12 }
                    MouseArea {
                        id: tplArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { const p = templateBar.mapToGlobal(0, templateBar.height); start.popupTemplates(p.x, p.y, templateBar.width) }
                    }
                }
            }

            Item { width: 1; height: 6 }
            LinkRow { label: qsTr("Apri file..."); icon: "folder"; tip: qsTr("Apre un disegno DWG, DXF o un modello DWT"); onClicked: start.requestOpen() }
            LinkRow { label: qsTr("Apri più file..."); icon: "files"; tip: qsTr("Sceglie più disegni insieme: ognuno in una scheda"); onClicked: start.requestOpenMany() }
            LinkRow { label: qsTr("Cartella dei modelli..."); icon: "template"; tip: qsTr("I modelli (.dwt, .dwg, .dxf) messi qui compaiono nell'elenco Modelli"); onClicked: start.openTemplatesFolder() }
        }

        // --- Colonna Documenti recenti
        Item {
            id: midCol
            visible: createPage.showMid
            x: leftCol.x + leftCol.width + createPage.gap; y: createPage.topY
            width: createPage.midW
            height: createPage.height - y - 24

            Text { id: recentTitle; text: qsTr("Documenti recenti"); color: Theme.text; font.pixelSize: 21; font.bold: true }
            Text {
                anchors.top: recentTitle.bottom; anchors.topMargin: 18
                visible: start.recent.length === 0
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Nessun documento recente: i disegni aperti compariranno qui, con la loro anteprima.")
                color: Theme.muted2; font.pixelSize: 14
            }

            ListView {
                id: list
                anchors.top: recentTitle.bottom; anchors.topMargin: 12
                anchors.bottom: viewBar.top; anchors.bottomMargin: 6
                width: parent.width
                clip: true
                spacing: start.viewMode === 0 ? 10 : start.viewMode === 1 ? 4 : 0
                boundsBehavior: Flickable.StopAtBounds
                model: start.recent
                section.property: "group"
                section.delegate: Item {
                    required property string section
                    width: list.width; height: 30
                    Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 6; text: parent.section; color: Theme.muted2; font.pixelSize: 12; font.bold: true }
                }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property int mode: start.viewMode
                    readonly property string thumb: start.thumbs[modelData.path] || ""
                    width: list.width - 12
                    height: mode === 0 ? 116 : mode === 1 ? 52 : 28
                    color: rowArea.containsMouse ? Theme.tabHover : "transparent"
                    opacity: modelData.exists ? 1 : 0.5

                    // Miniatura (o il riquadro scuro se il file non ne ha).
                    Rectangle {
                        id: thumbBox
                        visible: row.mode !== 2
                        x: 0; y: row.mode === 0 ? 2 : 4
                        width: row.mode === 0 ? 176 : 68
                        height: row.mode === 0 ? 112 : 44
                        color: "#11151a"
                        border.color: rowArea.containsMouse ? root.blue : "#545e6c"
                        gradient: row.thumb === "" ? emptyThumb : null
                        Gradient { id: emptyThumb; GradientStop { position: 0; color: "#2a2f37" } GradientStop { position: 1; color: "#0c0e11" } }
                        Image {
                            anchors.fill: parent; anchors.margins: 1
                            source: row.thumb
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true; mipmap: true
                        }
                        // Tipo di file in basso a sinistra.
                        Rectangle {
                            visible: row.mode === 0
                            x: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                            width: 26; height: 14; radius: 2
                            color: row.modelData.kind === "DWG" ? "#a8342a" : row.modelData.kind === "DWT" ? "#7a5a12" : "#2f5d94"
                            Text { anchors.centerIn: parent; text: row.modelData.kind; color: "white"; font.pixelSize: 8; font.bold: true }
                        }
                    }
                    // Etichetta del tipo nella vista a elenco.
                    Rectangle {
                        visible: row.mode === 2
                        x: 4; anchors.verticalCenter: parent.verticalCenter
                        width: 28; height: 16; radius: 2
                        color: row.modelData.kind === "DWG" ? "#a8342a" : row.modelData.kind === "DWT" ? "#7a5a12" : "#2f5d94"
                        Text { anchors.centerIn: parent; text: row.modelData.kind; color: "white"; font.pixelSize: 9; font.bold: true }
                    }

                    Column {
                        x: row.mode === 0 ? 190 : row.mode === 1 ? 80 : 42
                        width: row.width - x - 30
                        anchors.top: row.mode === 0 ? parent.top : undefined
                        anchors.topMargin: 2
                        anchors.verticalCenter: row.mode === 0 ? undefined : parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width
                            text: row.modelData.name
                            color: "white"; font.pixelSize: row.mode === 2 ? 13 : 15; font.bold: row.mode !== 2
                            wrapMode: row.mode === 0 ? Text.WrapAnywhere : Text.NoWrap
                            maximumLineCount: row.mode === 0 ? 3 : 1
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: row.mode !== 2
                            width: parent.width
                            text: row.modelData.exists ? row.modelData.when : qsTr("File non trovato: %1").arg(row.modelData.folder)
                            color: Theme.muted3; font.pixelSize: 12
                            wrapMode: row.mode === 0 ? Text.WordWrap : Text.NoWrap
                            elide: Text.ElideRight
                            maximumLineCount: 2
                        }
                    }
                    Text {
                        visible: row.mode === 2
                        anchors.right: parent.right; anchors.rightMargin: 30; anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.shortWhen; color: Theme.muted; font.pixelSize: 11
                    }

                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onEntered: { const p = mapToGlobal(mouseX + 12, mouseY + 18); ui.showToolTip(p.x, p.y, row.modelData.folder + "\\" + row.modelData.file) }
                        onExited: ui.hideToolTip()
                        onClicked: (mouse) => {
                            ui.hideToolTip();
                            if (mouse.button === Qt.RightButton) {
                                const p = mapToGlobal(mouse.x, mouse.y);
                                start.popupRecentMenu(row.modelData.path, p.x, p.y);
                            } else {
                                start.requestRecent(row.modelData.path);
                            }
                        }
                    }
                    // Puntina: blocca il file in cima all'elenco.
                    Item {
                        id: pin
                        visible: row.modelData.pinned || rowArea.containsMouse || pinArea.containsMouse
                        x: row.mode === 0 ? thumbBox.width - 24 : row.width - 26
                        y: row.mode === 0 ? 4 : (row.height - 22) / 2
                        width: 22; height: 22
                        Rectangle { anchors.fill: parent; radius: 3; color: pinArea.containsMouse ? "#80000000" : "transparent" }
                        VecIcon {
                            anchors.centerIn: parent; width: 18; height: 18
                            kind: row.modelData.pinned ? "pinOn" : "pin"
                            stroke: row.modelData.pinned ? "#ffd166" : "white"
                        }
                        MouseArea {
                            id: pinArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, row.modelData.pinned ? qsTr("Sblocca") : qsTr("Blocca in cima all'elenco")) }
                            onExited: ui.hideToolTip()
                            onClicked: { ui.hideToolTip(); start.togglePin(row.modelData.path) }
                        }
                    }
                }

                // Barra di scorrimento sottile.
                Rectangle {
                    visible: list.contentHeight > list.height
                    anchors.right: parent.right
                    y: list.visibleArea.yPosition * list.height
                    width: 5; radius: 2
                    height: Math.max(30, list.visibleArea.heightRatio * list.height)
                    color: "#7a8494"
                }
            }

            // Modo di vista dell'elenco.
            Row {
                id: viewBar
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                visible: start.recent.length > 0
                Repeater {
                    model: [{ kind: "viewLarge", tip: qsTr("Immagini grandi") }, { kind: "viewSmall", tip: qsTr("Immagini piccole") }, { kind: "viewList", tip: qsTr("Elenco") }]
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: 28; height: 26; radius: 2
                        color: start.viewMode === index ? "#55617a" : vArea.containsMouse ? Theme.hover : "transparent"
                        VecIcon { anchors.centerIn: parent; width: 18; height: 18; kind: modelData.kind; stroke: start.viewMode === index ? "white" : Theme.muted2 }
                        MouseArea {
                            id: vArea; anchors.fill: parent; hoverEnabled: true
                            onEntered: { const p = mapToGlobal(0, height + 2); ui.showToolTip(p.x, p.y, modelData.tip) }
                            onExited: ui.hideToolTip()
                            onClicked: start.viewMode = index
                        }
                    }
                }
            }
        }

        // --- Colonna Connetti
        Column {
            visible: createPage.showRight
            x: midCol.x + midCol.width + createPage.gap; y: createPage.topY
            width: createPage.rightW
            spacing: 12
            Text { text: qsTr("Connetti"); color: Theme.text; font.pixelSize: 21; font.bold: true }
            Rectangle {
                width: parent.width
                height: connectCol.height + 36
                color: root.cardLight
                border.color: "#aab1bb"
                Column {
                    id: connectCol
                    x: 18; y: 18; width: parent.width - 36
                    spacing: 6
                    Row {
                        spacing: 8
                        VecIcon { width: 20; height: 20; kind: "bug"; stroke: root.cardText }
                        Text { text: qsTr("Segnala un problema"); color: root.cardText; font.pixelSize: 15; font.bold: true }
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: qsTr("Un disegno che si vede male o un comando che manca? Scrivilo su GitHub, con il file se puoi.")
                        color: "#3c4450"; font.pixelSize: 13
                    }
                    Item { width: 1; height: 4 }
                    BlueButton { label: qsTr("Segnala..."); onClicked: start.openLink("https://github.com/SamuDiPi/CartesioCAD/issues") }
                    Item { width: 1; height: 18 }
                    Row {
                        spacing: 8
                        VecIcon { width: 20; height: 20; kind: "code"; stroke: root.cardText }
                        Text { text: qsTr("Progetto open source"); color: root.cardText; font.pixelSize: 15; font.bold: true }
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: qsTr("CartesioCAD %1 è software libero (GPL-3): codice, novità e versioni nuove.").arg(start.version)
                        color: "#3c4450"; font.pixelSize: 13
                    }
                    Item { width: 1; height: 4 }
                    BlueButton { label: qsTr("Vai al progetto"); onClicked: start.openLink("https://github.com/SamuDiPi/CartesioCAD") }
                }
            }
        }
    }

    // ---------------------------------------------------------------- APPRENDI
    Item {
        id: learnPage
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.bottom: bottomBar.top
        visible: start.page === 1

        Item {
            visible: createPage.wide
            anchors.right: parent.right; anchors.rightMargin: 22
            width: 90; height: 30
            anchors.verticalCenter: parent.verticalCenter
            Row {
                spacing: 12; anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right
                Text { text: qsTr("CREA"); color: createArea.containsMouse ? "white" : Theme.muted2; font.pixelSize: 13; font.bold: true }
                Text { text: "▶"; color: createArea.containsMouse ? "white" : Theme.muted2; font.pixelSize: 13 }
            }
            MouseArea { id: createArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: start.page = 0 }
        }

        Flickable {
            id: learnFlick
            anchors.fill: parent
            anchors.leftMargin: createPage.wide ? 140 : 28; anchors.rightMargin: createPage.wide ? 140 : 28
            anchors.topMargin: createPage.topY
            contentHeight: learnCol.height + 30
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: learnCol
                width: learnFlick.width
                spacing: 14
                Text { text: qsTr("Apprendi"); color: Theme.text; font.pixelSize: 21; font.bold: true }
                Text {
                    width: parent.width; wrapMode: Text.WordWrap
                    text: qsTr("I comandi si scrivono nella riga di comando, con i nomi inglesi (LINE, TRIM...) "
                               + "o con quelli italiani dove ci sono. Clicca un comando per provarlo: se non c'è un disegno aperto ne "
                               + "parte uno nuovo.")
                    color: Theme.muted3; font.pixelSize: 14
                }
                Flow {
                    width: parent.width
                    spacing: 16
                    Repeater {
                        model: [
                            { title: qsTr("Disegnare"), text: qsTr("Linee, polilinee, cerchi, archi, rettangoli e tratteggi."), cmds: ["LINE", "PLINE", "CIRCLE", "ARC", "RECTANG", "HATCH"] },
                            { title: qsTr("Modificare"), text: qsTr("Spostare, copiare, ruotare, tagliare, fare offset e raccordi."), cmds: ["MOVE", "COPY", "ROTATE", "TRIM", "OFFSET", "FILLET"] },
                            { title: qsTr("Annotare"), text: qsTr("Testi, quote, direttrici e tabelle."), cmds: ["TEXT", "MTEXT", "DIMLINEAR", "QDIM", "MLEADER", "TABLE"] },
                            { title: qsTr("Organizzare"), text: qsTr("Layer, blocchi, riferimenti esterni e ordine di visualizzazione."), cmds: ["LAYER", "BLOCK", "INSERT", "XREF", "DRAWORDER", "GROUP"] },
                            { title: qsTr("Stampare"), text: qsTr("Stampa in PDF o su stampante, impostazioni di pagina, tabelle stili CTB."), cmds: ["PLOT", "PAGESETUP", "PUBLISH", "STYLESMANAGER"] },
                            { title: qsTr("Cercare e selezionare"), text: qsTr("Trova e sostituisci testi, selezione rapida, informazioni sugli oggetti."), cmds: ["FIND", "QSELECT", "SELECTSIMILAR", "LIST"] }
                        ]
                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            readonly property int perRow: learnCol.width >= 3 * 260 + 32 ? 3 : learnCol.width >= 2 * 260 + 16 ? 2 : 1
                            width: Math.floor((learnCol.width - (perRow - 1) * 16) / perRow) - 1
                            height: cardCol.height + 30
                            color: Theme.bar
                            border.color: Theme.hoverBorder
                            Column {
                                id: cardCol
                                x: 16; y: 14; width: parent.width - 32
                                spacing: 6
                                Text { text: card.modelData.title; color: "white"; font.pixelSize: 16; font.bold: true }
                                Text { width: parent.width; wrapMode: Text.WordWrap; text: card.modelData.text; color: Theme.muted3; font.pixelSize: 13 }
                                Item { width: 1; height: 4 }
                                Flow {
                                    width: parent.width; spacing: 6
                                    Repeater {
                                        model: card.modelData.cmds
                                        delegate: Rectangle {
                                            required property string modelData
                                            width: cmdText.width + 16; height: 24; radius: 2
                                            color: cmdArea.containsMouse ? root.blue : "#3e4757"
                                            Text { id: cmdText; anchors.centerIn: parent; text: parent.modelData; color: "white"; font.pixelSize: 12; font.family: "Consolas" }
                                            MouseArea { id: cmdArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: start.runCommand(parent.modelData) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Item { width: 1; height: 6 }
                Row {
                    spacing: 16
                    Rectangle {
                        width: 220; height: 34; radius: 2
                        color: helpArea.containsMouse ? root.blueHover : root.blue
                        Text { anchors.centerIn: parent; text: qsTr("Guida dei comandi (AIUTO)"); color: "white"; font.pixelSize: 14; font.bold: true }
                        MouseArea { id: helpArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: start.runCommand("AIUTO") }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Suggerimento: F2 apre la cronologia dei comandi, Esc annulla il comando in corso.")
                        color: Theme.muted2; font.pixelSize: 13
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- schede in basso
    Rectangle {
        id: bottomBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 64
        color: "#2b3039"
        Rectangle { width: parent.width; height: 1; color: "#1c2028" }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            height: parent.height
            Repeater {
                model: [qsTr("APPRENDI"), qsTr("CREA")]
                delegate: Item {
                    required property string modelData
                    required property int index
                    readonly property bool active: (index === 0 && start.page === 1) || (index === 1 && start.page === 0)
                    width: 290; height: parent.height
                    Rectangle {
                        anchors.fill: parent
                        visible: parent.active
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#3a414d" }
                            GradientStop { position: 1; color: "#2b3039" }
                        }
                    }
                    // Tacca che indica la scheda attiva.
                    Canvas {
                        visible: parent.active
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: -1; width: 18; height: 9
                        onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = Theme.window; c.beginPath(); c.moveTo(0, 0); c.lineTo(18, 0); c.lineTo(9, 9); c.closePath(); c.fill() }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: parent.active || tabArea.containsMouse ? "white" : Theme.muted2
                        font.pixelSize: 20; font.bold: true
                    }
                    MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: start.page = index === 0 ? 1 : 0 }
                }
            }
        }
    }
}
