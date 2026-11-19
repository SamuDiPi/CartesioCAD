// Gruppo con titolo (come un QGroupBox).
import QtQuick
import CartesioCAD

Rectangle {
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
