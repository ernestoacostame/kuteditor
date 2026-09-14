import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KutComponents 1.0
import Qt.labs.platform as Platform

Rectangle {
    id: trackRoot
    height: trackHeight
    clip: true
    property real trackHeight: 210
    property real minTrackHeight: 210
    property real maxTrackHeight: 500
    property bool isResizing: false

    Behavior on height {
        NumberAnimation { duration: 30; easing.type: Easing.OutQuad }
    }
    color: isTrackSelected ? Qt.tint(Qt.lighter(panelColor, 1.28), Qt.rgba(trackRoot.trackColor.r, trackRoot.trackColor.g, trackRoot.trackColor.b, 0.12)) : panelColor  // Highlight when selected
    
    // Add border when track is selected
    border.color: isTrackSelected ? Qt.rgba(trackRoot.trackColor.r, trackRoot.trackColor.g, trackRoot.trackColor.b, 0.85) : "transparent"
    border.width: isTrackSelected ? 2 : 0

    // Referencia al root de la aplicación (main.qml ApplicationWindow). Se
    // pasa desde main.qml porque dentro de los Repeater delegates QML no
    // resuelve "root" por id (scope distinto). Todos los accesos al root
    // van vía esta propiedad.
    required property var appRoot

    // Capa de overlay global donde se crean los ghosts de drag,
    // para que no queden clippeados dentro de la pista origen.
    property var dragOverlayItem: null

    // --- Props de entrada ---
    required property int    trackIndex
    required property string trackName
    required property color  trackColor
    required property bool   isMuted
    required property bool   isSolo
    required property bool   isArmed
    required property string inputDeviceId
    required property string inputDeviceName
    required property real   gain
    required property real   pan
    required property real   duration
    required property bool   hasAudio
    
    // Track selection
    readonly property bool isTrackSelected: trackRoot.appRoot.selectedTracks.indexOf(trackIndex) >= 0

    // Paleta
    required property color accentColor
    required property color panelColor
    required property color textColor
    required property color subtextColor
    required property color dividerColor

    // Modelo de dispositivos (PipeWireManager)
    required property var devicesModel

    // Selector de canal (mono / estéreo)
    required property int inputMode            // 0=estéreo, 1=mono
    required property int inputChannelIndex    // índice de canal físico si mono

    // Ancho real del clipArea, expuesto para que main.qml pueda leerlo
    // y usarlo en la conversión centralizada pixel↔tiempo.
    readonly property real clipAreaWidth: clipArea.width

    // Señales
    signal requestArmToggle()
    signal requestMuteToggle()
    signal requestSoloToggle()
    signal requestDelete()
    signal requestClearAudio()
    signal requestNameChange(string newName)
    signal requestGainChange(real value)
    signal requestPanChange(real value)
    signal requestDeviceChange(string devId, string devName)
    signal requestChannelChange(int mode, int channelIndex)

    // Exponer nameField para Tab entre pistas
    property alias nameFieldItem: nameField

    // Índice del clip sobre el que se hizo right-click (menú contextual).
    property int contextClipIndex: -1

    function setFadeInPreset(sec) {
        const ci = trackRoot.contextClipIndex
        if (ci < 0) return
        const curOut = TrackModel.clipFadeOutSec(trackRoot.trackIndex, ci)
        UndoManager.setClipFades(trackRoot.trackIndex, ci, sec, curOut)
    }

    function setFadeOutPreset(sec) {
        const ci = trackRoot.contextClipIndex
        if (ci < 0) return
        const curIn = TrackModel.clipFadeInSec(trackRoot.trackIndex, ci)
        UndoManager.setClipFades(trackRoot.trackIndex, ci, curIn, sec)
    }

    // Estado del Shift+drag para selección de región por-pista.
    property bool _regionDragging: false
    property real _regionDragSec: 0

    // MouseArea for track selection
    MouseArea {
        id: trackSelectionArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        z: 0  // Below other elements but above the track background
        
        onClicked: (mouse) => {
            // Select this track when clicking on empty areas
            const additive = (mouse.modifiers & Qt.ControlModifier) !== 0 || (mouse.modifiers & Qt.ShiftModifier) !== 0
            trackRoot.appRoot.selectTrack(trackRoot.trackIndex, additive)
            
            // Also seek to the clicked position
            const total = clipArea.displayDuration
            if (total <= 0 || timelineInner.width <= 0) return
            const innerX = mouse.x - clipArea.x + appRoot.timelineScrollX
            const ratio = Math.max(0, Math.min(1, innerX / timelineInner.width))
            
            let targetSec = ratio * total
            // Magnetic snap: search within 15 pixels
            const thresholdSec = 15 * appRoot.secPerPixel
            targetSec = TrackModel.findNearestClipEdge(targetSec, thresholdSec)
            
            AudioEngine.seekTime(targetSec)
        }
        
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                AudioEngine.play()
            }
        }
    }

    // Borde izquierdo con el color de la pista
    Rectangle {
        id: colorStripe
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: isTrackSelected ? 8 : 5
        color: trackRoot.trackColor

        Behavior on width {
            NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
        }

        // Indicador brillante interior cuando la pista está seleccionada
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            color: "#ffffff"
            opacity: isTrackSelected ? 0.85 : 0.0
            Behavior on opacity {
                NumberAnimation { duration: 120 }
            }
        }
    }

    // Franja superior si está armada
    Rectangle {
        anchors.left: colorStripe.right
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2
        visible: trackRoot.isArmed
        color: trackRoot.accentColor
    }

    // -------- Panel izquierdo: controles --------
    Rectangle {
        id: controlsPanel
        anchors.left: colorStripe.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: appRoot.clipAreaLeftPx - (colorStripe.width + 21)  // 21 = VU(14) + divider(4) + margins(3)
        color: isTrackSelected ? Qt.rgba(trackRoot.trackColor.r, trackRoot.trackColor.g, trackRoot.trackColor.b, 0.06) : "transparent"
        clip: true

        // --- Drag para reordenar pistas ---
        // MouseArea de fondo (z bajo): permite arrastrar desde áreas vacías
        // del panel. Los controles encima (botones, combos, knobs) capturan
        // sus propios clicks y no interfieren.
        MouseArea {
            id: trackDragMA
            anchors.fill: parent
            z: -1  // debajo de los controles
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
            preventStealing: true

            property real pressGlobalY: 0
            property int draggedIndex: -1
            property bool dragging: false

            onPressed: (mouse) => {
                const global = mapToItem(null, mouse.x, mouse.y)
                pressGlobalY = global.y
                draggedIndex = trackRoot.trackIndex
                dragging = false
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const global = mapToItem(null, mouse.x, mouse.y)
                const dy = global.y - pressGlobalY

                // Umbral de 10px antes de empezar a arrastrar
                if (!dragging && Math.abs(dy) > 10) {
                    dragging = true
                }
                if (!dragging) return

                // Determinar si hay que mover arriba o abajo
                const trackH = trackRoot.height
                if (dy < -trackH * 0.4 && draggedIndex > 0) {
                    TrackModel.moveTrack(draggedIndex, draggedIndex - 1)
                    draggedIndex = draggedIndex - 1
                    pressGlobalY = global.y
                } else if (dy > trackH * 0.4 && draggedIndex < TrackModel.count - 1) {
                    TrackModel.moveTrack(draggedIndex, draggedIndex + 1)
                    draggedIndex = draggedIndex + 1
                    pressGlobalY = global.y
                }
            }
            onReleased: {
                dragging = false
                draggedIndex = -1
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            spacing: 6

            // Fila 1: nombre + cerrar
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    implicitHeight: 30
                    text: trackRoot.trackName
                    color: trackRoot.textColor
                    selectByMouse: true
                    placeholderText: qsTr("Nombre de la pista")
                    font.pixelSize: 13
                    background: Rectangle {
                        color: "transparent"
                        border.color: nameField.activeFocus
                                      ? trackRoot.accentColor
                                      : "transparent"
                        border.width: 1
                        radius: 3
                    }

                    // Importante: al escribir en el TextField, Qt rompe el binding
                    // text: trackRoot.trackName. Reparamos el sync manualmente
                    // cuando el nombre cambia en el modelo.
                    Connections {
                        target: trackRoot
                        function onTrackNameChanged() {
                            if (!nameField.activeFocus && nameField.text !== trackRoot.trackName)
                                nameField.text = trackRoot.trackName
                        }
                    }

                    function commitName() {
                        if (text !== trackRoot.trackName)
                            trackRoot.requestNameChange(text)
                        focus = false   // devolver foco al root para que atajos funcionen
                    }

                    onEditingFinished: commitName()   // Enter o pérdida de foco
                    onAccepted: commitName()          // Enter explícito

                    // Tab: confirmar nombre y pasar al campo de la siguiente pista
                    Keys.onTabPressed: {
                        commitName()
                        const nextIdx = trackRoot.trackIndex + 1
                        if (nextIdx < TrackModel.count) {
                            // Buscar el delegate de la siguiente pista en el ListView
                            const nextItem = trackRoot.appRoot.findTrackNameField(nextIdx)
                            if (nextItem) nextItem.forceActiveFocus()
                        }
                    }
                }

                Button {
                    id: deleteBtn
                    implicitWidth: 28
                    implicitHeight: 28
                    text: "\u2715"
                    ToolTip.text: qsTr("Eliminar pista")
                    ToolTip.visible: hovered
                    onClicked: trackRoot.requestDelete()
                    background: Rectangle {
                        color: deleteBtn.hovered ? "#c0392b" : "transparent"
                        border.color: deleteBtn.hovered
                                      ? "#c0392b"
                                      : trackRoot.dividerColor
                        border.width: 1
                        radius: 3
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: deleteBtn.hovered ? "white" : trackRoot.subtextColor
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Fila 2: Rec / M / S / FX / menú
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Button {
                    id: armBtn
                    implicitWidth: 32; implicitHeight: 28
                    text: "\u25CF"
                    ToolTip.text: trackRoot.isArmed
                                  ? qsTr("Desarmar (no grabar)")
                                  : qsTr("Armar para grabar")
                    ToolTip.visible: hovered
                    onClicked: trackRoot.requestArmToggle()
                    background: Rectangle {
                        color: trackRoot.isArmed
                               ? Qt.rgba(0.9, 0.2, 0.2, 0.22)
                               : "transparent"
                        border.color: trackRoot.isArmed
                                      ? trackRoot.accentColor
                                      : "#555"
                        border.width: 1; radius: 3
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: trackRoot.isArmed ? trackRoot.accentColor
                                                 : trackRoot.subtextColor
                        font.bold: true; font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Button {
                    text: "M"
                    implicitWidth: 32; implicitHeight: 28
                    onClicked: trackRoot.requestMuteToggle()
                    ToolTip.text: qsTr("Silenciar"); ToolTip.visible: hovered
                    background: Rectangle {
                        color: trackRoot.isMuted ? "#c0392b" : "#3d4146"
                        radius: 3
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text; color: "white"
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Button {
                    text: "S"
                    implicitWidth: 32; implicitHeight: 28
                    onClicked: trackRoot.requestSoloToggle()
                    ToolTip.text: qsTr("Solo"); ToolTip.visible: hovered
                    background: Rectangle {
                        color: trackRoot.isSolo ? "#f39c12" : "#3d4146"
                        radius: 3
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text; color: "white"
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Button {
                    id: fxButton
                    text: "FX"
                    implicitWidth: 36; implicitHeight: 28
                    property var _fxChain: TrackModel.trackFxChain(trackRoot.trackIndex)
                    property bool hasActive: _fxChain ? _fxChain.hasActiveEffects() : false
                    function _refreshFx() {
                        _fxChain = TrackModel.trackFxChain(trackRoot.trackIndex)
                        hasActive = _fxChain ? _fxChain.hasActiveEffects() : false
                    }
                    Connections {
                        target: fxButton._fxChain
                        enabled: !!fxButton._fxChain
                        function onChanged() { fxButton._refreshFx() }
                    }
                    Connections {
                        target: trackRoot
                        function onTrackIndexChanged() { fxButton._refreshFx() }
                    }
                    onClicked: trackRoot.appRoot.openTrackFx(trackRoot.trackIndex, trackRoot.trackName)
                    ToolTip.text: qsTr("Efectos de pista"); ToolTip.visible: hovered
                    background: Rectangle {
                        color: fxButton.hasActive ? "#c0392b" : "#3d4146"
                        radius: 3
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text; color: "white"
                        font.bold: true; font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Menú desplegable ▾
                Button {
                    id: trackMenuBtn
                    implicitWidth: 24; implicitHeight: 28
                    ToolTip.text: qsTr("Opciones de pista"); ToolTip.visible: hovered
                    onClicked: trackMenu.open()
                    background: Rectangle {
                        color: trackMenuBtn.hovered ? "#4a4d52" : "#3d4146"
                        radius: 3
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: "\u25BE"; color: "white"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    Platform.Menu {
                        id: trackMenu
                        Platform.MenuItem {
                            text: qsTr("Añadir Pista")
                            onTriggered: UndoManager.addTrack("")
                        }
                        Platform.MenuItem {
                            text: qsTr("Duplicar Pista")
                            onTriggered: {
                                // Leer parámetros de la pista actual
                                const src = TrackModel.getTrackData(trackRoot.trackIndex)
                                // Crear pista nueva (se añade al final)
                                UndoManager.addTrack(src.name + " (copia)")
                                const newIdx = TrackModel.count - 1
                                // Copiar dispositivo, canal, gain, pan y color
                                TrackModel.updateInputDevice(newIdx, src.inputDevice, src.inputDeviceName)
                                TrackModel.updateInputChannel(newIdx, src.inputMode, src.inputChannelIndex)
                                TrackModel.updateGain(newIdx, src.gain)
                                TrackModel.updatePan(newIdx, src.pan)
                                TrackModel.updateTrackColor(newIdx, src.color)
                            }
                        }
                        Platform.MenuSeparator {}
                        Platform.MenuItem {
                            text: qsTr("Eliminar Pista")
                            onTriggered: trackRoot.requestDelete()
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Fila 3: Dispositivo de entrada
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: qsTr("In")
                    color: trackRoot.subtextColor
                    font.pixelSize: 10
                    Layout.preferredWidth: 22
                }
                ComboBox {
                    id: deviceCombo
                    Layout.fillWidth: true
                    implicitHeight: 26
                    font.pixelSize: 10
                    model: trackRoot.devicesModel
                    textRole: "deviceDescription"
                    valueRole: "deviceId"

                    background: Rectangle {
                        color: "#1e2228"
                        radius: 3
                        border.color: deviceCombo.activeFocus ? trackRoot.accentColor : "#3a4050"
                        border.width: 1
                    }

                    contentItem: Label {
                        anchors.fill: parent
                        leftPadding: 6
                        rightPadding: 24
                        text: deviceCombo.displayText
                        color: "#ecf0f1"
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    // Evitar bucle de actualización entre el modelo y la UI.
                    property bool _updating: false

                    function syncFromModel() {
                        _updating = true
                        const idx = indexOfValue(trackRoot.inputDeviceId)
                        currentIndex = idx >= 0 ? idx : 0
                        _updating = false
                    }

                    Component.onCompleted: syncFromModel()
                    onModelChanged: Qt.callLater(syncFromModel)

                    // Cuando el usuario elige una opción
                    onActivated: {
                        if (_updating) return
                        const devId   = currentValue !== undefined ? currentValue : ""
                        const devName = currentText
                        trackRoot.requestDeviceChange(devId, devName)
                    }

                    // Si el modelo cambia el device por otra vía, resincronizar
                    Connections {
                        target: trackRoot
                        function onInputDeviceIdChanged() { deviceCombo.syncFromModel() }
                    }

                    ToolTip.text: qsTr("Dispositivo de entrada")
                    ToolTip.visible: hovered
                }
            }

            // Fila 3b: canal del dispositivo (mono/estéreo + índice)
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: qsTr("Ch")
                    color: trackRoot.subtextColor
                    font.pixelSize: 10
                    Layout.preferredWidth: 22
                }

                ComboBox {
                    id: modeCombo
                    Layout.preferredWidth: 72
                    implicitHeight: 26
                    font.pixelSize: 10
                    model: [ qsTr("Estéreo"), qsTr("Mono") ]
                    currentIndex: trackRoot.inputMode

                    background: Rectangle {
                        color: "#1e2228"
                        radius: 3
                        border.color: modeCombo.activeFocus ? trackRoot.accentColor : "#3a4050"
                        border.width: 1
                    }

                    contentItem: Label {
                        anchors.fill: parent
                        leftPadding: 6
                        rightPadding: 24
                        text: modeCombo.displayText
                        color: "#ecf0f1"
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    onActivated: {
                        // Al cambiar modo, el índice de canal puede dejar de
                        // ser válido (p.ej. Estéreo no permite el último canal
                        // como par). Resetear a 0.
                        trackRoot.requestChannelChange(currentIndex, 0)
                    }
                    ToolTip.text: qsTr("Modo: estéreo o mono (un solo canal del dispositivo)")
                    ToolTip.visible: hovered
                }

                // ComboBox con los canales físicos reales del device.
                ComboBox {
                    id: channelCombo
                    Layout.fillWidth: true
                    implicitHeight: 26
                    font.pixelSize: 10

                    background: Rectangle {
                        color: "#1e2228"
                        radius: 3
                        border.color: channelCombo.activeFocus ? trackRoot.accentColor : "#3a4050"
                        border.width: 1
                    }

                    contentItem: Label {
                        anchors.fill: parent
                        leftPadding: 6
                        rightPadding: 24
                        text: channelCombo.displayText
                        color: "#ecf0f1"
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    // Lista construida dinámicamente según canales del device
                    // y modo (estéreo = pares, mono = canal individual).
                    property var channelOptions: []

                    function rebuild() {
                        const totalCh = PipeWireManager.deviceChannelCount(trackRoot.inputDeviceId)
                        const n = totalCh > 0 ? totalCh : 2   // fallback 2 si desconocido
                        const opts = []
                        if (modeCombo.currentIndex === 1) {
                            // MONO: un canal.
                            for (let i = 0; i < n; i++) {
                                opts.push({ label: qsTr("Canal %1").arg(i + 1), value: i })
                            }
                        } else {
                            // ESTÉREO: pares consecutivos (i, i+1).
                            for (let i = 0; i + 1 < n; i++) {
                                opts.push({ label: qsTr("Canales %1/%2").arg(i + 1).arg(i + 2),
                                            value: i })
                            }
                            if (opts.length === 0) {
                                // Device mono: forzar un "par" del único canal.
                                opts.push({ label: qsTr("Canal 1"), value: 0 })
                            }
                        }
                        channelOptions = opts
                        // Model se toma de channelOptions.
                        // Tras rebuild, intentar restaurar el índice actual del modelo.
                        let targetIdx = 0
                        for (let k = 0; k < opts.length; k++) {
                            if (opts[k].value === trackRoot.inputChannelIndex) {
                                targetIdx = k; break
                            }
                        }
                        currentIndex = targetIdx
                    }

                    model: channelOptions
                    textRole: "label"
                    valueRole: "value"

                    Component.onCompleted: rebuild()

                    // Cuando cambia el device o el modo, repoblar el combo.
                    Connections {
                        target: trackRoot
                        function onInputDeviceIdChanged() { channelCombo.rebuild() }
                        function onInputModeChanged()    { channelCombo.rebuild() }
                    }
                    Connections {
                        target: PipeWireManager
                        function onDevicesChanged() {
                            // Los canales del device pueden cambiar cuando se
                            // termina la enumeración diferida.
                            Qt.callLater(channelCombo.rebuild)
                        }
                    }

                    onActivated: {
                        const val = currentValue !== undefined ? currentValue : 0
                        trackRoot.requestChannelChange(modeCombo.currentIndex, val)
                    }

                    ToolTip.text: modeCombo.currentIndex === 1
                                  ? qsTr("Canal físico del dispositivo (mono)")
                                  : qsTr("Par de canales del dispositivo (estéreo)")
                    ToolTip.visible: hovered
                }

                Item { Layout.preferredWidth: 0 }
            }

            // Fila 4: Gain + Pan con knobs
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: qsTr("Gain")
                    color: trackRoot.subtextColor
                    font.pixelSize: 10
                }
                MetalKnob {
                    id: gainKnob
                    size: 32
                    from: 0.0
                    to: 2.0
                    defaultValue: 1.0
                    value: trackRoot.gain
                    dotColor: trackRoot.accentColor
                    label: ""
                    Layout.preferredWidth: size
                    Layout.preferredHeight: size + 14
                    onMoved: (v) => trackRoot.requestGainChange(v)
                }
                Label {
                    text: {
                        const v = gainKnob._internalValue !== undefined ? gainKnob._internalValue : gainKnob.value
                        return (20 * Math.log10(Math.max(0.001, v))).toFixed(1)
                    }
                    color: trackRoot.subtextColor
                    font.pixelSize: 9
                    font.family: "monospace"
                }
                Label {
                    text: "dB"
                    color: trackRoot.subtextColor
                    font.pixelSize: 9
                }

                Item { Layout.fillWidth: true; Layout.preferredWidth: 4 }

                Label {
                    text: "L"
                    color: trackRoot.subtextColor
                    font.pixelSize: 10
                    font.bold: true
                }
                MetalKnob {
                    id: panKnob
                    size: 32
                    from: -1.0
                    to: 1.0
                    defaultValue: 0.0
                    value: trackRoot.pan
                    dotColor: "#e67e22"
                    label: ""
                    Layout.preferredWidth: size
                    Layout.preferredHeight: size + 14
                    onMoved: (v) => trackRoot.requestPanChange(v)
                }
                Label {
                    text: "R"
                    color: trackRoot.subtextColor
                    font.pixelSize: 10
                    font.bold: true
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    // Divisor arrastrable para redimensionar el panel de controles
    Rectangle {
        id: verticalDivider
        anchors.left: controlsPanel.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 4
        color: dividerMA.containsMouse || dividerMA.pressed ? trackRoot.accentColor : "#1a1d21"

        MouseArea {
            id: dividerMA
            anchors.fill: parent
            cursorShape: Qt.SplitHCursor
            hoverEnabled: true

            property real pressGlobalX: 0
            property int pressWidth: 0

            onPressed: (mouse) => {
                const global = mapToItem(null, mouse.x, mouse.y)
                pressGlobalX = global.x
                pressWidth = appRoot.clipAreaLeftPx
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const global = mapToItem(null, mouse.x, mouse.y)
                const dx = global.x - pressGlobalX
                appRoot.clipAreaLeftPx = Math.max(
                    appRoot.minPanelWidth,
                    Math.min(appRoot.maxPanelWidth, pressWidth + dx))
            }
        }
    }

    // -------- VU vertical por pista (a la izquierda del waveform) --------
    Rectangle {
        id: trackVU
        anchors.left: verticalDivider.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        anchors.leftMargin: 4
        width: 14
        color: "#111418"
        radius: 2
        border.color: "#3c4146"
        border.width: 1

        property real leftLevel: 0.0
        property real rightLevel: 0.0

        function refresh() {
            leftLevel  = TrackModel.getTrackLevelLeft(trackRoot.trackIndex)
            rightLevel = TrackModel.getTrackLevelRight(trackRoot.trackIndex)
        }

        // Convierte un valor lineal [0..1] a altura normalizada [0..1] usando
        // una escala IEC tipo VU digital: -60 dB => 0%, -20 dB => ~65%,
        // -9 dB => ~85%, 0 dB => 100%. Esto hace que la voz de podcast
        // (típicamente -20 a -14 dBFS) se vea cómodamente alta en el medidor.
        function levelToHeight(v) {
            if (v <= 0.0001) return 0.0   // <= -80 dBFS → vacío
            const db = 20.0 * Math.log(v) / Math.LN10   // dBFS
            if (db <= -60.0) return 0.0
            if (db >= 0.0)   return 1.0
            // Mapeo no lineal tipo IEC:
            //   -60..-40 dB → 0..30% (altura)
            //   -40..-20 dB → 30..65%
            //   -20..-9  dB → 65..85%
            //   -9..0    dB → 85..100%
            if (db < -40) return (db + 60) / 20 * 0.30
            if (db < -20) return 0.30 + (db + 40) / 20 * 0.35
            if (db < -9)  return 0.65 + (db + 20) / 11 * 0.20
            return 0.85 + (db + 9) / 9 * 0.15
        }

        Component.onCompleted: refresh()

        Connections {
            target: TrackModel
            function onTrackLevelsChanged() { trackVU.refresh() }
        }

        // Dos barras verticales (L|R)
        Row {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 1
            Rectangle {
                width: (parent.width - 1) / 2
                height: parent.height
                color: "#000"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * trackVU.levelToHeight(trackVU.leftLevel)
                    color: height > parent.height * 0.9 ? "#e74c3c"
                         : height > parent.height * 0.7 ? "#f39c12"
                         : "#2ecc71"
                }
            }
            Rectangle {
                width: (parent.width - 1) / 2
                height: parent.height
                color: "#000"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * trackVU.levelToHeight(trackVU.rightLevel)
                    color: height > parent.height * 0.9 ? "#e74c3c"
                         : height > parent.height * 0.7 ? "#f39c12"
                         : "#2ecc71"
                }
            }
        }
    }

    // -------- Zona del clip / timeline --------
    Rectangle {
        id: clipArea
        // IMPORTANTE: el borde izquierdo debe coincidir exactamente con el de
        // la ruler (que empieza en appRoot.clipAreaLeftPx desde el borde
        // izquierdo del padre). Antes se anclaba a trackVU.right + margins,
        // lo que daba una diferencia de unos píxeles y desincronizaba el
        // cursor de la flecha del triángulo.
        anchors.left: parent.left
        anchors.leftMargin: appRoot.clipAreaLeftPx
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: 6
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        color: isTrackSelected ? Qt.tint(Qt.darker(trackRoot.panelColor, 1.25), Qt.rgba(trackRoot.trackColor.r, trackRoot.trackColor.g, trackRoot.trackColor.b, 0.06)) : Qt.darker(trackRoot.panelColor, 1.35)
        radius: 4
        border.color: isTrackSelected ? Qt.rgba(trackRoot.trackColor.r, trackRoot.trackColor.g, trackRoot.trackColor.b, 0.65) : "#2d3137"
        border.width: isTrackSelected ? 2 : 1
        opacity: trackRoot.isMuted ? 0.4 : 1.0
        clip: true

        PinchHandler {
            id: pinchHandler
            target: null
            property real startZoom: 1.0

            onActiveChanged: {
                if (active) {
                    startZoom = appRoot.timelineZoom
                    appRoot.isUserInteracting = true
                }
            }

            onActiveScaleChanged: {
                if (active) {
                    const newZoom = Math.max(0.25, Math.min(512.0, startZoom * activeScale))
                    const oldZoom = appRoot.timelineZoom
                    if (oldZoom !== newZoom) {
                        const mouseViewX = centroid.position.x
                        const total = clipArea.displayDuration
                        if (total > 0 && clipArea.width > 0) {
                            const oldInner    = clipArea.width * oldZoom
                            const mouseInnerX = mouseViewX + appRoot.timelineScrollX
                            const ratio       = mouseInnerX / oldInner
                            appRoot.timelineZoom = newZoom
                            const newInner  = clipArea.width * newZoom
                            const newScroll = ratio * newInner - mouseViewX
                            const maxScroll = Math.max(0, newInner - clipArea.width)
                            appRoot.timelineScrollX = Math.max(0, Math.min(maxScroll, newScroll))
                        } else {
                            appRoot.timelineZoom = newZoom
                        }
                    }
                }
            }
        }

        // --- Función compartida de zoom/scroll ---
        // Llamada desde cualquier onWheel (wheelCatcher sobre zona vacía,
        // mouseZone sobre clips). mouseViewX es la posición X del puntero
        // relativa al clipArea.
        function handleWheel(wheel, mouseViewX) {
            // Suspender auto-scroll ANTES de cambiar el scroll (evita race condition
            // donde los bindings de Qt disparan _autoScrollFollowPlayhead antes
            // de que el flag esté establecido).
            if (AudioEngine.isPlaying || AudioEngine.isRecording)
                appRoot.isUserInteracting = true

            var modifiers = wheel.modifiers
            // Zoom por defecto con la rueda vertical (excepto si se pulsa Shift para scroll horizontal)
            var isZoomModifier = !(modifiers & Qt.ShiftModifier)

            var pDeltaX = 0
            var pDeltaY = 0
            if (wheel.pixelDelta) {
                pDeltaX = wheel.pixelDelta.x
                pDeltaY = wheel.pixelDelta.y
            }
            var aDeltaX = wheel.angleDelta.x
            var aDeltaY = wheel.angleDelta.y

            // 1. Caso de ZOOM: vertical scroll con modificador de zoom (o pinch)
            if (isZoomModifier && (Math.abs(pDeltaY) > 0 || Math.abs(aDeltaY) > 0)) {
                const oldZoom = appRoot.timelineZoom
                // Para pixelDelta (macOS trackpad), usamos un factor continuo y suave.
                // Para angleDelta (mouse convencional), usamos 1.08.
                var dir = 1.0
                if (pDeltaY !== 0) {
                    dir = Math.pow(1.005, pDeltaY)
                } else {
                    dir = aDeltaY > 0 ? 1.08 : 1/1.08
                }
                const newZoom = Math.max(0.25, Math.min(512.0, oldZoom * dir))
                const total = clipArea.displayDuration
                if (total > 0 && clipArea.width > 0) {
                    const oldInner    = clipArea.width * oldZoom
                    const mouseInnerX = mouseViewX + appRoot.timelineScrollX
                    const ratio       = mouseInnerX / oldInner
                    appRoot.timelineZoom = newZoom
                    const newInner  = clipArea.width * newZoom
                    const newScroll = ratio * newInner - mouseViewX
                    const maxScroll = Math.max(0, newInner - clipArea.width)
                    appRoot.timelineScrollX = Math.max(0, Math.min(maxScroll, newScroll))
                } else {
                    appRoot.timelineZoom = newZoom
                }
                wheel.accepted = true
                return
            }

            // 2. Caso de SCROLL HORIZONTAL (Shift + vertical scroll, o scroll horizontal nativo)
            var isHorizontalScroll = false
            var scrollAmountX = 0

            if (Math.abs(pDeltaX) > 0) {
                // Scroll horizontal nativo por pixelDelta (trackpad macOS)
                scrollAmountX = -pDeltaX
                isHorizontalScroll = true
            } else if (Math.abs(aDeltaX) > 0) {
                // Scroll horizontal por angleDelta
                scrollAmountX = -aDeltaX * 0.5
                isHorizontalScroll = true
            } else if ((modifiers & Qt.ShiftModifier) && Math.abs(pDeltaY) > 0) {
                // Shift + scroll vertical (usando pixelDelta)
                scrollAmountX = -pDeltaY
                isHorizontalScroll = true
            } else if ((modifiers & Qt.ShiftModifier) && Math.abs(aDeltaY) > 0) {
                // Shift + scroll vertical (usando angleDelta)
                scrollAmountX = -aDeltaY * 0.5
                isHorizontalScroll = true
            }

            if (isHorizontalScroll) {
                const maxScroll = Math.max(0, timelineInner.width - clipArea.width)
                appRoot.timelineScrollX = Math.max(0, Math.min(maxScroll, appRoot.timelineScrollX + scrollAmountX))
                wheel.accepted = true
                return
            }

            // 3. Si es scroll vertical sin modificador de zoom, no lo aceptamos
            // para que se propague a trackListView (haciendo scroll de pistas)
            wheel.accepted = false
        }

        // (El auto-scroll se reactiva al hacer seek o al detener/reiniciar playback.)

        // Acepta archivos arrastrados para importar (WAV/MP3/otros).
        DropArea {
            id: importDrop
            anchors.fill: parent
            z: 100
            property bool hovering: false
            onEntered: (drag) => {
                console.log("[DropArea:Track] onEntered hasUrls=" + drag.hasUrls
                            + " hasText=" + drag.hasText
                            + " formats=" + drag.formats)
                if (drag.hasUrls) hovering = true
            }
            onExited: { hovering = false; console.log("[DropArea:Track] onExited") }
            onDropped: (drop) => {
                hovering = false
                if (!drop.hasUrls) return
                // Calcular la posición temporal donde se soltó.
                const total = clipArea.displayDuration
                const innerX = drop.x + appRoot.timelineScrollX
                const ratio = timelineInner.width > 0
                                ? innerX / timelineInner.width : 0
                const startSec = Math.max(0, ratio * total)

                let imported = 0
                for (const url of drop.urls) {
                    // Pasar la URL tal cual al C++; importAudioFile usa
                    // QUrl::toLocalFile() para resolver el path.
                    const path = url.toString()
                    if (TrackModel.importAudioFile(trackRoot.trackIndex,
                                                   path,
                                                   startSec)) {
                        imported++
                    }
                }
                drop.accept()
            }
        }

        // Borde resaltado cuando un archivo se arrastra encima.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: appRoot.accent
            border.width: 2
            radius: 4
            visible: importDrop.hovering
            z: 99
            Label {
                anchors.centerIn: parent
                text: qsTr("Soltar para importar")
                color: "white"
                font.pixelSize: 14
                font.bold: true
                style: Text.Outline
                styleColor: Qt.rgba(0, 0, 0, 0.6)
            }
        }

        // Duración mostrada en la franja. Compartida globalmente vía
        // appRoot.timelineDisplayDuration (estable durante grabación).
        readonly property real displayDuration: appRoot.timelineDisplayDuration

        // Contenedor interno escalado por el zoom global. El scroll horizontal
        // compartido se aplica con un offset X negativo.
        Item {
            id: timelineInner
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: clipArea.width * appRoot.timelineZoom
            x: -appRoot.timelineScrollX

            // Rejilla sutil (10 divisiones sobre el ancho total escalado)
            Row {
                anchors.fill: parent
                Repeater {
                    model: 10
                    Rectangle {
                        width: timelineInner.width / 10
                        height: timelineInner.height
                        color: "transparent"
                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 1
                            color: Qt.rgba(1, 1, 1, 0.03)
                        }
                    }
                }
            }

            // -------- Clips (uno por clip del modelo) --------
            Item {
                id: clipsContainer
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right

                ListModel {
                    id: clipsModel
                }

                function refreshClips() {
                    const rawClips = TrackModel.clipsOf(trackRoot.trackIndex)
                    clipsModel.clear()
                    for (var i = 0; i < rawClips.length; ++i) {
                        clipsModel.append(rawClips[i])
                    }
                }

                Component.onCompleted: refreshClips()

                Connections {
                    target: TrackModel
                    function onClipsChanged(idx) {
                        if (idx === trackRoot.trackIndex) clipsContainer.refreshClips()
                    }
                }
                Connections {
                    target: trackRoot
                    function onHasAudioChanged() { clipsContainer.refreshClips() }
                }

                Repeater {
                    model: clipsModel
                    delegate: Rectangle {
                        id: clipRect
                        required property var modelData

                        readonly property int index: (typeof modelData !== "undefined" && modelData !== null && modelData.clipIndex !== undefined) ? modelData.clipIndex : 0

                        // Alias al appRoot dentro del scope del delegate,
                        // porque dentro del Repeater delegate el id "appRoot"
                        // del TrackComponent externo puede no resolverse.
                        readonly property var appRoot: trackRoot.appRoot

                        readonly property bool hasChapters: appRoot ? (appRoot.chapterModel ? appRoot.chapterModel.count > 0 : false) : false
                        readonly property real chapterHeaderHeight: hasChapters ? 24 : 0

                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 3
                        anchors.bottomMargin: 3

                        readonly property real clipStartSec: modelData ? (modelData.startSec || 0) : 0
                        readonly property real clipEndSec:   modelData ? (modelData.endSec   || 0) : 0
                        // Frames raw para posicionamiento preciso
                        readonly property real clipStartFrame: modelData ? (modelData.startFrame || 0) : 0
                        readonly property real clipLengthFrame: modelData ? (modelData.lengthFrame || 0) : 0
                        readonly property int clipSampleRate: modelData ? (modelData.sampleRate || 48000) : 48000
                        // Longitud "en vivo" durante grabación: si este clip
                        // es el clip activo, pedir la longitud real al modelo
                        // cada vez que llegan nuevos peaks. Si no, usar la
                        // longitud del snapshot (modelData).
                        property real liveLengthSec: clipEndSec - clipStartSec
                        property real liveLengthFrame: clipLengthFrame
                        readonly property real clipLenSec: liveLengthSec
                        readonly property bool isRecordingClip:
                            AudioEngine.isRecording &&
                            TrackModel.recordingClipIndex(trackRoot.trackIndex) === index

                        readonly property bool isGainModified: {
                            if (!modelData) return false
                            const hasEnvelope = (modelData.envelope || []).length > 0
                            const hasFades = (modelData.fadeInSec || 0) > 0 || (modelData.fadeOutSec || 0) > 0
                            const hasCustomGain = Math.abs(_liveGain - 1.0) > 1e-4
                            return hasEnvelope || hasFades || hasCustomGain
                        }

                        function refreshLiveLength() {
                            if (isRecordingClip) {
                                // Recording updates are driven by
                                // onSamplesRecordedChanged — do nothing here.
                                return
                            }
                            liveLengthSec = clipEndSec - clipStartSec
                            liveLengthFrame = clipLengthFrame
                        }

                        // Si cambia modelData (lista de clips refrescada), reajusta.
                        onClipEndSecChanged:   refreshLiveLength()
                        onClipStartSecChanged: refreshLiveLength()
                        onIsRecordingClipChanged: refreshLiveLength()

                        Connections {
                            target: TrackModel
                            function onTrackPeaksUpdated(idx) {
                                // During recording, DON'T refresh from here.
                                // The waveform data is ready but the cursor hasn't
                                // moved yet (different clock). Refreshing here makes
                                // the clip appear ahead of the cursor.
                                // Instead, recording refresh is driven by timeChanged
                                // (below) so clip and cursor update in the same frame.
                                if (idx === trackRoot.trackIndex && !clipRect.isRecordingClip)
                                    clipRect.refreshLiveLength()
                            }
                            function onClipTrimmed(idx, cIdx, newStart, newLength) {
                                if (idx === trackRoot.trackIndex && cIdx === clipRect.index) {
                                    liveLengthSec = newLength
                                    liveLengthFrame = newLength * clipSampleRate
                                    clipRect.x = appRoot.secToViewX(newStart)
                                }
                            }
                        }
                        // During recording, update clip length from the atomic
                        // samplesRecorded counter, emitted in the SAME timer tick
                        // as timeChanged. This guarantees waveform and cursor
                        // are rendered in the same Scene Graph frame.
                        Connections {
                            target: AudioEngine
                            enabled: clipRect.isRecordingClip
                            function onSamplesRecordedChanged() {
                                const samples = AudioEngine.samplesRecorded
                                if (samples > 0) {
                                    const sr = clipRect.clipSampleRate
                                    clipRect.liveLengthSec = samples / sr
                                    clipRect.liveLengthFrame = samples
                                }
                            }
                        }
                        Component.onCompleted: refreshLiveLength()

                        // Limpieza: si el Repeater destruye este delegate
                        // (porque clipsChanged refresca la lista), el ghost
                        // podría seguir vivo en el overlay. Destruirlo aquí.
                        Component.onDestruction: {
                            if (ghostVisual) {
                                ghostVisual.destroy()
                                ghostVisual = null
                            }
                        }

                        readonly property bool isSelected:
                            appRoot.isClipSelected(trackRoot.trackIndex, index)

                        // Offset visual durante el drag (para preview).
                        property var ghostVisual: null
                        property bool isGhostDragging: false
                        property real ghostStartX: 0
                        property real ghostStartY: 0
                        property real dragOffsetX: 0
                        property real dragOffsetY: 0
                        
                        property int dragNodeIndex: -1
                        property real dragNodeSec: 0
                        property real dragNodeGain: 0
                        
                        // Ghost component for dragging — incluye waveform real.
                        // Se crea en el dragOverlay (capa global) para que
                        // no se clippee al salir de la pista origen.
                        Component {
                            id: ghostComponent
                            Rectangle {
                                id: ghostRect
                                radius: 3
                                opacity: 0.85
                                color: Qt.rgba(trackRoot.trackColor.r,
                                               trackRoot.trackColor.g,
                                               trackRoot.trackColor.b, 0.5)
                                border.color: trackRoot.trackColor
                                border.width: 2

                                // Waveform real dentro del ghost.
                                WaveformItem {
                                    id: ghostWave
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    model: TrackModel
                                    trackIndex: trackRoot.trackIndex
                                    clipIndex: clipRect ? clipRect.index : -1
                                    gain: trackRoot.gain
                                    clipSourceOffset: clipRect ? (clipRect.modelData.sourceOffsetSec || 0) : 0
                                    clipLength: clipRect ? clipRect.clipLenSec : 0
                                    timelineZoom: appRoot.timelineZoom
                                    timelineScrollX: 0
                                    secPerPixel: appRoot.secPerPixel
                                    clipTimelineStart: 0
                                    fillColor: "#8cffffff"
                                }

                                Label {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.margins: 4
                                    text: (clipRect.clipLenSec || 0).toFixed(1) + " s"
                                    color: "white"
                                    font.pixelSize: 10
                                    font.bold: true
                                    visible: parent.width > 60
                                    style: Text.Outline
                                    styleColor: Qt.rgba(0, 0, 0, 0.6)
                                }
                            }
                        }
                        
                        // Nota: NO usar Behavior on opacity aquí.
                        // Cuando el Repeater recrea los delegates tras un split,
                        // una transición animada de opacidad causa un glitch
                        // visual de "crecimiento" del clip.

                        x: {
                            const totalFrames = clipArea.displayDuration * clipSampleRate
                            const baseX = totalFrames > 0
                                ? (clipStartFrame / totalFrames) * timelineInner.width
                                : 0
                            return baseX + _trimLeftOffset
                        }
                        y: 0
                        width: {
                            const totalFrames = clipArea.displayDuration * clipSampleRate
                            const baseW = totalFrames > 0
                                ? (liveLengthFrame / totalFrames) * timelineInner.width
                                : 0
                            // During recording, don't enforce the 10px minimum —
                            // at low zoom the minimum width overshoots the cursor,
                            // making the clip appear ahead of the playhead.
                            const minW = isRecordingClip ? 1 : 10
                            return Math.max(minW, baseW - _trimLeftOffset + _trimRightOffset)
                        }

                        radius: 3
                        clip: true
                        color: Qt.rgba(trackRoot.trackColor.r,
                                       trackRoot.trackColor.g,
                                       trackRoot.trackColor.b,
                                       isSelected ? 0.55 : 0.35)
                        border.color: trackRoot.trackColor
                        border.width: isSelected ? 2 : 1
                        opacity: (isRecordingClip && liveLengthSec <= 0) ? 0
                                 : clipRect.isGhostDragging ? 0.15
                                 : (mouseZone.pressedInside ? 0.85
                                 : (clipRect.isSelected ? 0.9 : 0.7))
                        z: isSelected ? 6 : 5
                        
                        onIsGhostDraggingChanged: {
                            if (!isGhostDragging && ghostVisual) {
                                ghostVisual.destroy()
                                ghostVisual = null
                            }
                        }

                        // Cursor y eventos del clip.
                        // z alto para que quede por encima del Canvas y Label
                        // del propio clip, y claramente por encima del
                        // timelineMA que está en z:-1.
                        MouseArea {
                            id: mouseZone
                            anchors.fill: parent
                            z: 20
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                            hoverEnabled: true

                            // Estado del drag.
                            property bool pressedInside: false
                            property real pressLocalX: 0
                            property real pressLocalY: 0
                            property bool dragging: false
                            property real dragStartX: 0
                            property real dragStartY: 0

                            onPressed: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    if (trackRoot.appRoot.blockContextMenu) return
                                    trackRoot.contextClipIndex = clipRect.index
                                    if (!clipRect.isSelected) {
                                        trackRoot.appRoot.selectClip(trackRoot.trackIndex, clipRect.index, false)
                                    }
                                     clipMenu.open()
                                     return
                                }

                                // Alt+Click o Ctrl+Shift+Click: añadir nodo envolvente
                                if (((mouse.modifiers & Qt.AltModifier) !== 0) ||
                                    (((mouse.modifiers & Qt.ControlModifier) !== 0) && ((mouse.modifiers & Qt.ShiftModifier) !== 0))) {
                                    const clipLenSec = modelData.lengthSec || 0
                                    const tSec = clipLenSec > 0 ? (mouse.x / clipRect.width) * clipLenSec : 0
                                    const g = Math.max(0, Math.min(2.0, 2.0 * (1.0 - mouse.y / clipRect.height)))
                                    UndoManager.addEnvelopeNode(trackRoot.trackIndex, clipRect.index, tSec, g)
                                    return
                                }

                                // Shift+drag: selección de región por-pista.
                                if ((mouse.modifiers & Qt.ShiftModifier) !== 0) {
                                    trackRoot._regionDragging = true
                                    const total = clipArea.displayDuration
                                    const innerX = clipRect.x + mouse.x + appRoot.timelineScrollX
                                    const ratio = total > 0 && timelineInner.width > 0
                                        ? Math.max(0, Math.min(1, innerX / timelineInner.width)) : 0
                                    trackRoot._regionDragSec = ratio * total
                                    appRoot.setTrackRegion(trackRoot.trackIndex,
                                        trackRoot._regionDragSec, trackRoot._regionDragSec)
                                    return
                                }
                                
                                const additive = (mouse.modifiers & Qt.ControlModifier) !== 0
                                
                                if (!clipRect.isSelected) {
                                    trackRoot.appRoot.selectClip(trackRoot.trackIndex, clipRect.index, additive)
                                } else if (additive) {
                                    trackRoot.appRoot.selectClip(trackRoot.trackIndex, clipRect.index, true)
                                }
                                
                                pressedInside = true
                                pressLocalX = mouse.x
                                pressLocalY = mouse.y
                                dragStartX = clipRect.x
                                dragStartY = clipRect.y
                                
                                // Store initial position for ghost
                                clipRect.ghostStartX = clipRect.x
                                clipRect.ghostStartY = clipRect.y
                            }

                            onPositionChanged: (mouse) => {
                                // Shift+drag: selección de región.
                                if (trackRoot._regionDragging) {
                                    const total = clipArea.displayDuration
                                    const innerX = clipRect.x + mouse.x + appRoot.timelineScrollX
                                    const ratio = total > 0 && timelineInner.width > 0
                                        ? Math.max(0, Math.min(1, innerX / timelineInner.width)) : 0
                                    appRoot.setTrackRegion(trackRoot.trackIndex,
                                        trackRoot._regionDragSec, ratio * total)
                                    return
                                }

                                if (!pressedInside) return
                                
                                const dx = mouse.x - pressLocalX
                                const dy = mouse.y - pressLocalY
                                
                                // Start ghost dragging after 4px movement threshold
                                if (!dragging && (Math.abs(dx) > 4 || Math.abs(dy) > 4)) {
                                    dragging = true
                                    clipRect.isGhostDragging = true
                                    
                                    // Crear ghost en el overlay global (no en la pista)
                                    // para que se vea por encima de todas las pistas.
                                    const overlay = trackRoot.dragOverlayItem
                                    if (overlay) {
                                        // Mapear posición del clip a coordenadas del overlay.
                                        const globalPos = clipRect.mapToItem(overlay, 0, 0)
                                        clipRect.ghostVisual = ghostComponent.createObject(overlay, {
                                            "x": globalPos.x,
                                            "y": globalPos.y,
                                            "width": clipRect.width,
                                            "height": clipRect.height,
                                            "color": clipRect.color,
                                            "border.color": clipRect.border.color,
                                            "opacity": 0.85,
                                            "z": 999
                                        })
                                        // Guardar posición inicial del ghost en coords overlay.
                                        clipRect.ghostStartX = globalPos.x
                                        clipRect.ghostStartY = globalPos.y
                                    }
                                }
                                
                                if (dragging && clipRect.ghostVisual) {
                                    // Solo mover el ghost — el clip original no se toca.
                                    // Bloquear en el segundo 0:00 (que en coords de dragOverlay es -trackRoot.appRoot.timelineScrollX)
                                    const targetX = Math.max(-trackRoot.appRoot.timelineScrollX, clipRect.ghostStartX + dx)
                                    clipRect.ghostVisual.x = targetX
                                    
                                    const dstTrack = trackRoot.appRoot.trackIndexFromDragY(
                                        trackRoot.trackIndex, clipRect.y + mouseZone.pressLocalY + dy)
                                    
                                    const overlay = trackRoot.dragOverlayItem
                                    if (overlay) {
                                        const targetY = trackRoot.appRoot.trackItemYInOverlay(dstTrack, overlay)
                                        clipRect.ghostVisual.y = targetY + 9
                                    } else {
                                        clipRect.ghostVisual.y = clipRect.ghostStartY + dy
                                    }
                                    
                                    // Guardar offsets para usarlos en onReleased.
                                    clipRect.dragOffsetX = targetX - clipRect.ghostStartX
                                    clipRect.dragOffsetY = dy
                                    
                                    // Show drop indicator on target track
                                    if (dstTrack !== trackRoot.trackIndex) {
                                        trackRoot.appRoot.showTrackDropIndicator(dstTrack, true)
                                    } else {
                                        trackRoot.appRoot.showTrackDropIndicator(-1, false)
                                    }
                                }
                            }

                            onReleased: (mouse) => {
                                if (trackRoot._regionDragging) {
                                    trackRoot._regionDragging = false
                                    return
                                }
                                if (!pressedInside) return
                                pressedInside = false
                                
                                if (dragging) {
                                    // Guardar TODO antes de tocar el modelo, porque
                                    // moveClips emite clipsChanged que destruye este delegate.
                                    const total = clipArea.displayDuration
                                    const pxPerSec = total > 0
                                        ? (timelineInner.width / total) : 0
                                    const deltaSec = pxPerSec > 0
                                        ? clipRect.dragOffsetX / pxPerSec : 0
                                    
                                    const srcTrack = trackRoot.trackIndex
                                    const dstTrack = trackRoot.appRoot.trackIndexFromDragY(
                                        srcTrack, clipRect.y + mouseZone.pressLocalY + clipRect.dragOffsetY)
                                    
                                    const selClips = trackRoot.appRoot.selectedClips
                                    let clipsToMove = selClips
                                    if (clipsToMove.length === 0) {
                                        clipsToMove = [{ track: srcTrack, clip: clipRect.index }]
                                    }
                                    
                                    const deltaTrack = dstTrack - srcTrack
                                    const theAppRoot = trackRoot.appRoot
                                    
                                    // Capture ghost reference before the delegate is
                                    // destroyed by moveClips → clipsChanged.
                                    const ghostRef = clipRect.ghostVisual
                                    clipRect.ghostVisual = null
                                    clipRect.isGhostDragging = false
                                    clipRect.dragOffsetX = 0
                                    clipRect.dragOffsetY = 0
                                    dragging = false
                                    
                                    theAppRoot.showTrackDropIndicator(-1, false)
                                    
                                    // Aplicar al modelo — esto destruye este delegate.
                                    // No acceder a trackRoot ni clipRect después de esto.
                                    if (deltaSec !== 0 || deltaTrack !== 0) {
                                        UndoManager.moveMultipleClips(
                                            clipsToMove, deltaSec, deltaTrack
                                        )
                                        theAppRoot.clearClipSelection()
                                    }
                                    
                                    // Destroy ghost AFTER model update so the new clip
                                    // delegate has time to appear — avoids 1-frame flicker.
                                    if (ghostRef) {
                                        Qt.callLater(function() { ghostRef.destroy() })
                                    }
                                }
                            }
                            
                            onExited: {
                                if (!pressedInside) {
                                    cursorShape = Qt.ArrowCursor
                                }
                            }

                            onWheel: (wheel) => {
                                // Convertir coordenadas del clip a clipArea.
                                const viewX = clipRect.x + wheel.x - appRoot.timelineScrollX
                                clipArea.handleWheel(wheel, viewX)
                            }

                            onDoubleClicked: (mouse) => {
                                if (mouse.button === Qt.LeftButton) {
                                    const mapped = mapToItem(clipArea, mouse.x, mouse.y)
                                    const targetSec = appRoot.viewXToSec(mapped.x)
                                    AudioEngine.seekTime(targetSec)
                                    AudioEngine.play()
                                }
                            }
                        }

                        WaveformItem {
                            id: waveCanvas
                            // During trim drag, the waveform stays "fixed" on the
                            // timeline.  clipRect.x shifts by _trimLeftOffset, so
                            // we slide the waveform back by the same amount.
                            // clipRect.width changes by (-_trimLeftOffset + _trimRightOffset),
                            // so we undo that to keep the original pixel width.
                            x: 2 - clipRect._trimLeftOffset
                            y: 2 + clipRect.chapterHeaderHeight
                            width: Math.max(1, clipRect.width - 4
                                            + clipRect._trimLeftOffset
                                            - clipRect._trimRightOffset)
                            height: clipRect.height - 4 - clipRect.chapterHeaderHeight

                            model: TrackModel
                            trackIndex: trackRoot.trackIndex
                            clipIndex: clipRect.index

                            timelineScrollX: appRoot.timelineScrollX
                            timelineZoom: appRoot.timelineZoom
                            secPerPixel: appRoot.secPerPixel

                            clipTimelineStart: clipRect.modelData ? (clipRect.modelData.startSec || 0) : 0
                            clipSourceOffset: clipRect.modelData ? (clipRect.modelData.sourceOffsetSec || 0) : 0
                            clipLength: clipRect.clipLenSec

                            // Viewport-aware rendering: tell the waveform which
                            // pixel range is visible so it only generates geometry
                            // for the on-screen portion.  clipRect.x is in
                            // timelineInner coords; scrollX shifts the viewport.
                            visibleStartPx: Math.max(0, appRoot.timelineScrollX - clipRect.x - 2)
                            visibleEndPx:   Math.min(waveCanvas.width,
                                                     appRoot.timelineScrollX + clipArea.width - clipRect.x - 2)

                            gain: trackRoot.gain * clipRect._liveGain
                            liveMode: clipRect.isRecordingClip

                            // Colores premium
                            fillColor: "#8cffffff"
                            warnColor: "#a0fad138"
                            clipColor: "#b4f24d3d"

                            // Construir la envolvente normalizada (x en segundos)
                            function buildEnvelope() {
                                const env = clipRect.modelData.envelope || []
                                const envLen = env.count !== undefined ? env.count : (env.length || 0)
                                if (envLen === 0) {
                                    waveCanvas.envelope = []
                                    return
                                }
                                var result = []
                                for (var i = 0; i < envLen; ++i) {
                                    const node = env.get !== undefined ? env.get(i) : env[i]
                                    var nx = node.x
                                    var ny = node.y
                                    if (i === clipRect.dragNodeIndex) {
                                        nx = clipRect.dragNodeSec
                                        ny = clipRect.dragNodeGain
                                    }
                                    result.push({"x": nx, "y": ny})
                                }
                                waveCanvas.envelope = result
                            }

                            Connections {
                                target: clipRect
                                function onDragNodeIndexChanged() { waveCanvas.buildEnvelope() }
                                function onDragNodeSecChanged() { waveCanvas.buildEnvelope() }
                                function onDragNodeGainChanged() { waveCanvas.buildEnvelope() }
                            }

                            Connections {
                                target: TrackModel
                                function onClipsChanged(idx) {
                                    if (idx === trackRoot.trackIndex) {
                                        waveCanvas.buildEnvelope()
                                    }
                                }
                            }

                            Component.onCompleted: buildEnvelope()
                        }

                        // ── Barra de info del clip (duración · gain · mute) ──
                        property bool _liveMuted: modelData ? (modelData.muted || false) : false

                        // Barra de controles — z:25 para estar encima de mouseZone (z:20)
                        // Posicionamiento "sticky": si el borde izquierdo del clip
                        // está fuera del viewport, la barra se desplaza para seguir
                        // siendo visible (como Reaper).
                        Row {
                            id: clipInfoBar
                            anchors.top: parent.top
                            anchors.topMargin: 3 + clipRect.chapterHeaderHeight
                            x: {
                                // Cuánto del borde izquierdo del clip está oculto
                                var hiddenLeft = appRoot.timelineScrollX - clipRect.x
                                if (hiddenLeft < 0) hiddenLeft = 0
                                // No dejar que la barra se salga del clip
                                var maxX = clipRect.width - width - 3
                                if (maxX < 3) maxX = 3
                                return Math.min(hiddenLeft + 3, maxX)
                            }
                            spacing: 3
                            visible: clipRect.width > 60 && !clipRect.isRecordingClip
                            z: 25

                            // Duración
                            Label {
                                text: clipRect.clipLenSec.toFixed(2) + "s"
                                color: "white"
                                font.pixelSize: 9
                                font.bold: true
                                style: Text.Outline
                                styleColor: Qt.rgba(0, 0, 0, 0.6)
                                verticalAlignment: Text.AlignVCenter
                                height: 14
                            }

                            // Separador
                            Label {
                                text: "·"
                                color: Qt.rgba(1, 1, 1, 0.5)
                                font.pixelSize: 9
                                verticalAlignment: Text.AlignVCenter
                                height: 14
                                visible: clipRect.width > 100
                            }

                            // ── Gain en dB (scroll para ajustar, dblclick = reset) ──
                            Rectangle {
                                id: gainBadge
                                width: gainLabel.implicitWidth + 8
                                height: 14
                                radius: 3
                                visible: clipRect.width > 100
                                color: gainArea.containsMouse
                                    ? Qt.rgba(1, 1, 1, 0.25)
                                    : Qt.rgba(0, 0, 0, 0.35)

                                property real gainDb: clipRect._liveGain > 0.0001
                                    ? (20 * Math.log(clipRect._liveGain) / Math.LN10)
                                    : -120

                                // Peak dB del audio real del clip (calculado una vez)
                                property real peakDb: -120
                                Component.onCompleted: {
                                    peakDb = TrackModel.computeClipPeakDb(trackRoot.trackIndex, clipRect.index)
                                }

                                // ¿El gain ha sido ajustado manualmente?
                                property bool gainModified: Math.abs(gainDb) > 0.05

                                Label {
                                    id: gainLabel
                                    anchors.centerIn: parent
                                    text: {
                                        if (gainBadge.gainModified) {
                                            // Mostrar gain ajustable
                                            if (gainBadge.gainDb <= -120) return "-∞"
                                            var v = gainBadge.gainDb.toFixed(1)
                                            return (gainBadge.gainDb > 0 ? "+" : "") + v + "dB"
                                        }
                                        // Mostrar peak real del clip
                                        if (gainBadge.peakDb <= -120) return "-∞ pk"
                                        return gainBadge.peakDb.toFixed(1) + "pk"
                                    }
                                    color: {
                                        if (gainBadge.gainModified) {
                                            if (gainBadge.gainDb > 0.1) return "#fad138"
                                            if (gainBadge.gainDb < -0.1) return "#b0b0b0"
                                            return "white"
                                        }
                                        // Colores para peak
                                        if (gainBadge.peakDb > -1) return "#f24d3d"   // clipping
                                        if (gainBadge.peakDb > -6) return "#fad138"   // hot
                                        return Qt.rgba(0.6, 0.85, 1, 0.9)             // normal
                                    }
                                    font.pixelSize: 9
                                    font.bold: true
                                    style: Text.Outline
                                    styleColor: Qt.rgba(0, 0, 0, 0.6)
                                }

                                MouseArea {
                                    id: gainArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton

                                    // Scroll = ajustar gain (0.5 dB por step)
                                    onWheel: (wheel) => {
                                        var currentDb = gainBadge.gainDb
                                        if (currentDb <= -120) currentDb = -40
                                        var step = 0.5
                                        if (wheel.modifiers & Qt.ShiftModifier) step = 0.1
                                        var newDb = currentDb + (wheel.angleDelta.y > 0 ? step : -step)
                                        newDb = Math.max(-60, Math.min(12, newDb))
                                        var linear = Math.pow(10, newDb / 20.0)
                                        clipRect._liveGain = Math.max(0, Math.min(4.0, linear))
                                        // Commit después de un breve delay
                                        gainCommitTimer.restart()
                                        // Mostrar tooltip
                                        gainTooltip.visible = true
                                        gainTooltipHide.restart()
                                        wheel.accepted = true
                                    }

                                    // Doble-click = reset a 0 dB
                                    onDoubleClicked: {
                                        clipRect._liveGain = 1.0
                                        UndoManager.setClipGain(trackRoot.trackIndex, clipRect.index, 1.0)
                                    }
                                }

                                // Timer para hacer commit del gain tras dejar de scrollear
                                Timer {
                                    id: gainCommitTimer
                                    interval: 500
                                    onTriggered: {
                                        UndoManager.setClipGain(trackRoot.trackIndex, clipRect.index, clipRect._liveGain)
                                    }
                                }

                                // Timer para ocultar tooltip
                                Timer {
                                    id: gainTooltipHide
                                    interval: 1200
                                    onTriggered: gainTooltip.visible = false
                                }
                            }

                            // ── Botón Mute del clip ──
                            Rectangle {
                                id: muteBadge
                                width: 14; height: 14
                                radius: 3
                                visible: clipRect.width > 130
                                color: clipRect._liveMuted
                                    ? Qt.rgba(0.9, 0.2, 0.2, 0.85)
                                    : (muteArea.containsMouse
                                       ? Qt.rgba(1, 1, 1, 0.25)
                                       : Qt.rgba(0, 0, 0, 0.35))

                                Label {
                                    anchors.centerIn: parent
                                    text: "M"
                                    color: clipRect._liveMuted ? "white" : Qt.rgba(1, 1, 1, 0.7)
                                    font.pixelSize: 8
                                    font.bold: true
                                }

                                MouseArea {
                                    id: muteArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        clipRect._liveMuted = !clipRect._liveMuted
                                        TrackModel.setClipMuted(trackRoot.trackIndex, clipRect.index, clipRect._liveMuted)
                                    }
                                }
                            }
                        }

                        // ── Tooltip flotante de gain (aparece al scrollear) ──
                        Rectangle {
                            id: gainTooltip
                            visible: false
                            x: clipInfoBar.x + (gainBadge.visible ? gainBadge.x : 0)
                            y: clipInfoBar.y + clipInfoBar.height + 2
                            width: tooltipLabel.implicitWidth + 12
                            height: 20
                            radius: 4
                            z: 30
                            color: Qt.rgba(0.1, 0.1, 0.13, 0.92)
                            border.color: Qt.rgba(1, 1, 1, 0.15)

                            Label {
                                id: tooltipLabel
                                anchors.centerIn: parent
                                text: {
                                    var db = gainBadge.gainDb
                                    if (db <= -120) return "Gain: -∞ dB"
                                    return "Gain: " + (db > 0 ? "+" : "") + db.toFixed(1) + " dB"
                                }
                                color: "white"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        // ── Overlay de clip muteado ──
                        Rectangle {
                            anchors.fill: parent
                            radius: clipRect.radius
                            color: Qt.rgba(0, 0, 0, 0.4)
                            visible: clipRect._liveMuted
                            z: 10

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("MUTED")
                                color: Qt.rgba(1, 1, 1, 0.5)
                                font.pixelSize: 12
                                font.bold: true
                                visible: clipRect.width > 80
                            }
                        }

                        // -------- Handles de fade In / fade Out --------
                        // Valores "live" durante el drag — evitan llamar a
                        // setClipFades() en cada frame (lo que destruiría el
                        // delegate vía clipsChanged). Solo se empujan al
                        // modelo en onReleased.
                        property real _liveFadeInSec:  modelData ? (modelData.fadeInSec  || 0) : 0
                        property real _liveFadeOutSec: modelData ? (modelData.fadeOutSec || 0) : 0
                        property real _liveGain:       modelData ? (modelData.gain !== undefined ? modelData.gain : 1.0) : 1.0

                        property real gainY: {
                            const h = height - chapterHeaderHeight
                            return chapterHeaderHeight + Math.max(0, Math.min(h, h - (h * (_liveGain / 2.0))))
                        }
                        
                        // --- Left Trim Handle ---
                        // Durante el drag, solo actualizamos variables visuales
                        // (_trimLeftOffset, _trimRightOffset) que desplazan x y width
                        // del clip SIN tocar el modelo C++ en cada frame.
                        // El commit al modelo se hace una sola vez en onReleased.
                        property real _trimLeftOffset: 0   // px offset visual borde izquierdo
                        property real _trimRightOffset: 0  // px offset visual borde derecho

                        MouseArea {
                            id: leftTrimHandle
                            // Posición: siempre pegado al borde izquierdo visual actual
                            x: 0
                            y: clipRect.chapterHeaderHeight
                            width: 10
                            height: parent.height - clipRect.chapterHeaderHeight
                            z: 30
                            cursorShape: Qt.SizeHorCursor
                            hoverEnabled: true

                            property real pressSceneX: 0
                            property real pressLeftOffset: 0
                            property real originalStartSec: 0
                            property real originalLenSec: 0

                            onPressed: (mouse) => {
                                pressSceneX = mapToItem(clipsContainer, mouse.x, 0).x
                                pressLeftOffset = clipRect._trimLeftOffset
                                originalStartSec = clipRect.clipStartSec
                                originalLenSec = clipRect.liveLengthSec
                                // Bloquear drag normal del clip
                                mouse.accepted = true
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return
                                const curSceneX = mapToItem(clipsContainer, mouse.x, 0).x
                                const dx = curSceneX - pressSceneX
                                // Limitar: no puede pasar el borde derecho - 10px
                                const maxDx = parent.width + clipRect._trimRightOffset - 10
                                const clampedDx = Math.max(-originalStartSec / appRoot.secPerPixel,
                                                           Math.min(dx, maxDx))
                                clipRect._trimLeftOffset = pressLeftOffset + clampedDx
                            }
                            onReleased: {
                                const totalDeltaSec = clipRect._trimLeftOffset * appRoot.secPerPixel
                                if (Math.abs(totalDeltaSec) > 0.001) {
                                    UndoManager.trimClip(trackRoot.trackIndex, clipRect.index, totalDeltaSec, 0)
                                }
                                // Reset visual — el modelo ya actualizó modelData
                                clipRect._trimLeftOffset = 0
                            }

                            // Indicador visual de hover
                            Rectangle {
                                anchors.fill: parent
                                color: "white"
                                opacity: leftTrimHandle.containsMouse || leftTrimHandle.pressed ? 0.35 : 0.0
                                radius: 2
                                visible: false
                            }
                            // Línea de borde izquierdo visible
                            Rectangle {
                                visible: false
                                x: 4; y: 4; width: 2
                                height: parent.height - 8
                                color: "white"
                                opacity: 0.8
                                radius: 1
                            }
                        }

                        // --- Right Trim Handle ---
                        MouseArea {
                            id: rightTrimHandle
                            x: parent.width - 10
                            y: clipRect.chapterHeaderHeight
                            width: 10
                            height: parent.height - clipRect.chapterHeaderHeight
                            z: 30
                            cursorShape: Qt.SizeHorCursor
                            hoverEnabled: true

                            property real pressSceneX: 0
                            property real pressRightOffset: 0
                            property real originalLenSec: 0

                            onPressed: (mouse) => {
                                pressSceneX = mapToItem(clipsContainer, mouse.x, 0).x
                                pressRightOffset = clipRect._trimRightOffset
                                originalLenSec = clipRect.liveLengthSec
                                mouse.accepted = true
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return
                                const curSceneX = mapToItem(clipsContainer, mouse.x, 0).x
                                const dx = curSceneX - pressSceneX
                                // Limitar: no puede acortar a menos de 10px
                                const minDx = -(parent.width + clipRect._trimLeftOffset - 10)
                                clipRect._trimRightOffset = pressRightOffset + Math.max(dx, minDx)
                            }
                            onReleased: {
                                const totalDeltaSec = clipRect._trimRightOffset * appRoot.secPerPixel
                                if (Math.abs(totalDeltaSec) > 0.001) {
                                    UndoManager.trimClip(trackRoot.trackIndex, clipRect.index, 0, totalDeltaSec)
                                }
                                clipRect._trimRightOffset = 0
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: "white"
                                opacity: rightTrimHandle.containsMouse || rightTrimHandle.pressed ? 0.35 : 0.0
                                radius: 2
                                visible: false
                            }
                            Rectangle {
                                visible: false
                                x: 4; y: 4; width: 2
                                height: parent.height - 8
                                color: "white"
                                opacity: 0.8
                                radius: 1
                            }
                        }

                        readonly property real fadeInPx: {
                            const clipLenSec = modelData.lengthSec || 0
                            if (clipLenSec <= 0) return 0
                            return (_liveFadeInSec / clipLenSec) * width
                        }
                        readonly property real fadeOutPx: {
                            const clipLenSec = modelData.lengthSec || 0
                            if (clipLenSec <= 0) return 0
                            return (_liveFadeOutSec / clipLenSec) * width
                        }

                        Canvas {
                            id: envelopeCanvas
                            anchors.fill: parent
                            visible: width > 0 && height > 0 && (
                                clipRect.isGainModified || 
                                mouseZone.containsMouse || 
                                mouseZone.pressedInside || 
                                gainHandle.containsMouse || 
                                gainHandle.pressed
                            )
                            z: 15
                            antialiasing: true
                            renderStrategy: Canvas.Cooperative
                            renderTarget: Canvas.Image
                            // Track envelope node count so we repaint when nodes
                            // are added/removed (especially the last one — the
                            // Repeater may reuse the delegate without recreating
                            // the canvas, so the old blue lines would persist).
                            readonly property int envLen: {
                                const env = clipRect.modelData.envelope
                                if (!env) return 0
                                return env.count !== undefined ? env.count : (env.length || 0)
                            }
                            onEnvLenChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            Connections {
                                target: clipRect
                                function onFadeInPxChanged() { envelopeCanvas.requestPaint() }
                                function onFadeOutPxChanged() { envelopeCanvas.requestPaint() }
                                function onGainYChanged() { envelopeCanvas.requestPaint() }
                            }
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                if (width <= 0 || height <= 0) return
                                
                                const envelope = clipRect.modelData.envelope || []
                                const envLen = envelope.count !== undefined ? envelope.count : (envelope.length || 0)
                                const yOffset = clipRect.chapterHeaderHeight
                                const h = height - yOffset

                                if (envLen > 0) {
                                    const clipLenSec = clipRect.modelData.lengthSec || 0
                                    
                                    // Relleno inferior
                                    ctx.fillStyle = Qt.rgba(0, 0, 0, 0.4)
                                    ctx.beginPath()
                                    ctx.moveTo(0, height)
                                    const firstNode = envelope.get !== undefined ? envelope.get(0) : envelope[0]
                                    if (firstNode.x > 0) {
                                        let nodeY = firstNode.y
                                        if (0 === clipRect.dragNodeIndex) nodeY = clipRect.dragNodeGain
                                        const y0 = yOffset + h - (h * (nodeY / 2.0))
                                        ctx.lineTo(0, y0)
                                    }
                                    for (let i = 0; i < envLen; ++i) {
                                        const node = envelope.get !== undefined ? envelope.get(i) : envelope[i]
                                        let nodeX = node.x
                                        let nodeY = node.y
                                        if (i === clipRect.dragNodeIndex) {
                                            nodeX = clipRect.dragNodeSec
                                            nodeY = clipRect.dragNodeGain
                                        }
                                        const px = clipLenSec > 0 ? (nodeX / clipLenSec) * width : 0
                                        const py = yOffset + h - (h * (nodeY / 2.0))
                                        ctx.lineTo(px, py)
                                    }
                                    if (envLen > 0) {
                                        const lastNode = envelope.get !== undefined ? envelope.get(envLen - 1) : envelope[envLen - 1]
                                        let lastNodeY = lastNode.y
                                        if (envLen - 1 === clipRect.dragNodeIndex) lastNodeY = clipRect.dragNodeGain
                                        const py = yOffset + h - (h * (lastNodeY / 2.0))
                                        ctx.lineTo(width, py)
                                    }
                                    ctx.lineTo(width, height)
                                    ctx.closePath()
                                    ctx.fill()
 
                                    // Línea de envolvente
                                    ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.9)
                                    ctx.lineWidth = 1.5
                                    ctx.beginPath()
                                    if (firstNode.x > 0) {
                                        let nodeY = firstNode.y
                                        if (0 === clipRect.dragNodeIndex) nodeY = clipRect.dragNodeGain
                                        const y0 = yOffset + h - (h * (nodeY / 2.0))
                                        ctx.moveTo(0, y0)
                                    } else {
                                        let nodeY = firstNode.y
                                        if (0 === clipRect.dragNodeIndex) nodeY = clipRect.dragNodeGain
                                        const y0 = yOffset + h - (h * (nodeY / 2.0))
                                        ctx.moveTo(0, y0)
                                    }
                                    for (let i = 0; i < envLen; ++i) {
                                        const node = envelope.get !== undefined ? envelope.get(i) : envelope[i]
                                        let nodeX = node.x
                                        let nodeY = node.y
                                        if (i === clipRect.dragNodeIndex) {
                                            nodeX = clipRect.dragNodeSec
                                            nodeY = clipRect.dragNodeGain
                                        }
                                        const px = clipLenSec > 0 ? (nodeX / clipLenSec) * width : 0
                                        const py = yOffset + h - (h * (nodeY / 2.0))
                                        ctx.lineTo(px, py)
                                    }
                                    if (envLen > 0) {
                                        const lastNode = envelope.get !== undefined ? envelope.get(envLen - 1) : envelope[envLen - 1]
                                        let lastNodeY = lastNode.y
                                        if (envLen - 1 === clipRect.dragNodeIndex) lastNodeY = clipRect.dragNodeGain
                                        const py = yOffset + h - (h * (lastNodeY / 2.0))
                                        ctx.lineTo(width, py)
                                    }
                                    ctx.stroke()
                                } else {
                                    const fy = clipRect.gainY
                                    const fInX = clipRect.fadeInPx
                                    const fOutX = width - clipRect.fadeOutPx
 
                                    // Relleno inferior oscuro
                                    ctx.fillStyle = Qt.rgba(0, 0, 0, 0.4)
                                    ctx.beginPath()
                                    ctx.moveTo(0, height)
                                    ctx.lineTo(fInX, fy)
                                    ctx.lineTo(fOutX, fy)
                                    ctx.lineTo(width, height)
                                    ctx.lineTo(0, height)
                                    ctx.closePath()
                                    ctx.fill()
 
                                    // Línea de envolvente
                                    ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.9)
                                    ctx.lineWidth = 1.5
                                    ctx.beginPath()
                                    ctx.moveTo(0, height)
                                    ctx.lineTo(fInX, fy)
                                    ctx.lineTo(fOutX, fy)
                                    ctx.lineTo(width, height)
                                    ctx.stroke()
                                }
                            }
                        }



                        // --- Gain handle ---
                        MouseArea {
                            id: gainHandle
                            x: clipRect.fadeInPx + (clipRect.width - clipRect.fadeOutPx - clipRect.fadeInPx) / 2 - width / 2
                            y: clipRect.gainY - height / 2
                            width: 16
                            height: 16
                            visible: (modelData.envelope || []).length === 0 && (clipRect.isGainModified || mouseZone.containsMouse || mouseZone.pressedInside)
                            z: 50
                            hoverEnabled: true
                            cursorShape: Qt.SizeVerCursor

                            property real startAbsY: 0
                            property real startGain: 0

                            Rectangle {
                                anchors.centerIn: parent
                                width: 10
                                height: 10
                                radius: 5
                                color: gainHandle.containsMouse || gainHandle.pressed
                                    ? "#ffffff" : Qt.rgba(1, 1, 1, 0.8)
                                border.color: "#000"
                                border.width: 1
                                opacity: 0.9
                            }

                            onPressed: (mouse) => {
                                startAbsY = mapToItem(clipRect, 0, mouse.y).y
                                startGain = clipRect._liveGain
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return
                                const absY = mapToItem(clipRect, 0, mouse.y).y
                                const dy = absY - startAbsY
                                const h = clipRect.height - clipRect.chapterHeaderHeight
                                let newGain = startGain - (dy / h) * 2.0
                                clipRect._liveGain = Math.max(0, Math.min(newGain, 2.0))
                                startAbsY = absY
                                startGain = clipRect._liveGain
                            }
                            onReleased: {
                                const oldGain = modelData.gain !== undefined ? modelData.gain : 1.0
                                const newGain = clipRect._liveGain
                                if (Math.abs(oldGain - newGain) > 1e-4) {
                                    UndoManager.setClipGain(trackRoot.trackIndex, clipRect.index, newGain)
                                }
                            }
                        }

                        // --- Nodos de Envolvente ---
                        Repeater {
                            model: modelData.envelope || []
                            delegate: MouseArea {
                                id: nodeArea
                                required property var modelData
                                required property int index
                                // Usar estado live durante el arrastre
                                readonly property bool isDragging: nodeArea.pressed
                                
                                readonly property real clipLenSec: clipRect.modelData.lengthSec || 0
                                readonly property real px: clipLenSec > 0 ? (isDragging ? liveNodeSec : modelData.x) / clipLenSec * clipRect.width : 0
                                readonly property real py: clipRect.chapterHeaderHeight + (clipRect.height - clipRect.chapterHeaderHeight) * (1.0 - (isDragging ? liveNodeGain : modelData.y) / 2.0)
                                
                                x: px - width / 2
                                y: py - height / 2
                                width: 14
                                height: 14
                                z: 60
                                hoverEnabled: true
                                cursorShape: Qt.SizeAllCursor

                                // Coordenadas absolutas dentro del clip (no relativas al MouseArea que se mueve)
                                property real startAbsX: 0
                                property real startAbsY: 0
                                property real startNodeX: 0
                                property real startNodeY: 0
                                property real liveNodeSec: modelData.x
                                property real liveNodeGain: modelData.y

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: nodeArea.containsMouse || nodeArea.pressed ? "#ffffff" : Qt.rgba(0.2, 0.6, 1.0, 0.9)
                                    border.color: "#000"
                                    border.width: 1
                                    opacity: 0.9
                                }

                                onPressed: (mouse) => {
                                    // Capturar posición absoluta dentro del clip
                                    const abs = mapToItem(clipRect, mouse.x, mouse.y)
                                    startAbsX = abs.x
                                    startAbsY = abs.y
                                    startNodeX = modelData.x
                                    startNodeY = modelData.y
                                    liveNodeSec = modelData.x
                                    liveNodeGain = modelData.y
                                }
                                onPositionChanged: (mouse) => {
                                    if (!pressed) return
                                    // Siempre usar coordenadas absolutas del clip
                                    const abs = mapToItem(clipRect, mouse.x, mouse.y)
                                    const dx = abs.x - startAbsX
                                    const dy = abs.y - startAbsY
                                    
                                    const pxPerSec = clipLenSec > 0 ? clipRect.width / clipLenSec : 0
                                    const pxPerGain = (clipRect.height - clipRect.chapterHeaderHeight) / 2.0
                                    
                                    let newSec = startNodeX + (pxPerSec > 0 ? dx / pxPerSec : 0)
                                    newSec = Math.max(0, Math.min(newSec, clipLenSec))
                                    
                                    let newGain = startNodeY - (dy / pxPerGain)
                                    newGain = Math.max(0, Math.min(newGain, 2.0))
                                    
                                    liveNodeSec = newSec
                                    liveNodeGain = newGain
                                    
                                    // Informar a envelopeCanvas para dibujar la línea en vivo
                                    clipRect.dragNodeIndex = index
                                    clipRect.dragNodeSec = newSec
                                    clipRect.dragNodeGain = newGain
                                    envelopeCanvas.requestPaint()
                                }
                                onReleased: {
                                    clipRect.dragNodeIndex = -1
                                    envelopeCanvas.requestPaint()
                                    if (Math.abs(liveNodeSec - startNodeX) > 1e-4 || Math.abs(liveNodeGain - startNodeY) > 1e-4) {
                                        UndoManager.setEnvelopeNode(trackRoot.trackIndex, clipRect.index, index, liveNodeSec, liveNodeGain)
                                    }
                                }
                                onClicked: (mouse) => {
                                    if (((mouse.modifiers & Qt.AltModifier) !== 0) ||
                                        (((mouse.modifiers & Qt.ControlModifier) !== 0) && ((mouse.modifiers & Qt.ShiftModifier) !== 0))) {
                                        UndoManager.removeEnvelopeNode(trackRoot.trackIndex, clipRect.index, index)
                                    }
                                }
                            }
                        }
                    }
                }
            }


            // -------- Región seleccionada (overlay semitransparente) --------
            Rectangle {
                id: selectionOverlay
                visible: appRoot.hasSelection
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                x: {
                    const total = clipArea.displayDuration
                    return total > 0 ? (appRoot.selStart / total) * timelineInner.width : 0
                }
                width: {
                    const total = clipArea.displayDuration
                    return total > 0
                        ? ((appRoot.selEnd - appRoot.selStart) / total) * timelineInner.width
                        : 0
                }
                color: Qt.rgba(trackRoot.accentColor.r,
                               trackRoot.accentColor.g,
                               trackRoot.accentColor.b, 0.20)
                border.color: trackRoot.accentColor
                border.width: 1
                z: 5
            }

            // -------- Chapter overlays drawn directly on top of the track --------
            Repeater {
                model: (appRoot && appRoot.chapterModel) ? appRoot.chapterModel : null
                delegate: Item {
                    id: trackChapterCol
                    required property int index
                    required property double startSec
                    required property double endSec
                    required property string title

                    // Shifted to timeline coordinates via secToViewX
                    x: appRoot.secToViewX(startSec)
                    y: 0
                    height: timelineInner.height
                    
                    // Width is determined from start to end of chapter
                    width: Math.max(0, appRoot.secToViewX(endSec) - x)
                    visible: width > 0
                    
                    // Completely ignore all mouse events so clicks pass through to clips
                    enabled: false
                    z: 8

                    // Vertical division line at the left boundary
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 1
                        color: {
                            var markerColors = ["#0ea5e9", "#8b5cf6", "#e67e22", "#2ecc71", "#e74c3c", "#f1c40f", "#1abc9c", "#9b59b6"]
                            return markerColors[trackChapterCol.index % markerColors.length]
                        }
                        opacity: 0.4
                        visible: trackChapterCol.index > 0 // don't draw line at the absolute start (0)
                    }

                    // Header bar at the top of the track
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 24
                        color: {
                            var markerColors = ["#0ea5e9", "#8b5cf6", "#e67e22", "#2ecc71", "#e74c3c", "#f1c40f", "#1abc9c", "#9b59b6"]
                            var baseColor = markerColors[trackChapterCol.index % markerColors.length]
                            // Semi-transparent matching color (opacity 0.25)
                            return Qt.rgba(
                                parseInt(baseColor.substring(1, 3), 16) / 255,
                                parseInt(baseColor.substring(3, 5), 16) / 255,
                                parseInt(baseColor.substring(5, 7), 16) / 255,
                                0.25
                            )
                        }
                        border.color: {
                            var markerColors = ["#0ea5e9", "#8b5cf6", "#e67e22", "#2ecc71", "#e74c3c", "#f1c40f", "#1abc9c", "#9b59b6"]
                            var baseColor = markerColors[trackChapterCol.index % markerColors.length]
                            return Qt.rgba(
                                parseInt(baseColor.substring(1, 3), 16) / 255,
                                parseInt(baseColor.substring(3, 5), 16) / 255,
                                parseInt(baseColor.substring(5, 7), 16) / 255,
                                0.5
                            )
                        }
                        border.width: 1

                        Label {
                            text: trackChapterCol.title
                            color: "#ffffff"
                            font.pixelSize: 10
                            font.bold: true
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }

        } // timelineInner

        // -------- Placeholder si no hay audio --------
        Label {
            anchors.centerIn: parent
            visible: !trackRoot.hasAudio
            text: trackRoot.isArmed
                  ? qsTr("Lista para grabar — pulsa ● Grabar en la barra superior")
                  : qsTr("Sin audio — arma la pista (●) para poder grabar")
            color: trackRoot.subtextColor
            font.italic: true
            font.pixelSize: 12
            
            // Make the placeholder text clickable for track selection
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    trackRoot.appRoot.forceSelectTrack(trackRoot.trackIndex)
                }
            }
        }

        // -------- Input de fondo del timeline --------
        // Click = seek, rueda con Ctrl = zoom, rueda sin Ctrl = scroll.
        // La selección temporal se hace desde la Ruler (arriba), NO aquí.
        // Queda detrás de los clips: si el click cae sobre un clip, los
        // MouseArea de ese clip (con z mayor) se llevan el evento antes.
        MouseArea {
            id: timelineMA
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: _regionDragLocal ? Qt.CrossCursor : Qt.IBeamCursor
            z: -1   // detrás de los clips y del playhead

            property bool _regionDragLocal: false
            property real _regionDragSecLocal: 0

            function xToSec(x) {
                return appRoot.viewXToSec(x)
            }

            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (trackRoot.appRoot.blockContextMenu) return
                    trackRoot.contextClipIndex = -1
                     clipMenu.open()
                     return
                }
                if ((mouse.modifiers & Qt.ShiftModifier) !== 0) {
                    _regionDragLocal = true
                    _regionDragSecLocal = xToSec(mouse.x)
                    appRoot.setTrackRegion(trackRoot.trackIndex,
                        _regionDragSecLocal, _regionDragSecLocal)
                    return
                }
                // Left click en zona vacía: mover cursor Y seleccionar pista
                trackRoot.appRoot.forceSelectTrack(trackRoot.trackIndex)
                AudioEngine.seekTime(xToSec(mouse.x))
                // Seek = reactivar auto-scroll
                appRoot.isUserInteracting = false
            }
            onPositionChanged: (mouse) => {
                if (!pressed || !_regionDragLocal) return
                appRoot.setTrackRegion(trackRoot.trackIndex,
                    _regionDragSecLocal, xToSec(mouse.x))
            }
            onReleased: (mouse) => {
                _regionDragLocal = false
            }

            onWheel: (wheel) => {
                clipArea.handleWheel(wheel, wheel.x)
            }

            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    AudioEngine.play()
                }
            }
        }


        // Overlay visual de la región por-pista de ESTA pista.
        Rectangle {
            visible: {
                const r = appRoot.trackRegions ? appRoot.trackRegions[trackRoot.trackIndex] : undefined
                return r !== undefined && r !== null && r.end > r.start
            }
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: {
                const r = appRoot.trackRegions[trackRoot.trackIndex]
                if (!r) return 0
                const total = clipArea.displayDuration
                return total > 0
                    ? (r.start / total) * timelineInner.width - appRoot.timelineScrollX
                    : 0
            }
            width: {
                const r = appRoot.trackRegions[trackRoot.trackIndex]
                if (!r) return 0
                const total = clipArea.displayDuration
                return total > 0
                    ? ((r.end - r.start) / total) * timelineInner.width
                    : 0
            }
            color: Qt.rgba(trackRoot.accentColor.r,
                           trackRoot.accentColor.g,
                           trackRoot.accentColor.b, 0.28)
            border.color: trackRoot.accentColor
            border.width: 1
            z: 40
        }

        Platform.Menu {
            id: clipMenu
            onAboutToHide: trackRoot.appRoot.restartMenuBlockTimer()
            Platform.MenuItem {
                text: qsTr("Seleccionar pista")
                onTriggered: trackRoot.appRoot.selectTrack(trackRoot.trackIndex)
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Eliminar selección  (Supr)")
                enabled: appRoot.hasSelection
                onTriggered: appRoot.deleteSelectedRegion()
            }
            Platform.MenuItem {
                text: qsTr("Quitar selección  (Esc)")
                enabled: appRoot.hasSelection
                onTriggered: appRoot.clearSelection()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Dividir clip en el cursor  (S)")
                onTriggered: appRoot.splitAtPlayhead()
            }
            Platform.MenuItem {
                text: {
                    const ci = trackRoot.contextClipIndex
                    const clip = (ci >= 0 && ci < clipsModel.count) ? clipsModel.get(ci) : null
                    const fi = clip ? (clip.fadeInSec || 0) : 0
                    return fi > 0 ? qsTr("Quitar Fade In (%1 s)").arg(fi.toFixed(2)) : qsTr("Fade In (0.50 s)")
                }
                enabled: trackRoot.contextClipIndex >= 0
                onTriggered: {
                    const ci = trackRoot.contextClipIndex
                    if (ci < 0) return
                    const curIn  = TrackModel.clipFadeInSec(trackRoot.trackIndex, ci)
                    const curOut = TrackModel.clipFadeOutSec(trackRoot.trackIndex, ci)
                    const newIn  = curIn > 0 ? 0.0 : 0.5
                    UndoManager.setClipFades(trackRoot.trackIndex, ci, newIn, curOut)
                }
            }
            Platform.Menu {
                title: qsTr("Ajustar Fade In")
                enabled: trackRoot.contextClipIndex >= 0
                Platform.MenuItem {
                    text: "0.10 s"
                    onTriggered: trackRoot.setFadeInPreset(0.10)
                }
                Platform.MenuItem {
                    text: "0.25 s"
                    onTriggered: trackRoot.setFadeInPreset(0.25)
                }
                Platform.MenuItem {
                    text: "0.50 s"
                    onTriggered: trackRoot.setFadeInPreset(0.50)
                }
                Platform.MenuItem {
                    text: "1.00 s"
                    onTriggered: trackRoot.setFadeInPreset(1.00)
                }
                Platform.MenuItem {
                    text: "2.00 s"
                    onTriggered: trackRoot.setFadeInPreset(2.00)
                }
                Platform.MenuItem {
                    text: "3.00 s"
                    onTriggered: trackRoot.setFadeInPreset(3.00)
                }
                Platform.MenuItem {
                    text: "5.00 s"
                    onTriggered: trackRoot.setFadeInPreset(5.00)
                }
                Platform.MenuSeparator {}
                Platform.MenuItem {
                    text: qsTr("Personalizado...")
                    onTriggered: {
                        const ci = trackRoot.contextClipIndex
                        if (ci >= 0) appRoot.openCustomFadeDialog(trackRoot.trackIndex, ci, true)
                    }
                }
            }
            Platform.MenuItem {
                text: {
                    const ci = trackRoot.contextClipIndex
                    const clip = (ci >= 0 && ci < clipsModel.count) ? clipsModel.get(ci) : null
                    const fo = clip ? (clip.fadeOutSec || 0) : 0
                    return fo > 0 ? qsTr("Quitar Fade Out (%1 s)").arg(fo.toFixed(2)) : qsTr("Fade Out (0.50 s)")
                }
                enabled: trackRoot.contextClipIndex >= 0
                onTriggered: {
                    const ci = trackRoot.contextClipIndex
                    if (ci < 0) return
                    const curIn  = TrackModel.clipFadeInSec(trackRoot.trackIndex, ci)
                    const curOut = TrackModel.clipFadeOutSec(trackRoot.trackIndex, ci)
                    const newOut = curOut > 0 ? 0.0 : 0.5
                    UndoManager.setClipFades(trackRoot.trackIndex, ci, curIn, newOut)
                }
            }
            Platform.Menu {
                title: qsTr("Ajustar Fade Out")
                enabled: trackRoot.contextClipIndex >= 0
                Platform.MenuItem {
                    text: "0.10 s"
                    onTriggered: trackRoot.setFadeOutPreset(0.10)
                }
                Platform.MenuItem {
                    text: "0.25 s"
                    onTriggered: trackRoot.setFadeOutPreset(0.25)
                }
                Platform.MenuItem {
                    text: "0.50 s"
                    onTriggered: trackRoot.setFadeOutPreset(0.50)
                }
                Platform.MenuItem {
                    text: "1.00 s"
                    onTriggered: trackRoot.setFadeOutPreset(1.00)
                }
                Platform.MenuItem {
                    text: "2.00 s"
                    onTriggered: trackRoot.setFadeOutPreset(2.00)
                }
                Platform.MenuItem {
                    text: "3.00 s"
                    onTriggered: trackRoot.setFadeOutPreset(3.00)
                }
                Platform.MenuItem {
                    text: "5.00 s"
                    onTriggered: trackRoot.setFadeOutPreset(5.00)
                }
                Platform.MenuSeparator {}
                Platform.MenuItem {
                    text: qsTr("Personalizado...")
                    onTriggered: {
                        const ci = trackRoot.contextClipIndex
                        if (ci >= 0) appRoot.openCustomFadeDialog(trackRoot.trackIndex, ci, false)
                    }
                }
            }
            Platform.MenuItem {
                text: qsTr("Quitar automatización de volumen")
                enabled: {
                    const ci = trackRoot.contextClipIndex
                    const clip = (ci >= 0 && ci < clipsModel.count) ? clipsModel.get(ci) : null
                    return clip && (clip.envelope || []).length > 0
                }
                onTriggered: {
                    const ci = trackRoot.contextClipIndex
                    if (ci >= 0) {
                        UndoManager.clearClipEnvelope(trackRoot.trackIndex, ci)
                    }
                }
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Borrar audio de esta pista")
                enabled: trackRoot.hasAudio
                onTriggered: trackRoot.requestClearAudio()
            }
            Platform.MenuItem {
                text: qsTr("Eliminar espacios de esta pista")
                enabled: trackRoot.hasAudio
                onTriggered: UndoManager.removeGaps(trackRoot.trackIndex)
            }
            Platform.MenuItem {
                text: qsTr("Eliminar espacios de todas las pistas")
                onTriggered: UndoManager.removeGaps(-1)
            }
            Platform.MenuSeparator {}
            Platform.Menu {
                title: qsTr("Normalizar")
                Platform.MenuItem {
                    text: qsTr("Clip dinámicamente (%1 LUFS)").arg(
                        appRoot.dynamicNormTargetLufs.toFixed(1))
                    enabled: trackRoot.contextClipIndex >= 0
                    onTriggered: UndoManager.dynamicNormalizeClip(
                        trackRoot.trackIndex, trackRoot.contextClipIndex, appRoot.dynamicNormTargetLufs)
                }
                Platform.MenuItem {
                    text: qsTr("Clip a pico (-1.0 dB)")
                    enabled: trackRoot.contextClipIndex >= 0
                    onTriggered: UndoManager.normalizeClipPeak(
                        trackRoot.trackIndex, trackRoot.contextClipIndex, -1.0)
                }
                Platform.MenuSeparator {}
                Platform.MenuItem {
                    text: qsTr("Pista dinámicamente (%1 LUFS)").arg(
                        appRoot.dynamicNormTargetLufs.toFixed(1))
                    enabled: trackRoot.hasAudio
                    onTriggered: UndoManager.dynamicNormalizeTrack(
                        trackRoot.trackIndex, appRoot.dynamicNormTargetLufs)
                }
                Platform.MenuItem {
                    text: qsTr("Pista a pico (-1.0 dB)")
                    enabled: trackRoot.hasAudio
                    onTriggered: UndoManager.normalizeTrackPeak(
                        trackRoot.trackIndex, -1.0)
                }
            }
            Platform.MenuItem {
                text: qsTr("Autodetectar y eliminar zumbido (Hum)")
                enabled: trackRoot.hasAudio
                onTriggered: {
                    let start = appRoot.hasSelection ? appRoot.selStart : 0
                    let end = appRoot.hasSelection ? appRoot.selEnd : trackRoot.duration
                    let freq = TrackModel.detectHumFrequency(trackRoot.trackIndex, start, end)
                    let fxChain = TrackModel.trackFxChain(trackRoot.trackIndex)
                    if (fxChain && fxChain.notchFilter) {
                        fxChain.notchFilter.setFrequency(freq)
                        fxChain.notchFilter.setEnabled(true)
                        appRoot.infoDialog.message = qsTr("Zumbido detectado y atenuado a %1 Hz.").arg(freq.toFixed(1))
                        appRoot.infoDialog.open()
                    }
                }
            }
            Platform.MenuItem {
                text: qsTr("Eliminar clics (De-Clicker)...")
                enabled: trackRoot.hasAudio
                onTriggered: {
                    let start = appRoot.hasSelection ? appRoot.selStart : 0
                    let end = appRoot.hasSelection ? appRoot.selEnd : trackRoot.duration
                    appRoot.deClickerDialog.trackIndex = trackRoot.trackIndex
                    appRoot.deClickerDialog.startSec = start
                    appRoot.deClickerDialog.endSec = end
                    appRoot.deClickerDialog.open()
                }
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Unir clips (mantener selección)")
                enabled: {
                    let count = 0
                    for (let i = 0; i < appRoot.selectedClips.length; i++) {
                        if (appRoot.selectedClips[i].track === trackRoot.trackIndex) count++
                    }
                    return count > 1
                }
                onTriggered: {
                    let indices = []
                    for (let i = 0; i < appRoot.selectedClips.length; i++) {
                        if (appRoot.selectedClips[i].track === trackRoot.trackIndex)
                            indices.push(appRoot.selectedClips[i].clip)
                    }
                    if (indices.length > 1)
                        UndoManager.mergeClips(trackRoot.trackIndex, indices)
                }
            }
            Platform.MenuItem {
                text: qsTr("Unir clips (deseleccionar)")
                enabled: {
                    let count = 0
                    const selClips = trackRoot.appRoot.selectedClips
                    for (let i = 0; i < selClips.length; i++) {
                        if (selClips[i].track === trackRoot.trackIndex)
                            count++
                    }
                    return count >= 2
                }
                onTriggered: {
                    let indices = []
                    const selClips = trackRoot.appRoot.selectedClips
                    for (let i = 0; i < selClips.length; i++) {
                        if (selClips[i].track === trackRoot.trackIndex)
                            indices.push(selClips[i].clip)
                    }
                    if (indices.length >= 2) {
                        UndoManager.mergeClips(trackRoot.trackIndex, indices)
                        trackRoot.appRoot.clearClipSelection()
                    }
                }
            }
            Platform.MenuItem {
                text: qsTr("Unir todos los clips de la pista")
                enabled: trackRoot.hasAudio
                onTriggered: UndoManager.mergeAllClips(trackRoot.trackIndex)
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Mover pista arriba")
                enabled: trackRoot.trackIndex > 0
                onTriggered: UndoManager.moveTrack(trackRoot.trackIndex, trackRoot.trackIndex - 1)
            }
            Platform.MenuItem {
                text: qsTr("Mover pista abajo")
                enabled: trackRoot.trackIndex < TrackModel.count - 1
                onTriggered: UndoManager.moveTrack(trackRoot.trackIndex, trackRoot.trackIndex + 1)
            }
        }
    }

    // Handle de resize vertical en el borde inferior de la pista.
    // Arrastrando hacia abajo la pista crece, hacia arriba se encoge.
    Rectangle {
        id: resizeHandle
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 5
        color: "transparent"
        z: 100

        MouseArea {
            id: resizeMA
            anchors.fill: parent
            cursorShape: Qt.SplitVCursor
            hoverEnabled: true

            property real pressGlobalY: 0
            property real pressHeight: 0

            onPressed: (mouse) => {
                const global = mapToItem(null, mouse.x, mouse.y)
                pressGlobalY = global.y
                pressHeight = trackRoot.trackHeight
                trackRoot.isResizing = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const global = mapToItem(null, mouse.x, mouse.y)
                const dy = global.y - pressGlobalY
                trackRoot.trackHeight = Math.max(
                    trackRoot.minTrackHeight,
                    Math.min(trackRoot.maxTrackHeight, pressHeight + dy)
                )
            }
            onReleased: {
                trackRoot.isResizing = false
            }
        }
    }
}
