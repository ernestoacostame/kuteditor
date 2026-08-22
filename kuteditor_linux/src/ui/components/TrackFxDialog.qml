import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

/**
 * TrackFxDialog: ventana de efectos por pista, estilo Reaper.
 *
 * Ventana independiente (no modal) que flota sobre la app.
 * No se esconde al perder el foco.
 */
Window {
    id: fxDialog
    width: 580
    height: 480
    flags: Qt.Window
    color: "#22252a"

    function open() { visible = true; raise() }
    function close() { visible = false }

    property int trackIndex: -1
    property string trackName: ""
    property var fxChain: null
    property int selectedFx: -1

    title: qsTr("FX: Pista %1 \"%2\"").arg(trackIndex + 1).arg(trackName)

    onTrackIndexChanged: {
        if (trackIndex >= 0)
            fxChain = TrackModel.trackFxChain(trackIndex)
        else
            fxChain = null
        selectedFx = -1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Panel izquierdo: lista de efectos ---
        Rectangle {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            color: "#1a1d21"
            border.color: "#3c4146"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#2a2d32"
                    Label {
                        anchors.centerIn: parent
                        text: qsTr("Efectos")
                        color: "#ccc"
                        font.bold: true
                        font.pixelSize: 11
                    }
                }

                // Lista
                ListView {
                    id: fxListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: fxDialog.fxChain ? fxDialog.fxChain.effectCount() : 0
                    currentIndex: fxDialog.selectedFx

                    delegate: Rectangle {
                        width: fxListView.width
                        height: 28
                        color: index === fxDialog.selectedFx
                            ? "#3498db33" : (hoverMA.containsMouse ? "#ffffff11" : "transparent")
                        border.color: index === fxDialog.selectedFx ? "#3498db" : "transparent"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            spacing: 6

                            CheckBox {
                                id: fxCheck
                                checked: fxDialog.fxChain ? fxDialog.fxChain.effectEnabled(index) : false
                                onToggled: {
                                    if (fxDialog.fxChain)
                                        fxDialog.fxChain.setEffectEnabled(index, checked)
                                }
                                // Reactualizar cuando cambia cualquier efecto
                                Connections {
                                    target: fxDialog.fxChain
                                    function onChanged() {
                                        fxCheck.checked = fxDialog.fxChain
                                            ? fxDialog.fxChain.effectEnabled(index) : false
                                    }
                                }
                                indicator: Rectangle {
                                    implicitWidth: 14; implicitHeight: 14
                                    radius: 2; y: (parent.height - height) / 2
                                    color: fxCheck.checked ? "#e74c3c" : "#333"
                                    border.color: fxCheck.checked ? "#c0392b" : "#555"
                                    Label {
                                        anchors.centerIn: parent
                                        text: "✓"; color: "#fff"; font.pixelSize: 10
                                        visible: fxCheck.checked
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: fxDialog.fxChain ? fxDialog.fxChain.effectName(index) : ""
                                color: fxCheck.checked ? "#eee" : "#888"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: hoverMA
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            onClicked: fxDialog.selectedFx = index
                        }
                    }
                }

                // Barra de presets
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: "#2a2d32"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        Button {
                            text: qsTr("Guardar")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            font.pixelSize: 10
                            onClicked: presetNameDialog.open()
                            background: Rectangle { color: "#3d4146"; radius: 3 }
                            contentItem: Label {
                                anchors.centerIn: parent
                                text: parent.text; color: "#ccc"
                                font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        Button {
                            text: qsTr("Cargar")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            font.pixelSize: 10
                            onClicked: {
                                presetListDialog.refreshPresets()
                                presetListDialog.open()
                            }
                            background: Rectangle { color: "#3d4146"; radius: 3 }
                            contentItem: Label {
                                anchors.centerIn: parent
                                text: parent.text; color: "#ccc"
                                font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        // --- Panel derecho: parámetros del efecto seleccionado ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#22252a"
            border.color: "#3c4146"
            border.width: 1

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true

                Column {
                    width: parent.width
                    spacing: 10

                    // Nada seleccionado
                    Label {
                        visible: fxDialog.selectedFx < 0
                        text: qsTr("Selecciona un efecto de la lista para ver sus parámetros.")
                        color: "#888"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    // Contenido dinámico según el efecto seleccionado
                    Loader {
                        id: fxParamsLoader
                        width: parent.width
                        active: fxDialog.selectedFx >= 0 && fxDialog.fxChain !== null
                        sourceComponent: {
                            if (!active) return null
                            var name = fxDialog.fxChain ? fxDialog.fxChain.effectName(fxDialog.selectedFx) : ""
                            if (name === "Compresor")           return compressorVisualComponent
                            if (name === "Puerta de ruido")     return noiseGateVisualComponent
                            if (name === "Ecualizador")          return equalizerVisualComponent
                            if (name === "Reductor de ruido")    return denoiserVisualComponent
                            if (name === "Deep Denoise")        return deepFilterComponent
                            if (name === "Inversión de fase")    return phaseInvertComponent
                            if (name === "Auto Duck")            return autoDuckComponent
                            if (name === "Truncar silencio")     return silenceTrimComponent
                            // Todos los demás usan FxKnobPanel genérico
                            return fxKnobComponent
                        }
                    }
                }
            }
        }
    }

    // ── Tabla de configuraciones de knobs por efecto ─────────────────────
    function fxKnobConfig(name) {
        var C = {
            "Ganancia (Trim)": {
                accent: "#cccccc",
                knobs: [
                    {prop:"gainDb", label:"GAIN", from:-24, to:24, unit:" dB", color:"#ffffff", decimals:1, defaultValue:0}
                ]
            },
            "Filtro paso alto": {
                accent: "#e67e22",
                knobs: [
                    {prop:"cutoffHz", label:"CUTOFF", from:20, to:500, unit:" Hz", color:"#e67e22", decimals:0, defaultValue:20}
                ]
            },
            "Filtro paso bajo": {
                accent: "#3498db",
                knobs: [
                    {prop:"cutoffHz", label:"CUTOFF", from:1000, to:20000, unit:" Hz", color:"#3498db", decimals:0, defaultValue:20000}
                ]
            },
            "Filtro Notch": {
                accent: "#e74c3c",
                knobs: [
                    {prop:"frequency", label:"FREQ",  from:20, to:20000, unit:" Hz", color:"#e74c3c", decimals:0, defaultValue:1000},
                    {prop:"q",         label:"Q",     from:0.5, to:30, unit:"",    color:"#c0392b", decimals:1, defaultValue:1.0}
                ]
            },
            "Ensanchador estéreo": {
                accent: "#1abc9c",
                knobs: [
                    {prop:"width", label:"WIDTH", from:0, to:2, unit:"", color:"#1abc9c", decimals:2, defaultValue:1.0}
                ]
            },
            "Puerta de ruido": {
                accent: "#2ecc71",
                knobs: [
                    {prop:"thresholdDb", label:"THRESH",  from:-80, to:0,   unit:" dB", color:"#2ecc71",  decimals:0, defaultValue:-40},
                    {prop:"attackMs",    label:"ATTACK",  from:0.1, to:50,  unit:" ms", color:"#27ae60",  decimals:1, defaultValue:1},
                    {prop:"holdMs",      label:"HOLD",    from:0,   to:500, unit:" ms", color:"#1e8449",  decimals:0, defaultValue:50},
                    {prop:"releaseMs",   label:"RELEASE", from:1,   to:500, unit:" ms", color:"#145a32",  decimals:0, defaultValue:50},
                    {prop:"rangeDb",     label:"RANGE",   from:-80, to:0,   unit:" dB", color:"#a9cce3",  decimals:0, defaultValue:-80}
                ]
            },
            "De-esser (Sibilantes)": {
                accent: "#f1c40f",
                knobs: [
                    {prop:"frequency",   label:"FREQ",    from:2000, to:12000, unit:" Hz", color:"#f1c40f", decimals:0, defaultValue:6000},
                    {prop:"thresholdDb", label:"THRESH",  from:-40,  to:0,     unit:" dB", color:"#d4ac0d", decimals:0, defaultValue:-20},
                    {prop:"ratio",       label:"RATIO",   from:1,    to:10,    unit:"",    color:"#b7950b", decimals:1, defaultValue:2},
                    {prop:"releaseMs",   label:"RELEASE", from:1,    to:200,   unit:" ms", color:"#9a7d0a", decimals:0, defaultValue:50}
                ]
            },
            "Expansor": {
                accent: "#9b59b6",
                knobs: [
                    {prop:"thresholdDb", label:"THRESH",  from:-80, to:0,   unit:" dB", color:"#9b59b6", decimals:0, defaultValue:-40},
                    {prop:"ratio",       label:"RATIO",   from:1,   to:10,  unit:"",    color:"#8e44ad", decimals:1, defaultValue:2},
                    {prop:"attackMs",    label:"ATTACK",  from:0.1, to:100, unit:" ms", color:"#7d3c98", decimals:1, defaultValue:10},
                    {prop:"releaseMs",   label:"RELEASE", from:1,   to:500, unit:" ms", color:"#6c3483", decimals:0, defaultValue:100}
                ]
            },
            "Limitador": {
                accent: "#e74c3c",
                knobs: [
                    {prop:"thresholdDb", label:"CEILING", from:-12, to:0, unit:" dB", color:"#e74c3c", decimals:1, defaultValue:-1}
                ]
            },
            "Ganancia automática": {
                accent: "#2ecc71",
                knobs: [
                    {prop:"targetDb",   label:"TARGET",   from:-30, to:0,    unit:" dB", color:"#2ecc71", decimals:0, defaultValue:-18},
                    {prop:"responseMs", label:"RESPONSE", from:50,  to:3000, unit:" ms", color:"#27ae60", decimals:0, defaultValue:500},
                    {prop:"maxGainDb",  label:"MAX GAIN", from:0,   to:24,   unit:" dB", color:"#1e8449", decimals:0, defaultValue:12}
                ]
            },
            "Mezcla mono": {
                accent: "#95a5a6",
                knobs: []
            }
        }
        return C[name] || {accent: "#3498db", knobs: []}
    }

    // ── Componente genérico de knobs (dispatch principal) ───────────────────
    Component {
        id: fxKnobComponent
        FxKnobPanel {
            property var _fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null
            property string _name: fxDialog.fxChain ? fxDialog.fxChain.effectName(fxDialog.selectedFx) : ""
            property var _cfg: fxKnobConfig(_name)
            fx: _fx
            fxName: _name
            knobs: _cfg.knobs
            accentColor: _cfg.accent
        }
    }

    // ── Inversión de fase (booleanos L/R) ────────────────────────────────────
    Component {
        id: phaseInvertComponent
        FxKnobPanel {
            property var _fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null
            fx: _fx
            fxName: "Inversión de fase"
            accentColor: "#e74c3c"
            knobs: [
                {prop:"invertLeft",  label:"Invertir canal L", isBool:true},
                {prop:"invertRight", label:"Invertir canal R", isBool:true}
            ]
        }
    }

    // ── Auto Duck (auto-detección + knobs horizontales) ──────────────────────
    Component {
        id: autoDuckComponent
        Column {
            spacing: 12; width: parent ? parent.width : 400
            property var fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null

            // Header
            Row {
                spacing: 12
                Rectangle {
                    width: 48; height: 22; radius: 4
                    color: fx && fx.enabled ? Qt.rgba(0.9,0.6,0.1,0.22) : Qt.rgba(1,1,1,0.06)
                    border.color: fx && fx.enabled ? "#e67e22" : "#484e56"
                    Label { anchors.centerIn:parent; text:fx && fx.enabled ? "ON":"OFF"; color:fx && fx.enabled ? "#e67e22":"#666"; font.bold:true; font.pixelSize:10; font.letterSpacing:1 }
                    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:{ if(fx) fx.setEnabled(!fx.enabled) } }
                }
                Label { text:"Auto Duck"; color:"#e8eaed"; font.bold:true; font.pixelSize:13; anchors.verticalCenter:parent.children[0].verticalCenter }
            }
            Rectangle { width:parent.width; height:1; color:"#2a2d32" }

            // Tip + Auto-detectar
            Rectangle {
                width:parent.width; height:36; color:"#1a2030"; radius:4; border.color:"#2a3850"
                Row {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    Label { text:"💡 Asigna pistas manualmente o usa Auto-detectar (voz=esta pista, música=siguiente)."; color:"#5d8fd4"; font.pixelSize:9; wrapMode:Text.WordWrap; width:parent.width - autoBtn.width - 16; anchors.verticalCenter:autoBtn.verticalCenter }
                    Button {
                        id: autoBtn; text:"Auto"; width:50; height:22
                        onClicked: { if(!fx) return; fx.setControlTrack(fxDialog.trackIndex); fx.setTargetTrack((fxDialog.trackIndex+1)%Math.max(1,TrackModel.count)) }
                        background: Rectangle { color:parent.pressed?"#1a4a7a":"#1e3a5f"; radius:3; border.color:"#2980b9" }
                        contentItem: Label { anchors.centerIn: parent; text:parent.text; color:"#5dade2"; font.pixelSize:9; font.bold:true; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter }
                    }
                }
            }

            // Selectores de pista (en fila)
            Row {
                spacing:8; width:parent.width
                Column { width:(parent.width-8)/2; spacing:3
                    Label { text:"VOZ (Control)"; color:"#777"; font.pixelSize:8; font.bold:true; font.letterSpacing:0.5 }
                    ComboBox {
                        width:parent.width; height:26
                        model: { var a=["— Ninguna —"]; for(var i=0;i<TrackModel.count;i++){var d=TrackModel.getTrackData(i); a.push("P"+(i+1)+": "+(d.name||"Sin nombre"))} return a }
                        currentIndex: { if(!fx) return 0; var v=fx.controlTrack; return (v>=0)?v+1:0 }
                        onActivated: { if(fx) fx.setControlTrack(currentIndex-1) }
                        background: Rectangle { color:"#1e2228"; radius:3; border.color:"#3a4050" }
                        contentItem: Label { anchors.fill: parent; rightPadding: 24; elide: Text.ElideRight; leftPadding:6; text:parent.displayText; color:"#ccc"; font.pixelSize:10; verticalAlignment:Text.AlignVCenter }
                    }
                }
                Column { width:(parent.width-8)/2; spacing:3
                    Label { text:"MÚSICA (Objetivo)"; color:"#777"; font.pixelSize:8; font.bold:true; font.letterSpacing:0.5 }
                    ComboBox {
                        width:parent.width; height:26
                        model: { var a=["— Ninguna —"]; for(var i=0;i<TrackModel.count;i++){var d=TrackModel.getTrackData(i); a.push("P"+(i+1)+": "+(d.name||"Sin nombre"))} return a }
                        currentIndex: { if(!fx) return 0; var v=fx.targetTrack; return (v>=0)?v+1:0 }
                        onActivated: { if(fx) fx.setTargetTrack(currentIndex-1) }
                        background: Rectangle { color:"#1e2228"; radius:3; border.color:"#3a4050" }
                        contentItem: Label { anchors.fill: parent; rightPadding: 24; elide: Text.ElideRight; leftPadding:6; text:parent.displayText; color:"#ccc"; font.pixelSize:10; verticalAlignment:Text.AlignVCenter }
                    }
                }
            }
            Rectangle { width:parent.width; height:1; color:"#2a2d32" }

            // Knobs horizontales
            Row {
                spacing: 8
                Repeater {
                    model: [
                        {prop:"thresholdDb",    label:"UMBRAL",  from:-60, to:0,    unit:" dB", color:"#e67e22"},
                        {prop:"duckAmountDb",   label:"AMOUNT",  from:-40, to:0,    unit:" dB", color:"#d35400"},
                        {prop:"innerFadeDownMs",label:"FADE ↓",  from:1,   to:500,  unit:" ms", color:"#a04000"},
                        {prop:"innerFadeUpMs",  label:"FADE ↑",  from:10,  to:3000, unit:" ms", color:"#7e5109"}
                    ]
                    Item {
                        width: 82; height: 88
                        property var kd: modelData
                        Label {
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: {
                                if (!fx) return "—"
                                var v = fx[kd.prop]
                                return v !== undefined ? Number(v).toFixed(0) + (kd.unit || "") : "—"
                            }
                            color: kd.color
                            font.pixelSize: 9; font.family: "monospace"
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width
                        }
                        MetalKnob {
                            anchors.top: parent.top
                            anchors.topMargin: 14
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: 52; from: kd.from; to: kd.to; dotColor: kd.color; label: ""
                            value: {
                                if (!fx) return 0
                                var v = fx[kd.prop]
                                return v !== undefined ? Number(v) : 0
                            }
                            onMoved: (v) => {
                                if (fx) {
                                    var s = "set" + kd.prop.charAt(0).toUpperCase() + kd.prop.slice(1)
                                    if (typeof fx[s] === "function") fx[s](v)
                                }
                            }
                        }
                        Label {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: kd.label; color: "#555"
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.5
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width
                        }
                    }
                }
            }
        }
    }

    // ── Truncar silencio (knobs horizontales + botón Procesar) ──────────────
    Component {
        id: silenceTrimComponent
        Column {
            spacing:12; width: parent ? parent.width : 400
            property var fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null

            Row {
                spacing:12
                Rectangle {
                    width:48; height:22; radius:4
                    color: fx && fx.enabled ? Qt.rgba(0.6,0.6,0.6,0.2) : Qt.rgba(1,1,1,0.06)
                    border.color: fx && fx.enabled ? "#aaa" : "#484e56"
                    Label { anchors.centerIn:parent; text:fx && fx.enabled ? "ON":"OFF"; color:fx && fx.enabled ? "#ccc":"#666"; font.bold:true; font.pixelSize:10; font.letterSpacing:1 }
                    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:{ if(fx) fx.setEnabled(!fx.enabled) } }
                }
                Label { text:"Truncar silencio"; color:"#e8eaed"; font.bold:true; font.pixelSize:13; anchors.verticalCenter:parent.children[0].verticalCenter }
            }
            Rectangle { width:parent.width; height:1; color:"#2a2d32" }

            // Advertencia + Procesar
            Rectangle {
                width:parent.width; height:36; color:"#241e10"; radius:4; border.color:"#5a3e10"
                Row {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    Label { text:"⚠  Elimina silencios del clip. Configura y pulsa Procesar."; color:"#d4a017"; font.pixelSize:10; wrapMode:Text.WordWrap; width:parent.width - procBtn.width - 16; anchors.verticalCenter:procBtn.verticalCenter }
                    Button {
                        id:procBtn; text:"Procesar"; width:80; height:24
                        onClicked: { if(fx && typeof fx.apply==="function") fx.apply() }
                        background: Rectangle { color:parent.pressed?"#7a5a00":"#5a4200"; radius:3; border.color:"#d4a017"; border.width:1 }
                        contentItem: Label { anchors.centerIn: parent; text:parent.text; color:"#f0c040"; font.pixelSize:10; font.bold:true; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter }
                    }
                }
            }

            // Knobs horizontales
            Row {
                spacing: 8
                Repeater {
                    model: [
                        {prop:"thresholdDb",  label:"UMBRAL",    from:-80, to:0,    unit:" dB", color:"#95a5a6"},
                        {prop:"minSilenceMs", label:"MÍN. SIL.", from:100, to:5000, unit:" ms", color:"#7f8c8d"},
                        {prop:"maxSilenceMs", label:"MÁX. SIL.", from:0,   to:2000, unit:" ms", color:"#626567"},
                        {prop:"paddingMs",    label:"MARGEN",    from:0,   to:500,  unit:" ms", color:"#515a5a"}
                    ]
                    Item {
                        width: 82; height: 88
                        property var kd: modelData
                        Label {
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: {
                                if (!fx) return "—"
                                var v = fx[kd.prop]
                                return v !== undefined ? Number(v).toFixed(0) + (kd.unit || "") : "—"
                            }
                            color: kd.color
                            font.pixelSize: 9; font.family: "monospace"
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width
                        }
                        MetalKnob {
                            anchors.top: parent.top
                            anchors.topMargin: 14
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: 52; from: kd.from; to: kd.to; dotColor: kd.color; label: ""
                            value: {
                                if (!fx) return 0
                                var v = fx[kd.prop]
                                return v !== undefined ? Number(v) : 0
                            }
                            onMoved: (v) => {
                                if (fx) {
                                    var s = "set" + kd.prop.charAt(0).toUpperCase() + kd.prop.slice(1)
                                    if (typeof fx[s] === "function") fx[s](v)
                                }
                            }
                        }
                        Label {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: kd.label; color: "#555"
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.5
                            horizontalAlignment: Text.AlignHCenter
                            width: parent.width
                        }
                    }
                }
            }
        }
    }



    // Directorio de presets
    readonly property string presetsDir: FileHelper.configDir() + "/fx-presets"

    // --- Diálogo para nombrar un preset nuevo ---
    Dialog {
        id: presetNameDialog
        title: qsTr("Guardar preset")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel
        width: 320

        Column {
            spacing: 8; width: parent.width
            Label { text: qsTr("Nombre del preset:"); color: "#ccc" }
            TextField {
                id: presetNameField
                width: parent.width
                placeholderText: qsTr("Mi preset")
                onAccepted: presetNameDialog.accepted()
            }
        }

        onAccepted: {
            var name = presetNameField.text.trim()
            if (name === "" || !fxDialog.fxChain) return
            FileHelper.ensureDir(fxDialog.presetsDir)
            var safeName = name.replace(/[\/\\:*?"<>|]/g, "_")
            var json = JSON.stringify(fxDialog.fxChain.toJson(), null, 2)
            FileHelper.writeTextFile(fxDialog.presetsDir + "/" + safeName + ".json", json)
            presetNameField.text = ""
        }
    }

    // --- Diálogo para cargar un preset ---
    Dialog {
        id: presetListDialog
        title: qsTr("Cargar preset")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Cancel
        width: 360
        height: 320

        property var presetFiles: []

        function refreshPresets() {
            FileHelper.ensureDir(fxDialog.presetsDir)
            var files = FileHelper.listDir(fxDialog.presetsDir) || []
            var filtered = []
            for (var i = 0; i < files.length; i++) {
                if (files[i].endsWith(".json"))
                    filtered.push(files[i])
            }
            presetFiles = filtered
        }

        contentItem: Column {
            spacing: 4

            Label {
                text: presetListDialog.presetFiles.length === 0
                    ? qsTr("No hay presets guardados.")
                    : qsTr("Selecciona un preset:")
                color: "#aaa"; font.pixelSize: 12
            }

            ListView {
                width: parent.width
                height: 220
                clip: true
                model: presetListDialog.presetFiles
                delegate: Rectangle {
                    width: parent.width; height: 32
                    color: presetHover.containsMouse ? "#ffffff11" : "transparent"
                    radius: 3

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 4

                        Label {
                            Layout.fillWidth: true
                            text: modelData.replace(".json", "")
                            color: "#eee"; font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Button {
                            text: qsTr("Cargar")
                            implicitHeight: 24
                            onClicked: {
                                var path = fxDialog.presetsDir + "/" + modelData
                                var raw = FileHelper.readTextFile(path)
                                if (raw && fxDialog.fxChain) {
                                    try {
                                        var obj = JSON.parse(raw)
                                        fxDialog.fxChain.mergeJson(obj)
                                    } catch(e) {}
                                }
                                presetListDialog.close()
                            }
                            background: Rectangle { color: "#2980b9"; radius: 3 }
                            contentItem: Label {
                                anchors.centerIn: parent
                                text: parent.text; color: "#fff"; font.pixelSize: 10
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        Button {
                            text: "✕"
                            implicitWidth: 24; implicitHeight: 24
                            onClicked: {
                                FileHelper.removeFile(fxDialog.presetsDir + "/" + modelData)
                                presetListDialog.refreshPresets()
                            }
                            background: Rectangle { color: "#c0392b"; radius: 3 }
                            contentItem: Label {
                                anchors.centerIn: parent
                                text: parent.text; color: "#fff"; font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    MouseArea {
                        id: presetHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
            }
        }
    }

    // =====================================================================
    //  Compresor visual (estilo Hindenburg: VU + knob) para per-track
    // =====================================================================
    Component {
        id: compressorVisualComponent

        Column {
            spacing: 10
            width: parent ? parent.width : 300

            property var fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null
            property real compAmount: 0.0

            Component.onCompleted: {
                if (fx && fx.thresholdDb < -6) {
                    compAmount = Math.min(1.0, Math.max(0.0,
                        (-6.0 - fx.thresholdDb) / 34.0))
                }
            }

            function applyAmount(v) {
                compAmount = v
                if (!fx || !fx.enabled) return
                fx.setThresholdDb(-6.0 - v * 34.0)
                fx.setRatio(1.5 + v * 10.5)
                fx.setAutoMakeup(true)
                fx.setAttackMs(10.0 - v * 5.0 + 5.0)
                fx.setReleaseMs(100.0 + v * 200.0)
            }

            // === Header + Toggle ===
            Row {
                spacing: 10
                Label {
                    text: "Compresor"
                    color: "#fff"; font.bold: true; font.pixelSize: 14
                    anchors.verticalCenter: parent.children[1].verticalCenter
                }
                Button {
                    width: 48; height: 26
                    text: fx && fx.enabled ? "on" : "off"
                    onClicked: {
                        if (!fx) return
                        fx.setEnabled(!fx.enabled)
                        if (fx.enabled) applyAmount(compAmount)
                    }
                    background: Rectangle {
                        radius: 3
                        color: fx && fx.enabled ? Qt.rgba(0.18,0.8,0.44,0.4) : Qt.rgba(1,1,1,0.08)
                        border.color: fx && fx.enabled ? "#2ecc71" : "#555"
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: fx && fx.enabled ? "#2ecc71" : "#888"
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // === VU METER ===
            Rectangle {
                width: parent.width
                height: 160
                color: "#1a1c20"
                radius: 6
                border.color: "#444"; border.width: 1

                Canvas {
                    id: trackVuCanvas
                    anchors.fill: parent
                    anchors.margins: 6

                    property real grDb: fx && fx.enabled
                        ? Math.abs(fx.gainReduction) : 0.0

                    onGrDbChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        var w = width, h = height
                        ctx.clearRect(0, 0, w, h)

                        var bgGrad = ctx.createLinearGradient(0, 0, 0, h)
                        bgGrad.addColorStop(0, "#2a2520")
                        bgGrad.addColorStop(0.6, "#1a1815")
                        bgGrad.addColorStop(1, "#0f0d0b")
                        ctx.fillStyle = bgGrad
                        ctx.fillRect(0, 0, w, h)

                        var cx = w / 2
                        var cy = h * 0.88
                        var radius = Math.min(w, h) * 0.72
                        var angStartDeg = 150
                        var angSweepDeg = 120

                        function dbToXY(dbVal, r) {
                            var frac = dbVal / 20.0
                            var angDeg = angStartDeg - frac * angSweepDeg
                            var angRad = angDeg * Math.PI / 180
                            return {
                                x: cx + Math.cos(angRad) * r,
                                y: cy - Math.sin(angRad) * r
                            }
                        }

                        var marks = [0, 4, 8, 12, 16, 20]
                        ctx.textAlign = "center"
                        ctx.textBaseline = "middle"

                        for (var i = 0; i < marks.length; i++) {
                            var p1 = dbToXY(marks[i], radius * 0.88)
                            var p2 = dbToXY(marks[i], radius * 0.97)
                            ctx.beginPath()
                            ctx.moveTo(p1.x, p1.y)
                            ctx.lineTo(p2.x, p2.y)
                            ctx.strokeStyle = "#ccc"
                            ctx.lineWidth = marks[i] === 0 ? 2 : 1.5
                            ctx.stroke()

                            var pText = dbToXY(marks[i], radius * 0.78)
                            ctx.fillStyle = "#bbb"
                            ctx.font = (marks[i] === 0 ? "bold " : "") + "11px monospace"
                            ctx.fillText(marks[i].toString(), pText.x, pText.y)
                        }

                        for (var d = 2; d < 20; d += 2) {
                            if (d % 4 === 0) continue
                            var pm1 = dbToXY(d, radius * 0.91)
                            var pm2 = dbToXY(d, radius * 0.97)
                            ctx.beginPath()
                            ctx.moveTo(pm1.x, pm1.y)
                            ctx.lineTo(pm2.x, pm2.y)
                            ctx.strokeStyle = "#777"
                            ctx.lineWidth = 1
                            ctx.stroke()
                        }

                        ctx.fillStyle = "#888"
                        ctx.font = "9px sans-serif"
                        ctx.textAlign = "center"
                        var labelY = cy - radius * 0.38
                        ctx.fillText("dB", cx, labelY)
                        ctx.fillText("COMPRESSION", cx, labelY + 12)

                        var grClamped = Math.min(Math.max(grDb, 0), 20.0)
                        var needleP = dbToXY(grClamped, radius * 0.87)

                        ctx.save()
                        ctx.shadowColor = "rgba(0,0,0,0.5)"
                        ctx.shadowBlur = 5
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.lineTo(needleP.x, needleP.y)
                        ctx.strokeStyle = "#f0f0f0"
                        ctx.lineWidth = 2
                        ctx.stroke()
                        ctx.restore()

                        ctx.beginPath()
                        ctx.arc(cx, cy, 4, 0, Math.PI * 2)
                        ctx.fillStyle = "#666"
                        ctx.fill()
                    }

                    Timer {
                        interval: 50
                        running: fxDialog.visible && fx && fx.enabled
                        repeat: true
                        onTriggered: trackVuCanvas.requestPaint()
                    }
                }
            }

            // === KNOB ===
            Item {
                width: parent.width
                height: 130

                BigMetalKnob {
                    id: trackKnobCanvas
                    size: 110
                    anchors.centerIn: parent
                    from: 0
                    to: 1.0
                    value: compAmount
                    activeColor: "#aaa"
                    onMoved: (val) => {
                        applyAmount(val)
                    }
                    onDoubleClicked: {
                        applyAmount(0)
                    }
                }

                Label {
                    anchors.left: parent.left; anchors.leftMargin: 20
                    anchors.bottom: parent.bottom
                    text: "OFF"; color: "#888"; font.pixelSize: 11; font.bold: true
                }
                Label {
                    anchors.right: parent.right; anchors.rightMargin: 20
                    anchors.bottom: parent.bottom
                    text: "MAX"; color: "#888"; font.pixelSize: 11; font.bold: true
                }
            }
        }
    }

    // =====================================================================
    //  Ecualizador visual (curva de respuesta) para per-track
    // =====================================================================
    Component {
        id: equalizerVisualComponent
        EqualizerPanel {
            fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null
        }
    }

    // =====================================================================
    //  DeNoiser visual (espectro + dials) para per-track
    // =====================================================================
    Component {
        id: noiseGateVisualComponent
        NoiseGatePanel {
            fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null
        }
    }

    Component {
        id: denoiserVisualComponent

        Column {
            spacing: 12
            width: parent ? parent.width : 300

            property var fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null

            // Header + Toggle
            Row {
                spacing: 10
                Label {
                    text: qsTr("Reductor de Ruido")
                    color: "#fff"; font.bold: true; font.pixelSize: 14
                    anchors.verticalCenter: parent.children[1].verticalCenter
                }
                Button {
                    width: 48; height: 26
                    text: fx && fx.enabled ? "on" : "off"
                    onClicked: { if (fx) fx.setEnabled(!fx.enabled) }
                    background: Rectangle {
                        radius: 3
                        color: fx && fx.enabled ? Qt.rgba(0.1, 0.6, 1.0, 0.3) : Qt.rgba(1,1,1,0.08)
                        border.color: fx && fx.enabled ? "#3498db" : "#555"
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: fx && fx.enabled ? "#3498db" : "#888"
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Visualizador Espectral
            Item {
                width: parent.width; height: 160
                
                SpectralDisplay {
                    anchors.fill: parent
                    active: fx && fx.enabled
                    inputMags: fx ? fx.lastInputSnaps : []
                    outputMags: fx ? fx.lastOutputSnaps : []
                    noiseMags: fx ? fx.noiseMags : []
                    
                    Rectangle {
                        anchors.fill: parent; color: "transparent"
                        border.color: "#3c4146"; border.width: 1; radius: 4
                    }
                }
            }

            // Dial de Control
            Item {
                width: parent.width; height: 140

                BigMetalKnob {
                    id: trackNrReduction
                    size: 110
                    from: 0; to: 1.0
                    value: fx ? fx.reduction : 1.0
                    activeColor: "#3498db"
                    anchors.centerIn: parent
                    onMoved: (val) => { if (fx) fx.setReduction(val) }
                    
                    Label {
                        text: qsTr("Reduction")
                        anchors.top: trackNrReduction.bottom
                        anchors.topMargin: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#aaa"
                        font.pixelSize: 12
                    }
                    
                    Label {
                        anchors.top: parent.top; anchors.topMargin: -12
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (parent.value * 100).toFixed(0) + "%"
                        color: "#666"; font.pixelSize: 9
                    }
                }
            }

            Label {
                text: qsTr("Utiliza el botón 'Capturar' en el menú principal para crear el perfil de ruido inicial.")
                color: "#555"; font.pixelSize: 10; wrapMode: Text.WordWrap; width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    Component {
        id: deepFilterComponent

        Column {
            spacing: 12
            width: parent ? parent.width : 300

            property var fx: fxDialog.fxChain ? fxDialog.fxChain.effectAt(fxDialog.selectedFx) : null

            Row {
                spacing: 10
                Label {
                    text: qsTr("Deep Denoise")
                    color: "#fff"; font.bold: true; font.pixelSize: 14
                    anchors.verticalCenter: parent.children[1].verticalCenter
                }
                Button {
                    width: 48; height: 26
                    text: fx && fx.enabled ? "on" : "off"
                    onClicked: { if (fx) fx.setEnabled(!fx.enabled) }
                    background: Rectangle {
                        radius: 3
                        color: fx && fx.enabled ? Qt.rgba(0.5, 0.1, 0.8, 0.3) : Qt.rgba(1,1,1,0.08)
                        border.color: fx && fx.enabled ? "#8e44ad" : "#555"
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: fx && fx.enabled ? "#8e44ad" : "#888"
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Rectangle {
                width: parent.width; height: 1; color: "#2a2d32"
            }

            // Descarga o advertencia
            Rectangle {
                width: parent.width; height: 48
                color: fx && fx.isReady ? "#1a2030" : "#241e10"
                radius: 4; border.color: fx && fx.isReady ? "#2a3850" : "#5a3e10"
                visible: fx !== null
                
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 8
                    
                    Label {
                        Layout.fillWidth: true
                        text: {
                            if (!fx) return ""
                            if (fx.isReady) return "✓ Modelo IA cargado y listo."
                            if (fx.downloadProgress > 0 && fx.downloadProgress < 1.0)
                                return "Descargando modelo: " + Math.round(fx.downloadProgress * 100) + "%"
                            return "⚠ Faltan los pesos del modelo (~20MB)."
                        }
                        color: fx && fx.isReady ? "#5dade2" : "#d4a017"
                        font.pixelSize: 10; wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    Button {
                        text: "Descargar"
                        visible: fx && !fx.isReady && fx.downloadProgress === 0
                        onClicked: { if(fx) fx.retryDownload() }
                        background: Rectangle { color: parent.pressed ? "#7a5a00" : "#5a4200"; radius: 3; border.color: "#d4a017"; border.width: 1 }
                        contentItem: Label { anchors.centerIn: parent; text: parent.text; color: "#f0c040"; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }

            // Dial de Control
            Item {
                width: parent.width; height: 140
                visible: fx && fx.isReady

                BigMetalKnob {
                    id: deepNrReduction
                    size: 110
                    from: 0; to: 100.0
                    value: fx ? fx.reductionDb : 40.0
                    activeColor: "#8e44ad"
                    anchors.centerIn: parent
                    onMoved: (val) => { if (fx) fx.setReductionDb(val) }
                    
                    Label {
                        text: qsTr("Reduction (dB)")
                        anchors.top: deepNrReduction.bottom
                        anchors.topMargin: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#aaa"
                        font.pixelSize: 12
                    }
                    
                    Label {
                        anchors.top: parent.top; anchors.topMargin: -12
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: parent.value.toFixed(1) + " dB"
                        color: "#666"; font.pixelSize: 9
                    }
                }
            }
        }
    }
}
