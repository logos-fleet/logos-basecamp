import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Controls
import Logos.Icons
import Logos.Theme

import Basecamp.Backend
import Basecamp.Common

// Settings → Module Inspector. Lists every core module liblogos knows about
// with its load state and live CPU / memory stats, a Load/Unload toggle per
// row, and the per-module Interface drill-down. Uninstall is deliberately
// absent — module management lives in the Package Manager; this view is
// read-only apart from the load toggle.
//
// Formerly the "Core Modules" tab of ModulesView. Split out into its own
// Settings section alongside Apps Inspector, restyled onto LogosTable +
// InspectorPanelHeader to match the App Manager and Package Manager panels.
Item {
    id: root
    objectName: "moduleInspectorView"

    // ─── Public API ───
    // View emits, ContentViews calls the backend — see the Settings wiring in
    // Basecamp/Shell/ContentViews.qml. The Interface screen is the exception:
    // PluginInterfaceView still queries the backend for methods/events itself.
    // `searchText` is owned by SettingsView (one shared page-level search bar,
    // styled like AppManagerView).
    property var sourceModel: null
    property bool loading: false
    property string searchText: ""

    signal reloadRequested()
    signal loadRequested(string name)
    signal unloadRequested(string name)

    property string selectedPlugin: ""
    property bool showingInterface: false

    // Core modules Basecamp itself runs on: the package pipeline
    // (package_manager, package_downloader) and the auth-token registry
    // (capability_module). Their Load/Unload toggle renders disabled — unloading
    // any of them from the inspector would take the app's own plumbing down.
    readonly property var protectedModules: ["package_manager", "package_downloader", "capability_module"]

    // ─── Compact (handset / tablet) layout ───
    // `desktopColumnsWidth` is what the desktop set asks for: the sum of its
    // columns' preferredWidth (240 + 130 + 90 + 110 + 260), pinned to that sum
    // by a test. Narrower than that the row's action — the LAST column — is
    // pushed off the right edge, and Qt delivers a press by coordinate, so it
    // is then not merely awkward but unreachable: on a phone, and on a 13-inch
    // iPad in portrait, nothing can be loaded or unloaded by hand
    // (logos-workspace#84).
    //
    // The PREFERRED total, not the minimum one: a RowLayout squeezed between
    // the two does not shrink every column proportionally, so the row already
    // overflows well before the minimums bite. Measured on a physical iPad Air
    // (4th gen), whose 700-px pane is the minimum total to the pixel and still
    // put the toggle at x=710 in a 724-wide viewport.
    //
    // Below it the row keeps only what it cannot do without: which module it
    // is, and the control. Status, CPU and memory fold into the module cell,
    // and the Interface drill-down moves to the row itself.
    readonly property int desktopColumnsWidth: 830
    readonly property bool compact: root.width > 0 && root.width < desktopColumnsWidth

    // Open a specific module's Interface screen (methods + events) by name.
    // Equivalent to clicking that module's "Interface" button — exposed for UI
    // automation/tests, which can't disambiguate the per-row buttons by their
    // identical label.
    function openInterface(name) {
        selectedPlugin = name;
        showingInterface = true;
    }

    ModulesFilterProxy {
        id: tableModel

        sourceModel:  root.sourceModel
        searchText:   root.searchText
        sortRoleName: modulesTable.sortRole
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.showingInterface ? 1 : 0

        // ─── 0: module list ───
        // Vertical insets are Layout margins on the children rather than
        // anchors on this ColumnLayout — StackLayout owns its children's
        // geometry, so anchor margins here would be ignored.
        ColumnLayout {
            spacing: Theme.spacing.medium

            InspectorPanelHeader {
                id: header

                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacing.large
                Layout.rightMargin: Theme.spacing.large
                Layout.topMargin: Theme.spacing.large
                Layout.minimumWidth: 0

                title: qsTr("Module Inspector")
                subtitle: qsTr("Core modules known to the runtime, with live resource usage.")
                reloadObjectName: "moduleInspector.reloadButton"
                loading: root.loading
                visibleCount: tableModel.visibleCount
                totalCount: tableModel.totalCount

                onReloadClicked: root.reloadRequested()
            }

            LogosTable {
                id: modulesTable
                objectName: "moduleInspector.table"

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.bottomMargin: Theme.spacing.large

                model: tableModel
                // Two lines of module cell when the stats columns are folded
                // into it.
                rowHeight: root.compact ? 72 : 56
                sortRole: "label"
                sortOrder: Qt.AscendingOrder
                emptyText: tableModel.totalCount === 0
                           ? qsTr("No core modules available.")
                           : qsTr("No modules match the current filter.")

                onSortRequested: function(role, order) {
                    modulesTable.sortRole = role
                    modulesTable.sortOrder = order
                    tableModel.applySortOrder(order)
                }

                // Compact rows carry no Interface button, so the row itself is
                // the way in — same destination, one tap.
                onRowClicked: function(index, row) {
                    if (root.compact && row && row.isLoaded) root.openInterface(row.name)
                }

                // Keep every row instantiated — see AppsInspectorView for why.
                Component.onCompleted: if (view) view.cacheBuffer = 20000

                columns: root.compact ? compactColumns : desktopColumns

                // ─── Compact: what the module is, and the control ───
                property list<QtObject> compactColumns: [
                    LogosTableColumn {
                        title: qsTr("Module")
                        role: "label"
                        minWidth: 140
                        preferredWidth: 200
                        fillWidth: true
                        sortable: true
                        cellDelegate: compactModuleCellComponent
                    },
                    LogosTableColumn {
                        title: ""
                        // The toggle (100) plus this table's cell padding on
                        // both sides. Below it the button would be clipped,
                        // which is the whole bug.
                        minWidth: 100 + 2 * modulesTable.defaultCellPadding
                        preferredWidth: minWidth
                        alignment: Qt.AlignRight | Qt.AlignVCenter
                        cellDelegate: actionsCellComponent
                    }
                ]

                property list<QtObject> desktopColumns: [
                    LogosTableColumn {
                        title: qsTr("Module")
                        role: "label"
                        minWidth: 180
                        preferredWidth: 240
                        sortable: true
                        cellDelegate: moduleCellComponent
                    },
                    LogosTableColumn {
                        title: qsTr("Status")
                        role: "statusText"
                        minWidth: 110
                        preferredWidth: 130
                        sortable: true
                        cellDelegate: statusCellComponent
                    },
                    LogosTableColumn {
                        title: qsTr("CPU")
                        role: "cpu"
                        minWidth: 80
                        preferredWidth: 90
                        sortable: true
                        cellDelegate: cpuCellComponent
                    },
                    LogosTableColumn {
                        title: qsTr("Memory")
                        role: "memory"
                        minWidth: 90
                        preferredWidth: 110
                        sortable: true
                        cellDelegate: memoryCellComponent
                    },
                    LogosTableColumn {
                        title: ""
                        minWidth: 240
                        preferredWidth: 260
                        fillWidth: true
                        alignment: Qt.AlignRight | Qt.AlignVCenter
                        cellDelegate: actionsCellComponent
                    }
                ]

                Component {
                    id: moduleCellComponent

                    ColumnLayout {
                        spacing: 0

                        LogosText {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: rowItem ? rowItem.label : ""
                            font.pixelSize: Theme.typography.primaryText
                            font.weight: Theme.typography.weightMedium
                            color: Theme.palette.text
                            elide: Text.ElideRight
                        }

                        // Raw module name — the identifier other modules call
                        // it by. Hidden when it matches the display name.
                        LogosText {
                            Layout.fillWidth: true
                            visible: rowItem && rowItem.label !== rowItem.name
                            text: rowItem ? rowItem.name : ""
                            font.pixelSize: Theme.typography.secondaryText
                            color: Theme.palette.textTertiary
                            elide: Text.ElideRight
                        }
                    }
                }

                // The compact row's whole left side: the module, then the
                // status badge with the stats beside it. The badge keeps the
                // same automation handle it has in the Status column — the
                // Shell's iOS driver counts the rows on screen by it, and a
                // row is a row whichever layout drew it.
                Component {
                    id: compactModuleCellComponent

                    ColumnLayout {
                        spacing: Theme.spacing.tiny

                        LogosText {
                            Layout.fillWidth: true
                            text: rowItem ? rowItem.label : ""
                            font.pixelSize: Theme.typography.primaryText
                            font.weight: Theme.typography.weightMedium
                            color: Theme.palette.text
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacing.small

                            ModuleStatusBadge {
                                objectName: "moduleInspector.status."
                                            + (rowItem && rowItem.name ? rowItem.name : "")
                                row: rowItem
                            }

                            LogosText {
                                Layout.fillWidth: true
                                visible: rowItem && rowItem.isLoaded
                                text: rowItem
                                      ? Number(rowItem.cpu).toFixed(1) + "%  ·  "
                                        + Number(rowItem.memory).toFixed(1) + " MB"
                                      : ""
                                font.pixelSize: Theme.typography.secondaryText
                                color: Theme.palette.textTertiary
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Component {
                    id: statusCellComponent

                    Item {
                        ModuleStatusBadge {
                            // Automation-only: per-module handle so UI tests
                            // can assert one row's load state — the badge
                            // wording ("Loaded"/"Not loaded") also appears in
                            // other tables, so text matching is ambiguous.
                            objectName: "moduleInspector.status."
                                        + (rowItem && rowItem.name ? rowItem.name : "")
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            row: rowItem
                        }
                    }
                }

                // Stats only mean something for a running module; unloaded rows
                // render an em dash so the column stays aligned without
                // implying "0% CPU" is a measurement.
                Component {
                    id: cpuCellComponent

                    LogosText {
                        text: (rowItem && rowItem.isLoaded)
                              ? Number(rowItem.cpu).toFixed(1) + "%" : "—"
                        color: (rowItem && rowItem.isLoaded) ? Theme.palette.text
                                                            : Theme.palette.textMuted
                        font.pixelSize: Theme.typography.primaryText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Component {
                    id: memoryCellComponent

                    LogosText {
                        text: (rowItem && rowItem.isLoaded)
                              ? Number(rowItem.memory).toFixed(1) + " MB" : "—"
                        color: (rowItem && rowItem.isLoaded) ? Theme.palette.text
                                                            : Theme.palette.textMuted
                        font.pixelSize: Theme.typography.primaryText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Component {
                    id: actionsCellComponent

                    Item {
                        ModuleRowActions {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            row: rowItem
                            busy: root.loading
                            locked: !!rowItem && root.protectedModules.indexOf(rowItem.name) !== -1
                            // Compact rows have room for one control, and the
                            // toggle is the one that cannot be reached another
                            // way; Interface moves to the row (onRowClicked).
                            interfaceEnabled: !root.compact

                            onLoadToggleRequested: {
                                if (rowItem.isLoaded) root.unloadRequested(rowItem.name)
                                else                 root.loadRequested(rowItem.name)
                            }
                            onInterfaceRequested: root.openInterface(rowItem.name)
                        }
                    }
                }
            }
        }

        // ─── 1: Interface (methods + events) ───
        Item {
            PluginInterfaceView {
                anchors.fill: parent
                anchors.margins: Theme.spacing.large
                pluginName: root.selectedPlugin
                onBackClicked: root.showingInterface = false
            }
        }
    }

    // Same Reload feedback as the App Manager / Package Manager panels.
    LoadingOverlay {
        anchors.fill: parent
        visible: root.loading
    }
}
