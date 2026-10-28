import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    margins: 12
    padding: 6
    horizontalPadding: 10

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: control.font
        color: "#ecf0f1"
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        color: "#1a1d21"
        border.color: "#3c4146"
        border.width: 1
        radius: 4
    }
}
