import QtQuick
import QtQuick.Controls

import Audacity.UiThemes

Slider {
   id: root
   value: 0.5

   background: Rectangle {
      x: root.height / 2
      y: root.availableHeight / 2 - height / 2
      width: root.availableWidth - handleId.width
      height: 4
      radius: 2
      color: UiTheme.buttonColor

      Rectangle {
         width: root.visualPosition * parent.width
         height: parent.height
         color: UiTheme.accentColor
         radius: 2
      }
   }

   handle: Rectangle {
      id: handleId
      x: root.visualPosition * (root.availableWidth - width)
      y: root.availableHeight / 2 - height / 2
      implicitWidth: 16
      implicitHeight: 16
      radius: 8
      color: UiTheme.textFieldColor
      border.color: UiTheme.fontColor1
      antialiasing: true
   }
}
