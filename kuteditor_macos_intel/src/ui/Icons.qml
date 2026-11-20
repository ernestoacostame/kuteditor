pragma Singleton
import QtQuick

QtObject {
    // --- Toolbar: Archivo ---
    readonly property string newTrack:     "playlist_add"
    readonly property string newProject:   "note_add"
    readonly property string openFile:     "folder_open"
    readonly property string importAudio:  "file_download"
    readonly property string exportAudio:  "file_upload"

    // --- Toolbar: Edición ---
    readonly property string split:        "flip" //flip split_scene arrow_menu_open split_screen_landscape text_compare align_justify_center horizontal_align_center
    readonly property string cut:          "content_cut"
    readonly property string copy:         "content_copy"
    readonly property string paste:        "content_paste"
    readonly property string cleanTrack:   "cleaning_services"
    readonly property string deleteSpace:  "cell_merge" //text_select_move_back_word transition_push view_week view_array

    // --- Toolbar: Deshacer/Rehacer ---
    readonly property string undo:         "undo"
    readonly property string redo:         "redo"

    // --- Toolbar: Zoom ---
    readonly property string zoomIn:       "zoom_in"
    readonly property string zoomOut:      "zoom_out"
    readonly property string zoomFit:      "fit_screen"

    // --- Toolbar: Toggle ---
    readonly property string magnet:       "combine_columns" // "text_select_move_back_word" // "compare_arrows" cell_merge

    // --- Transporte ---
    readonly property string play:         "play_arrow"
    readonly property string stop:         "stop"
    readonly property string record:       "fiber_manual_record"
    readonly property string backward:     "skip_previous"
    readonly property string forward:      "skip_next"

    // --- Archivo ---
    readonly property string save:         "save"

    // --- Común ---
    readonly property string trash:        "delete"
    readonly property string pencil:       "edit"
    readonly property string microphone:   "mic"

    // --- KutPod / online ---
    readonly property string publish:      "public"     // globo: subir a la red
    readonly property string cloud:        "cloud"
    readonly property string cloudUpload:  "cloud_upload"
    readonly property string link:         "link"
    readonly property string logout:       "logout"
}
