import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Controls
import Logos.Theme

// THE CATALOG, ON A STORE SHELL.
//
// AppManagerView next door is the DESKTOP's App Manager: a grid over
// `backend.appsModel`, with repositories, categories, a search field and a
// context menu per tile. None of those four exist on a phone -- there is no
// UI-plugin directory to scan (ADR 0003), and the catalog a Store shell shows
// comes from package_downloader through basecamp::appmanager::StoreAppManager,
// which publishes it as a plain list of maps rather than as a model.
//
// So this is the same section, over that object, at a phone's width:
//
//   one column, because a Qt layout handed less room than its minimum
//   OVERFLOWS rather than shrinking, and a control past the right edge cannot
//   be pressed by a finger at all (logos-workspace#84, #87)
//   one control per row, and only when the row may be installed
//
// WHAT IT IS FOR (logos-workspace#169, ADR 0007's honest-availability rule).
// Every verdict here is decided elsewhere -- package_manager owns the variant
// vocabulary, and the Platform floor is DERIVED by the build from the catalog
// and the shell's Bundled closure -- and until this view existed the verdict's
// only home on a phone was a console line. A user cannot read a build log. An
// unavailable row states its reason IN THE ROW and carries no install control:
// a greyed-out button is not an answer to "why can I not install this".
Rectangle {
    id: root

    // basecamp::appmanager::StoreAppManager, or null. Null is the DESKTOP,
    // whose backend has no such object -- and the empty state below is what a
    // Store shell without the package modules gets, which is a legitimate
    // build (ADR 0007) rather than an error.
    property QtObject appManager: null

    readonly property var entries: appManager ? appManager.catalogEntries : []
    readonly property string unavailableReason:
        appManager ? appManager.catalogUnavailableReason : ""

    color: Theme.palette.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.large
        spacing: Theme.spacing.medium

        LogosText {
            objectName: "storeCatalog.title"
            text: qsTr("Applications")
            font.pixelSize: Theme.typography.panelTitleText
            font.weight: Theme.typography.weightBold
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        // WHY THERE IS NO CATALOG, when there is none. An empty list would read
        // as a catalog with nothing in it, which is a different and much more
        // alarming thing than a build that carries no package modules.
        LogosText {
            objectName: "storeCatalog.unavailable"
            text: root.unavailableReason
            visible: text !== ""
            color: Theme.palette.textSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            objectName: "storeCatalog.list"
            model: root.entries
            clip: true
            spacing: Theme.spacing.small
            Layout.fillWidth: true
            Layout.fillHeight: true

            delegate: Rectangle {
                id: row

                required property var modelData

                readonly property string entryName: modelData.name || ""
                readonly property bool refused: modelData.available === false
                readonly property bool installable: modelData.canInstall === true

                objectName: "storeCatalog.row." + entryName
                width: ListView.view ? ListView.view.width : 0
                implicitHeight: rowContent.implicitHeight + 2 * Theme.spacing.medium
                height: implicitHeight
                radius: Theme.spacing.radiusMedium
                color: Theme.palette.backgroundSecondary
                border.width: 1
                border.color: row.refused ? Theme.palette.borderSubtle
                                          : Theme.palette.borderSecondary

                ColumnLayout {
                    id: rowContent
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.medium
                    spacing: Theme.spacing.tiny

                    LogosText {
                        objectName: "storeCatalog.name." + row.entryName
                        text: (row.modelData.displayName || row.entryName)
                              + " " + (row.modelData.version || "")
                        font.weight: Theme.typography.weightBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    LogosText {
                        text: row.modelData.description || ""
                        visible: text !== ""
                        color: Theme.palette.textSecondary
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // THE REFUSAL, in the words it was refused in. One sentence
                    // from the App Manager, wrapped rather than elided: it names
                    // the module that is missing, and the name is the end of the
                    // sentence ("requires delivery_module, not in this build").
                    //
                    // EVERY OPTIONAL PART OF A ROW IS A Loader, here and below,
                    // and that is the whole rendering rule: what a row does not
                    // say is ABSENT from the scene rather than present and
                    // hidden. A driver and a finger meet the same thing, and
                    // "this row offers no install" is then a fact about the
                    // scene instead of about one property of a button still in
                    // it.
                    Loader {
                        active: row.refused
                        Layout.fillWidth: true
                        Layout.topMargin: active ? Theme.spacing.tiny : 0
                        sourceComponent: LogosText {
                            objectName: "storeCatalog.reason." + row.entryName
                            text: row.modelData.unavailableReason || ""
                            color: Theme.palette.textSubtle
                            wrapMode: Text.WordWrap
                        }
                    }

                    // What this device already has. Not a control: the upgrade
                    // path is a different one with its own confirmation.
                    Loader {
                        active: row.modelData.installed === true
                        Layout.fillWidth: true
                        Layout.topMargin: active ? Theme.spacing.tiny : 0
                        sourceComponent: LogosText {
                            objectName: "storeCatalog.state." + row.entryName
                            text: qsTr("Installed %1").arg(row.modelData.installedVersion || "")
                            color: Theme.palette.success
                        }
                    }

                    // AND THE INSTALL CONTROL, which EXISTS only when the row
                    // may be installed.
                    Loader {
                        active: row.installable
                        Layout.topMargin: active ? Theme.spacing.small : 0
                        sourceComponent: LogosButton {
                            objectName: "storeCatalog.install." + row.entryName
                            text: qsTr("Install")
                            variant: LogosButton.Variant.Primary
                            onClicked: root.installRequested(row.entryName)
                        }
                    }
                }
            }
        }
    }

    // Pressed Install on a row. The App Manager owns the gate that follows
    // (signer prompt, consent, the core's load); nothing here decides any of it.
    signal installRequested(string packageName)

    onInstallRequested: function(packageName) {
        if (appManager)
            appManager.beginInstall(packageName)
    }
}
