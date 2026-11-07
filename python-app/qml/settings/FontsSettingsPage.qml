// qmllint disable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CutPro 1.0
import "../theme"
import "../components/common"

// The Fonts settings page: import your own fonts, download the built-in cloud
// fonts, and see every family a caption can use. The backend owns the catalog
// (backend_fonts.cpp); this page only renders it and calls the import/download/
// remove verbs, so it never hardcodes a font family.
SettingsPage {
    id: root

    // captionFontCatalog() is a Q_INVOKABLE, so it goes stale on its own. Naming
    // the two font properties - both notified by captionFontsChanged - forces a
    // re-read whenever a font is imported, downloaded or removed.
    readonly property var fontCatalog: (Backend.downloadedCaptionFonts,
                                        Backend.customCaptionFonts,
                                        Backend.captionFontCatalog())
    readonly property var customEntries: root.fontCatalog.filter(e => e.category === "custom")
    readonly property var cloudEntries: root.fontCatalog.filter(e => e.category === "cloud")
    readonly property var systemEntries: root.fontCatalog.filter(e => e.category === "system")

    // The full Google Fonts library the browser searches over. The download and
    // remove verbs live in the backend (backend_fonts.cpp); this page only
    // filters the list and renders installed state.
    readonly property var googleCatalog: Backend.googleFontsCatalog

    // Families already registered (downloaded before, or user-imported), so the
    // browser can show "Installed" instead of a Download button. Rebuilt whenever
    // a font is added or removed via the caption-fonts notify.
    readonly property var installedSet: {
        const set = ({})
        const downloaded = Backend.downloadedCaptionFonts
        const custom = Backend.customCaptionFonts
        for (let i = 0; i < downloaded.length; i++)
            set[downloaded[i]] = true
        for (let i = 0; i < custom.length; i++)
            set[custom[i]] = true
        return set
    }

    // Just the downloaded cloud fonts, so the browser can offer a Remove on the
    // ones it fetched (custom imports are managed in their own section).
    readonly property var downloadedSet: {
        const set = ({})
        const downloaded = Backend.downloadedCaptionFonts
        for (let i = 0; i < downloaded.length; i++)
            set[downloaded[i]] = true
        return set
    }

    // Families the user clicked to preview: fetched and registered but not
    // installed, so their sample renders in the row while Download still shows.
    readonly property var previewedSet: {
        const set = ({})
        const previewed = Backend.previewedCaptionFonts
        for (let i = 0; i < previewed.length; i++)
            set[previewed[i]] = true
        return set
    }

    // Cap the rendered rows: the library is ~1500 families and the page is a
    // scroll view, so we show a slice and lean on search to narrow it.
    readonly property int googleResultLimit: 60
    readonly property var googleResults: {
        const catalog = root.googleCatalog
        const query = googleSearch.text.trim().toLowerCase()
        const out = []
        for (let i = 0; i < catalog.length && out.length < root.googleResultLimit; i++) {
            const family = catalog[i].family
            if (query === "" || family.toLowerCase().indexOf(query) !== -1)
                out.push(catalog[i])
        }
        return out
    }

    // Pull the catalog once when the settings dialog first builds this page; the
    // backend ignores the call if it is already loading or loaded.
    Component.onCompleted: Backend.fetchGoogleFontsCatalog()


    component PillButton: Button {
        id: btn
        implicitHeight: 30
        hoverEnabled: true
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        contentItem: Text {
            text: btn.text
            color: btn.hovered ? Theme.accent : Theme.textPrimary
            font.pixelSize: Theme.fsSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            leftPadding: 12
            rightPadding: 12
        }
        background: Rectangle {
            color: btn.down ? Theme.hover : btn.hovered ? Theme.bgCardHeader : Theme.bgPrimary
            border.color: btn.hovered ? Theme.accent : Theme.border
            radius: Theme.radiusSm
        }
    }

    FileDialog {
        id: fontFileDialog
        title: qsTr("Import fonts")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Fonts (*.ttf *.otf *.ttc)")]
        onAccepted: Backend.importCaptionFontFiles(selectedFiles.map(u => u.toString()))
    }

    FolderDialog {
        id: fontFolderDialog
        title: qsTr("Import a font folder")
        onAccepted: Backend.importCaptionFontFolder(selectedFolder.toString())
    }

    SettingsSection {
        title: qsTr("Custom fonts")
        help: qsTr("Import your own .ttf, .otf or .ttc files. Imported fonts are copied into Cut Pro, stay available after a restart, and show up in the caption Font list.")

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PillButton { text: qsTr("Import files...");  onClicked: fontFileDialog.open() }
            PillButton { text: qsTr("Import folder..."); onClicked: fontFolderDialog.open() }
            Item { Layout.fillWidth: true }
        }

        Text {
            visible: root.customEntries.length === 0
            Layout.fillWidth: true
            text: qsTr("No custom fonts yet.")
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        Repeater {
            model: root.customEntries
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumHeight: 34
                spacing: 12
                Text {
                    text: modelData.family
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    Layout.preferredWidth: 190
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Aa Bb Cc  123")
                    color: Theme.textSecondary
                    font.family: modelData.family
                    font.pixelSize: 18
                    elide: Text.ElideRight
                }
                PillButton {
                    text: qsTr("Remove")
                    onClicked: Backend.removeCustomCaptionFont(modelData.family)
                }
            }
        }
    }

    SettingsSection {
        title: qsTr("Google Fonts")
        help: qsTr("Search the whole Google Fonts library and download any font on demand. Downloaded fonts are added to Cut Pro and show up in the caption Font list.")

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            SearchField {
                id: googleSearch
                Layout.fillWidth: true
                placeholder: qsTr("Search Google Fonts...")
            }
        }

        // Loading / error / empty states for the catalog fetch.
        Text {
            visible: Backend.googleFontsLoading
            Layout.fillWidth: true
            text: qsTr("Loading Google Fonts...")
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        RowLayout {
            visible: !Backend.googleFontsLoading && Backend.googleFontsError !== ""
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: Backend.googleFontsError
                color: Theme.textMuted
                font.pixelSize: Theme.fsSm
                elide: Text.ElideRight
            }
            PillButton {
                text: qsTr("Retry")
                onClicked: Backend.fetchGoogleFontsCatalog()
            }
        }

        Text {
            visible: !Backend.googleFontsLoading && Backend.googleFontsError === ""
                     && root.googleResults.length === 0
            Layout.fillWidth: true
            text: googleSearch.text.trim() === ""
                  ? qsTr("Type to search the font library.")
                  : qsTr("No fonts match “%1”.").arg(googleSearch.text.trim())
            color: Theme.textMuted
            font.pixelSize: Theme.fsSm
        }

        Repeater {
            model: root.googleResults
            delegate: RowLayout {
                required property var modelData
                readonly property bool installed: Boolean(root.installedSet[modelData.family])
                readonly property bool downloaded: Boolean(root.downloadedSet[modelData.family])
                // Show the real font sample once it is renderable - either
                // installed already, or fetched by clicking the row to preview.
                readonly property bool previewable: installed || Boolean(root.previewedSet[modelData.family])
                Layout.fillWidth: true
                Layout.minimumHeight: 34
                spacing: 12

                // The clickable preview region: family name plus either the
                // rendered sample or, until previewed, the category + a hint.
                Item {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 34
                    RowLayout {
                        anchors.fill: parent
                        spacing: 12
                        Text {
                            text: modelData.family
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fsSm
                            Layout.preferredWidth: 190
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: previewable
                            text: qsTr("Aa Bb Cc  123")
                            color: Theme.textSecondary
                            font.family: modelData.family
                            font.pixelSize: 18
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !previewable
                            text: modelData.category + qsTr("  ·  click to preview")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsXs
                            elide: Text.ElideRight
                        }
                    }
                    HoverHandler {
                        enabled: !previewable
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        enabled: !previewable
                        onTapped: Backend.previewGoogleFont(modelData.family)
                    }
                }

                Text {
                    visible: installed && !downloaded
                    text: qsTr("Installed")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                }
                PillButton {
                    visible: !installed
                    text: qsTr("Download")
                    onClicked: Backend.downloadGoogleFont(modelData.family)
                }
                PillButton {
                    visible: downloaded
                    text: qsTr("Remove")
                    onClicked: Backend.removeDownloadedCaptionFont(modelData.family)
                }
            }
        }
    }

    SettingsSection {
        title: qsTr("System fonts")
        help: qsTr("Always available on this device.")

        Repeater {
            model: root.systemEntries
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumHeight: 34
                spacing: 12
                Text {
                    text: modelData.family
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fsSm
                    Layout.preferredWidth: 190
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Aa Bb Cc  123")
                    color: Theme.textSecondary
                    font.family: modelData.family
                    font.pixelSize: 18
                    elide: Text.ElideRight
                }
            }
        }
    }
}
