pragma ComponentBehavior: Bound
//qmllint disable
import QtQuick
import CutPro 1.0
import "../../theme"
import "../common"

// The browser pane's one delegate. A row in the model is either something
// EffectBrowserRow already draws - a folder, or a single effect on its own line -
// or a "grid" row, which is a whole category's worth of cards wrapped across the
// pane. Both live in one component rather than two delegates chosen at runtime,
// because a delegate declared in a Component of its own does not get its required
// model property filled here (the category rail was drawn as an empty column for
// exactly that reason).
Item {
    id: cell

    property var rowData: ({})
    property var gridDefinitions: []
    property bool canApply: false
    // A grid row holds video and audio entries side by side, so "can this be
    // applied" cannot be one answer for the row. Both are passed in and each tile
    // picks the one that matches what it is.
    property bool canApplyVideo: false
    property bool canApplyAudio: false
    property string clipId: ""
    property string basePreviewUrl: ""
    signal folderToggled(string key)
    signal effectRequested(var definition)

    readonly property bool isGrid: String(cell.rowData.type) === "grid"

    // Cards are aimed at, not read, so they are sized to a target width and the
    // column count follows from the pane - two in a narrow panel, five or six in
    // a wide one, and never a single column, which is the layout this replaces.
    readonly property int tileSpacing: 6
    readonly property int gridMargin: 10
    readonly property real gridWidth: Math.max(1, cell.width - cell.gridMargin * 2)
    readonly property int tileColumns: Math.max(
        2, Math.floor((cell.gridWidth + cell.tileSpacing)
                      / (128 + cell.tileSpacing)))
    readonly property int tileWidth: Math.floor(
        (cell.gridWidth - cell.tileSpacing * (cell.tileColumns - 1))
        / cell.tileColumns)

    implicitHeight: cell.isGrid ? tileFlow.implicitHeight + 8
                                : listRow.implicitHeight
    height: implicitHeight

    // Kept for the grid rows too, invisible, rather than loaded conditionally: a
    // Loader whose width comes from the view and whose height has to come back
    // out of the loaded item is the one arrangement that can size itself in a
    // circle. It is handed a blank folder there because EffectBrowserRow reads
    // rowData.definition.icon without checking, and a grid row has no definition.
    EffectBrowserRow {
        id: listRow
        width: cell.width
        visible: !cell.isGrid
        enabled: !cell.isGrid
        rowData: cell.isGrid ? ({
                                   type: "folder",
                                   name: "",
                                   key: "",
                                   depth: 0,
                                   expanded: false
                               }) : cell.rowData
        canApply: cell.canApply
        clipId: cell.clipId
        basePreviewUrl: cell.basePreviewUrl
        onFolderToggled: key => cell.folderToggled(key)
        onEffectRequested: definition => cell.effectRequested(definition)
    }

    Flow {
        id: tileFlow
        x: cell.gridMargin
        y: 4
        width: cell.gridWidth
        visible: cell.isGrid
        spacing: cell.tileSpacing

        Repeater {
            // Empty unless this row is a grid, so a folder row costs no tiles.
            model: cell.isGrid ? cell.gridDefinitions : []
            delegate: EffectBrowserTile {
                id: tile
                required property var modelData
                width: cell.tileWidth
                definition: tile.modelData
                canApply: String(tile.modelData.mediaType) === "video"
                          ? cell.canApplyVideo : cell.canApplyAudio
                clipId: cell.clipId
                basePreviewUrl: cell.basePreviewUrl
                onEffectRequested: definition => cell.effectRequested(definition)
            }
        }
    }
}
