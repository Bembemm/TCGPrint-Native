import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    visible: true

    width: 1280
    height: 800

    minimumWidth: 900
    minimumHeight: 600

    title: "TCGPrint Native — N1"

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter

            text: "TCGPrint Native"

            font.pixelSize: 32
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter

            text: "C++23 + Qt Quick"

            font.pixelSize: 18
        }

        Label {
            Layout.alignment: Qt.AlignHCenter

            text: "N1 · Native Foundation"

            opacity: 0.65
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter

            width: 420
            height: 1

            color: palette.mid
        }

        Label {
            Layout.alignment: Qt.AlignHCenter

            text: "Compositor GPU · Artwork · PDF · Projects"

            opacity: 0.75
        }

        Label {
            Layout.alignment: Qt.AlignHCenter

            text: "Plotter integration deferred"

            opacity: 0.5
        }
    }
}
