/*
    Settings window — an ordinary window, run as `dropterm settings`.

    Separate from the terminal process on purpose: that one sets
    QT_WAYLAND_SHELL_INTEGRATION=layer-shell, and LayerShellQt makes every
    window in a process a layer surface with no fallback, so a settings window
    there could never be decorated, moved or tiled.

    Controls write straight through to Settings, which persists on assignment.
    A running terminal watches the file and picks changes up live.

    Copyright 2026 ajunca — MIT License
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    // Supplied by main.cpp.
    property QtObject settings: null

    color: palette.window

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 10

            ColumnLayout {
                Layout.margins: 16
                Layout.fillWidth: true
                spacing: 12

                Label {
                    text: qsTr("Size")
                    font.bold: true
                }

                GridLayout {
                    columns: 3
                    columnSpacing: 8
                    rowSpacing: 10
                    Layout.fillWidth: true

                    Label { text: qsTr("Width") }
                    Slider {
                        id: widthSlider
                        Layout.fillWidth: true
                        from: 0.2; to: 1.0
                        value: root.settings.widthPercent
                        onMoved: root.settings.widthPercent = value
                    }
                    Label {
                        text: Math.round(widthSlider.value * 100) + "%"
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }

                    Label { text: qsTr("Height") }
                    Slider {
                        id: heightSlider
                        Layout.fillWidth: true
                        from: 0.15; to: 1.0
                        value: root.settings.heightPercent
                        onMoved: root.settings.heightPercent = value
                    }
                    Label {
                        text: Math.round(heightSlider.value * 100) + "%"
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Label {
                    text: qsTr("Appearance")
                    font.bold: true
                    Layout.topMargin: 6
                }

                GridLayout {
                    columns: 3
                    columnSpacing: 8
                    rowSpacing: 10
                    Layout.fillWidth: true

                    Label { text: qsTr("Font") }
                    TextField {
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        text: root.settings.fontFamily
                        placeholderText: qsTr("Monospace family, e.g. Hack")
                        onEditingFinished: root.settings.fontFamily = text
                    }

                    Label { text: qsTr("Font size") }
                    Slider {
                        id: sizeSlider
                        Layout.fillWidth: true
                        from: 6; to: 24; stepSize: 0.5
                        value: root.settings.fontSize
                        onMoved: root.settings.fontSize = value
                    }
                    Label {
                        text: sizeSlider.value.toFixed(1)
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }

                    Label { text: qsTr("Opacity") }
                    Slider {
                        id: opacitySlider
                        Layout.fillWidth: true
                        from: 0.0; to: 1.0
                        value: root.settings.backgroundOpacity
                        onMoved: root.settings.backgroundOpacity = value
                    }
                    Label {
                        text: Math.round(opacitySlider.value * 100) + "%"
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }

                    Label { text: qsTr("Corners") }
                    Slider {
                        id: radiusSlider
                        Layout.fillWidth: true
                        from: 0; to: 32; stepSize: 1
                        value: root.settings.cornerRadius
                        onMoved: root.settings.cornerRadius = value
                    }
                    Label {
                        text: Math.round(radiusSlider.value) + "px"
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }

                    Label { text: qsTr("Animation") }
                    Slider {
                        id: animSlider
                        Layout.fillWidth: true
                        from: 0; to: 600; stepSize: 10
                        value: root.settings.animationMs
                        onMoved: root.settings.animationMs = value
                    }
                    Label {
                        text: Math.round(animSlider.value) + "ms"
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Label {
                    text: qsTr("Shell")
                    font.bold: true
                    Layout.topMargin: 6
                }

                TextField {
                    Layout.fillWidth: true
                    text: root.settings.shellProgram
                    placeholderText: qsTr("Leave empty to use your login shell")
                    onEditingFinished: root.settings.shellProgram = text
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    wrapMode: Text.WordWrap
                    color: palette.placeholderText
                    font.pixelSize: 11
                    text: qsTr("Saved to ~/.config/dropterm/dropterm.conf. Changes reach a "
                             + "running terminal immediately; the shell applies to new tabs.")
                }
            }
        }
    }
}
