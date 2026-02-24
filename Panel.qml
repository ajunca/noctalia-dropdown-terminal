import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import dropterm

Item {
    id: root

    property var pluginApi: null
    property var screen
    readonly property var geometryPlaceholder: panelContainer
    property real contentPreferredWidth: Screen.width * @WIDTH_PERCENT@
    property real contentPreferredHeight: Screen.height * @HEIGHT_PERCENT@
    readonly property bool allowAttach: true
    anchors.fill: parent

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

            font.family: "@FONT_FAMILY@"
            font.pointSize: @FONT_SIZE@

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
                        // Click on track — jump to position
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
                anchors.fill: parent
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

                // Scroll active tab into view
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
