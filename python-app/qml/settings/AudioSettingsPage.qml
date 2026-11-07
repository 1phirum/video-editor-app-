import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components/common"
import "../components/effects"
import "../components/export"
import "../components/lumetri"
import "../components/project"
import "../components/subtitles"
import "../components/timeline"

SettingsPage {
    id: root

    // Vocal-separation engine choice, read by the section below. Kept on root so
    // the child rows can see them (properties on a SettingsSection are not in the
    // delegates' scope).
    readonly property var vocalEngineCodes: ["demucs", "mdxnet"]
    readonly property var vocalEngineLabels: [qsTr("Demucs (best quality)"), qsTr("MDX-Net (light & fast)")]
    readonly property string vocalEngine: String(root.settings.vocalEngine || "demucs")

    SettingsSection {
        title: qsTr("Output")
        SettingsRow {
            label: qsTr("Master volume")
            LumetriSlider {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: root.settings.masterVolume === undefined
                       ? 100 : Number(root.settings.masterVolume)
                suffix: "%"
                onValueCommitted: value => root.settingChanged(
                                      "masterVolume", Math.round(value))
            }
        }
        SettingsRow {
            label: qsTr("Mute all audio")
            CaptionSwitch {
                checked: root.settings.muteAllAudio === true
                onToggled: root.settingChanged("muteAllAudio", checked)
            }
        }
    }

    SettingsSection {
        title: qsTr("Meters")
        SettingsRow {
            label: qsTr("Refresh interval")
            SettingsComboBox {
                model: [qsTr("60 ms"), qsTr("110 ms"), qsTr("200 ms"), qsTr("350 ms")]
                currentIndex: {
                    var wanted = Number(root.settings.audioMeterRefreshMs || 110)
                    var values = [60, 110, 200, 350]
                    return Math.max(0, values.indexOf(wanted))
                }
                onActivated: root.settingChanged(
                                 "audioMeterRefreshMs", [60, 110, 200, 350][currentIndex])
            }
        }
    }

    SettingsSection {
        title: qsTr("Vocal separation")
        help: root.vocalEngine === "mdxnet"
              ? qsTr("MDX-Net separates on ONNX (no heavy torch), so it is much faster on a weak computer with quality close to Demucs. Download it under Settings > Requirements before first use.")
              : qsTr("Demucs gives the cleanest separation but is heavy. Fast uses one model; Clean uses the fine-tuned 4-model ensemble - roughly 4x slower for the best result. If a run is too slow, switch to Fast, or to the MDX-Net engine. Download Demucs under Settings > Requirements before first use.")

        // Engine and (for Demucs) quality that "Remove Vocals (AI)" uses. Download
        // the chosen engine once under Settings > Requirements before first use.

        SettingsRow {
            label: qsTr("Engine")
            SettingsComboBox {
                model: root.vocalEngineLabels
                currentIndex: Math.max(0, root.vocalEngineCodes.indexOf(root.vocalEngine))
                onActivated: root.settingChanged("vocalEngine", root.vocalEngineCodes[currentIndex])
            }
        }

        SettingsRow {
            label: qsTr("Demucs quality")
            visible: root.vocalEngine === "demucs"
            SettingsComboBox {
                // Fast = htdemucs (1 pass); Clean = htdemucs_ft (4-model ensemble).
                model: [qsTr("Fast - faster, good"), qsTr("Clean - slowest, best")]
                currentIndex: String(root.settings.vocalDemucsQuality || "fast") === "clean" ? 1 : 0
                onActivated: root.settingChanged(
                                 "vocalDemucsQuality", currentIndex === 1 ? "clean" : "fast")
            }
        }
    }
}
