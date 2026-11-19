// Anteprima di stampa (modello: PreviewDialog): il foglio con zoom (rotellina e pulsanti) e trascinamento.
import QtQuick
import CartesioCAD

Rectangle {
    id: root
    color: "#2a2f37"
    focus: true
    Keys.onEscapePressed: preview.close()

    property real zoom: 1
    property bool userZoom: false   // finché non si zooma a mano, l'anteprima si adatta alla finestra
    function fit() {
        if (flick.width <= 40 || flick.height <= 40 || preview.imageWidth <= 0)
            return
        zoom = Math.min((flick.width - 40) / preview.imageWidth, (flick.height - 40) / preview.imageHeight)
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
    }
    function zoomAt(factor, px, py) {
        userZoom = true
        // Il punto sotto il mouse resta fermo.
        const old = zoom
        const z = Math.max(0.05, Math.min(8, zoom * factor))
        const cx = flick.contentX + px, cy = flick.contentY + py
        zoom = z
        flick.contentX = Math.max(0, cx * z / old - px)
        flick.contentY = Math.max(0, cy * z / old - py)
    }

    Row {
        id: bar
        x: 10; y: 8
        spacing: 8
        UiPushButton { label: qsTr("Stampa"); primary: true; onClicked: preview.plot() }
        UiPushButton { label: qsTr("Adatta"); onClicked: { root.userZoom = false; root.fit() } }
        UiPushButton { label: "+"; implicitWidth: 34; onClicked: root.zoomAt(1.25, flick.width / 2, flick.height / 2) }
        UiPushButton { label: "−"; implicitWidth: 34; onClicked: root.zoomAt(0.8, flick.width / 2, flick.height / 2) }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(root.zoom * 100) + " %"
            color: Theme.muted2
            font.pixelSize: 12
        }
    }
    UiPushButton {
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: 8
        label: qsTr("Chiudi anteprima")
        onClicked: preview.close()
    }

    Flickable {
        id: flick
        anchors.top: bar.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: Math.max(width, sheet.width + 40)
        contentHeight: Math.max(height, sheet.height + 40)
        boundsBehavior: Flickable.StopAtBounds
        interactive: true

        Rectangle {   // ombra
            x: sheet.x + 6; y: sheet.y + 6
            width: sheet.width; height: sheet.height
            color: "#5a000000"
        }
        Image {
            id: sheet
            source: preview.source
            width: preview.imageWidth * root.zoom
            height: preview.imageHeight * root.zoom
            x: (flick.contentWidth - width) / 2
            y: (flick.contentHeight - height) / 2
            smooth: true
            mipmap: true
            cache: false
            Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#5a5a5a" }
        }
        WheelHandler {
            onWheel: function(event) {
                const p = flick.mapFromItem(null, event.x, event.y)
                root.zoomAt(event.angleDelta.y > 0 ? 1.15 : 1 / 1.15, event.x, event.y)
            }
        }
    }
    Component.onCompleted: Qt.callLater(fit)
    Connections {
        target: flick
        function onWidthChanged() { if (!root.userZoom) Qt.callLater(root.fit) }
        function onHeightChanged() { if (!root.userZoom) Qt.callLater(root.fit) }
    }
}
