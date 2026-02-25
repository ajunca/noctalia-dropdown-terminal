import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import qs.Commons
import qs.Widgets

Item {
    id: rootItem
    implicitWidth: 400
    implicitHeight: root.implicitHeight
    width: Math.max(implicitWidth, parent ? parent.width : 0)

    property var pluginApi: null

    ColumnLayout {
        id: root
        width: parent.width
        spacing: Style.marginM

        property var cfg: rootItem.pluginApi?.pluginSettings || ({})
        property var defaults: rootItem.pluginApi?.manifest?.metadata?.defaultSettings || ({})

        property real valueWidthPercent: cfg.widthPercent ?? defaults.widthPercent ?? 0.6
        property real valueHeightPercent: cfg.heightPercent ?? defaults.heightPercent ?? 0.3
        property string valueFontFamily: cfg.fontFamily ?? defaults.fontFamily ?? "Hack"
        property real valueFontSize: cfg.fontSize ?? defaults.fontSize ?? 10.5

        function save() {
            if (!rootItem.pluginApi) return
            rootItem.pluginApi.pluginSettings.widthPercent = valueWidthPercent
            rootItem.pluginApi.pluginSettings.heightPercent = valueHeightPercent
            rootItem.pluginApi.pluginSettings.fontFamily = valueFontFamily
            rootItem.pluginApi.pluginSettings.fontSize = valueFontSize
            rootItem.pluginApi.saveSettings()
        }

        // Header
        NText {
            Layout.fillWidth: true
            text: "Dropdown Terminal"
            font.pixelSize: Style.fontSizeXL
            font.bold: true
        }

        // Panel size section
        NText {
            text: "Panel Size"
            font.pixelSize: Style.fontSizeL
            opacity: 0.7
        }

        // Width
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginM

            NText { text: "Width"; Layout.preferredWidth: 60 }

            Slider {
                id: widthSlider
                Layout.fillWidth: true
                from: 0.2; to: 1.0; stepSize: 0.01
                value: root.valueWidthPercent
                onMoved: {
                    root.valueWidthPercent = Math.round(value * 100) / 100
                    root.save()
                }
            }

            NText {
                text: Math.round(root.valueWidthPercent * 100) + "%"
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        // Height
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginM

            NText { text: "Height"; Layout.preferredWidth: 60 }

            Slider {
                id: heightSlider
                Layout.fillWidth: true
                from: 0.15; to: 0.9; stepSize: 0.01
                value: root.valueHeightPercent
                onMoved: {
                    root.valueHeightPercent = Math.round(value * 100) / 100
                    root.save()
                }
            }

            NText {
                text: Math.round(root.valueHeightPercent * 100) + "%"
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        // Font section
        NText {
            text: "Font"
            font.pixelSize: Style.fontSizeL
            opacity: 0.7
            Layout.topMargin: Style.marginS
        }

        // Font family
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginM

            NText { text: "Family"; Layout.preferredWidth: 60 }

            TextField {
                Layout.fillWidth: true
                text: root.valueFontFamily
                font.pixelSize: Style.fontSizeL
                onEditingFinished: {
                    root.valueFontFamily = text
                    root.save()
                }
            }
        }

        // Font size
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginM

            NText { text: "Size"; Layout.preferredWidth: 60 }

            Slider {
                id: sizeSlider
                Layout.fillWidth: true
                from: 6; to: 24; stepSize: 0.5
                value: root.valueFontSize
                onMoved: {
                    root.valueFontSize = Math.round(value * 10) / 10
                    root.save()
                }
            }

            NText {
                text: root.valueFontSize.toFixed(1)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        // Shortcuts section
        NText {
            text: "Keyboard Shortcuts"
            font.pixelSize: Style.fontSizeL
            opacity: 0.7
            Layout.topMargin: Style.marginS
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Style.marginL
            rowSpacing: Style.marginS

            NText { text: "Ctrl+Shift+C"; font.family: "monospace"; opacity: 0.8 }
            NText { text: "Copy" }
            NText { text: "Ctrl+Shift+V"; font.family: "monospace"; opacity: 0.8 }
            NText { text: "Paste" }
            NText { text: "Ctrl+Shift+T"; font.family: "monospace"; opacity: 0.8 }
            NText { text: "New tab" }
            NText { text: "Ctrl+Shift+W"; font.family: "monospace"; opacity: 0.8 }
            NText { text: "Close tab" }
            NText { text: "Ctrl+Tab"; font.family: "monospace"; opacity: 0.8 }
            NText { text: "Next tab" }
            NText { text: "Ctrl+Shift+Tab"; font.family: "monospace"; opacity: 0.8 }
            NText { text: "Previous tab" }
        }
    }
}
