import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects

Window {
    id: root
    width: 800
    height: 480
    visible: true

    property int cameraHeight: root.height * 2 / 3
    property int cameraMaxWidth: cameraHeight* 4 / 3
    property int cameraPlaceHolderWidth: cameraMaxWidth

    Behavior on cameraPlaceHolderWidth {
        NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0

        Canvas {
            id: rpmCanvas
            Layout.preferredWidth: 300 - root.cameraPlaceHolderWidth * (300.0/800)
            Layout.fillHeight: true

            property int rpm: dashboard.rpm

            // SequentialAnimation on rpm {
            //     loops: Animation.Infinite
            //     PropertyAnimation { to: 7000; duration: 4000 }
            //     PropertyAnimation { to: 0;    duration: 1000 }
            // }
            onRpmChanged: requestPaint()

            Text {
                anchors.centerIn: parent
                text: parent.rpm + " rpm"
                font.family: "Courier New"
                color: "black"
                font.pixelSize: 18
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var cx = width / 2
                var cy = height / 2
                // var radius = 80 * (cameraCanvas.visible ? 0.75:1)
                var radius = 80 * (1-(root.cameraPlaceHolderWidth/root.cameraMaxWidth)*0.25)

                var startAngle = 135 * Math.PI / 180
                var sweepRads  = 270 * Math.PI / 180
                var fraction   = rpm / 7000
                var needleEnd  = startAngle + fraction * sweepRads

                ctx.beginPath()
                ctx.arc(cx, cy, radius, startAngle, startAngle + sweepRads, false)
                ctx.strokeStyle = "#3A3A5C"
                ctx.lineWidth = 16
                ctx.stroke()

                ctx.beginPath()
                ctx.arc(cx, cy, radius, startAngle, needleEnd, false)
                ctx.strokeStyle = "#89B4FA"
                ctx.lineWidth = 16
                ctx.stroke()
            }
        }

        Canvas {
            id: mymainCanvas
            Layout.fillWidth: true
            Layout.fillHeight: true

            property real speed: dashboard.speed

            // SequentialAnimation on speed {
            //     loops: Animation.Infinite
            //     PropertyAnimation { to: 200; duration: 4000 }
            //     PropertyAnimation { to: 0;   duration: 1000 }
            // }
            onSpeedChanged: requestPaint()

            Text {
                anchors.centerIn: parent
                text: Math.floor(parent.speed) + " Km/h"
                font.family: "Courier New"
                color: "black"
                font.pixelSize: 18
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var cx = width / 2
                var cy = height / 2
                // var radius = 120 * (cameraCanvas.visible ? 0.75:1)
                var radius = 120 * (1-(root.cameraPlaceHolderWidth/root.cameraMaxWidth)*0.25)

                var startAngle = 135 * Math.PI / 180
                var sweepRads  = 270 * Math.PI / 180
                var fraction   = speed / 240
                var needleEnd  = startAngle + fraction * sweepRads

                ctx.beginPath()
                ctx.arc(cx, cy, radius, startAngle, startAngle + sweepRads, false)
                ctx.strokeStyle = "#3A3A5C"
                ctx.lineWidth = 16
                ctx.stroke()

                ctx.beginPath()
                ctx.arc(cx, cy, radius, startAngle, needleEnd, false)
                ctx.strokeStyle = "#89B4FA"
                ctx.lineWidth = 16
                ctx.stroke()
            }
        }

        Rectangle {
            id: cameraCanvas
            layer.enabled: true    // add this
            Layout.preferredWidth: root.cameraPlaceHolderWidth
            Layout.preferredHeight: root.cameraHeight
            color: "#0A0A1A"
            radius:12
            visible: root.cameraPlaceHolderWidth > 0

            Image {
                anchors.fill: parent
                source: dashboard.cameraFrameSequence > 0
                        ? "image://rpicamera/frame/" + dashboard.cameraFrameSequence
                        : ""
                fillMode: Image.PreserveAspectCrop
                cache: false
                visible: dashboard.cameraFrameSequence > 0
            }

            Text {
                anchors.centerIn: parent
                text: "CAM"
                color: "white"
                font.pixelSize: 24
                font.family: "Courier New"
                visible: dashboard.cameraFrameSequence === 0
            }

            // blinking FrameAnimation
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                // "#F38BA8"  // red
                // "#F9E2AF"  // yellow
                // "#A6E3A1"  // green
                // "#89B4FA"  // blue
                border.color: "#A6E3A1"
                border.width: 3
                radius: parent.radius

                SequentialAnimation on border.color {

                    loops: Animation.Infinite
                    ColorAnimation { to: "#A6E3A1"; duration: 800 }
                    ColorAnimation { to: "transparent"; duration: 800 }
                }
            }
        }
    }
    // MultiEffect {
    //     autoPaddingEnabled: false
    //     property int margin : 60
    //     x: cameraCanvas.x + cameraCanvas.parent.x - margin
    //     y: (root.height - root.cameraHeight) / 2 - margin
    //     width: root.cameraPlaceHolderWidth + margin *2
    //     height: root.cameraHeight + margin *2
    //     source: cameraCanvas
    //     shadowEnabled: true
    //     shadowBlur: 0.8
    //     shadowColor: "#CC000000"
    //     shadowHorizontalOffset: -30
    //     shadowVerticalOffset: 0
    //     shadowScale: 1
    //     visible: root.cameraPlaceHolderWidth > 0
    // }

    Item {
        focus: true
        Keys.onReturnPressed: (event) => {
            root.cameraPlaceHolderWidth = root.cameraPlaceHolderWidth > 0 ? 0 : root.cameraMaxWidth
            event.accepted = true
        }
    }
    Item {
        focus: true
        Keys.onEscapePressed: Qt.quit()
    }

}
