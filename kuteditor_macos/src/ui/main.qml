import QtQuick
import QtCore
import QtQuick.Controls
import Qt.labs.platform as Platform
import QtQuick.Layouts
import QtQuick.Dialogs
import KutComponents 1.0
import KutIcons 1.0
import "components"

ApplicationWindow {
    id: root
    width: 1368
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    visible: FeatureKutPod
    title: showLibrary
           ? qsTr("Kut Editor — Biblioteca")
           : (ProjectIO.isDirty ? "● " : "") +
             (ProjectIO.currentDisplayName) +
             " — Kut Editor"

    color: "#1a1d21"

    readonly property string cmdModifier: Qt.platform.os === "osx" ? "Cmd" : "Ctrl"

    palette.window: "#1a1d21"
    palette.windowText: "#ecf0f1"
    palette.base: "#2a2d31"
    palette.text: "#ecf0f1"
    palette.button: "#34383d"
    palette.buttonText: "#ecf0f1"
    palette.highlight: "#e74c3c"
    palette.highlightedText: "#ffffff"
    palette.midlight: "#3c4146"
    palette.mid: "#1f2226"
    palette.dark: "#111418"
    palette.toolTipBase: "#1a1d21"
    palette.toolTipText: "#ecf0f1"

    // --- Modo biblioteca vs editor ---
    property bool showLibrary: showLibraryOnStart
    property bool showLibraryOnStart: true  // persistente, se guarda en Settings
    property bool autoTranscribeOnShow: false

    // --- Bloqueo temporal de menú contextual para evitar rebotes ---
    property bool blockContextMenu: false
    Timer {
        id: menuBlockTimer
        interval: 180
        repeat: false
        onTriggered: root.blockContextMenu = false
    }
    function restartMenuBlockTimer() {
        root.blockContextMenu = true
        menuBlockTimer.restart()
    }

    // --- Sistema de cierre con confirmación ---
    property bool _quitAfterSave: false
    property bool _forceQuit: false

    onClosing: (close) => {
        if (_forceQuit) return  // ya confirmó, dejar cerrar
        if (AudioEngine.isRecording) AudioEngine.stop()
        if (!ProjectIO.isDirty) return  // nada que guardar
        close.accepted = false
        closeConfirmDialog.open()
    }

    function _doForceQuit() {
        _forceQuit = true
        root.close()
    }

    Dialog {
        id: closeConfirmDialog
        width: 380
        anchors.centerIn: parent
        title: qsTr("Guardar cambios")
        modal: true
        standardButtons: Dialog.NoButton
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        contentItem: Column {
            spacing: 16
            padding: 8
            Label {
                text: qsTr("El proyecto tiene cambios sin guardar.\n¿Deseas guardar antes de salir?")
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 8
                anchors.right: parent.right
                Button {
                    text: qsTr("Guardar")
                    onClicked: {
                        closeConfirmDialog.close()
                        root._quitAfterSave = true
                        root.handleSaveProject()
                        // Si ya tenía ruta, save() es síncrono y ya guardó.
                        // Si no, se abrió el FileDialog y _quitAfterSave
                        // hará que cierre al terminar de guardar.
                        if (ProjectIO.currentPath !== "" && !ProjectIO.isDirty) {
                            root._doForceQuit()
                        }
                    }
                }
                Button {
                    text: qsTr("Descartar")
                    onClicked: {
                        closeConfirmDialog.close()
                        root._doForceQuit()
                    }
                }
                Button {
                    text: qsTr("Cancelar")
                    onClicked: closeConfirmDialog.close()
                }
            }
        }
    }

    // --- Ventana "Acerca de" ---
    Window {
        id: aboutDialog
        width: 420
        height: 520
        minimumWidth: 420
        maximumWidth: 420
        minimumHeight: 520
        maximumHeight: 520
        title: qsTr("Acerca de Kut Editor")
        modality: Qt.WindowModal
        flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowCloseButtonHint
        color: "#2a2d31"
        visible: false

        function open() {
            aboutDialog.show()
            aboutDialog.raise()
            aboutDialog.requestActivate()
        }

        function close() {
            aboutDialog.hide()
        }

        Column {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 20

            // Logo
            Image {
                source: "qrc:/resources/kuteditor.svg"
                sourceSize.width: 80
                sourceSize.height: 80
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // Nombre y versión
            Column {
                spacing: 4
                anchors.horizontalCenter: parent.horizontalCenter
                Label {
                    text: "Kut Editor"
                    font.pixelSize: 22
                    font.bold: true
                    color: root.textPri
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Label {
                    text: qsTr("Versión %1").arg(AppVersion)
                    font.pixelSize: 14
                    color: root.textSec
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Label {
                    text: qsTr("Editor de podcast multipista")
                    font.pixelSize: 12
                    color: root.textSec
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }

            // Separador
            Rectangle {
                width: parent.width - 48
                height: 1
                color: root.divider
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // Opciones de compilación
            Column {
                spacing: 6
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 24
                anchors.rightMargin: 24

                Label {
                    text: qsTr("Opciones de compilación")
                    font.pixelSize: 13
                    font.bold: true
                    color: root.textPri
                }

                Repeater {
                    model: [
                        { name: "CoreAudio (audio)",           enabled: FeatureJack },
                        { name: "libsamplerate (resample)",    enabled: FeatureSamplerate },
                        { name: "TagLib (capítulos MP3)",      enabled: FeatureTagLib },
                        { name: "Whisper (transcripción)",     enabled: FeatureWhisper },
                        { name: "KutPod (biblioteca online)",  enabled: FeatureKutPod },
                        { name: "DeepFilterNet (noise reduction)", enabled: FeatureDeepFilterNet }
                    ]

                    delegate: Row {
                        spacing: 8
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: modelData.enabled ? "#2ecc71" : "#e74c3c"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            text: modelData.name
                            font.pixelSize: 12
                            color: modelData.enabled ? root.textPri : root.textSec
                        }
                    }
                }
            }

            // Copyright
            Label {
                text: "© 2025 Kut Studio"
                font.pixelSize: 11
                color: root.textSec
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // Espaciador flexible
            Item {
                width: 1
                height: 10
            }

            // Botón de cierre
            Button {
                text: qsTr("Cerrar")
                anchors.right: parent.right
                palette.window: "#2a2d31"
                palette.windowText: "#ecf0f1"
                palette.text: "#ecf0f1"
                palette.base: "#1a1d21"
                palette.button: "#34383d"
                palette.buttonText: "#ecf0f1"
                onClicked: aboutDialog.close()
            }
        }
    }

    property color accent:   "#e74c3c"
    property color panelBg:  "#2a2d31"
    property color panelBg2: "#34383d"
    property color textPri:  "#ecf0f1"
    property color textSec:  "#95a5a6"
    property color divider:  "#3c4146"
    property color btnBorder: "#1f2226"   // borde de botones (más oscuro que panelBg)

    // Modo ripple: al borrar clips con Supr, los clips posteriores se
    // desplazan para cerrar el hueco. Toggle desde la toolbar (botón imán).
    property bool rippleEdit: false

    // Preferencias persistentes (QSettings en ~/.config/PodcastEditor/)
    // Normalización dinámica automática (master bus)
    property bool dynamicNormEnabled: false
    property real dynamicNormTargetLufs: -16.0
    // Preferencias de export/proyecto
    property int prefSampleRate: 48000       // 48000 o 44100
    property int prefBitDepth: 24            // 16, 24 o 32 (para WAV)
    property string prefProjectsDir: ""      // vacío = ~/Documents por defecto
    property string lastExportPath: ""       // última ruta de exportación usada
    property bool returnPlayheadOnStop: false
    onReturnPlayheadOnStopChanged: AudioEngine.returnPlayheadOnStop = returnPlayheadOnStop

    Settings {
        id: appSettings
        category: "app"
        property alias rippleEdit: root.rippleEdit
        property alias dynamicNormEnabled: root.dynamicNormEnabled
        property alias dynamicNormTargetLufs: root.dynamicNormTargetLufs
        property alias prefSampleRate: root.prefSampleRate
        property alias prefBitDepth: root.prefBitDepth
        property alias prefProjectsDir: root.prefProjectsDir
        property alias lastExportPath: root.lastExportPath
        property alias showLibraryOnStart: root.showLibraryOnStart
        property alias returnPlayheadOnStop: root.returnPlayheadOnStop
        property alias autoTranscribeOnShow: root.autoTranscribeOnShow
    }

    // Factor de zoom del timeline. 1.0 = ajustar a ancho visible.
    // >1.0 = acercar (más ancho, más scroll). <1.0 = alejar.
    property real timelineZoom: 1.0
    // Scroll horizontal compartido por todos los clipAreas.
    property real timelineScrollX: 0
    // Flag que suspende el auto-scroll mientras el usuario interactúa
    // con zoom, scroll manual o click en el timeline. Se reactiva
    // automáticamente tras 2 s de inactividad.
    property bool isUserInteracting: false

    // Offset horizontal (px) donde empieza la zona de clips dentro de cada
    // pista: colorStripe(5) + controlsPanel(310) + divider(1) + margin(4)
    // + trackVU(14) + margin(6) = 340. Se usa también para alinear la Ruler
    // global encima del ScrollView.
    property int clipAreaLeftPx: 340
    readonly property int minPanelWidth: 200
    readonly property int maxPanelWidth: 500

    // ---- Conversión centralizada pixel ↔ tiempo (estilo KWave) ----
    // Todas las conversiones pasan por estas dos funciones para evitar
    // inconsistencias entre el click y la posición del playhead.
    // "viewX" es la coordenada X relativa al borde izquierdo del clipArea.
    // Se usa clipArea.width (del primer track) como referencia canónica.

    // Segundos por pixel (equivalente al m_zoom de KWave, pero en seg en vez de samples)
    readonly property real secPerPixel: {
        const vp = _clipAreaWidth
        if (vp <= 0 || timelineZoom <= 0) return 0
        return timelineDisplayDuration / (vp * timelineZoom)
    }

    // Ancho real del clipArea — se lee del primer delegate del ListView.
    // Evita depender de timelineScroll.width que puede incluir scrollbar.
    property real _clipAreaWidth: {
        if (trackListView.count > 0 && trackListView.contentItem.children.length > 0) {
            const first = trackListView.contentItem.children[0]
            if (first && first.clipAreaWidth !== undefined)
                return first.clipAreaWidth
        }
        return Math.max(1, trackListView.width - clipAreaLeftPx - 6)
    }

    /// Convierte una coordenada X relativa al clipArea a tiempo en segundos.
    function viewXToSec(viewX) {
        if (secPerPixel <= 0) return 0
        return (viewX + timelineScrollX) * secPerPixel
    }

    /// Convierte un tiempo en segundos a coordenada X relativa al clipArea.
    function secToViewX(sec) {
        if (secPerPixel <= 0) return 0
        return sec / secPerPixel - timelineScrollX
    }

    // Duración visible del timeline, compartida entre todas las pistas.
    // Debe ser "pegajosa": durante grabación crece a saltos, no continuamente,
    // para evitar que el waveform se reescale en vivo (lo que daba la
    // sensación de que el waveform aparecía desde la derecha).
    property real timelineDisplayDuration: 60
    onTimelineDisplayDurationChanged: {
        // Sincronizar duración del episodio con el modelo de capítulos
        // para que el endTime del último CHAP coincida con la duración real.
        ChapterModel.episodeDurationMs = Math.round(timelineDisplayDuration * 1000)
    }

    function _updateTimelineDisplayDuration() {
        const need = Math.max(
            AudioEngine.totalTime || 0,
            AudioEngine.currentTime || 0,
            20.0)
        if (need > timelineDisplayDuration) {
            const step = 30.0
            timelineDisplayDuration = Math.ceil((need + 5) / step) * step
        }
    }

    Connections {
        target: AudioEngine
        function onTimeChanged() {
            root._updateTimelineDisplayDuration()
            root._autoScrollFollowPlayhead()
        }
        function onTotalTimeChanged() { root._updateTimelineDisplayDuration() }
        // Reactivar auto-scroll al cambiar estado de playback/grabación
        function onStateChanged() { root.isUserInteracting = false }
        function onPlaybackStopped() {
            if (root.returnPlayheadOnStop) {
                root._autoScrollFollowPlayhead(true)
            }
        }
    }

    // Si el playhead se sale del viewport durante playback/grabación, desplazar
    // el scroll para mantenerlo visible con un margen cómodo del 75% (estilo
    // Reaper: cuando el cursor llega al 75% del viewport, el timeline "salta"
    // y el cursor vuelve al 25% visible).
    function _autoScrollFollowPlayhead(force) {
        if (root.isUserInteracting && !force) return   // usuario navegando manualmente
        // Solo seguir el playhead durante playback o grabación activa (a menos que se fuerce).
        if (!AudioEngine.isPlaying && !AudioEngine.isRecording && !force) return
        if (trackListView.width <= 0) return
        const total = root.timelineDisplayDuration
        if (total <= 0) return

        const viewportPx = Math.max(0, trackListView.width - root.clipAreaLeftPx - 6)
        const innerWidth = viewportPx * root.timelineZoom
        if (innerWidth <= viewportPx) return  // todo cabe, no hay scroll posible

        const playInnerX = (AudioEngine.currentTime / total) * innerWidth
        const playViewX  = playInnerX - root.timelineScrollX
        const marginRight = viewportPx * 0.75
        const marginLeft  = viewportPx * 0.05

        if (playViewX > marginRight) {
            // Saltar: dejar el playhead al 25% del viewport
            const target = playInnerX - viewportPx * 0.25
            const maxScroll = Math.max(0, innerWidth - viewportPx)
            root.timelineScrollX = Math.max(0, Math.min(maxScroll, target))
        } else if (playViewX < marginLeft) {
            // Usuario hizo seek hacia atrás fuera del viewport
            const target = playInnerX - viewportPx * 0.25
            root.timelineScrollX = Math.max(0, target)
        }
    }

    Connections {
        target: ProjectIO
        function onProjectLoaded() {
            // Recalcular el display duration al cargar: resetear y dejar
            // que el siguiente tick del engine lo ajuste según totalTime.
            root.timelineDisplayDuration = 60
            root._updateTimelineDisplayDuration()
        }
    }

    // Auto-nivelado al parar grabación (estilo Hindenburg Magic).
    Connections {
        target: AudioEngine
        function onRecordingStopped(totalFrames) {
            if (totalFrames <= 0) return
            if (root.dynamicNormEnabled)
                UndoManager.dynamicNormalizeAllTracks(root.dynamicNormTargetLufs)
        }
    }

    // --- Selección global del timeline (en segundos) ---
    // Si selStart == selEnd, no hay selección.
    property real selStart: 0
    property real selEnd: 0
    readonly property bool hasSelection: selEnd > selStart

    // --- Selección de clips: lista de { track, clip } ---
    property var selectedClips: []
 
    // --- Selección de pista ---
    property int selectedTrack: -1  // Track index that is currently selected
    property var selectedTracks: [] // List of selected track indices
    property bool isManuscriptVisible: true // Control manual de visibilidad del manuscrito
 
    // --- Selección de región POR PISTA (Shift+drag en la pista) ---
    // Mapa { trackIndex: {start, end} }. Solo se usa cuando lastSelection ==
    // "trackRegions". Permite borrar audio solo de las pistas seleccionadas
    // en un rango específico, sin tocar las demás.
    property var trackRegions: ({})
    readonly property bool hasTrackRegions: {
        for (const k in trackRegions) {
            const r = trackRegions[k]
            if (r && r.end > r.start) return true
        }
        return false
    }
 
    // Para decidir qué borra Supr, guardamos qué fue lo último seleccionado.
    // "none" | "region" | "clips" | "trackRegions"
    property string lastSelection: "none"
 
    function setTrackRegion(trackIdx, startSec, endSec) {
        if (endSec < startSec) { const t = startSec; startSec = endSec; endSec = t }
        startSec = Math.max(0, startSec)
        endSec = Math.max(startSec, endSec)
        // Copiar el objeto para que QML detecte el cambio.
        const copy = {}
        for (const k in trackRegions) copy[k] = trackRegions[k]
        if (endSec > startSec) {
            copy[trackIdx] = { start: startSec, end: endSec }
            trackRegions = copy
            lastSelection = "trackRegions"
            // Una selección por-pista invalida la global y las de clips.
            clearSelection()
            clearClipSelection()
        } else {
            delete copy[trackIdx]
            trackRegions = copy
        }
    }
 
    function clearTrackRegions() {
        if (Object.keys(trackRegions).length === 0) return
        trackRegions = ({})
        if (lastSelection === "trackRegions") lastSelection = "none"
    }
 
    function setSelection(s, e) {
        if (e < s) { const tmp = s; s = e; e = tmp }
        selStart = Math.max(0, s)
        selEnd = Math.max(selStart, e)
        if (hasSelection) {
            lastSelection = "region"
            clearClipSelection()
            clearTrackRegions()
            AudioEngine.setLoopRegion(selStart, selEnd)
        } else {
            AudioEngine.clearLoopRegion()
        }
    }
    function clearSelection() {
        selStart = 0; selEnd = 0
        if (lastSelection === "region") lastSelection = "none"
        AudioEngine.clearLoopRegion()
    }
 
 
    function isClipSelected(track, clip) {
        for (const s of selectedClips) {
            if (s.track === track && s.clip === clip) return true
        }
        return false
    }
 
    function selectClip(track, clip, additive) {
        if (additive) {
            if (isClipSelected(track, clip)) {
                selectedClips = selectedClips.filter(
                    s => !(s.track === track && s.clip === clip))
            } else {
                selectedClips = selectedClips.concat([{ track, clip }])
            }
        } else {
            selectedClips = [{ track, clip }]
        }
        if (selectedClips.length > 0) {
            lastSelection = "clips"
            selectedTrack = track  // La pista del clip seleccionado es la activa
            // Get unique tracks containing the selected clips
            let tracks = []
            for (let i = 0; i < selectedClips.length; i++) {
                let t = selectedClips[i].track
                if (tracks.indexOf(t) === -1) {
                    tracks.push(t)
                }
            }
            selectedTracks = tracks
            clearSelection()
            clearTrackRegions()
        } else {
            lastSelection = "none"
            selectedTrack = -1
            selectedTracks = []
        }
    }
 
    function selectTrack(trackIndex, additive) {
        if (additive) {
            if (selectedTracks.indexOf(trackIndex) >= 0) {
                selectedTracks = selectedTracks.filter(t => t !== trackIndex)
                if (selectedTrack === trackIndex) {
                    selectedTrack = selectedTracks.length > 0 ? selectedTracks[selectedTracks.length - 1] : -1
                }
            } else {
                selectedTracks = selectedTracks.concat([trackIndex])
                selectedTrack = trackIndex
            }
        } else {
            selectedTracks = [trackIndex]
            selectedTrack = trackIndex
        }
        clearClipSelection()
    }

    function selectAllTracks() {
        let tracks = []
        for (let i = 0; i < TrackModel.count; i++) {
            tracks.push(i)
        }
        selectedTracks = tracks
        selectedTrack = TrackModel.count > 0 ? 0 : -1
        clearClipSelection()
        clearSelection()
        clearTrackRegions()
    }
 
    // Forzar selección de pista sin toggle (para usar después de paste, etc.)
    function forceSelectTrack(trackIndex) {
        selectedTrack = trackIndex
        selectedTracks = [trackIndex]
    }
 
    function clearClipSelection() {
        if (selectedClips.length > 0) selectedClips = []
        if (lastSelection === "clips") lastSelection = "none"
        // Don't clear track selection here - track selection is independent
    }
 
    function clearTrackSelection() {
        selectedTrack = -1
        selectedTracks = []
    }

    // Borra lo último seleccionado: clips, región global, regiones por pista o pista seleccionada.
    function deleteSelected() {
        if (lastSelection === "clips" && selectedClips.length > 0) {
            UndoManager.deleteMultipleClips(selectedClips, root.rippleEdit)
            selectedClips = []
            lastSelection = "none"
        } else if (lastSelection === "region" && hasSelection) {
            deleteSelectedRegion()
        } else if (lastSelection === "trackRegions" && hasTrackRegions) {
            deleteSelectedTrackRegions()
        } else if (selectedTracks.length > 0) {
            confirmDeleteDialog.askDelete(selectedTracks)
            clearTrackSelection()
        }
    }

    // Borra las regiones que hay por pista (UNA región por cada pista).
    // Respeta el toggle de ripple. Reversible con Ctrl+Z.
    function deleteSelectedTrackRegions() {
        if (!hasTrackRegions) return
        const regions = []
        for (const k in trackRegions) {
            const r = trackRegions[k]
            if (!r || r.end <= r.start) continue
            regions.push({ track: parseInt(k), start: r.start, end: r.end })
        }
        if (regions.length === 0) return
        UndoManager.deleteTrackRegions(regions, root.rippleEdit)
        clearTrackRegions()
    }

    // Borra la región temporal (cierra brecha).
    // Si hay pistas seleccionadas, solo borra la región de esas pistas.
    function deleteSelectedRegion() {
        if (!root.hasSelection) return
        const s = selStart
        const e = selEnd
        UndoManager.deleteRegion(s, e, root.rippleEdit, root.selectedTracks)
        const ph = AudioEngine.currentTime
        if (root.rippleEdit) {
            // Ripple ON: el resto se ha desplazado hacia la izquierda.
            const removed = e - s
            if (ph >= e)      AudioEngine.seekTime(ph - removed)
            else if (ph > s)  AudioEngine.seekTime(s)
        } else {
            // Ripple OFF: queda hueco; cursor sigue donde estaba si cae dentro.
            if (ph > s && ph < e) AudioEngine.seekTime(s)
        }
        clearSelection()
    }

    // Corta / divide la pista actual o pistas seleccionadas en la posición del cursor.
    function splitAtPlayhead() {
        if (root.selectedTracks.length > 0) {
            UndoManager.splitAt(AudioEngine.currentTime, root.selectedTracks)
        } else {
            UndoManager.splitAt(AudioEngine.currentTime, -1)
        }
    }

    function openTrackFx(trackIndex, trackName) {
        trackFxDialog.trackIndex = trackIndex
        trackFxDialog.trackName = trackName
        trackFxDialog.open()
    }

    // Clipboard de clips: guarda referencias { sourceTrack, clipIndex }
    // La copia real de audio se hace en C++ con copyClipToTrack.
    property var clipboardClips: []

    // Para "Cortar", guardamos snapshots de cada clip ANTES de borrar.
    // Los snapshots contienen toda la info necesaria para recrear el clip.
    property var clipboardSnapshots: []   // lista de { snapshot, sourceTrack }
    property bool clipboardIsCut: false

    function _getRegionSnapshots() {
        var snaps = []
        var tracksToCheck = []
        if (lastSelection === "region" && hasSelection) {
            if (selectedTracks.length > 0) {
                for (var t = 0; t < selectedTracks.length; t++) {
                    tracksToCheck.push({track: selectedTracks[t], start: selStart, end: selEnd})
                }
            } else {
                for (var i = 0; i < TrackModel.count; i++) {
                    tracksToCheck.push({track: i, start: selStart, end: selEnd})
                }
            }
        } else if (lastSelection === "trackRegions" && hasTrackRegions) {
            for (var k in trackRegions) {
                var r = trackRegions[k]
                if (r && r.end > r.start) {
                    tracksToCheck.push({track: parseInt(k), start: r.start, end: r.end})
                }
            }
        }

        for (var tIdx = 0; tIdx < tracksToCheck.length; tIdx++) {
            var t = tracksToCheck[tIdx].track
            var rStart = tracksToCheck[tIdx].start
            var rEnd = tracksToCheck[tIdx].end
            var clips = TrackModel.clipsOf(t)
            for (var j = 0; j < clips.length; j++) {
                var c = clips[j]
                var cStart = c.startSec || 0
                var cLen = c.lengthSec || 0
                var cEnd = cStart + cLen
                
                if (cStart < rEnd && cEnd > rStart) {
                    var snap = TrackModel.clipSnapshot(t, j)
                    if (!snap || (snap.length === undefined && Object.keys(snap).length === 0)) continue

                    var sr = TrackModel.trackSampleRate(t) || 48000
                    var overlapStart = Math.max(cStart, rStart)
                    var overlapEnd = Math.min(cEnd, rEnd)
                    var overlapLen = overlapEnd - overlapStart
                    
                    var startOffsetSec = overlapStart - cStart
                    
                    snap.timelineStart = Math.round(overlapStart * sr)
                    snap.sourceOffset = snap.sourceOffset + Math.round(startOffsetSec * sr)
                    snap.length = Math.round(overlapLen * sr)
                    
                    if (startOffsetSec > 0) snap.fadeInLen = 0
                    if (overlapEnd < cEnd) snap.fadeOutLen = 0
                    
                    snaps.push({ snapshot: snap, sourceTrack: t })
                }
            }
        }
        return snaps
    }

    function copySelectedClips() {
        clipboardIsCut = false
        clipboardSnapshots = []
        clipboardClips = []

        if (lastSelection === "clips" && selectedClips.length > 0) {
            clipboardClips = selectedClips.map(sc => ({
                sourceTrack: sc.track,
                clipIndex: sc.clip
            }))
        } else if (lastSelection === "region" || lastSelection === "trackRegions") {
            clipboardSnapshots = _getRegionSnapshots()
        }
    }

    function cutSelectedClips() {
        clipboardSnapshots = []
        clipboardClips = []
        clipboardIsCut = true

        if (lastSelection === "clips" && selectedClips.length > 0) {
            var snaps = []
            for (var i = 0; i < selectedClips.length; i++) {
                var sc = selectedClips[i]
                var snap = TrackModel.clipSnapshot(sc.track, sc.clip)
                if (snap && (snap.length !== undefined ? snap.length > 0 : Object.keys(snap).length > 0)) {
                    snaps.push({ snapshot: snap, sourceTrack: sc.track })
                }
            }
            clipboardSnapshots = snaps
            clipboardClips = selectedClips.map(sc => ({
                sourceTrack: sc.track,
                clipIndex: sc.clip
            }))
            deleteSelected()
        } else if (lastSelection === "region" || lastSelection === "trackRegions") {
            clipboardSnapshots = _getRegionSnapshots()
            deleteSelected()
        }
    }

    function pasteClips() {
        if (clipboardClips.length === 0 && clipboardSnapshots.length === 0) return

        var targetTrack = root.selectedTrack >= 0 ? root.selectedTrack : 0
        var ph = AudioEngine.currentTime

        if (clipboardSnapshots.length > 0) {
            UndoManager.pasteMultipleClipSnapshots(targetTrack, clipboardSnapshots, ph, true)
        } else {
            // Copia normal: los clips originales siguen existiendo
            UndoManager.copyMultipleClipsToTrack(clipboardClips, targetTrack, ph, true)
        }

        clearClipSelection()
        var st = targetTrack
        Qt.callLater(function() { root.forceSelectTrack(st) })
    }

    function qint64FromSec(sec, sr) { return Math.round(sec * sr) }

    function duplicateSelectedClips() {
        copySelectedClips()
        pasteClips()
    }

    // Add a function to get the currently focused track:
    function getFocusedTrack() {
        // Try to get track with keyboard focus
        // For now, return the first track with selected clips or track 0
        if (selectedClips.length > 0) {
            return selectedClips[0].track
        }
        return 0
    }

    // Busca el nameField de una pista por índice (para Tab entre pistas)
    function findTrackNameField(trackIndex) {
        const item = trackListView.itemAtIndex(trackIndex)
        if (item && item.children) {
            // El delegate es Item > TrackComponent. Buscar el TrackComponent.
            for (let i = 0; i < item.children.length; i++) {
                const child = item.children[i]
                if (child.nameFieldItem) return child.nameFieldItem
            }
        }
        return null
    }

    // Dada una pista origen y un delta Y del drag, devuelve el índice de
    // pista destino o el mismo origen si no cruza ninguna frontera.
    // Usa las posiciones visuales reales de los items en el ListView.
    // Track switching with hysteresis to prevent "magnetic repulsion"
    property var lastTrackSwitch: ({ track: -1, time: 0 })
    property real trackSwitchHysteresis: 0.3 // 30% of track height
    property var dragState: ({ 
        sourceTrack: -1, 
        sourceTop: 0,
        sourceBottom: 0,
        lastTrack: -1
    })

    function trackItemYInOverlay(trackIndex, overlayItem) {
        const item = trackListView.itemAtIndex(trackIndex)
        if (!item || !overlayItem) return 0
        const globalPos = item.mapToItem(overlayItem, 0, 0)
        return globalPos.y
    }

    function trackIndexFromDragY(sourceTrack, offsetY) {
        // Get the actual visual position from the ListView
        const sourceItem = trackListView.itemAtIndex(sourceTrack)
        if (!sourceItem) return sourceTrack
        
        const sourceY = sourceItem.y
        const sourceHeight = sourceItem.height
        const sourceTop = sourceY
        const sourceBottom = sourceY + sourceHeight
        const targetY = sourceY + offsetY
        
        // Initialize drag state on first call
        if (dragState.sourceTrack !== sourceTrack) {
            dragState.sourceTrack = sourceTrack
            dragState.sourceTop = sourceTop
            dragState.sourceBottom = sourceBottom
            dragState.lastTrack = sourceTrack
        }
        
        // Histéresis real: no cambiamos de pista a menos que el cursor cruce
        // el límite por un margen definido (ej: 15 píxeles).
        const hysteresisThreshold = 15
        if (targetY >= sourceTop - hysteresisThreshold && targetY < sourceBottom + hysteresisThreshold) {
            return sourceTrack
        }
        
        // Buscar en qué pista está el cursor
        for (let i = 0; i < trackListView.count; i++) {
            const item = trackListView.itemAtIndex(i)
            if (!item) continue
            
            const itemTop = item.y
            const itemBottom = item.y + item.height
            
            // Si el cursor cae dentro de los límites de la pista i
            if (targetY >= itemTop && targetY < itemBottom) {
                // Para cambiar de pista, requerimos que el cursor supere la mitad (50%) de la pista de destino.
                // Esto previene saltos accidentales inmediatos y bloquea el clip en la pista actual
                // hasta que de verdad se arrastra el cursor hacia el centro de la otra pista.
                if (i > dragState.lastTrack) {
                    // Moviendo hacia abajo: requiere pasar del 50% de la pista destino
                    if (targetY < itemTop + item.height * 0.5) {
                        return dragState.lastTrack
                    }
                } else if (i < dragState.lastTrack) {
                    // Moviendo hacia arriba: requiere pasar del 50% de la pista destino (desde abajo)
                    if (targetY > itemTop + item.height * 0.5) {
                        return dragState.lastTrack
                    }
                }

                // Evitar rebotes rápidos (200ms de debounce)
                const now = Date.now()
                if (lastTrackSwitch.track === i && (now - lastTrackSwitch.time) < 200) {
                    return dragState.lastTrack
                }
                
                lastTrackSwitch = { track: i, time: now }
                dragState.lastTrack = i
                return i
            }
        }
        
        // Si no está en ninguna, mantener la última pista válida
        return dragState.lastTrack
    }

    // Visual indicator for drag-and-drop between tracks
    property var dropIndicator: null
    function showTrackDropIndicator(trackIndex, show) {
        const item = trackListView.itemAtIndex(trackIndex)
        if (!item) return
        
        if (show) {
            // Create or show drop indicator
            if (!dropIndicator) {
                dropIndicator = Qt.createQmlObject(`
                    import QtQuick 2.15
                    Rectangle {
                        id: dropIndicatorRect
                        color: "#e74c3c"
                        height: 3
                        opacity: 0.7
                        z: 1000
                        visible: FeatureKutPod
                    }
                `, trackListView.contentItem)
            }
            
            dropIndicator.width = trackListView.width
            dropIndicator.y = item.y
            dropIndicator.visible = true
        } else if (dropIndicator) {
            dropIndicator.visible = false
        }
    }

    function zoomIn()  { root.timelineZoom = Math.min(512.0, root.timelineZoom * 1.25) }
    function zoomOut() { root.timelineZoom = Math.max(0.25, root.timelineZoom / 1.25) }
    function zoomReset() { root.timelineZoom = 1.0; root.timelineScrollX = 0 }
    function openChapterEditor() { chapterEditorDialog.open() }
    function openCustomFadeDialog(trackIndex, clipIndex, isFadeIn) {
        customFadeDialog.trackIndex = trackIndex
        customFadeDialog.clipIndex = clipIndex
        customFadeDialog.isFadeIn = isFadeIn
        const curSec = isFadeIn 
            ? TrackModel.clipFadeInSec(trackIndex, clipIndex)
            : TrackModel.clipFadeOutSec(trackIndex, clipIndex)
        customFadeDialog.currentSec = curSec > 0 ? curSec : 0.5
        customFadeDialog.open()
    }
    function zoomToFit() {
        // Calcular el zoom necesario para que todo el audio quepa en el viewport.
        const total = AudioEngine.totalTime || 0
        if (total <= 0) { zoomReset(); return }
        const viewportPx = Math.max(1, trackListView.width - root.clipAreaLeftPx - 6)
        // zoom = 1 significa que viewportPx muestra timelineDisplayDuration.
        // Queremos que viewportPx muestre 'total' segundos exactos (+5% margen).
        const needed = total * 1.05
        root.timelineDisplayDuration = Math.max(needed, 20)
        root.timelineZoom = root.timelineDisplayDuration / needed
        root.timelineScrollX = 0
    }

    // ---------- Atajos de teclado globales ----------
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (AudioEngine.isPlaying) AudioEngine.stop()
            else if (AudioEngine.isRecording) AudioEngine.stop()
            else {
                // Si hay selección, arrancar el playback desde selStart
                // para empezar el loop correctamente en esa región.
                if (root.hasSelection) AudioEngine.seekTime(root.selStart)
                AudioEngine.play()
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+R"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (AudioEngine.isRecording) AudioEngine.stop()
            else AudioEngine.record()
        }
    }
    Shortcut {
        sequence: "S"
        context: Qt.ApplicationShortcut
        onActivated: root.splitAtPlayhead()
    }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        context: Qt.ApplicationShortcut
        enabled: root.hasSelection
              || root.selectedClips.length > 0
              || root.hasTrackRegions
              || root.selectedTrack >= 0  // Add track selection
        onActivated: root.deleteSelected()
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: root.hasSelection || root.selectedClips.length > 0 || root.hasTrackRegions
        onActivated: {
            root.clearSelection()
            root.clearClipSelection()
            root.clearTrackRegions()
        }
    }
    Shortcut {
        sequences: ["Undo", "Ctrl+Z", "Cmd+Z"]
        context: Qt.ApplicationShortcut
        enabled: UndoManager.canUndo
        onActivated: UndoManager.undo()
    }
    Shortcut {
        sequences: ["Redo", "Ctrl+Shift+Z", "Ctrl+Y", "Cmd+Shift+Z"]
        context: Qt.ApplicationShortcut
        enabled: UndoManager.canRedo
        onActivated: UndoManager.redo()
    }
    Shortcut {
        sequence: "Ctrl+Plus"
        context: Qt.ApplicationShortcut
        onActivated: root.zoomIn()
    }
    Shortcut {
        sequence: "Ctrl+-"
        context: Qt.ApplicationShortcut
        onActivated: root.zoomOut()
    }
    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        onActivated: root.zoomToFit()
    }
    Shortcut {
        sequence: "Home"
        context: Qt.ApplicationShortcut
        onActivated: {
            AudioEngine.seekTime(0)
            root.timelineScrollX = 0
        }
    }
    Shortcut {
        sequence: "End"
        context: Qt.ApplicationShortcut
        onActivated: AudioEngine.seekTime(AudioEngine.totalTime)
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        context: Qt.ApplicationShortcut
        enabled: root.selectedClips.length > 0 || root.hasSelection || root.hasTrackRegions
        onActivated: root.copySelectedClips()
    }
    Shortcut {
        sequences: [StandardKey.Cut]
        context: Qt.ApplicationShortcut
        enabled: root.selectedClips.length > 0 || root.hasSelection || root.hasTrackRegions
        onActivated: root.cutSelectedClips()
    }
    Shortcut {
        sequences: [StandardKey.Paste]
        context: Qt.ApplicationShortcut
        enabled: root.clipboardClips.length > 0 || root.clipboardSnapshots.length > 0
        onActivated: root.pasteClips()
    }
    Shortcut {
        sequence: "Ctrl+D"
        context: Qt.ApplicationShortcut
        enabled: root.selectedClips.length > 0
        onActivated: root.duplicateSelectedClips()
    }
    Shortcut {
        sequences: [StandardKey.New]
        context: Qt.ApplicationShortcut
        onActivated: root.handleNewProject()
    }
    Shortcut {
        sequences: [StandardKey.Open]
        context: Qt.ApplicationShortcut
        onActivated: root.handleOpenProject()
    }
    Shortcut {
        sequences: [StandardKey.Save]
        context: Qt.ApplicationShortcut
        onActivated: root.handleSaveProject()
    }
    Shortcut {
        sequences: [StandardKey.SaveAs]
        context: Qt.ApplicationShortcut
        onActivated: root.handleSaveAsProject()
    }
    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: root.close()
    }
    Shortcut {
        sequence: "Ctrl+Return"
        context: Qt.ApplicationShortcut
        onActivated: {
            UndoManager.addTrack("")
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+N"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (root.selectedTracks.length > 0) {
                UndoManager.dynamicNormalizeTracks(root.selectedTracks, root.dynamicNormTargetLufs)
            } else if (root.selectedTrack >= 0) {
                UndoManager.dynamicNormalizeTrack(root.selectedTrack, root.dynamicNormTargetLufs)
            } else {
                UndoManager.dynamicNormalizeAllTracks(root.dynamicNormTargetLufs)
            }
        }
    }
    Shortcut {
        sequence: "M"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (TrackModel.count > 0)
                UndoManager.addChapterAtSec(AudioEngine.currentTime)
        }
    }
    Shortcut {
        sequences: ["SelectAll", "Ctrl+A", "Cmd+A"]
        context: Qt.ApplicationShortcut
        enabled: TrackModel.count > 0
        onActivated: root.selectAllTracks()
    }

    // ---------- Helpers para guardar/cargar ----------
    function handleNewProject() {
        if (ProjectIO.isDirty) {
            confirmDiscardDialog.pendingAction = "new"
            confirmDiscardDialog.open()
        } else {
            ProjectIO.newProject()
            UndoManager.clear()
        }
    }
    function handleOpenProject() {
        if (ProjectIO.isDirty) {
            confirmDiscardDialog.pendingAction = "open"
            confirmDiscardDialog.open()
        } else {
            openFileDialog.open()
        }
    }
    function handleSaveProject() {
        if (ProjectIO.currentPath === "") {
            saveFileDialog.open()
        } else {
            if (!ProjectIO.save()) {
                errorDialog.message = ProjectIO.lastError()
                errorDialog.open()
            }
        }
    }
    function handleSaveAsProject() { saveFileDialog.open() }
    
    function triggerTranscription(trackIndex) {
        if (trackIndex < 0) return
        if (Transcription.hasTranscription(trackIndex)) {
            confirmOverwriteTranscriptionDialog.pendingTrackIndex = trackIndex
            confirmOverwriteTranscriptionDialog.open()
        } else {
            Transcription.transcribeTrack(trackIndex)
        }
    }

    function checkAutoTranscribe() {
        if (autoTranscribeOnShow && isManuscriptVisible && selectedTrack >= 0) {
            if (!Transcription.hasTranscription(selectedTrack) && !Transcription.isTranscribing(selectedTrack)) {
                Transcription.transcribeTrack(selectedTrack)
            }
        }
    }

    onIsManuscriptVisibleChanged: checkAutoTranscribe()
    onSelectedTrackChanged: checkAutoTranscribe()
    onAutoTranscribeOnShowChanged: checkAutoTranscribe()

    QtObject {
        id: openFileDialog
        function open() {
            const initialDir = root.prefProjectsDir !== "" ? root.prefProjectsDir : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString().replace(/^file:\/\//, "")
            const path = FileHelper.getOpenFileName(
                qsTr("Abrir proyecto"),
                initialDir,
                qsTr("Proyectos Kut (*.kutproj)")
            )
            if (path !== "") {
                if (!ProjectIO.open(path)) {
                    errorDialog.message = ProjectIO.lastError()
                    errorDialog.open()
                } else {
                    UndoManager.clear()
                }
            }
        }
    }

    QtObject {
        id: saveFileDialog
        function open() {
            const initialDir = root.prefProjectsDir !== "" ? root.prefProjectsDir : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString().replace(/^file:\/\//, "")
            let defaultPath = initialDir + "/untitled.kutproj"
            if (ProjectIO.currentPath !== "") {
                defaultPath = ProjectIO.currentPath
            }
            let path = FileHelper.getSaveFileName(
                qsTr("Guardar proyecto como"),
                defaultPath,
                qsTr("Proyectos Kut (*.kutproj)"),
                "kutproj"
            )
            if (path !== "") {
                if (!path.endsWith(".kutproj")) path += ".kutproj"
                if (!ProjectIO.save(path)) {
                    errorDialog.message = ProjectIO.lastError()
                    errorDialog.open()
                    root._quitAfterSave = false
                } else if (root._quitAfterSave) {
                    root._quitAfterSave = false
                    root._doForceQuit()
                }
            } else {
                root._quitAfterSave = false
            }
        }
    }

    QtObject {
        id: importAudioDialog
        function open() {
            const home = FileHelper.homeDir()
            const files = FileHelper.getOpenFileNames(
                qsTr("Importar audio"),
                home,
                qsTr("Audio (*.wav *.mp3 *.flac *.ogg *.opus *.m4a *.aac);;Todos los archivos (*)")
            )
            if (files && files.length > 0) {
                for (var i = 0; i < files.length; i++) {
                    const path = files[i]
                    let targetIdx = root.selectedTrack
                    if (targetIdx < 0) {
                        TrackModel.addTrack("")
                        targetIdx = TrackModel.count - 1
                    }
                    if (!TrackModel.importAudioFile(targetIdx, path, AudioEngine.currentTime)) {
                        console.warn("[Import] Falló la importación de:", path)
                    }
                }
            }
        }
    }

    QtObject {
        id: chapterJsonDlg
        function open() {
            const home = FileHelper.homeDir()
            const path = FileHelper.getSaveFileName(
                qsTr("Exportar capítulos JSON"),
                home + "/untitled.json",
                "JSON (*.json)",
                "json"
            )
            if (path !== "") {
                var chapters = ChapterModel.toJson(true)
                var wrapper = { "version": "1.2.0", "chapters": chapters }
                var jsonStr = JSON.stringify(wrapper, null, 2)
                FileHelper.writeTextFile(path, jsonStr)
            }
        }
    }
    Dialog {
        id: confirmDiscardDialog
        title: qsTr("Cambios sin guardar")
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        modal: true
        anchors.centerIn: parent
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"
        property string pendingAction: ""
        Label { text: qsTr("El proyecto tiene cambios sin guardar. ¿Qué hacemos?") }
        onAccepted: {
            // "Save": guardar y luego ejecutar la acción
            if (ProjectIO.currentPath === "") {
                saveFileDialog.open()
                // No completamos la acción aquí (el diálogo no es tan sofisticado)
            } else {
                ProjectIO.save()
                if (pendingAction === "new") {
                    ProjectIO.newProject(); UndoManager.clear()
                } else if (pendingAction === "open") {
                    openFileDialog.open()
                } else if (pendingAction === "library") {
                    root.showLibrary = true
                    libraryView.refreshPodcasts()
                }
            }
            pendingAction = ""
        }
        onDiscarded: {
            if (pendingAction === "new") {
                ProjectIO.newProject(); UndoManager.clear()
            } else if (pendingAction === "open") {
                openFileDialog.open()
            } else if (pendingAction === "library") {
                root.showLibrary = true
                libraryView.refreshPodcasts()
            }
            pendingAction = ""
        }
        onRejected: { pendingAction = "" }
    }

    Dialog {
        id: customFadeDialog
        title: isFadeIn ? qsTr("Ajustar Fade In") : qsTr("Ajustar Fade Out")
        modal: true
        anchors.centerIn: parent
        width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel

        property int trackIndex: -1
        property int clipIndex: -1
        property bool isFadeIn: true
        property real currentSec: 0.5

        onOpened: {
            fadeSecField.text = currentSec.toFixed(2)
            fadeSecField.selectAll()
            fadeSecField.forceActiveFocus()
        }

        onAccepted: {
            const val = parseFloat(fadeSecField.text)
            if (isNaN(val) || val < 0) return
            if (isFadeIn) {
                const curOut = TrackModel.clipFadeOutSec(trackIndex, clipIndex)
                UndoManager.setClipFades(trackIndex, clipIndex, val, curOut)
            } else {
                const curIn = TrackModel.clipFadeInSec(trackIndex, clipIndex)
                UndoManager.setClipFades(trackIndex, clipIndex, curIn, val)
            }
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: customFadeDialog.isFadeIn
                    ? qsTr("Duración del Fade In (segundos):")
                    : qsTr("Duración del Fade Out (segundos):")
                color: root.textPri
                font.bold: true
            }
            RowLayout {
                spacing: 6
                Layout.fillWidth: true
                Button {
                    text: "- 0.5s"
                    onClicked: {
                        let v = Math.max(0, (parseFloat(fadeSecField.text) || 0) - 0.5)
                        fadeSecField.text = v.toFixed(2)
                    }
                }
                Button {
                    text: "- 0.1s"
                    onClicked: {
                        let v = Math.max(0, (parseFloat(fadeSecField.text) || 0) - 0.1)
                        fadeSecField.text = v.toFixed(2)
                    }
                }
                TextField {
                    id: fadeSecField
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    selectByMouse: true
                    onAccepted: customFadeDialog.accept()
                }
                Button {
                    text: "+ 0.1s"
                    onClicked: {
                        let v = (parseFloat(fadeSecField.text) || 0) + 0.1
                        fadeSecField.text = v.toFixed(2)
                    }
                }
                Button {
                    text: "+ 0.5s"
                    onClicked: {
                        let v = (parseFloat(fadeSecField.text) || 0) + 0.5
                        fadeSecField.text = v.toFixed(2)
                    }
                }
            }
        }
    }

    Window {
        id: errorDialog
        title: qsTr("Error")
        width: 420
        height: 140
        minimumWidth: 320
        minimumHeight: 120
        flags: Qt.Dialog | Qt.WindowCloseButtonHint
        modality: Qt.NonModal
        visible: false
        color: "#2a2d31"
        property string message: ""
        function open() { visible = true; raise(); requestActivate() }
        function close() { visible = false }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: errorDialog.message
                wrapMode: Text.WordWrap
                color: "#ecf0f1"
                verticalAlignment: Text.AlignVCenter
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Aceptar")
                onClicked: errorDialog.close()
            }
        }
    }
    Window {
        id: infoDialog
        title: qsTr("Información")
        width: 420
        height: 140
        minimumWidth: 320
        minimumHeight: 120
        flags: Qt.Dialog | Qt.WindowCloseButtonHint
        modality: Qt.NonModal
        visible: false
        color: "#2a2d31"
        property string message: ""
        function open() { visible = true; raise(); requestActivate() }
        function close() { visible = false }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: infoDialog.message
                wrapMode: Text.WordWrap
                color: "#ecf0f1"
                verticalAlignment: Text.AlignVCenter
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Aceptar")
                onClicked: infoDialog.close()
            }
        }
    }
    Window {
        id: deClickerDialog
        title: qsTr("Eliminar clics (De-Clicker)")
        width: 380
        height: 280
        minimumWidth: 380
        minimumHeight: 280
        maximumWidth: 380
        maximumHeight: 280
        flags: Qt.Window | Qt.WindowStaysOnTopHint
        color: "#1e222b"
        function open() { visible = true; raise() }
        function close() { visible = false }

        property int trackIndex: -1
        property real startSec: 0
        property real endSec: 0

        Column {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 18

            Label {
                text: qsTr("De-Clicker Heurístico")
                color: "#ffffff"
                font.pixelSize: 16
                font.bold: true
            }

            Label {
                text: qsTr("Reconstruye picos y chasquidos usando interpolación spline cúbica de Hermite.")
                color: "#8a95a5"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Rectangle {
                width: parent.width
                height: 1
                color: "#2f3542"
            }

            RowLayout {
                width: parent.width
                spacing: 12
                Label {
                    text: qsTr("Sensibilidad:")
                    color: "#a4b0be"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                ComboBox {
                    id: deClickSensCombo
                    currentIndex: 1 // Media
                    model: [qsTr("Baja"), qsTr("Media"), qsTr("Alta")]
                    width: 120
                }
            }

            RowLayout {
                width: parent.width
                spacing: 12
                Label {
                    text: qsTr("Umbral (dB):")
                    color: "#a4b0be"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                SpinBox {
                    id: deClickThreshSpin
                    from: -60
                    to: -10
                    value: -30
                    editable: true
                    width: 120
                }
            }

            Item {
                width: parent.width
                height: 12
            }

            Button {
                text: qsTr("Aplicar De-Clicker")
                width: parent.width
                height: 38
                onClicked: {
                    let sens = 0.5
                    if (deClickSensCombo.currentIndex === 0) sens = 0.2
                    else if (deClickSensCombo.currentIndex === 1) sens = 0.5
                    else if (deClickSensCombo.currentIndex === 2) sens = 0.8

                    let clicks = TrackModel.deClickSelection(
                        deClickerDialog.trackIndex,
                        deClickerDialog.startSec,
                        deClickerDialog.endSec,
                        deClickThreshSpin.value,
                        sens
                    )
                    
                    deClickerDialog.close()
                    infoDialog.message = qsTr("De-Clicker completado: se repararon %1 clics en el rango seleccionado.").arg(clicks)
                    infoDialog.open()
                }
                background: Rectangle {
                    color: parent.pressed ? "#1e7e34" : (parent.hovered ? "#218838" : "#28a745")
                    radius: 4
                }
                contentItem: Text {
                    anchors.centerIn: parent
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // ========================================================================
    //  Ventana de Atajos de Teclado
    // ========================================================================
    Window {
        id: shortcutsDialog
        title: qsTr("Atajos de teclado")
        width: 500
        height: 500
        flags: Qt.Window
        color: "#22252a"
        palette.window: "#22252a"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a2026"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"
        function open() { visible = true; raise() }
        function close() { visible = false }

        ScrollView {
            anchors.fill: parent
            clip: true

            Column {
                id: shortcutsCol
                width: parent.width
                spacing: 2
                padding: 8

                Label {
                    text: qsTr("Transporte")
                    color: root.accent; font.bold: true; font.pixelSize: 13
                    bottomPadding: 4
                }
                Repeater {
                    model: [
                        { key: "Espacio", desc: qsTr("Reproducir / Pausar") },
                        { key: "Ctrl+R", desc: qsTr("Grabar / Detener grabación") },
                        { key: "Inicio", desc: qsTr("Ir al inicio") },
                        { key: "Fin",    desc: qsTr("Ir al final") },
                    ]
                    delegate: Row {
                        width: parent.width - 16; spacing: 8
                        Label { text: modelData.key.replace("Ctrl", root.cmdModifier); color: root.textPri; font.family: "Menlo"; font.pixelSize: 11; width: 160 }
                        Label { text: modelData.desc; color: root.textSec; font.pixelSize: 11 }
                    }
                }

                Item { width: 1; height: 8 }
                Label {
                    text: qsTr("Edición")
                    color: root.accent; font.bold: true; font.pixelSize: 13
                    bottomPadding: 4
                }
                Repeater {
                    model: [
                        { key: "S",           desc: qsTr("Dividir clip en el cursor") },
                        { key: "Supr / ⌫",   desc: qsTr("Eliminar selección") },
                        { key: "Escape",      desc: qsTr("Quitar selección") },
                        { key: "Ctrl+Z",      desc: qsTr("Deshacer") },
                        { key: "Ctrl+Shift+Z", desc: qsTr("Rehacer") },
                        { key: "Ctrl+C",      desc: qsTr("Copiar clips seleccionados") },
                        { key: "Ctrl+X",      desc: qsTr("Cortar clips seleccionados") },
                        { key: "Ctrl+V",      desc: qsTr("Pegar clips") },
                        { key: "Ctrl+D",      desc: qsTr("Duplicar clips seleccionados") },
                        { key: "Ctrl+Shift+N", desc: qsTr("Normalización dinámica de la pista seleccionada (o todas)") },
                        { key: "M",           desc: qsTr("Añadir marcador de capítulo en el cursor") },
                    ]
                    delegate: Row {
                        width: parent.width - 16; spacing: 8
                        Label { text: modelData.key.replace("Ctrl", root.cmdModifier); color: root.textPri; font.family: "Menlo"; font.pixelSize: 11; width: 160 }
                        Label { text: modelData.desc; color: root.textSec; font.pixelSize: 11 }
                    }
                }

                Item { width: 1; height: 8 }
                Label {
                    text: qsTr("Proyecto")
                    color: root.accent; font.bold: true; font.pixelSize: 13
                    bottomPadding: 4
                }
                Repeater {
                    model: [
                        { key: "Ctrl+N",        desc: qsTr("Nuevo proyecto") },
                        { key: "Ctrl+O",        desc: qsTr("Abrir proyecto") },
                        { key: "Ctrl+S",        desc: qsTr("Guardar proyecto") },
                        { key: "Ctrl+Shift+S",  desc: qsTr("Guardar como…") },
                        { key: "Ctrl+Enter",    desc: qsTr("Añadir pista") },
                        { key: "Ctrl+Q",        desc: qsTr("Salir") },
                    ]
                    delegate: Row {
                        width: parent.width - 16; spacing: 8
                        Label { text: modelData.key.replace("Ctrl", root.cmdModifier); color: root.textPri; font.family: "Menlo"; font.pixelSize: 11; width: 160 }
                        Label { text: modelData.desc; color: root.textSec; font.pixelSize: 11 }
                    }
                }

                Item { width: 1; height: 8 }
                Label {
                    text: qsTr("Vista")
                    color: root.accent; font.bold: true; font.pixelSize: 13
                    bottomPadding: 4
                }
                Repeater {
                    model: [
                        { key: "Ctrl++",       desc: qsTr("Zoom in") },
                        { key: "Ctrl+-",       desc: qsTr("Zoom out") },
                        { key: "Ctrl+0",       desc: qsTr("Ajustar zoom al contenido") },
                        { key: "Ctrl+Rueda",   desc: qsTr("Zoom centrado en el cursor") },
                        { key: "Rueda",        desc: qsTr("Scroll horizontal") },
                    ]
                    delegate: Row {
                        width: parent.width - 16; spacing: 8
                        Label { text: modelData.key.replace("Ctrl", root.cmdModifier); color: root.textPri; font.family: "Menlo"; font.pixelSize: 11; width: 160 }
                        Label { text: modelData.desc; color: root.textSec; font.pixelSize: 11 }
                    }
                }

                Item { width: 1; height: 8 }
                Label {
                    text: qsTr("Timeline / Ratón")
                    color: root.accent; font.bold: true; font.pixelSize: 13
                    bottomPadding: 4
                }
                Repeater {
                    model: [
                        { key: "Alt+Click",       desc: qsTr("Añadir nodo de envolvente en clip") },
                        { key: "Alt+Click nodo",  desc: qsTr("Eliminar nodo de envolvente") },
                        { key: "Shift+Arrastre",  desc: qsTr("Seleccionar región en pista") },
                    ]
                    delegate: Row {
                        width: parent.width - 16; spacing: 8
                        Label { text: modelData.key.replace("Ctrl", root.cmdModifier); color: root.textPri; font.family: "Menlo"; font.pixelSize: 11; width: 160 }
                        Label { text: modelData.desc; color: root.textSec; font.pixelSize: 11 }
                    }
                }
            }
        }
    }

    ChapterEditor {
        id: chapterEditorDialog
    }

    ApplicationWindow {
        id: preferencesDialog
        title: qsTr("Preferencias")
        width: 560; height: 680
        minimumWidth: 480; minimumHeight: 500
        flags: Qt.Window
        color: "#22252a"

        palette.window: "#22252a"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#2a2d32"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.highlight: "#e74c3c"
        palette.highlightedText: "#ffffff"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        function open() { visible = true; raise() }
        function close() { visible = false }

        ScrollView {
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 10

                // ── Encabezado ──────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: "#2a2d32"

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.leftMargin: 20
                        text: qsTr("Preferencias")
                        color: root.textPri
                        font.pixelSize: 16; font.bold: true
                    }
                }

                Item { Layout.preferredHeight: 4 }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 10

                    // ── NORMALIZACIÓN DINÁMICA ───────────────────────────
                    Label {
                        text: qsTr("NORMALIZACIÓN DINÁMICA")
                        color: root.accent
                        font.pixelSize: 11; font.bold: true
                    }

                    Frame {
                        Layout.fillWidth: true
                        padding: 12
                        background: Rectangle { color: "#2a2d32"; radius: 6 }

                        ColumnLayout {
                            id: prefDynCol
                            width: parent.width
                            spacing: 8

                            CheckBox {
                                id: prefDynNorm
                                text: qsTr("Normalización automática y dinámica")
                                checked: root.dynamicNormEnabled
                                onToggled: root.dynamicNormEnabled = checked
                                Layout.fillWidth: true
                                palette.windowText: "#ecf0f1"
                                palette.text: "#ecf0f1"
                            }

                            Label {
                                text: qsTr("Analiza LUFS por clip (EBU R128) al parar la grabación "
                                         + "y ajusta cada clip al nivel objetivo. Incluye limitador "
                                         + "de picos (-1 dBTP) para evitar saturación.")
                                color: root.textSec
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                            }

                            // ── Dial compacto + label de preset ──────────
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                enabled: prefDynNorm.checked
                                opacity: enabled ? 1.0 : 0.4
                                spacing: 12

                                Slider {
                                    id: prefDynNormSlider
                                    from: -30; to: -10
                                    stepSize: 1
                                    value: root.dynamicNormTargetLufs
                                    Layout.fillWidth: true
                                    onMoved: root.dynamicNormTargetLufs = value

                                    ToolTip {
                                        parent: prefDynNormSlider.handle
                                        visible: prefDynNormSlider.pressed
                                        text: root.dynamicNormTargetLufs.toFixed(0) + " LUFS"
                                        font.pixelSize: 11
                                    }
                                }

                                Label {
                                    text: {
                                        var v = root.dynamicNormTargetLufs
                                        if (Math.abs(v - (-16)) < 0.5) return "-16 LUFS  Podcast"
                                        if (Math.abs(v - (-14)) < 0.5) return "-14 LUFS  Spotify"
                                        if (Math.abs(v - (-19)) < 0.5) return "-19 LUFS  Apple"
                                        if (Math.abs(v - (-23)) < 0.5) return "-23 LUFS  EBU R128"
                                        if (Math.abs(v - (-24)) < 0.5) return "-24 LUFS  Broadcast US"
                                        return v.toFixed(0) + " LUFS"
                                    }
                                    color: root.textPri
                                    font.pixelSize: 13
                                    font.family: "Menlo"
                                    Layout.preferredWidth: 160
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 2 }

                    // ── EDICIÓN ──────────────────────────────────────────
                    Label {
                        text: qsTr("EDICIÓN")
                        color: root.accent
                        font.pixelSize: 11; font.bold: true
                    }

                    Frame {
                        Layout.fillWidth: true
                        padding: 12
                        background: Rectangle { color: "#2a2d32"; radius: 6 }

                        ColumnLayout {
                            id: prefEditCol
                            width: parent.width
                            spacing: 8

                            CheckBox {
                                text: qsTr("Modo ripple por defecto (imán activado al iniciar)")
                                checked: root.rippleEdit
                                onToggled: root.rippleEdit = checked
                                Layout.fillWidth: true
                                palette.windowText: "#ecf0f1"
                                palette.text: "#ecf0f1"
                            }

                            CheckBox {
                                text: qsTr("Devolver el cursor a la posición de inicio al detener la reproducción")
                                checked: root.returnPlayheadOnStop
                                onToggled: root.returnPlayheadOnStop = checked
                                Layout.fillWidth: true
                                palette.windowText: "#ecf0f1"
                                palette.text: "#ecf0f1"
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 2 }

                    // ── EXPORTACIÓN / PROYECTO ───────────────────────────
                    Label {
                        text: qsTr("EXPORTACIÓN / PROYECTO")
                        color: root.accent
                        font.pixelSize: 11; font.bold: true
                    }

                    Frame {
                        Layout.fillWidth: true
                        padding: 12
                        background: Rectangle { color: "#2a2d32"; radius: 6 }

                        ColumnLayout {
                            id: prefExportCol
                            width: parent.width
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: qsTr("Sample rate:")
                                    color: root.textPri
                                    Layout.preferredWidth: 140
                                }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: [
                                        { text: "48000 Hz (recomendado)", value: 48000 },
                                        { text: "44100 Hz (CD)",          value: 44100 }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: root.prefSampleRate === 44100 ? 1 : 0
                                    onActivated: root.prefSampleRate = currentValue
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: qsTr("Bit depth (WAV):")
                                    color: root.textPri
                                    Layout.preferredWidth: 140
                                }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: [
                                        { text: "16 bits (PCM)", value: 16 },
                                        { text: "24 bits (PCM)", value: 24 },
                                        { text: "32 bits (float)", value: 32 }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: root.prefBitDepth === 16 ? 0
                                                : root.prefBitDepth === 32 ? 2 : 1
                                    onActivated: root.prefBitDepth = currentValue
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Label {
                                    text: qsTr("Carpeta de proyectos:")
                                    color: root.textPri
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    TextField {
                                        Layout.fillWidth: true
                                        text: root.prefProjectsDir
                                        placeholderText: qsTr("(usa ~/Documents)")
                                        placeholderTextColor: root.textSec
                                        color: root.textPri
                                        palette.text: root.textPri
                                        palette.placeholderText: root.textSec
                                        readOnly: true
                                        background: Rectangle {
                                            color: "#1f2226"
                                            radius: 4
                                            border.color: root.divider
                                        }
                                    }
                                    Button {
                                        text: qsTr("Elegir…")
                                        onClicked: prefProjectsDirDlg.open()
                                    }
                                    Button {
                                        text: qsTr("Quitar")
                                        enabled: root.prefProjectsDir !== ""
                                        onClicked: root.prefProjectsDir = ""
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 16 }
                }
            }
        }
    }

    FolderDialog {
        id: prefProjectsDirDlg
        title: qsTr("Carpeta por defecto para proyectos")
        onAccepted: {
            root.prefProjectsDir = selectedFolder.toString().replace(/^file:\/\//, "")
        }
    }

    Platform.MenuBar {
        Platform.Menu {
            title: qsTr("&Archivo")
            Platform.MenuItem {
                text: qsTr("Nuevo proyecto")
                onTriggered: root.handleNewProject()
            }
            Platform.MenuItem {
                text: qsTr("Abrir proyecto…")
                onTriggered: root.handleOpenProject()
            }
            Platform.MenuItem {
                text: qsTr("Recuperar sesión no guardada…")
                enabled: ProjectIO.hasRecoveryProject()
                onTriggered: recoveryDialog.open()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Guardar")
                onTriggered: root.handleSaveProject()
            }
            Platform.MenuItem {
                text: qsTr("Guardar como…")
                onTriggered: root.handleSaveAsProject()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Importar audio…")
                onTriggered: importAudioDialog.open()
            }
            Platform.MenuItem {
                text: qsTr("Exportar episodio…")
                enabled: TrackModel.count > 0
                onTriggered: exportDialog.open()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Biblioteca de podcasts")
                onTriggered: {
                    if (ProjectIO.isDirty) {
                        confirmDiscardDialog.pendingAction = "library"
                        confirmDiscardDialog.open()
                    } else {
                        root.showLibrary = true
                        libraryView.refreshPodcasts()
                    }
                }
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Preferencias…")
                onTriggered: preferencesDialog.open()
            }
            Platform.MenuItem {
                text: qsTr("Atajos de teclado…")
                onTriggered: shortcutsDialog.open()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Salir")
                onTriggered: root.close()
            }
        }
        Platform.Menu {
            title: qsTr("&Editar")
            Platform.MenuItem {
                text: qsTr("Deshacer")
                enabled: UndoManager.canUndo
                onTriggered: UndoManager.undo()
            }
            Platform.MenuItem {
                text: qsTr("Rehacer")
                enabled: UndoManager.canRedo
                onTriggered: UndoManager.redo()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Copiar clips seleccionados")
                enabled: root.selectedClips.length > 0
                onTriggered: root.copySelectedClips()
            }
            Platform.MenuItem {
                text: qsTr("Pegar clips")
                enabled: root.clipboardClips.length > 0
                onTriggered: root.pasteClips()
            }
            Platform.MenuItem {
                text: qsTr("Duplicar clips seleccionados")
                enabled: root.selectedClips.length > 0
                onTriggered: root.duplicateSelectedClips()
            }
        }
        Platform.Menu {
            title: qsTr("E&fectos")

            Platform.Menu {
                title: qsTr("Utilidades")
                Platform.MenuItem { text: qsTr("Trim / Ganancia…"); onTriggered: trimGainDialog.open() }
                Platform.MenuItem { text: qsTr("Inversión de fase…"); onTriggered: phaseInvertDialog.open() }
                Platform.MenuItem { text: qsTr("Mono mixer…"); onTriggered: monoMixerDialog.open() }
                Platform.MenuItem { text: qsTr("Stereo widener…"); onTriggered: stereoWidenerDialog.open() }
            }

            Platform.Menu {
                title: qsTr("Filtros")
                Platform.MenuItem { text: qsTr("Filtro paso alto…"); onTriggered: hpfDialog.open() }
                Platform.MenuItem { text: qsTr("Filtro paso bajo…"); onTriggered: lpfDialog.open() }
                Platform.MenuItem { text: qsTr("Filtro notch…"); onTriggered: notchDialog.open() }
                Platform.MenuItem { text: qsTr("Ecualizador…"); onTriggered: equalizerDialog.open() }
            }

            Platform.Menu {
                title: qsTr("Reducción de ruido")
                Platform.MenuItem { text: qsTr("Noise Reduction (Spectral)…"); onTriggered: noiseReductionDialog.open() }
                Platform.MenuItem { text: qsTr("Deep Denoise (AI)…"); onTriggered: deepDenoiseDialog.open() }
                Platform.MenuItem { text: qsTr("Puerta de ruido…"); onTriggered: noiseGateDialog.open() }
                Platform.MenuItem { text: qsTr("De-esser…"); onTriggered: deEsserDialog.open() }
                Platform.MenuSeparator {}
                Platform.MenuItem { text: qsTr("Truncar silencio…"); onTriggered: truncateSilenceDialog.open() }
            }

            Platform.Menu {
                title: qsTr("Dinámica")
                Platform.MenuItem { text: qsTr("Compresor…"); onTriggered: compressorDialog.open() }
                Platform.MenuItem { text: qsTr("Expander…"); onTriggered: expanderDialog.open() }
                Platform.MenuItem { text: qsTr("Limitador…"); onTriggered: limiterDialog.open() }
            }

            Platform.Menu {
                title: qsTr("Automatización")
                Platform.MenuItem { text: qsTr("Auto-gain…"); onTriggered: autoGainDialog.open() }
                Platform.MenuItem { text: qsTr("Autoduck…"); onTriggered: autoduckDialog.open() }
            }
        }
        Platform.Menu {
            title: qsTr("He&rramientas")
            Platform.MenuItem {
                text: qsTr("Transcripción local (Whisper)…")
                onTriggered: transcriptionSettingsDialog.open()
            }
            Platform.MenuItem {
                text: qsTr("Transcribir pista seleccionada")
                enabled: Transcription.available && root.selectedTrack >= 0
                onTriggered: root.triggerTranscription(root.selectedTrack)
            }
            Platform.MenuItem {
                text: qsTr("Auto-transcribir al mostrar manuscrito")
                checkable: true
                checked: root.autoTranscribeOnShow
                onTriggered: root.autoTranscribeOnShow = !root.autoTranscribeOnShow
            }
            Platform.MenuItem {
                text: root.isManuscriptVisible ? qsTr("Ocultar Manuscrito") : qsTr("Mostrar Manuscrito")
                onTriggered: root.isManuscriptVisible = !root.isManuscriptVisible
            }
            Platform.MenuSeparator {}
            Platform.Menu {
                title: qsTr("Capítulos")
                Platform.MenuItem {
                    text: qsTr("Editor de capítulos…")
                    onTriggered: chapterEditorDialog.open()
                }
                Platform.MenuSeparator {}
                Platform.MenuItem {
                    text: qsTr("Añadir capítulo en cursor")
                    enabled: TrackModel.count > 0
                    onTriggered: UndoManager.addChapterAtSec(AudioEngine.currentTime)
                }
                Platform.MenuItem {
                    text: qsTr("Eliminar todos los capítulos")
                    enabled: ChapterModel.count > 0
                    onTriggered: UndoManager.clearChapters()
                }
                Platform.MenuSeparator {}
                Platform.MenuItem {
                    text: qsTr("Exportar capítulos JSON (Podcasting 2.0)")
                    enabled: ChapterModel.count > 0
                    onTriggered: chapterJsonDlg.open()
                }
            }
        }
        Platform.Menu {
            title: qsTr("Ay&uda")
            Platform.MenuItem {
                text: qsTr("Buscar actualizaciones…")
                onTriggered: SparkleUpdater.checkForUpdates()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Acerca de Kut Editor…")
                onTriggered: aboutDialog.open()
            }
        }
    }

    // ---------- Barra superior: iconos SVG, estilo Hindenburg Pro ----------
    header: ToolBar {
        visible: !root.showLibrary
        background: Rectangle { color: root.panelBg }
        height: 40

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 6

            // === Helper para botón con icono MDI ===
            component ToolBtn : Button {
                implicitWidth: 32; implicitHeight: 32
                flat: true
                property string tip: ""
                property string ico: ""
                property color icoColor: enabled ? root.textPri : Qt.rgba(1,1,1,0.25)
                ToolTip.text: tip
                ToolTip.visible: hovered && tip !== ""
                ToolTip.delay: 300
                background: Rectangle {
                    radius: 4
                    color: parent.hovered && parent.enabled
                        ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                }
                contentItem: Text {
                    anchors.centerIn: parent
                    text: ico
                    color: icoColor
                    font.family: "Material Symbols Outlined"
                    font.pixelSize: 24
                    font.weight: Font.Thin
                    renderType: Text.NativeRendering
                    opacity: 0.85
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // --- New Track + Archivo ---
            ToolBtn { ico: Icons.newTrack; tip: qsTr("Insertar Pista (%1+Enter)").arg(root.cmdModifier)
                onClicked: { UndoManager.addTrack(""); AudioEngine.seekTime(0); root.timelineScrollX = 0 }
            }
            ToolBtn { ico: Icons.newProject; tip: qsTr("Nuevo Proyecto (%1+N)").arg(root.cmdModifier); onClicked: root.handleNewProject() }
            ToolBtn { ico: Icons.openFile; tip: qsTr("Abrir Proyecto (%1+O)").arg(root.cmdModifier); onClicked: root.handleOpenProject() }
            ToolBtn { ico: Icons.save; tip: qsTr("Guardar Proyecto (%1+S)").arg(root.cmdModifier); onClicked: root.handleSaveProject() }
            ToolBtn { ico: Icons.importAudio; tip: qsTr("Importar Audio"); onClicked: importAudioDialog.open() }
            ToolBtn { ico: Icons.exportAudio; tip: qsTr("Exportar Audio"); enabled: TrackModel.count > 0; onClicked: exportDialog.open() }
            ToolBtn { ico: Icons.publish; tip: qsTr("Publicar en KutPod"); enabled: TrackModel.count > 0; onClicked: { publishWindow.editMode = false; publishWindow.open() } }

            Rectangle { width: 1; height: 24; color: root.divider }

            // --- Edición ---
            ToolBtn { ico: Icons.cut; tip: qsTr("Cortar"); enabled: root.selectedClips.length > 0 || root.hasSelection || root.hasTrackRegions
                onClicked: root.cutSelectedClips()
            }
            ToolBtn { ico: Icons.copy; tip: qsTr("Copiar (%1+C)").arg(root.cmdModifier); enabled: root.selectedClips.length > 0 || root.hasSelection || root.hasTrackRegions
                onClicked: root.copySelectedClips()
            }
            ToolBtn { ico: Icons.paste; tip: qsTr("Pegar (%1+V)").arg(root.cmdModifier); enabled: root.clipboardClips.length > 0 || root.clipboardSnapshots.length > 0
                onClicked: root.pasteClips()
            }
            ToolBtn {
                ico: Icons.cleanTrack
                tip: root.selectedTracks.length > 1
                     ? qsTr("Limpiar pistas seleccionadas")
                     : qsTr("Limpiar Pista")
                enabled: root.selectedTracks.length > 0
                onClicked: {
                    UndoManager.clearAudio(root.selectedTracks)
                    AudioEngine.seekTime(0)
                    root.timelineScrollX = 0
                }
            }
            ToolBtn { ico: Icons.split; tip: qsTr("Split (S)"); onClicked: root.splitAtPlayhead() }
            ToolBtn {
                ico: Icons.deleteSpace
                tip: root.selectedTracks.length > 1
                    ? qsTr("Eliminar espacios en pistas seleccionadas")
                    : (root.selectedTracks.length === 1
                       ? qsTr("Eliminar espacios en pista %1").arg(root.selectedTracks[0] + 1)
                       : qsTr("Eliminar espacios en todas las pistas"))
                enabled: root.selectedTracks.length > 0
                onClicked: {
                    UndoManager.removeGaps(root.selectedTracks)
                }
            }

            Rectangle { width: 1; height: 24; color: root.divider }

            // --- Deshacer / Rehacer ---
            ToolBtn { ico: Icons.undo; tip: UndoManager.canUndo ? qsTr("Deshacer: %1 (%2+Z)").arg(UndoManager.undoText).arg(root.cmdModifier) : qsTr("Nada que deshacer")
                enabled: UndoManager.canUndo; onClicked: UndoManager.undo()
            }
            ToolBtn { ico: Icons.redo; tip: UndoManager.canRedo ? qsTr("Rehacer: %1 (%2+Shift+Z)").arg(UndoManager.redoText).arg(root.cmdModifier) : qsTr("Nada que rehacer")
                enabled: UndoManager.canRedo; onClicked: UndoManager.redo()
            }

            Rectangle { width: 1; height: 24; color: root.divider }

            // --- Zoom ---
            ToolBtn { ico: Icons.zoomIn; tip: qsTr("Zoom In (%1++)").arg(root.cmdModifier); onClicked: root.zoomIn() }
            ToolBtn { ico: Icons.zoomOut; tip: qsTr("Zoom Out (%1+-)").arg(root.cmdModifier); onClicked: root.zoomOut() }
            ToolBtn { ico: Icons.zoomFit; tip: qsTr("Ver todo el proyecto (%1+0)").arg(root.cmdModifier); onClicked: root.zoomToFit() }

            Rectangle { width: 1; height: 24; color: root.divider }

            // Ripple edit (magnet)
            Button {
                id: rippleBtn
                implicitWidth: 32; implicitHeight: 32; flat: true
                checked: root.rippleEdit
                onClicked: root.rippleEdit = !root.rippleEdit
                ToolTip.text: checked ? qsTr("Ripple ON") : qsTr("Ripple OFF")
                ToolTip.visible: hovered
                background: Rectangle {
                    radius: 4
                    color: rippleBtn.checked ? Qt.rgba(0.91,0.3,0.24,0.18) : (rippleBtn.hovered ? Qt.rgba(1,1,1,0.08) : "transparent")
                    border.color: rippleBtn.checked ? root.accent : "transparent"
                    border.width: rippleBtn.checked ? 1 : 0
                }
                contentItem: Text {
                    anchors.centerIn: parent
                    text: Icons.magnet
                    color: rippleBtn.checked ? root.accent : root.textPri
                    font.family: "Material Symbols Outlined"
                    font.pixelSize: 24
                    font.weight: Font.Thin
                    renderType: Text.NativeRendering
                    opacity: 0.85
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }

            // --- Proyecto / Título ---
            Rectangle {
                visible: ProjectIO.currentPath !== ""
                Layout.preferredHeight: 26
                implicitWidth: projectLabel.implicitWidth + 20
                Layout.maximumWidth: 300
                color: "transparent"; border.color: root.btnBorder; border.width: 1; radius: 4
                Label {
                    id: projectLabel
                    anchors.centerIn: parent
                    text: (ProjectIO.isDirty ? "\u25CF " : "") + ProjectIO.currentDisplayName
                    color: ProjectIO.isDirty ? root.accent : root.textPri
                    font.pixelSize: 11; elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, parent.width - 10)
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor; hoverEnabled: true
                    ToolTip.text: ProjectIO.currentPath !== "" ? ProjectIO.currentPath : qsTr("Proyecto sin guardar")
                    ToolTip.visible: containsMouse; ToolTip.delay: 400
                    onClicked: root.handleSaveProject()
                }
            }

            // --- Salida de audio ---
            ComboBox {
                id: outputDeviceCombo
                Layout.preferredWidth: 180; implicitHeight: 26; font.pixelSize: 10
                model: PipeWireOutputs; textRole: "deviceDescription"; valueRole: "deviceId"

                background: Rectangle {
                    color: "#1e2228"
                    radius: 3
                    border.color: outputDeviceCombo.activeFocus ? root.accent : "#3a4050"
                    border.width: 1
                }

                contentItem: Label {
                    anchors.fill: parent
                    leftPadding: 6
                    rightPadding: 24
                    text: outputDeviceCombo.displayText
                    color: "#ecf0f1"
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                property bool _updating: false
                function syncFromManager() {
                    _updating = true
                    var idx = indexOfValue(PipeWireManager.outputDeviceId)
                    currentIndex = idx >= 0 ? idx : 0
                    _updating = false
                }
                Component.onCompleted: syncFromManager()
                onModelChanged: Qt.callLater(syncFromManager)
                Connections {
                    target: PipeWireManager
                    function onOutputDeviceChanged() { outputDeviceCombo.syncFromManager() }
                    function onDevicesChanged() { Qt.callLater(outputDeviceCombo.syncFromManager) }
                }
                onActivated: { if (!_updating) PipeWireManager.outputDeviceId = currentValue !== undefined ? currentValue : "" }
                ToolTip.text: qsTr("Dispositivo de salida")
                ToolTip.visible: hovered
            }
        }
    }

    // ---------- Biblioteca (pantalla inicial) ----------
    Library {
        id: libraryView
        anchors.fill: parent
        visible: root.showLibrary
        projectsDir: root.prefProjectsDir
        z: 100

        onOpenEpisode: (path) => {
            if (ProjectIO.open(path)) {
                UndoManager.clear()
            } else {
                errorDialog.message = ProjectIO.lastError()
                errorDialog.open()
            }
            root.showLibrary = false
        }

        onSkipLibrary: {
            root.showLibraryOnStart = false
            root.showLibrary = false
        }

        onPodcastCreated: {
            // Si el usuario crea un podcast, activar la biblioteca al inicio
            root.showLibraryOnStart = true
        }

        onRecoverSessionRequested: {
            if (ProjectIO.recoverProject()) {
                UndoManager.clear()
                root.showLibrary = false
            } else {
                errorDialog.message = ProjectIO.lastError()
                errorDialog.open()
            }
        }
    }

    // ---------- Cuerpo ----------
    SplitView {
        id: bodyArea
        anchors.fill: parent
        visible: !root.showLibrary
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: 6
            color: SplitHandle.pressed ? root.accent : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
            
            Rectangle {
                anchors.centerIn: parent
                width: parent.width; height: 1
                color: root.divider
                opacity: 0.5
            }
        }

        Item {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 100

            MouseArea {
                anchors.fill: parent
                z: -2
                onClicked: root.clearTrackSelection()
            }

            Label {
            anchors.centerIn: parent
            visible: trackListView.count === 0
            text: trackListView.count === 0
                  ? qsTr("No hay pistas.\nPulsa «+ Añadir pista» o arrastra un archivo de audio aquí.")
                  : ""
            horizontalAlignment: Text.AlignHCenter
            color: root.textSec
            font.pixelSize: 14
        }

        // DropArea global: si arrastras un archivo fuera de una pista,
        // crea una pista nueva e importa. Los DropAreas de cada pista
        // tienen z mayor y se llevan el evento si cae dentro de una pista.
        DropArea {
            anchors.fill: parent
            property bool hovering: false
            onEntered: (drag) => {
                console.log("[DropArea:Global] onEntered hasUrls=" + drag.hasUrls
                            + " hasText=" + drag.hasText
                            + " formats=" + drag.formats)
                if (drag.hasUrls) hovering = true
            }
            onExited: { hovering = false; console.log("[DropArea:Global] onExited") }
            onDropped: (drop) => {
                hovering = false
                if (!drop.hasUrls) return
                for (const url of drop.urls) {
                    // Pasar la URL tal cual al C++; importAudioFile usa
                    // QUrl::toLocalFile() para resolver el path real.
                    const path = url.toString()
                    TrackModel.addTrack("")
                    const idx = TrackModel.count - 1
                    if (!TrackModel.importAudioFile(idx, path, 0)) {
                        console.warn("[Drop] Falló la importación de:", path)
                    }
                }
                drop.accept()
            }
        }

        // Ruler: barra horizontal superior para seleccionar y seekear
        Ruler {
            id: timelineRuler
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            appRoot: root
            leftOffsetPx: root.clipAreaLeftPx
            visible: trackListView.count > 0
            z: 2
        }

        // Overlay de capítulos: se posiciona sobre el área de clips,
        // alineado con la zona de audio (misma referencia que los clips).
        ChapterMarkers {
            id: chapterOverlay
            anchors.left: parent.left
            anchors.leftMargin: root.clipAreaLeftPx
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.top: timelineRuler.bottom
            anchors.bottom: timelineHScrollbar.top
            visible: trackListView.count > 0 && ChapterModel.count > 0
            z: 3  // sobre las pistas pero debajo de popups
            appRoot: root
            chapterModel: ChapterModel
        }

        ScrollView {
            id: timelineScroll
            anchors.left: parent.left
            anchors.top: trackListView.count > 0 ? timelineRuler.bottom : parent.top
            anchors.bottom: timelineHScrollbar.top
            anchors.right: parent.right
            clip: true
            visible: trackListView.count > 0

            ListView {
                id: trackListView
                model: TrackModel
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds

                // Qt 6 moderno: los roles del modelo se inyectan como
                // "required property" con el mismo nombre que el role.
                // Ya no existe la variable mágica `model.xxx`; hay que
                // declararlas explícitamente aquí en el delegate root.
                delegate: Item {
                    id: delegateRoot
                    width: trackListView.width
                    height: trackComp.height

                    // Roles inyectados por TrackModel (ver roleNames())
                    required property int     index
                    required property string  name
                    required property color   color
                    required property string  inputDevice
                    required property string  inputDeviceName
                    required property int     inputMode
                    required property int     inputChannelIndex
                    required property real    gain
                    required property real    pan
                    required property bool    muted
                    required property bool    solo
                    required property bool    armed
                    required property real    duration
                    required property bool    hasAudio

                    TrackComponent {
                        id: trackComp
                        width: parent.width

                        appRoot: root
                        dragOverlayItem: dragOverlay

                        trackIndex:        delegateRoot.index
                        trackName:         delegateRoot.name
                        trackColor:        delegateRoot.color
                        isMuted:           delegateRoot.muted
                        isSolo:            delegateRoot.solo
                        isArmed:           delegateRoot.armed
                        inputDeviceId:     delegateRoot.inputDevice
                        inputDeviceName:   delegateRoot.inputDeviceName
                        inputMode:         delegateRoot.inputMode
                        inputChannelIndex: delegateRoot.inputChannelIndex
                        gain:              delegateRoot.gain
                        pan:               delegateRoot.pan
                        duration:          delegateRoot.duration
                        hasAudio:          delegateRoot.hasAudio

                        accentColor:  root.accent
                        panelColor:   root.panelBg2
                        textColor:    root.textPri
                        subtextColor: root.textSec
                        dividerColor: root.divider

                        devicesModel: PipeWireManager

                        onRequestArmToggle:    { UndoManager.toggleArmed(delegateRoot.index) }
                        onRequestMuteToggle:   { UndoManager.toggleMute(delegateRoot.index) }
                        onRequestSoloToggle:   { UndoManager.toggleSolo(delegateRoot.index) }
                        onRequestDelete:       { confirmDeleteDialog.askDelete(delegateRoot.index) }
                        onRequestClearAudio:   {
                            UndoManager.clearAudio(delegateRoot.index)
                            AudioEngine.seekTime(0)
                            root.timelineScrollX = 0
                        }
                        onRequestNameChange:   (newName) => UndoManager.renameTrack(delegateRoot.index, newName)
                        onRequestGainChange:   (v)       => UndoManager.setGain(delegateRoot.index, v)
                        onRequestPanChange:    (v)       => UndoManager.setPan(delegateRoot.index, v)
                        onRequestDeviceChange: (devId, devName) => TrackModel.updateInputDevice(delegateRoot.index, devId, devName)
                        onRequestChannelChange: (mode, chIdx)   => TrackModel.updateInputChannel(delegateRoot.index, mode, chIdx)
                    }
                }
            }
        }

        // -------- Capa de overlay para clips arrastrados --------
        Item {
            id: dragOverlay
            anchors.left: timelineScroll.left
            anchors.leftMargin: root.clipAreaLeftPx
            anchors.right: timelineScroll.right
            anchors.top: timelineScroll.top
            anchors.bottom: timelineScroll.bottom
            clip: true
            z: 1000
        }

        // -------- Playhead global (cursor único desde la Ruler hasta abajo) --------
        Rectangle {
            id: globalPlayhead
            width: 2
            anchors.top: timelineRuler.top
            anchors.bottom: timelineScroll.bottom
            visible: trackListView.count > 0
            z: 999
            color: root.accent
            opacity: 0.9

            x: {
                const vx = root.secToViewX(AudioEngine.currentTime)
                const px = root.clipAreaLeftPx + vx
                return Math.max(root.clipAreaLeftPx - 1, Math.min(timelineScroll.width - 2, px - 1))
            }
        }

        // ---------- Scrollbar horizontal compartida del timeline ----------
        // Visible cuando hay zoom > 1 (es decir, hay contenido fuera del viewport).
        // Arrastrable para mover timelineScrollX global.
        Rectangle {
            id: timelineHScrollbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: visible ? 14 : 0
            color: visible ? "#1f2226" : "transparent"
            border.color: root.divider
            border.width: visible ? 1 : 0
            visible: trackListView.count > 0 && root.timelineZoom > 1.001
            //  - viewport visible = trackListView.width - clipAreaLeftPx
            //  - contenido total = viewport * timelineZoom
            //  - thumb width proporcional = viewport / content = 1/zoom
            readonly property real viewportPx: Math.max(1,
                trackListView.width - root.clipAreaLeftPx - 6)
            readonly property real trackPx: Math.max(1,
                width - root.clipAreaLeftPx - 6)
            readonly property real scrollRangePx:
                Math.max(0, viewportPx * root.timelineZoom - viewportPx)

            // Área "caminable" del thumb (excluye la zona del panel izquierdo).
            Item {
                id: scrollTrack
                anchors.left: parent.left
                anchors.leftMargin: root.clipAreaLeftPx
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.top: parent.top
                anchors.bottom: parent.bottom

                // El thumb ocupa proporcionalmente el viewport respecto al total.
                Rectangle {
                    id: scrollThumb
                    y: 2
                    height: parent.height - 4
                    width: Math.max(24, scrollTrack.width / root.timelineZoom)
                    x: {
                        if (timelineHScrollbar.scrollRangePx <= 0) return 0
                        const ratio = root.timelineScrollX / timelineHScrollbar.scrollRangePx
                        const freePx = scrollTrack.width - width
                        return Math.max(0, Math.min(freePx, ratio * freePx))
                    }
                    color: scrollMA.pressed
                        ? Qt.rgba(1, 1, 1, 0.38)
                        : (scrollMA.containsMouse
                            ? Qt.rgba(1, 1, 1, 0.28)
                            : Qt.rgba(1, 1, 1, 0.18))
                    radius: 4
                    border.color: Qt.rgba(0, 0, 0, 0.4)
                    border.width: 1
                }

                MouseArea {
                    id: scrollMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    property real pressXInThumb: 0

                    onPressed: (mouse) => {
                        // Si se pulsa fuera del thumb, centrarlo en el click.
                        if (mouse.x < scrollThumb.x ||
                            mouse.x > scrollThumb.x + scrollThumb.width) {
                            const freePx = scrollTrack.width - scrollThumb.width
                            const newX = Math.max(0, Math.min(freePx, mouse.x - scrollThumb.width / 2))
                            const ratio = freePx > 0 ? newX / freePx : 0
                            root.timelineScrollX = ratio * timelineHScrollbar.scrollRangePx
                            pressXInThumb = scrollThumb.width / 2
                        } else {
                            pressXInThumb = mouse.x - scrollThumb.x
                        }
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        const freePx = scrollTrack.width - scrollThumb.width
                        if (freePx <= 0) return
                        const newX = Math.max(0, Math.min(freePx, mouse.x - pressXInThumb))
                        const ratio = newX / freePx
                        root.timelineScrollX = ratio * timelineHScrollbar.scrollRangePx
                    }
                }
            }
        }
    }

    TranscriptionPanel {
            id: transcriptionTrack
            SplitView.preferredHeight: 200
            SplitView.minimumHeight: 80
            appRoot: root
        }
    }

    // ---------- Diálogo de confirmación de borrado ----------
    Dialog {
        id: confirmDeleteDialog
        width: 350
        property var pendingIndices: []
        title: confirmDeleteDialog.pendingIndices.length > 1 ? qsTr("Eliminar pistas") : qsTr("Eliminar pista")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        function askDelete(indices) {
            if (typeof indices === "number") {
                confirmDeleteDialog.pendingIndices = [indices]
            } else {
                confirmDeleteDialog.pendingIndices = indices
            }
            open()
        }

        Label {
            text: confirmDeleteDialog.pendingIndices.length > 1
                  ? qsTr("¿Eliminar las pistas seleccionadas y todo su audio?\nEsta acción no se puede deshacer.")
                  : qsTr("¿Eliminar la pista y todo su audio?\nEsta acción no se puede deshacer.")
            color: root.textPri
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (confirmDeleteDialog.pendingIndices.length > 0) {
                // Sort descending to avoid index shifts
                const sorted = confirmDeleteDialog.pendingIndices.slice().sort((a, b) => b - a)
                for (const idx of sorted) {
                    UndoManager.removeTrack(idx)
                }
                AudioEngine.seekTime(0)
                root.timelineScrollX = 0
            }
            confirmDeleteDialog.pendingIndices = []
        }
        onRejected: confirmDeleteDialog.pendingIndices = []
    }

    Dialog {
        id: confirmOverwriteTranscriptionDialog
        width: 380
        property int pendingTrackIndex: -1
        title: qsTr("Sobrescribir transcripción")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: parent
        palette.window: "#2a2d31"
        palette.windowText: "#ecf0f1"
        palette.text: "#ecf0f1"
        palette.base: "#1a1d21"
        palette.button: "#34383d"
        palette.buttonText: "#ecf0f1"
        palette.toolTipBase: "#1a1d21"
        palette.toolTipText: "#ecf0f1"

        contentItem: Column {
            spacing: 12
            Label {
                text: qsTr("Esta pista ya tiene una transcripción generada.")
                font.bold: true
                color: root.textPri
            }
            Label {
                text: qsTr("¿Deseas volver a transcribirla? Se eliminará la transcripción anterior permanentemente.")
                wrapMode: Text.WordWrap
                width: parent.width
                color: root.textSec
            }
        }
        onAccepted: {
            if (pendingTrackIndex >= 0) {
                Transcription.transcribeTrack(pendingTrackIndex)
            }
            pendingTrackIndex = -1
        }
        onRejected: pendingTrackIndex = -1
    }

    // ========== BARRA INFERIOR DE TRANSPORTE (estilo Hindenburg Pro) ==========
    // Propiedades del VU meter en el scope de root para evitar problemas de referencia
    property real _vuMasterL: 0
    property real _vuMasterR: 0
    property real _vuPeakL: 0
    property real _vuPeakR: 0

    function _vuLevelToWidth(v) {
        if (v <= 0.0001) return 0.0
        var db = 20.0 * Math.log(v) / Math.LN10
        if (db <= -60.0) return 0.0
        if (db >= 0.0) return 1.0
        if (db < -40) return (db + 60) / 20 * 0.30
        if (db < -20) return 0.30 + (db + 40) / 20 * 0.35
        if (db < -9)  return 0.65 + (db + 20) / 11 * 0.20
        return 0.85 + (db + 9) / 9 * 0.15
    }

    Connections {
        target: AudioEngine
        function onLevelsChanged() {
            root._vuMasterL = AudioEngine.masterLeftLevel
            root._vuMasterR = AudioEngine.masterRightLevel
            if (root._vuMasterL > root._vuPeakL) { root._vuPeakL = root._vuMasterL; vuPeakLTimer.restart() }
            if (root._vuMasterR > root._vuPeakR) { root._vuPeakR = root._vuMasterR; vuPeakRTimer.restart() }
        }
    }
    Timer { id: vuPeakLTimer; interval: 2000; onTriggered: root._vuPeakL = 0 }
    Timer { id: vuPeakRTimer; interval: 2000; onTriggered: root._vuPeakR = 0 }

    footer: ToolBar {
        id: transportBar
        visible: !root.showLibrary
        background: Rectangle { color: "#1a1d21"; border.color: root.divider; border.width: 1 }
        height: 44

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8; anchors.rightMargin: 8
            spacing: 4

            Button {
                implicitWidth: 32; implicitHeight: 32; flat: true
                onClicked: { AudioEngine.seekTime(0); root.timelineScrollX = 0 }
                ToolTip.text: qsTr("Ir al inicio (Home)"); ToolTip.visible: hovered
                background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1,1,1,0.08) : "transparent" }
                contentItem: Text { anchors.centerIn: parent; text: Icons.backward; color: root.textPri; font.family: "Material Symbols Outlined"; font.pixelSize: 24; font.weight: Font.Thin; renderType: Text.NativeRendering; opacity: 0.85; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }

            Button {
                id: footerPlayBtn
                implicitWidth: 32; implicitHeight: 32; flat: true
                enabled: TrackModel.count > 0 && !AudioEngine.isRecording
                onClicked: {
                    if (AudioEngine.isPlaying) AudioEngine.stop()
                    else { if (root.hasSelection) AudioEngine.seekTime(root.selStart); AudioEngine.play() }
                }
                ToolTip.text: AudioEngine.isPlaying ? qsTr("Pausar (Espacio)") : qsTr("Reproducir (Espacio)")
                ToolTip.visible: hovered
                background: Rectangle {
                    radius: 4
                    color: AudioEngine.isPlaying ? Qt.rgba(0.18,0.8,0.44,0.15) : (footerPlayBtn.hovered && footerPlayBtn.enabled ? Qt.rgba(1,1,1,0.08) : "transparent")
                    border.color: AudioEngine.isPlaying ? "#2ecc71" : "transparent"; border.width: AudioEngine.isPlaying ? 1 : 0
                }
                contentItem: Text {
                    anchors.centerIn: parent
                    text: Icons.play
                    color: footerPlayBtn.enabled ? (AudioEngine.isPlaying ? "#2ecc71" : root.textPri) : root.textSec
                    font.family: "Material Symbols Outlined"
                    font.pixelSize: 24
                    font.weight: Font.Thin
                    renderType: Text.NativeRendering
                    opacity: 0.85
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                implicitWidth: 32; implicitHeight: 32; flat: true
                onClicked: AudioEngine.stop()
                ToolTip.text: qsTr("Detener"); ToolTip.visible: hovered
                background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1,1,1,0.08) : "transparent" }
                contentItem: Text { anchors.centerIn: parent; text: Icons.stop; color: root.textPri; font.family: "Material Symbols Outlined"; font.pixelSize: 24; font.weight: Font.Thin; renderType: Text.NativeRendering; opacity: 0.85; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }

            Button {
                id: footerRecBtn
                implicitWidth: 32; implicitHeight: 32; flat: true
                enabled: TrackModel.armedCount > 0 && !AudioEngine.isPlaying
                onClicked: { if (AudioEngine.isRecording) AudioEngine.stop(); else AudioEngine.record() }
                ToolTip.text: TrackModel.armedCount > 0 ? qsTr("Grabar (%1+R)").arg(root.cmdModifier) : qsTr("Arma al menos una pista")
                ToolTip.visible: hovered
                background: Rectangle {
                    id: footerRecBg; radius: 4
                    color: AudioEngine.isRecording ? "#c0392b" : (footerRecBtn.hovered && footerRecBtn.enabled ? Qt.rgba(1,1,1,0.08) : "transparent")
                    SequentialAnimation on opacity {
                        running: AudioEngine.isRecording; loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.5; duration: 650 }
                        NumberAnimation { from: 0.5; to: 1.0; duration: 650 }
                        onRunningChanged: if (!running) footerRecBg.opacity = 1.0
                    }
                }
                contentItem: Text {
                    anchors.centerIn: parent
                    text: Icons.record
                    color: AudioEngine.isRecording ? "white" : (footerRecBtn.enabled ? root.accent : root.textSec)
                    font.family: "Material Symbols Outlined"
                    font.pixelSize: 24
                    font.weight: Font.Thin
                    renderType: Text.NativeRendering
                    opacity: 0.85
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                implicitWidth: 32; implicitHeight: 32; flat: true
                onClicked: AudioEngine.seekTime(AudioEngine.totalTime)
                ToolTip.text: qsTr("Ir al final (End)"); ToolTip.visible: hovered
                background: Rectangle { radius: 4; color: parent.hovered ? Qt.rgba(1,1,1,0.08) : "transparent" }
                contentItem: Text { anchors.centerIn: parent; text: Icons.forward; color: root.textPri; font.family: "Material Symbols Outlined"; font.pixelSize: 24; font.weight: Font.Thin; renderType: Text.NativeRendering; opacity: 0.85; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }

            Rectangle { width: 1; height: 28; color: root.divider }

            // Selector de velocidad de reproducción
            Rectangle {
                id: speedSelector
                Layout.preferredWidth: 64
                Layout.preferredHeight: 28
                color: speedMouse.containsMouse ? Qt.rgba(1,1,1,0.06) : "#111418"
                radius: 4; border.color: root.divider; border.width: 1

                readonly property var speeds: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0]
                property int currentIdx: 2 // default 1.0x

                Text {
                    id: speedText; anchors.centerIn: parent
                    font.family: "Menlo"; font.pixelSize: 12; font.bold: true
                    color: AudioEngine.playbackRate !== 1.0 ? root.accent : root.textPri
                    text: {
                        var r = AudioEngine.playbackRate
                        if (r === Math.floor(r)) return r.toFixed(0) + "x"
                        if (r * 10 === Math.floor(r * 10)) return r.toFixed(1) + "x"
                        return r.toFixed(2) + "x"
                    }
                }

                MouseArea {
                    id: speedMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        // Ciclar al siguiente valor
                        speedSelector.currentIdx = (speedSelector.currentIdx + 1) % speedSelector.speeds.length
                        AudioEngine.setPlaybackRate(speedSelector.speeds[speedSelector.currentIdx])
                    }
                    // Doble click = volver a 1x
                    onDoubleClicked: {
                        speedSelector.currentIdx = 2
                        AudioEngine.setPlaybackRate(1.0)
                    }
                }

                ToolTip.text: qsTr("Velocidad: click para cambiar, doble-click para 1x")
                ToolTip.visible: speedMouse.containsMouse
                ToolTip.delay: 600
            }

            // Timer grande estilo Hindenburg
            Rectangle {
                Layout.preferredWidth: 156
                Layout.preferredHeight: 32
                color: "#111418"; radius: 4; border.color: root.divider; border.width: 1
                Text {
                    id: timerText; anchors.centerIn: parent
                    font.family: "Menlo"; font.pixelSize: 22; font.bold: true; color: root.textPri
                    text: {
                        var t = AudioEngine.currentTime
                        var m = Math.floor(t / 60)
                        var s = Math.floor(t % 60)
                        var ms = Math.floor((t * 1000) % 1000)
                        return m.toString().padStart(2,'0') + ":" + s.toString().padStart(2,'0') + "." + ms.toString().padStart(3,'0')
                    }
                }
            }

            Rectangle { width: 1; height: 28; color: root.divider }

            // Indicador de efectos Master activos
            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 24
                radius: 4
                color: AudioEngine.activeMasterFxCount > 0 ? "#c0392b" : "#333"
                border.color: AudioEngine.activeMasterFxCount > 0 ? "#e74c3c" : root.divider
                border.width: 1

                Row {
                    id: masterFxRow
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        text: "graphic_eq"
                        font.family: "Material Symbols Outlined"
                        font.pixelSize: 14
                        color: AudioEngine.activeMasterFxCount > 0 ? "#fff" : "#666"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: AudioEngine.activeMasterFxCount > 0
                              ? qsTr("FX %1").arg(AudioEngine.activeMasterFxCount)
                              : qsTr("FX")
                        color: AudioEngine.activeMasterFxCount > 0 ? "#fff" : "#666"
                        font.pixelSize: 11
                        font.bold: AudioEngine.activeMasterFxCount > 0
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: masterFxMenu.open()
                    ToolTip.visible: containsMouse && !masterFxMenu.visible
                    ToolTip.text: AudioEngine.activeMasterFxCount > 0
                                  ? qsTr("%1 efectos master activos — click para ver").arg(AudioEngine.activeMasterFxCount)
                                  : qsTr("Efectos master — click para abrir")
                    ToolTip.delay: 400
                }

                Platform.Menu {
                    id: masterFxMenu

                    Platform.MenuItem {
                        text: qsTr("Trim / Ganancia…")
                        checkable: true
                        checked: AudioEngine.trimGain.enabled
                        onTriggered: trimGainDialog.open()
                    }
                    Platform.MenuSeparator {}
                    Platform.MenuItem {
                        text: qsTr("Filtro paso alto…")
                        checkable: true
                        checked: AudioEngine.highPassFilter.enabled
                        onTriggered: hpfDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("Filtro paso bajo…")
                        checkable: true
                        checked: AudioEngine.lowPassFilter.enabled
                        onTriggered: lpfDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("Ecualizador…")
                        checkable: true
                        checked: AudioEngine.equalizer.enabled
                        onTriggered: equalizerDialog.open()
                    }
                    Platform.MenuSeparator {}
                    Platform.MenuItem {
                        text: qsTr("Noise Reduction (Spectral)…")
                        checkable: true
                        checked: AudioEngine.deNoiser.enabled
                        onTriggered: denoiserDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("Deep Denoise…")
                        checkable: true
                        checked: AudioEngine.deepFilter.enabled
                        onTriggered: deepDenoiseDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("De-esser…")
                        checkable: true
                        checked: AudioEngine.deEsser.enabled
                        onTriggered: deEsserDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("Puerta de ruido…")
                        checkable: true
                        checked: AudioEngine.noiseGate.enabled
                        onTriggered: noiseGateDialog.open()
                    }
                    Platform.MenuSeparator {}
                    Platform.MenuItem {
                        text: qsTr("Compresor…")
                        checkable: true
                        checked: AudioEngine.compressor.enabled
                        onTriggered: compressorDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("Limitador…")
                        checkable: true
                        checked: AudioEngine.limiter.enabled
                        onTriggered: limiterDialog.open()
                    }
                    Platform.MenuSeparator {}
                    Platform.MenuItem {
                        text: qsTr("Auto-gain…")
                        checkable: true
                        checked: AudioEngine.autoGain.enabled
                        onTriggered: autoGainDialog.open()
                    }
                    Platform.MenuItem {
                        text: qsTr("Auto-duck…")
                        checkable: true
                        checked: AudioEngine.autoDuck.enabled
                        onTriggered: autoduckDialog.open()
                    }
                }
            }

            // VU Meter Master horizontal
            Item {
                Layout.fillWidth: true; Layout.maximumWidth: 500; Layout.preferredHeight: 28
                Column {
                    anchors.fill: parent; spacing: 2; topPadding: 2
                    Item {
                        width: parent.width; height: 10
                        Repeater {
                            model: [{db:-48,l:"-48"},{db:-36,l:"-36"},{db:-24,l:"-24"},{db:-12,l:"-12"},{db:-6,l:"-6"},{db:0,l:"0"}]
                            Text {
                                x: root._vuLevelToWidth(Math.pow(10, modelData.db/20)) * parent.width - 8
                                text: modelData.l; color: "#666"; font.pixelSize: 7
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 6; color: "#111"; radius: 2
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width * root._vuLevelToWidth(root._vuMasterL); radius: 2
                            color: width > parent.width * 0.9 ? "#e74c3c" : width > parent.width * 0.7 ? "#f39c12" : "#2ecc71"
                        }
                        Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; width: 2
                            x: parent.width * root._vuLevelToWidth(root._vuPeakL); color: "#fff"; visible: root._vuPeakL > 0.001 }
                    }
                    Rectangle {
                        width: parent.width; height: 6; color: "#111"; radius: 2
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: parent.width * root._vuLevelToWidth(root._vuMasterR); radius: 2
                            color: width > parent.width * 0.9 ? "#e74c3c" : width > parent.width * 0.7 ? "#f39c12" : "#2ecc71"
                        }
                        Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; width: 2
                            x: parent.width * root._vuLevelToWidth(root._vuPeakR); color: "#fff"; visible: root._vuPeakR > 0.001 }
                    }
                }
            }


        }
    }

    // ---------- Exportación ----------
    ExportDialog {
        id: exportDialog

        panelBg:  root.panelBg
        panelBg2: root.panelBg2
        textPri:  root.textPri
        textSec:  root.textSec
        divider:  root.divider
        accent:   root.accent

        defaultSampleRate: root.prefSampleRate
        defaultBitDepth:   root.prefBitDepth
        lastExportPath:    root.lastExportPath
        onLastExportPathChanged: root.lastExportPath = lastExportPath
        activeLocalPodcast: libraryView.currentPodcast
    }

    // ---------- Publicación en KutPod ----------
    PublishWindow {
        id: publishWindow
        prefSampleRate: root.prefSampleRate
        activeLocalPodcast: libraryView.currentPodcast
    }

    Connections {
        target: ExportManager
        function onExportFinished(success, filePath, errorMessage) {
            if (success) {
                // Actualizar preferencias del podcast si "Recordar" está activo.
                if (libraryView.currentPodcast && ProjectIO.currentPath !== "") {
                    const md = ProjectIO.metadata
                    const trackConfigs = []
                    for (let i = 0; i < TrackModel.count; i++) {
                        const td = TrackModel.getTrackData(i)
                        trackConfigs.push({
                            name:              td.name || "",
                            color:             td.color ? td.color.toString() : "#e74c3c",
                            inputDeviceId:     td.inputDevice || "",
                            inputDeviceName:   td.inputDeviceName || "",
                            inputMode:         td.inputMode || 0,
                            inputChannelIndex: td.inputChannelIndex || -1,
                            gain:              td.gain || 1.0
                        })
                    }
                    libraryView.updatePodcastAfterExport(
                        libraryView.currentPodcast.path,
                        md.episode || 0,
                        md.season || 1,
                        trackConfigs)
                }
            }
        }
    }

    // ========================================================================
    //  Ventana de Trim / Ganancia
    // ========================================================================
    Window {
        id: trimGainDialog
        title: qsTr("Trim / Ganancia")
        width: 340; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.trimGain
            fxName: "Trim / Ganancia"
            accentColor: "#cccccc"
            knobs: [{prop:"gainDb", label:"GAIN", from:-24, to:24, unit:" dB", color:"#ffffff", decimals:1, defaultValue:0}]
        }
    }

    // ========================================================================
    //  Ventana de Inversión de Fase
    // ========================================================================
    Window {
        id: phaseInvertDialog
        title: qsTr("Inversión de Fase")
        width: 380
        height: 450
        flags: Qt.Window
        color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        Column {
            spacing: 16
            padding: 16

            Row {
                spacing: 12
                Button {
                    id: phaseToggle
                    width: 80; height: 32
                    property bool active: false
                    text: active ? qsTr("ON") : qsTr("OFF")
                    Component.onCompleted: active = AudioEngine.phaseInvert.enabled
                    onClicked: { active = !active; AudioEngine.phaseInvert.setEnabled(active) }
                    background: Rectangle {
                        radius: 4
                        color: phaseToggle.active ? Qt.rgba(0.83,0.33,0.33,0.3) : Qt.rgba(1,1,1,0.08)
                        border.color: phaseToggle.active ? "#d45454" : root.divider; border.width: 1
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text; color: phaseToggle.active ? "#d45454" : root.textSec
                        font.bold: true; font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    anchors.verticalCenter: parent.children[0].verticalCenter
                    text: qsTr("Inversión de Fase"); color: root.textPri; font.pixelSize: 14; font.bold: true
                }
            }

            Row {
                spacing: 20
                Column {
                    spacing: 4
                    Button {
                        id: phaseLeftBtn
                        width: 130; height: 36
                        property bool inv: true
                        text: inv ? qsTr("⟁ L Invertido") : qsTr("L Normal")
                        Component.onCompleted: inv = AudioEngine.phaseInvert.invertLeft
                        onClicked: { inv = !inv; AudioEngine.phaseInvert.setInvertLeft(inv) }
                        background: Rectangle {
                            radius: 4
                            color: phaseLeftBtn.inv ? Qt.rgba(0.83,0.33,0.33,0.2) : Qt.rgba(1,1,1,0.08)
                            border.color: phaseLeftBtn.inv ? "#d45454" : root.divider; border.width: 1
                        }
                        contentItem: Label {
                            anchors.centerIn: parent
                            text: parent.text; color: phaseLeftBtn.inv ? "#d45454" : root.textSec
                            font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                Column {
                    spacing: 4
                    Button {
                        id: phaseRightBtn
                        width: 130; height: 36
                        property bool inv: true
                        text: inv ? qsTr("⟁ R Invertido") : qsTr("R Normal")
                        Component.onCompleted: inv = AudioEngine.phaseInvert.invertRight
                        onClicked: { inv = !inv; AudioEngine.phaseInvert.setInvertRight(inv) }
                        background: Rectangle {
                            radius: 4
                            color: phaseRightBtn.inv ? Qt.rgba(0.83,0.33,0.33,0.2) : Qt.rgba(1,1,1,0.08)
                            border.color: phaseRightBtn.inv ? "#d45454" : root.divider; border.width: 1
                        }
                        contentItem: Label {
                            anchors.centerIn: parent
                            text: parent.text; color: phaseRightBtn.inv ? "#d45454" : root.textSec
                            font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Label {
                width: parent.width - 32
                text: qsTr("Invierte la polaridad de los canales seleccionados. Útil para corregir problemas de fase en grabaciones multi-micrófono.")
                color: root.textSec; font.pixelSize: 10; wrapMode: Text.WordWrap
            }
        }
    }

    // ========================================================================
    //  Ventana de Mono Mixer
    // ========================================================================
    Window {
        id: monoMixerDialog
        title: qsTr("Mono Mixer")
        width: 300; height: 130
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.monoMixer
            fxName: "Mezcla mono"
            accentColor: "#95a5a6"
            knobs: []
        }
    }

    // ========================================================================
    //  Ventana de Stereo Widener
    // ========================================================================
    Window {
        id: stereoWidenerDialog
        title: qsTr("Stereo Widener")
        width: 300; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.stereoWidener
            fxName: "Ensanchador estéreo"
            accentColor: "#1abc9c"
            knobs: [{prop:"width", label:"WIDTH", from:0, to:2, unit:"", color:"#1abc9c", decimals:2, defaultValue:1.0}]
        }
    }

    // ========================================================================
    //  Ventana del Filtro Paso Bajo (LPF)
    // ========================================================================
    Window {
        id: lpfDialog
        title: qsTr("Filtro Paso Bajo")
        width: 300; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.lowPassFilter
            fxName: "Corte de Agudos"
            accentColor: "#3498db"
            knobs: [{prop:"cutoffHz", label:"CUTOFF", from:1000, to:20000, unit:" Hz", color:"#3498db", decimals:0, defaultValue:20000}]
        }
    }

    // ========================================================================
    //  Ventana del Filtro Notch
    // ========================================================================
    Window {
        id: notchDialog
        title: qsTr("Filtro Notch")
        width: 380; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.notchFilter
            fxName: "Filtro Notch"
            accentColor: "#8e44ad"
            knobs: [
                {prop:"frequency", label:"FREQ",  from:20, to:20000, unit:" Hz", color:"#9b59b6", decimals:0, defaultValue:1000},
                {prop:"q",         label:"Q",     from:0.5, to:30, unit:"", color:"#6c3483", decimals:1, defaultValue:1.0}
            ]
        }
    }

    // ========================================================================
    //  Ventana del De-esser
    // ========================================================================
    Window {
        id: deEsserDialog
        title: qsTr("De-esser")
        width: 460; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.deEsser
            fxName: "De-esser"
            accentColor: "#f1c40f"
            knobs: [
                {prop:"frequency",   label:"FREQ",    from:2000, to:12000, unit:" Hz", color:"#f1c40f", decimals:0, defaultValue:6000},
                {prop:"thresholdDb", label:"THRESH",  from:-40,  to:0,     unit:" dB", color:"#d4ac0d", decimals:0, defaultValue:-20},
                {prop:"ratio",       label:"RATIO",   from:1,    to:10,    unit:"",    color:"#b7950b", decimals:1, defaultValue:2},
                {prop:"releaseMs",   label:"RELEASE", from:1,    to:200,   unit:" ms", color:"#9a7d0a", decimals:0, defaultValue:50}
            ]
        }
    }



    // ========================================================================
    //  Ventana del Expander
    // ========================================================================
    Window {
        id: expanderDialog
        title: qsTr("Expander")
        width: 460; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.expander
            fxName: "Expansor"
            accentColor: "#9b59b6"
            knobs: [
                {prop:"thresholdDb", label:"THRESH",  from:-80, to:0,   unit:" dB", color:"#9b59b6", decimals:0, defaultValue:-40},
                {prop:"ratio",       label:"RATIO",   from:1,   to:10,  unit:"",    color:"#8e44ad", decimals:1, defaultValue:2},
                {prop:"attackMs",    label:"ATTACK",  from:0.1, to:100, unit:" ms", color:"#7d3c98", decimals:1, defaultValue:10},
                {prop:"releaseMs",   label:"RELEASE", from:1,   to:500, unit:" ms", color:"#6c3483", decimals:0, defaultValue:100}
            ]
        }
    }



    // ========================================================================
    //  Ventana del Auto-Gain
    // ========================================================================
    Window {
        id: autoGainDialog
        title: qsTr("Auto-Gain")
        width: 380; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.autoGain
            fxName: "Ganancia automática"
            accentColor: "#2ecc71"
            knobs: [
                {prop:"targetDb",   label:"TARGET",   from:-30, to:0,    unit:" dB", color:"#2ecc71", decimals:0, defaultValue:-18},
                {prop:"responseMs", label:"RESPONSE", from:50,  to:3000, unit:" ms", color:"#27ae60", decimals:0, defaultValue:500},
                {prop:"maxGainDb",  label:"MAX GAIN", from:0,   to:24,   unit:" dB", color:"#1e8449", decimals:0, defaultValue:12}
            ]
        }
    }


    // ========================================================================
    //  Ventana del Filtro Paso Alto (HPF)
    // ========================================================================
    Window {
        id: hpfDialog
        title: qsTr("Filtro Paso Alto")
        width: 300; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        FxKnobPanel {
            anchors.fill: parent
            fx: AudioEngine.highPassFilter
            fxName: "Corte de Graves"
            accentColor: "#e67e22"
            knobs: [{prop:"cutoffHz", label:"CUTOFF", from:20, to:500, unit:" Hz", color:"#e67e22", decimals:0, defaultValue:20}]
        }
    }

    // ========================================================================
    //  Ventana de Truncar Silencio
    // ========================================================================
    Window {
        id: truncateSilenceDialog
        title: qsTr("Truncar silencio")
        width: 440
        height: 280
        flags: Qt.Window
        color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            // Selector de pista
            Row {
                spacing: 8
                Label { text: qsTr("Pista objetivo:"); color: "#ccc"; anchors.verticalCenter: parent.children[1].verticalCenter; font.pixelSize: 12; font.bold: true }
                ComboBox {
                    id: tsTrackCombo; width: 220
                    model: {
                        var items = [qsTr("Todas las pistas")]
                        for (var i = 0; i < TrackModel.count; i++)
                            items.push(qsTr("Pista %1: %2").arg(i + 1).arg(TrackModel.getTrackData(i).name || qsTr("Sin nombre")))
                        return items
                    }
                }
            }

            TruncateSilencePanel {
                id: tsPanel
                width: parent.width
                onApplyRequested: (threshold, minSilence, maxSilence) => {
                    var trackIdx = tsTrackCombo.currentIndex - 1
                    var total = 0
                    if (trackIdx < 0) {
                        for (var i = 0; i < TrackModel.count; i++)
                            total += UndoManager.truncateSilence(i, threshold, minSilence / 1000.0, maxSilence / 1000.0)
                    } else {
                        total = UndoManager.truncateSilence(trackIdx, threshold, minSilence / 1000.0, maxSilence / 1000.0)
                    }
                    tsStatus.text = qsTr("%1 regiones de silencio procesadas.").arg(total)
                    tsStatus.color = total > 0 ? "#2ecc71" : "#f39c12"
                }
            }

            Label { id: tsStatus; text: ""; color: "#888"; font.pixelSize: 11; anchors.horizontalCenter: parent.horizontalCenter }
        }
    }

    // ========================================================================
    //  Ventana de Noise Reduction (sustracción espectral)
    // ========================================================================
    Window {
        id: noiseReductionDialog
        title: qsTr("Noise Reduction (Avanzado)")
        width: 440
        height: 620
        flags: Qt.Window | Qt.WindowStaysOnTopHint
        color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        property bool hasProfile: TrackModel.hasNoiseProfile

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Label {
                text: qsTr("Noise Reduction — Spectral Subtraction")
                color: "#fff"; font.pixelSize: 14; font.bold: true
            }

            // --- Visualizador del Perfil Capturado ---
            SpectralDisplay {
                width: parent.width
                height: 140
                active: noiseReductionDialog.hasProfile
                // Solo mostramos el ruido capturado ya que es un proceso offline
                noiseMags: TrackModel.noiseProfileMagnitudes
                inputMags: []
                outputMags: []
                
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Sin perfil capturado")
                    color: "#555"; font.pixelSize: 12
                    visible: !noiseReductionDialog.hasProfile
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#3c4146" }

            // Paso 1: Captura
            Label {
                text: qsTr("Paso 1: Capturar perfil de ruido")
                color: "#1abc9c"; font.pixelSize: 12; font.bold: true
            }
            
            Grid {
                columns: 2; spacing: 8; width: parent.width
                Label { text: qsTr("Pista:"); color: "#888"; verticalAlignment: Text.AlignVCenter; height: 32 }
                ComboBox {
                    id: nrTrackCombo; width: parent.width - 60
                    model: {
                        var items = []
                        for (var i = 0; i < TrackModel.count; i++)
                            items.push(qsTr("Pista %1").arg(i + 1))
                        return items
                    }
                }
                Label { text: qsTr("Clip:"); color: "#888"; verticalAlignment: Text.AlignVCenter; height: 32 }
                SpinBox { id: nrClipSpin; from: 1; to: 99; value: 1 }
            }

            Button {
                text: qsTr("Obtener perfil de ruido")
                width: parent.width
                onClicked: {
                    TrackModel.noiseProfileCapture(
                        nrTrackCombo.currentIndex, nrClipSpin.value - 1,
                        nrFromSpin.value, nrToSpin.value)
                    nrStatus.text = TrackModel.hasNoiseProfile ? qsTr("Perfil capturado.") : qsTr("Error al capturar.")
                    nrStatus.color = TrackModel.hasNoiseProfile ? "#2ecc71" : "#e74c3c"
                }
                background: Rectangle { color: "#2980b9"; radius: 4 }
            }

            Row {
                spacing: 8; width: parent.width
                Label { text: qsTr("Rango (s):"); color: "#888"; anchors.verticalCenter: parent.verticalCenter }
                SpinBox { id: nrFromSpin; from: 0; to: 999; value: 0; editable: true }
                Label { text: "-"; color: "#888"; anchors.verticalCenter: parent.verticalCenter }
                SpinBox { id: nrToSpin; from: 0; to: 999; value: 0; editable: true }
            }

            Rectangle { width: parent.width; height: 1; color: "#3c4146" }

            // Paso 2: Aplicar
            Label {
                text: qsTr("Paso 2: Parámetros y Aplicación")
                color: noiseReductionDialog.hasProfile ? "#1abc9c" : "#444"
                font.pixelSize: 12; font.bold: true
            }

            Row {
                width: parent.width; spacing: 30
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: noiseReductionDialog.hasProfile
                opacity: enabled ? 1.0 : 0.3

                Column {
                    spacing: 4
                    anchors.verticalCenter: parent.verticalCenter
                    BigMetalKnob {
                        id: nrReductionDial
                        size: 100
                        from: 0; to: 40
                        value: 12
                        activeColor: "#1abc9c"
                    }
                    Label {
                        text: qsTr("Reduction")
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#aaa"
                        font.pixelSize: 12
                    }
                }

                Column {
                    spacing: 4
                    anchors.verticalCenter: parent.verticalCenter
                    BigMetalKnob {
                        id: nrSensitivityDial
                        size: 100
                        from: 0; to: 24
                        value: 6
                        activeColor: "#f39c12"
                    }
                    Label {
                        text: qsTr("Sensitivity")
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#aaa"
                        font.pixelSize: 12
                    }
                }
            }

            Button {
                text: qsTr("Aplicar Noise Reduction")
                width: parent.width
                enabled: noiseReductionDialog.hasProfile
                onClicked: {
                    var ok = UndoManager.noiseReduce(
                        nrTrackCombo.currentIndex, nrClipSpin.value - 1,
                        nrReductionDial.value, nrSensitivityDial.value)
                    nrStatus.text = ok ? qsTr("Efecto aplicado correctamente.") : qsTr("Error al aplicar.")
                }
                background: Rectangle { color: parent.enabled ? "#27ae60" : "#444"; radius: 4 }
            }

            Label { id: nrStatus; text: ""; color: "#888"; font.pixelSize: 11; anchors.horizontalCenter: parent.horizontalCenter }
        }
    }

    // ========================================================================
    //  Ventana del DeNoiser
    // ========================================================================
    Window {
        id: denoiserDialog
        title: qsTr("Noise Reduction")
        width: 420
        height: 600
        flags: Qt.Window | Qt.WindowStaysOnTopHint
        color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 24

            // --- Visualizador Espectral ---
            SpectralDisplay {
                width: parent.width
                height: 180
                active: denoiserToggle.active && denoiserDialog.visible
                inputMags: AudioEngine.deNoiser.inputMagnitudes
                outputMags: AudioEngine.deNoiser.outputMagnitudes
                noiseMags: AudioEngine.deNoiser.noiseMagnitudes
            }

            // --- Control de estado (ON/OFF) ---
            Row {
                width: parent.width
                layoutDirection: Qt.RightToLeft
                
                Rectangle {
                    id: denoiserToggle
                    width: 44; height: 22
                    radius: 4
                    property bool active: AudioEngine.deNoiser.enabled
                    color: active ? "#2980b9" : "#333"
                    border.color: active ? "#3498db" : "#444"
                    
                    Text {
                        anchors.centerIn: parent
                        text: parent.active ? "on" : "off"
                        color: parent.active ? "white" : "#888"
                        font.pixelSize: 10; font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            denoiserToggle.active = !denoiserToggle.active
                            AudioEngine.deNoiser.setEnabled(denoiserToggle.active)
                        }
                    }
                }
            }

            // --- Diales de Control ---
            Item {
                width: parent.width; height: 140

                BigMetalKnob {
                    id: denoiserReductionDial
                    size: 120
                    from: 0; to: 1.0
                    value: AudioEngine.deNoiser.reduction
                    activeColor: "#3498db"
                    anchors.centerIn: parent
                    onMoved: (val) => AudioEngine.deNoiser.setReduction(val)
                    
                    Label {
                        text: qsTr("Reduction")
                        anchors.top: denoiserReductionDial.bottom
                        anchors.topMargin: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#aaa"
                        font.pixelSize: 12
                    }
                    Label {
                        anchors.top: parent.top; anchors.topMargin: -16
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: parent.value.toFixed(1) + " dB"
                        color: "#666"; font.pixelSize: 9
                    }
                }
            }

            Label {
                text: qsTr("Smoothing: valores altos = más suave. Bajos = más agresivo.")
                color: "#555"; font.pixelSize: 10
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // ========================================================================
    //  Ventana de la Puerta de Ruido
    // ========================================================================
    Window {
        id: noiseGateDialog
        title: qsTr("Puerta de Ruido")
        width: 300; height: 360
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }
        NoiseGatePanel {
            anchors.fill: parent
            anchors.margins: 16
            fx: AudioEngine.noiseGate
        }
    }

    // ========================================================================
    //  Ventana de Deep Denoise (AI) Master
    // ========================================================================
    Window {
        id: deepDenoiseDialog
        title: qsTr("Deep Denoise")
        width: 320; height: 320
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        property var dfx: AudioEngine.deepFilter

        Column {
            spacing: 12
            anchors.fill: parent
            anchors.margins: 16

            Row {
                spacing: 10
                Label {
                    text: qsTr("Deep Denoise")
                    color: "#fff"; font.bold: true; font.pixelSize: 14
                    anchors.verticalCenter: parent.children[1].verticalCenter
                }
                Button {
                    width: 48; height: 26
                    text: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.enabled ? "on" : "off"
                    onClicked: { if (deepDenoiseDialog.dfx) deepDenoiseDialog.dfx.setEnabled(!deepDenoiseDialog.dfx.enabled) }
                    background: Rectangle {
                        radius: 3
                        color: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.enabled ? Qt.rgba(0.5, 0.1, 0.8, 0.3) : Qt.rgba(1,1,1,0.08)
                        border.color: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.enabled ? "#8e44ad" : "#555"
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.enabled ? "#8e44ad" : "#888"
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#2a2d32" }

            Rectangle {
                width: parent.width; height: 48
                color: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.isReady ? "#1a2030" : "#241e10"
                radius: 4; border.color: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.isReady ? "#2a3850" : "#5a3e10"
                visible: deepDenoiseDialog.dfx !== null
                
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 8
                    
                    Label {
                        Layout.fillWidth: true
                        text: {
                            if (!deepDenoiseDialog.dfx) return ""
                            if (deepDenoiseDialog.dfx.isReady) return "✓ Modelo IA cargado y listo."
                            if (deepDenoiseDialog.dfx.downloadProgress > 0 && deepDenoiseDialog.dfx.downloadProgress < 1.0)
                                return "Descargando modelo: " + Math.round(deepDenoiseDialog.dfx.downloadProgress * 100) + "%"
                            return "⚠ Faltan los pesos del modelo (~20MB)."
                        }
                        color: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.isReady ? "#5dade2" : "#d4a017"
                        font.pixelSize: 10; wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    Button {
                        text: "Descargar"
                        visible: deepDenoiseDialog.dfx && !deepDenoiseDialog.dfx.isReady && deepDenoiseDialog.dfx.downloadProgress === 0
                        onClicked: { if(deepDenoiseDialog.dfx) deepDenoiseDialog.dfx.retryDownload() }
                        background: Rectangle { color: parent.pressed ? "#7a5a00" : "#5a4200"; radius: 3; border.color: "#d4a017"; border.width: 1 }
                        contentItem: Label { anchors.centerIn: parent; text: parent.text; color: "#f0c040"; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }

            Item {
                width: parent.width; height: 160
                visible: deepDenoiseDialog.dfx && deepDenoiseDialog.dfx.isReady

                BigMetalKnob {
                    id: deepNrReductionMaster
                    size: 110
                    from: 0; to: 100.0
                    value: deepDenoiseDialog.dfx ? deepDenoiseDialog.dfx.reductionDb : 40.0
                    activeColor: "#8e44ad"
                    anchors.centerIn: parent
                    onMoved: (val) => { if (deepDenoiseDialog.dfx) deepDenoiseDialog.dfx.setReductionDb(val) }
                    
                    Label {
                        text: qsTr("Reduction (dB)")
                        anchors.top: deepNrReductionMaster.bottom
                        anchors.topMargin: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#aaa"
                        font.pixelSize: 12
                    }
                    
                    Label {
                        anchors.top: parent.top; anchors.topMargin: -12
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Math.round(deepNrReductionMaster.value) + " dB"
                        color: "#8e44ad"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }
    }

    // ========================================================================
    //  Ventana del Autoduck
    // ========================================================================
    Window {
        id: autoduckDialog
        title: qsTr("Autoduck")
        width: 440
        height: 380
        flags: Qt.Window
        color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        AutoDuckPanel {
            anchors.fill: parent
            anchors.margins: 16
            fx: AudioEngine.autoDuck
        }
    }

    // ========================================================================
    //  Ventana del Ecualizador Master (curva de respuesta + sliders)
    // ========================================================================
    Window {
        id: equalizerDialog
        title: qsTr("Ecualizador")
        width: 500
        height: 340
        flags: Qt.Window
        color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        Column {
            anchors.fill: parent
            anchors.margins: 12

            EqualizerPanel {
                fx: AudioEngine.equalizer
                showHeader: true
                width: parent.width
            }
        }
    }

    // ========================================================================
    //  Ventana del Limitador Master
    // ========================================================================
    Window {
        id: limiterDialog
        title: qsTr("Limitador Master")
        width: 340; height: 180
        flags: Qt.Window; color: "#22252a"
        function open() { visible = true; raise() }
        function close() { visible = false }

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 14

            // Header con badge ON/OFF idéntico al FxKnobPanel
            Row {
                spacing: 12
                Rectangle {
                    width: 48; height: 22; radius: 4
                    color: AudioEngine.limiterEnabled ? Qt.rgba(0.91,0.3,0.24,0.22) : Qt.rgba(1,1,1,0.06)
                    border.color: AudioEngine.limiterEnabled ? "#e74c3c" : "#484e56"
                    border.width: 1
                    Label { anchors.centerIn: parent; text: AudioEngine.limiterEnabled ? "ON" : "OFF"
                        color: AudioEngine.limiterEnabled ? "#e74c3c" : "#666"; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: AudioEngine.limiterEnabled = !AudioEngine.limiterEnabled }
                }
                Label { text: "Limitador Master"; color: "#e8eaed"; font.bold: true; font.pixelSize: 13
                    anchors.verticalCenter: parent.children[0].verticalCenter }
            }

            Rectangle { width: parent.width; height: 1; color: "#2a2d32" }

            // Knobs horizontales
            Row {
                spacing: 8
                // THRESH
                Item {
                    width: 82; height: 90
                    Label { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                        text: AudioEngine.limiterThresholdDb.toFixed(1) + " dB"
                        color: "#e74c3c"; font.pixelSize: 9; font.family: "Menlo"
                        horizontalAlignment: Text.AlignHCenter; width: parent.width }
                    MetalKnob { anchors.top: parent.top; anchors.topMargin: 14; anchors.horizontalCenter: parent.horizontalCenter
                        size: 52; from: -20; to: 0; dotColor: "#e74c3c"; label: ""
                        value: AudioEngine.limiterThresholdDb
                        onMoved: (v) => { AudioEngine.limiterThresholdDb = v } }
                    Label { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                        text: "THRESH"; color: "#555"; font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.5
                        horizontalAlignment: Text.AlignHCenter; width: parent.width }
                }
                // GR meter vertical integrado
                Item {
                    width: 82; height: 90
                    anchors.verticalCenter: undefined
                    Rectangle {
                        anchors.centerIn: parent
                        width: 60; height: 14; radius: 3
                        color: "#111"; border.color: "#333"
                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
                            width: Math.min(parent.width - 4,
                                (parent.width - 4) * Math.abs(AudioEngine.limiterGainReduction) / 20)
                            color: Math.abs(AudioEngine.limiterGainReduction) > 6 ? "#e74c3c"
                                : Math.abs(AudioEngine.limiterGainReduction) > 2 ? "#f39c12" : "#2ecc71"
                            radius: 2
                            Behavior on width { NumberAnimation { duration: 60 } }
                        }
                    }
                    Label { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                        text: "GR METER"; color: "#555"; font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.5
                        horizontalAlignment: Text.AlignHCenter; width: parent.width }
                    Label {
                        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                        text: AudioEngine.limiterEnabled
                            ? (AudioEngine.limiterGainReduction < -0.1 ? AudioEngine.limiterGainReduction.toFixed(1) + " dB" : "0.0 dB")
                            : "off"
                        color: "#666"; font.pixelSize: 9; font.family: "Menlo"
                        horizontalAlignment: Text.AlignHCenter; width: parent.width }
                }
            }
        }
    }


    // ========================================================================
    //  Ventana del Compresor Master  (estilo Hindenburg: VU + knob)
    // ========================================================================
    Window {
        id: compressorDialog
        title: qsTr("Compresor")
        width: 380
        height: 480
        flags: Qt.Window
        color: "#2a2d32"
        function open() { visible = true; raise() }
        function close() { visible = false }

        // Valor único del knob (0 = OFF, 1 = MAX)
        property real compAmount: 0.0
        property bool compActive: false

        // Mapear el valor del knob a parámetros reales del compresor
        function applyAmount(v) {
            compAmount = v
            if (!compActive) return
            // threshold: -6 (suave) a -40 (agresivo)
            var thresh = -6.0 - v * 34.0
            // ratio: 1.5:1 (suave) a 12:1 (agresivo)
            var ratio = 1.5 + v * 10.5
            // makeup: 0 a 12 dB
            var makeup = v * 12.0
            AudioEngine.compThresholdDb = thresh
            AudioEngine.compRatio = ratio
            AudioEngine.compMakeupDb = makeup
            AudioEngine.compAttackMs = 10.0 - v * 5.0 + 5.0
            AudioEngine.compReleaseMs = 100.0 + v * 200.0
        }

        Component.onCompleted: {
            compActive = AudioEngine.compEnabled
            // Inferir el knob value desde los parámetros actuales
            if (AudioEngine.compThresholdDb < -6) {
                compAmount = Math.min(1.0, Math.max(0.0,
                    (-6.0 - AudioEngine.compThresholdDb) / 34.0))
            }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // === VU METER (Canvas) ===
            Rectangle {
                width: parent.width
                height: 180
                color: "#1a1c20"
                radius: 6
                border.color: "#444"; border.width: 1

                Canvas {
                    id: vuCanvas
                    anchors.fill: parent
                    anchors.margins: 8

                    // GR en dB (negativo, lo convertimos a positivo para display)
                    property real grDb: compressorDialog.compActive
                        ? Math.abs(AudioEngine.compGainReduction) : 0.0

                    onGrDbChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        var w = width, h = height
                        ctx.clearRect(0, 0, w, h)

                        // Fondo degradado del medidor
                        var bgGrad = ctx.createLinearGradient(0, 0, 0, h)
                        bgGrad.addColorStop(0, "#2a2520")
                        bgGrad.addColorStop(0.6, "#1a1815")
                        bgGrad.addColorStop(1, "#0f0d0b")
                        ctx.fillStyle = bgGrad
                        ctx.fillRect(0, 0, w, h)

                        // Pivote (centro abajo) y radio del arco
                        var cx = w / 2
                        var cy = h * 0.88
                        var radius = Math.min(w, h) * 0.72

                        // Ángulos en coordenadas de pantalla (Y hacia abajo):
                        // 0 dB = upper-left (150°), 20 dB = upper-right (30°)
                        // En radianes: 150° = 5π/6, 30° = π/6
                        // Sweep = 120° total
                        var angStartDeg = 150  // 0 dB (izquierda)
                        var angSweepDeg = 120  // barrido total

                        function dbToXY(dbVal, r) {
                            var frac = dbVal / 20.0
                            var angDeg = angStartDeg - frac * angSweepDeg
                            var angRad = angDeg * Math.PI / 180
                            return {
                                x: cx + Math.cos(angRad) * r,
                                y: cy - Math.sin(angRad) * r
                            }
                        }

                        // Marcas de escala: 0, 4, 8, 12, 16, 20 dB
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

                            // Número
                            var pText = dbToXY(marks[i], radius * 0.78)
                            ctx.fillStyle = "#bbb"
                            ctx.font = (marks[i] === 0 ? "bold " : "") + "12px Menlo"
                            ctx.fillText(marks[i].toString(), pText.x, pText.y)
                        }

                        // Ticks menores (cada 2 dB entre marcas)
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

                        // Etiqueta "dB COMPRESSION"
                        ctx.fillStyle = "#888"
                        ctx.font = "10px sans-serif"
                        ctx.textAlign = "center"
                        var labelY = cy - radius * 0.38
                        ctx.fillText("dB", cx, labelY)
                        ctx.fillText("COMPRESSION", cx, labelY + 14)

                        // === Aguja ===
                        var grClamped = Math.min(Math.max(grDb, 0), 20.0)
                        var needleP = dbToXY(grClamped, radius * 0.87)

                        // Sombra
                        ctx.save()
                        ctx.shadowColor = "rgba(0,0,0,0.5)"
                        ctx.shadowBlur = 6
                        ctx.shadowOffsetY = 2

                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.lineTo(needleP.x, needleP.y)
                        ctx.strokeStyle = "#f0f0f0"
                        ctx.lineWidth = 2
                        ctx.stroke()
                        ctx.restore()

                        // Pivote central
                        ctx.beginPath()
                        ctx.arc(cx, cy, 5, 0, Math.PI * 2)
                        ctx.fillStyle = "#666"
                        ctx.fill()
                        ctx.strokeStyle = "#888"
                        ctx.lineWidth = 1
                        ctx.stroke()
                    }

                    // Refresh periódico si el compresor está activo
                    Timer {
                        interval: 50
                        running: compressorDialog.visible && compressorDialog.compActive
                        repeat: true
                        onTriggered: vuCanvas.requestPaint()
                    }
                }
            }

            // === Fila: ON toggle + label ===
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 12

                Label {
                    anchors.verticalCenter: parent.children[1].verticalCenter
                    text: qsTr("Compresor")
                    color: root.textPri; font.pixelSize: 14; font.bold: true
                }

                Button {
                    id: compOnBtn
                    width: 48; height: 26
                    text: compressorDialog.compActive ? "on" : "off"
                    onClicked: {
                        compressorDialog.compActive = !compressorDialog.compActive
                        AudioEngine.compEnabled = compressorDialog.compActive
                        if (compressorDialog.compActive) {
                            compressorDialog.applyAmount(compressorDialog.compAmount)
                        }
                    }
                    background: Rectangle {
                        radius: 3
                        color: compressorDialog.compActive
                            ? Qt.rgba(0.2, 0.5, 1.0, 0.4)
                            : Qt.rgba(1, 1, 1, 0.08)
                        border.color: compressorDialog.compActive ? "#3498db" : root.divider
                        border.width: 1
                    }
                    contentItem: Label {
                        anchors.centerIn: parent
                        text: parent.text
                        color: compressorDialog.compActive ? "#5dade2" : root.textSec
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // === KNOB rotatorio ===
            Item {
                width: parent.width
                height: 160
                anchors.horizontalCenter: parent.horizontalCenter

                BigMetalKnob {
                    id: knobCanvas
                    size: 140
                    anchors.centerIn: parent
                    from: 0
                    to: 1.0
                    value: compressorDialog.compAmount
                    activeColor: "#aaa"
                    onMoved: (val) => {
                        compressorDialog.applyAmount(val)
                    }
                    onDoubleClicked: {
                        compressorDialog.applyAmount(0)
                    }
                }

                // Labels OFF / MAX
                Label {
                    anchors.left: parent.left; anchors.leftMargin: 30
                    anchors.bottom: parent.bottom
                    text: "OFF"; color: root.textSec; font.pixelSize: 12; font.bold: true
                }
                Label {
                    anchors.right: parent.right; anchors.rightMargin: 30
                    anchors.bottom: parent.bottom
                    text: "MAX"; color: root.textSec; font.pixelSize: 12; font.bold: true
                }
            }
        }
    }

    // ========================================================================
    //  Ventana FX por pista (tipo Reaper)
    // ========================================================================
    TrackFxDialog {
        id: trackFxDialog
    }

    // ========================================================================
    //  Diálogo de Transcripción (Settings)
    // ========================================================================
    TranscriptionSettings {
        id: transcriptionSettingsDialog
    }

    Window {
        id: recoveryDialog
        width: 420
        height: 160
        minimumWidth: 320
        minimumHeight: 140
        flags: Qt.Dialog | Qt.WindowCloseButtonHint
        modality: Qt.NonModal
        visible: false
        color: "#2a2d31"
        title: qsTr("Recuperar grabación")
        function open() { visible = true; raise(); requestActivate() }
        function close() { visible = false }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("Se ha detectado una sesión de grabación no guardada de un cierre inesperado anterior.\n¿Deseas recuperarla?")
                wrapMode: Text.WordWrap
                color: "#ecf0f1"
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    text: qsTr("Recuperar")
                    onClicked: {
                        recoveryDialog.close()
                        if (ProjectIO.recoverProject()) {
                            UndoManager.clear()
                            root.showLibrary = false
                        } else {
                            errorDialog.message = ProjectIO.lastError()
                            errorDialog.open()
                        }
                    }
                }
                Button {
                    text: qsTr("No ahora")
                    onClicked: {
                        recoveryDialog.close()
                    }
                }
                Button {
                    text: qsTr("Descartar copia")
                    flat: true
                    onClicked: {
                        recoveryDialog.close()
                        ProjectIO.discardRecovery()
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        if (ProjectIO.hasRecoveryProject()) {
            recoveryDialog.open()
        }
    }
}
