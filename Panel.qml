import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import dropterm

Item {
    id: root

    property real widthPercent: @WIDTH_PERCENT@
    property real heightPercent: @HEIGHT_PERCENT@
    property string termFontFamily: "@FONT_FAMILY@"
    property real termFontSize: @FONT_SIZE@

    property var pluginApi: null
    property var screen
    readonly property var geometryPlaceholder: panelContainer
    property real contentPreferredWidth: Screen.width * Math.max(0.2, widthPercent)
    property real contentPreferredHeight: Screen.height * Math.max(0.15, heightPercent)
    readonly property bool allowAttach: true
    anchors.fill: parent

    onPluginApiChanged: if (pluginApi) loadSettings()

    function loadSettings() {
        if (!pluginApi || !pluginApi.pluginSettings) return
        var s = pluginApi.pluginSettings
        if (s.widthPercent !== undefined) widthPercent = s.widthPercent
        if (s.heightPercent !== undefined) heightPercent = s.heightPercent
        if (s.fontFamily !== undefined) termFontFamily = s.fontFamily
        if (s.fontSize !== undefined) termFontSize = s.fontSize
    }

    function saveSettings() {
        if (!pluginApi) return
        pluginApi.pluginSettings.widthPercent = widthPercent
        pluginApi.pluginSettings.heightPercent = heightPercent
        pluginApi.pluginSettings.fontFamily = termFontFamily
        pluginApi.pluginSettings.fontSize = termFontSize
        pluginApi.saveSettings()
    }

    function closeSettings() {
        settingsPopup.visible = false
        textrender.forceActiveFocus()
    }

    onWidthPercentChanged: saveSettings()
    onHeightPercentChanged: saveSettings()
    onTermFontFamilyChanged: saveSettings()
    onTermFontSizeChanged: saveSettings()

    Rectangle {
        id: panelContainer
        anchors.fill: parent
        color: "transparent"

        TextRender {
            id: textrender
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: scrollBar.left
            anchors.bottom: tabBar.top
            anchors.leftMargin: 6
            anchors.rightMargin: 2
            focus: true
            property int cutAfter: height
            dragMode: TextRender.DragSelect

            font.family: root.termFontFamily
            font.pointSize: root.termFontSize

            contentItem: Item {
                anchors.fill: parent
            }

            cellDelegate: Rectangle {
                color: "transparent"
            }

            cellContentsDelegate: Text {
                property bool blinking: false
                textFormat: Text.PlainText
                opacity: blinking ? 0.5 : 1.0
            }

            cursorDelegate: Rectangle {
                id: cursor
                opacity: 0.7
                SequentialAnimation {
                    running: true
                    loops: Animation.Infinite
                    NumberAnimation {
                        target: cursor
                        property: "opacity"
                        to: 1.0
                        duration: 400
                    }
                    PauseAnimation { duration: 400 }
                    NumberAnimation {
                        target: cursor
                        property: "opacity"
                        to: 0.3
                        duration: 400
                    }
                    PauseAnimation { duration: 200 }
                }
            }

            selectionDelegate: Rectangle {
                color: "#88c0d0"
                opacity: 0.4
            }

            Component.onCompleted: {
                textrender.forceActiveFocus();
            }
        }

        // Scrollbar
        Rectangle {
            id: scrollBar
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: tabBar.top
            width: scrollBar.hasScrollableContent ? 12 : 0
            color: "transparent"

            property bool hasScrollableContent: scrollBar.scrollRange > 0

            Behavior on width { NumberAnimation { duration: 150 } }

            property real totalHeight: textrender.contentHeight
            property real viewHeight: textrender.visibleHeight
            property real scrollPos: textrender.contentY
            property real scrollRange: totalHeight - viewHeight

            Rectangle {
                id: scrollThumb
                anchors.horizontalCenter: parent.horizontalCenter
                width: 6
                radius: 3
                color: scrollDrag.pressed ? Qt.rgba(1, 1, 1, 0.6)
                     : scrollDrag.containsMouse ? Qt.rgba(1, 1, 1, 0.45)
                     : Qt.rgba(1, 1, 1, 0.3)
                visible: scrollBar.hasScrollableContent

                property real thumbRatio: scrollBar.viewHeight > 0 && scrollBar.totalHeight > 0
                    ? Math.min(1.0, scrollBar.viewHeight / scrollBar.totalHeight) : 1.0
                property real posRatio: scrollBar.scrollRange > 0
                    ? scrollBar.scrollPos / scrollBar.scrollRange : 0

                height: Math.max(24, scrollBar.height * thumbRatio)
                y: (scrollBar.height - height) * posRatio
            }

            MouseArea {
                id: scrollDrag
                anchors.fill: parent
                hoverEnabled: true
                property real dragStartY: 0
                property real dragStartRatio: 0

                function scrollToRatio(ratio) {
                    ratio = Math.max(0, Math.min(1, ratio));
                    var targetPos = Math.round(ratio * scrollBar.scrollRange);
                    textrender.setScrollPosition(targetPos);
                }

                onPressed: (mouse) => {
                    if (mouse.y >= scrollThumb.y && mouse.y <= scrollThumb.y + scrollThumb.height) {
                        dragStartY = mouse.y;
                        dragStartRatio = scrollThumb.posRatio;
                    } else {
                        var ratio = mouse.y / scrollBar.height;
                        scrollToRatio(ratio);
                        dragStartY = mouse.y;
                        dragStartRatio = ratio;
                    }
                }

                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    var trackRange = scrollBar.height - scrollThumb.height;
                    if (trackRange <= 0) return;
                    var dy = mouse.y - dragStartY;
                    var ratioChange = dy / trackRange;
                    scrollToRatio(dragStartRatio + ratioChange);
                }

                onReleased: {
                    textrender.forceActiveFocus();
                }
            }
        }

        // Backdrop to close settings when clicking outside
        MouseArea {
            anchors.fill: parent
            visible: settingsPopup.visible
            z: 10
            onClicked: root.closeSettings()
        }

        // Settings popup
        Rectangle {
            id: settingsPopup
            visible: false
            z: 11
            anchors.right: parent.right
            anchors.bottom: tabBar.top
            anchors.rightMargin: 8
            anchors.bottomMargin: 4
            width: 290
            property real idealHeight: settingsContent.implicitHeight + 24
            property real maxHeight: panelContainer.height - tabBar.height - 16
            height: Math.min(idealHeight, maxHeight)
            radius: 8
            color: Qt.rgba(0.1, 0.1, 0.12, 0.95)
            border.color: Qt.rgba(1, 1, 1, 0.1)
            border.width: 1
            clip: true

            Flickable {
                id: settingsFlick
                anchors.fill: parent
                anchors.margins: 12
                contentHeight: settingsContent.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

            Column {
                id: settingsContent
                width: settingsFlick.width
                spacing: 8

                // Header
                RowLayout {
                    width: parent.width
                    Text {
                        text: "Settings"
                        color: "#ffffff"
                        font.pixelSize: 13
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        width: 18; height: 18; radius: 9
                        color: closeBtnArea.containsMouse ? Qt.rgba(1,1,1,0.15) : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "\u2715"
                            color: "#888888"
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: closeBtnArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.closeSettings()
                        }
                    }
                }

                // Width
                RowLayout {
                    width: parent.width; spacing: 8
                    Text { text: "Width"; color: "#aaaaaa"; font.pixelSize: 11; Layout.preferredWidth: 55 }
                    Rectangle {
                        Layout.fillWidth: true; height: 24; radius: 4
                        color: Qt.rgba(1, 1, 1, 0.06)
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 2; spacing: 0
                            Rectangle {
                                Layout.preferredWidth: 24; Layout.fillHeight: true; radius: 3
                                color: wMinusArea.containsMouse ? Qt.rgba(1,1,1,0.12) : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25C2"; color: "#aaaaaa"; font.pixelSize: 10 }
                                MouseArea { id: wMinusArea; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.widthPercent = Math.max(0.20, Math.round((root.widthPercent - 0.01) * 100) / 100) }
                            }
                            Text {
                                text: Math.round(root.widthPercent * 100) + "%"
                                color: "#ffffff"; font.pixelSize: 11
                                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                            }
                            Rectangle {
                                Layout.preferredWidth: 24; Layout.fillHeight: true; radius: 3
                                color: wPlusArea.containsMouse ? Qt.rgba(1,1,1,0.12) : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25B8"; color: "#aaaaaa"; font.pixelSize: 10 }
                                MouseArea { id: wPlusArea; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.widthPercent = Math.min(1.0, Math.round((root.widthPercent + 0.01) * 100) / 100) }
                            }
                        }
                    }
                }

                // Height
                RowLayout {
                    width: parent.width; spacing: 8
                    Text { text: "Height"; color: "#aaaaaa"; font.pixelSize: 11; Layout.preferredWidth: 55 }
                    Rectangle {
                        Layout.fillWidth: true; height: 24; radius: 4
                        color: Qt.rgba(1, 1, 1, 0.06)
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 2; spacing: 0
                            Rectangle {
                                Layout.preferredWidth: 24; Layout.fillHeight: true; radius: 3
                                color: hMinusArea.containsMouse ? Qt.rgba(1,1,1,0.12) : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25C2"; color: "#aaaaaa"; font.pixelSize: 10 }
                                MouseArea { id: hMinusArea; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.heightPercent = Math.max(0.15, Math.round((root.heightPercent - 0.01) * 100) / 100) }
                            }
                            Text {
                                text: Math.round(root.heightPercent * 100) + "%"
                                color: "#ffffff"; font.pixelSize: 11
                                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                            }
                            Rectangle {
                                Layout.preferredWidth: 24; Layout.fillHeight: true; radius: 3
                                color: hPlusArea.containsMouse ? Qt.rgba(1,1,1,0.12) : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25B8"; color: "#aaaaaa"; font.pixelSize: 10 }
                                MouseArea { id: hPlusArea; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.heightPercent = Math.min(0.9, Math.round((root.heightPercent + 0.01) * 100) / 100) }
                            }
                        }
                    }
                }

                // Font family
                RowLayout {
                    width: parent.width; spacing: 8
                    Text { text: "Font"; color: "#aaaaaa"; font.pixelSize: 11; Layout.preferredWidth: 55 }
                    Rectangle {
                        Layout.fillWidth: true; height: 24; radius: 4
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.color: fontInput.activeFocus ? Qt.rgba(1,1,1,0.3) : "transparent"
                        border.width: 1
                        TextInput {
                            id: fontInput
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: TextInput.AlignVCenter
                            color: "#ffffff"
                            font.pixelSize: 11
                            selectByMouse: true
                            selectionColor: "#88c0d0"
                            text: root.termFontFamily
                            onTextEdited: root.termFontFamily = text
                        }
                    }
                }

                // Font size
                RowLayout {
                    width: parent.width; spacing: 8
                    Text { text: "Size"; color: "#aaaaaa"; font.pixelSize: 11; Layout.preferredWidth: 55 }
                    Rectangle {
                        Layout.fillWidth: true; height: 24; radius: 4
                        color: Qt.rgba(1, 1, 1, 0.06)
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 2; spacing: 0
                            Rectangle {
                                Layout.preferredWidth: 24; Layout.fillHeight: true; radius: 3
                                color: sMinusArea.containsMouse ? Qt.rgba(1,1,1,0.12) : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25C2"; color: "#aaaaaa"; font.pixelSize: 10 }
                                MouseArea { id: sMinusArea; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.termFontSize = Math.max(6, Math.round((root.termFontSize - 0.5) * 10) / 10) }
                            }
                            Text {
                                text: root.termFontSize.toFixed(1)
                                color: "#ffffff"; font.pixelSize: 11
                                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                            }
                            Rectangle {
                                Layout.preferredWidth: 24; Layout.fillHeight: true; radius: 3
                                color: sPlusArea.containsMouse ? Qt.rgba(1,1,1,0.12) : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25B8"; color: "#aaaaaa"; font.pixelSize: 10 }
                                MouseArea { id: sPlusArea; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.termFontSize = Math.min(24, Math.round((root.termFontSize + 0.5) * 10) / 10) }
                            }
                        }
                    }
                }

                // Divider
                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

                // Shortcuts header
                Text { text: "Shortcuts"; color: "#ffffff"; font.pixelSize: 13; font.bold: true }

                // Shortcuts grid
                Grid {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 5
                    width: parent.width

                    Text { text: "Ctrl+Shift+C"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "Copy"; color: "#aaaaaa"; font.pixelSize: 10 }
                    Text { text: "Ctrl+Shift+V"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "Paste"; color: "#aaaaaa"; font.pixelSize: 10 }
                    Text { text: "Shift+Insert"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "Paste"; color: "#aaaaaa"; font.pixelSize: 10 }
                    Text { text: "Ctrl+Shift+T"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "New tab"; color: "#aaaaaa"; font.pixelSize: 10 }
                    Text { text: "Ctrl+Shift+W"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "Close tab"; color: "#aaaaaa"; font.pixelSize: 10 }
                    Text { text: "Ctrl+Tab"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "Next tab"; color: "#aaaaaa"; font.pixelSize: 10 }
                    Text { text: "Ctrl+Shift+Tab"; color: "#88c0d0"; font.pixelSize: 10; font.family: "Hack" }
                    Text { text: "Previous tab"; color: "#aaaaaa"; font.pixelSize: 10 }
                }
            }
            }
        }

        // Tab bar at the bottom
        Item {
            id: tabBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            height: 28

            Flickable {
                id: tabFlick
                anchors.left: parent.left
                anchors.right: settingsBtn.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: 10
                anchors.rightMargin: 4
                contentWidth: tabRow.width
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                WheelHandler {
                    orientation: Qt.Vertical
                    onWheel: (event) => {
                        tabFlick.contentX = Math.max(0,
                            Math.min(tabFlick.contentWidth - tabFlick.width,
                                tabFlick.contentX - event.angleDelta.y));
                    }
                }

                function ensureVisible(idx) {
                    var x = 0;
                    for (var i = 0; i < idx; i++)
                        x += tabRow.children[i].width + tabRow.spacing;
                    var tabW = tabRow.children[idx] ? tabRow.children[idx].width : 0;
                    if (x < contentX)
                        contentX = x;
                    else if (x + tabW > contentX + width)
                        contentX = x + tabW - width;
                }

                Row {
                    id: tabRow
                    spacing: 2
                    height: parent.height

                    Repeater {
                        id: tabRepeater
                        model: textrender.sessionCount

                        Rectangle {
                            required property int index
                            height: 22
                            width: tabLabel.implicitWidth + closeBtn.width + 20
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 4
                            color: index === textrender.activeSession
                                   ? Qt.rgba(1, 1, 1, 0.15)
                                   : Qt.rgba(1, 1, 1, 0.05)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 4
                                spacing: 4

                                Text {
                                    id: tabLabel
                                    text: textrender.sessionTitle(index)
                                    color: index === textrender.activeSession ? "#ffffff" : "#aaaaaa"
                                    font.pixelSize: 11
                                    font.family: "Hack"
                                    Layout.fillWidth: true
                                }

                                Text {
                                    id: closeBtn
                                    text: "x"
                                    color: "#888888"
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 12
                                    visible: textrender.sessionCount > 1

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: textrender.closeSession(index)
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.rightMargin: closeBtn.width + 4
                                onClicked: {
                                    textrender.activeSession = index;
                                    textrender.forceActiveFocus();
                                }
                            }
                        }
                    }

                    // "+" button
                    Rectangle {
                        height: 22
                        width: 28
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 4
                        color: Qt.rgba(1, 1, 1, 0.05)

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: "#aaaaaa"
                            font.pixelSize: 14
                            font.family: "Hack"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                textrender.newSession();
                                textrender.forceActiveFocus();
                            }
                        }
                    }
                }
            }

            // Settings gear button
            Rectangle {
                id: settingsBtn
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                radius: 4
                color: settingsBtnArea.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.05)

                Text {
                    anchors.centerIn: parent
                    text: "\u2699"
                    color: settingsPopup.visible ? "#ffffff" : "#aaaaaa"
                    font.pixelSize: 13
                }

                MouseArea {
                    id: settingsBtnArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (settingsPopup.visible)
                            root.closeSettings()
                        else
                            settingsPopup.visible = true
                    }
                }
            }

            // Scroll active tab into view when it changes
            Connections {
                target: textrender
                function onActiveSessionChanged() {
                    tabFlick.ensureVisible(textrender.activeSession);
                }
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            textrender.forceActiveFocus();
        }
    }
}
