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

    // THE SECOND HALF OF AN INSTALL, and until logos-workspace#249 nothing in
    // any scene was bound to it.
    //
    // Pressing Install does not install. It runs StoreAppManager::beginInstall,
    // which downloads the package, asks package_manager who signed it and then
    // STOPS -- nothing is installed behind a signer the user has not seen
    // (InstallGate.h, step 4). So the press publishes `signerPrompt` and waits
    // for approveSigner() / rejectSigner(), and a page that draws neither turns
    // the whole flow into a button that does nothing: the operator on #249
    // pressed Install on two rows the model itself called installable and got
    // no install, no error and no visible change, twice.
    //
    // A map, and an EMPTY map is truthy in JavaScript -- `{}` passes `if (x)`.
    // So "is one on screen" is a key count and not a null check; reading it as
    // one puts the gate up from the first frame over an empty prompt.
    readonly property var signerPrompt: appManager ? appManager.signerPrompt : null
    readonly property bool awaitingSigner:
        !!signerPrompt && Object.keys(signerPrompt).length > 0

    // WHO WAS REFUSED, when the refusal came from the signer step and named a
    // key. Identity WITHOUT an install control, which is the whole point of it
    // (InstallGate::refusedSigner): under a Store shell's `require` policy the
    // only way forward from "signed by a key your keyring does not vouch for"
    // is for that DID to be anchored, and the sentence in `lastError` cannot
    // carry a DID.
    readonly property var refusedSigner: appManager ? appManager.refusedSigner : null
    readonly property bool hasRefusedSigner:
        !!refusedSigner && Object.keys(refusedSigner).length > 0

    // ...and the sentence itself. The one place a refused install is visible at
    // all: every step of the gate reports through it, and a download that
    // failed on the network looks exactly like an inert button without it.
    readonly property string lastError: appManager ? appManager.lastError : ""

    // Pressed Install on a row. The App Manager owns the gate that follows
    // (signer prompt, consent, the core's load); nothing here decides any of it.
    signal installRequested(string packageName)

    color: Theme.palette.background

    onInstallRequested: function(packageName) {
        if (appManager)
            appManager.beginInstall(packageName)
    }

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

        // WHY THE LAST PRESS DID NOT INSTALL. A refusal arrives from five
        // different steps of the gate -- an unavailable row, a failed download,
        // a signer this device does not vouch for, a refused install, the
        // user's own Cancel -- and every one of them used to end in a property
        // no scene was bound to.
        LogosText {
            objectName: "storeCatalog.error"
            text: root.lastError
            visible: text !== ""
            color: Theme.palette.error
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // ...AND THE KEY IT WAS REFUSED OVER, when there was one. `lastError`
        // is a sentence; this is the DID, which is the only actionable thing
        // about that refusal and the one a sentence cannot hold.
        Loader {
            active: root.hasRefusedSigner
            Layout.fillWidth: true
            sourceComponent: LogosText {
                objectName: "storeCatalog.refusedSigner"
                text: qsTr("signed by %1\n%2")
                          .arg(root.refusedSigner.signerName || qsTr("an unnamed publisher"))
                          .arg(root.refusedSigner.signerDid || "")
                color: Theme.palette.textSubtle
                wrapMode: Text.WrapAnywhere
            }
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

    // ── THE SIGNER GATE ────────────────────────────────────────────────────
    //
    // The other half of the press, and the reason pressing Install did nothing
    // at all until logos-workspace#249. `beginInstall` downloads the package,
    // asks package_manager who signed it and STOPS at stage 4 of five
    // (InstallGate.h); the install happens in `approveSigner`, which nothing
    // in any scene could call. So the flow's whole visible surface was a button
    // that started a download and then parked for ever.
    //
    // IN THIS PAGE, not a Popup and not the desktop's overlay layer. Both of
    // those are the wrong scene for a Store shell: OverlayDialogs.qml is driven
    // by MainUIBackend's dialog signals, which ShellModulesBackend does not
    // emit, and a QtQuick Popup is parented to an Overlay beside the view --
    // invisible to a driver that walks from the root object, and on the venue's
    // physical iPad a popup that takes focus and raises a keyboard while
    // NOTHING of it is on the screen (logos-workspace#187). A Loader over this
    // ColumnLayout is reachable by a finger and by a driver in exactly the way
    // the rows are.
    //
    // NAME AND DID TOGETHER, and that is the criterion rather than a layout
    // choice (InstallGate::signerPrompt): the name is self-asserted by whoever
    // published the package, the DID is what this device's keyring was checked
    // against, and either one alone tells the user nothing they can verify.
    Loader {
        objectName: "storeCatalog.signerGate"
        active: root.awaitingSigner
        anchors.fill: parent
        z: 10

        sourceComponent: Rectangle {
            // OPAQUE, AND IT SWALLOWS PRESSES. A second press on the row
            // behind this would run the gate again from stage 1 -- a second
            // download, and `m_installing` repointed at whatever was pressed
            // last while the first package is the one on disk.
            color: Qt.rgba(0, 0, 0, 0.75)

            MouseArea {
                anchors.fill: parent
                // Nothing: the two controls below are the only ways out.
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.spacing.large,
                                480 - 2 * Theme.spacing.large)
                implicitHeight: gateContent.implicitHeight + 2 * Theme.spacing.large
                height: implicitHeight
                radius: Theme.spacing.radiusMedium
                color: Theme.palette.backgroundSecondary
                border.width: 1
                border.color: Theme.palette.borderSecondary

                ColumnLayout {
                    id: gateContent
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.large
                    spacing: Theme.spacing.small

                    LogosText {
                        objectName: "storeCatalog.signer.title"
                        text: qsTr("Install %1 %2?")
                                  .arg(root.signerPrompt.name || "")
                                  .arg(root.signerPrompt.version || "")
                        font.weight: Theme.typography.weightBold
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    LogosText {
                        objectName: "storeCatalog.signer.identity"
                        // WrapAnywhere: a DID is one unbroken token ~100
                        // characters long, and WordWrap leaves it overflowing a
                        // phone-width panel rather than breaking it.
                        text: qsTr("Signed by %1\n%2")
                                  .arg(root.signerPrompt.signerName
                                       || qsTr("an unnamed publisher"))
                                  .arg(root.signerPrompt.signerDid || "")
                        color: Theme.palette.textSecondary
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }

                    // WHETHER THIS DEVICE VOUCHES FOR THAT KEY, in its own
                    // words. package_manager decided it; a Store shell's
                    // `require` policy (ADR 0008) is why a package can reach
                    // this panel at all, and the user is the last check on it.
                    LogosText {
                        objectName: "storeCatalog.signer.status"
                        text: (root.signerPrompt.signatureStatus || qsTr("unknown"))
                              + " \u00b7 "
                              + (root.signerPrompt.trusted === true
                                     ? qsTr("trusted on this device as '%1'")
                                           .arg(root.signerPrompt.trustedAs || "")
                                     : qsTr("NOT in this device's keyring"))
                        color: root.signerPrompt.trusted === true
                                   ? Theme.palette.success : Theme.palette.warning
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: Theme.spacing.small
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacing.small

                        Item { Layout.fillWidth: true }

                        LogosButton {
                            objectName: "storeCatalog.signer.cancel"
                            text: qsTr("Cancel")
                            variant: LogosButton.Variant.Secondary
                            onClicked: {
                                if (root.appManager)
                                    root.appManager.rejectSigner()
                            }
                        }

                        LogosButton {
                            objectName: "storeCatalog.signer.install"
                            text: qsTr("Install")
                            variant: LogosButton.Variant.Primary
                            onClicked: {
                                if (root.appManager)
                                    root.appManager.approveSigner()
                            }
                        }
                    }
                }
            }
        }
    }
}
