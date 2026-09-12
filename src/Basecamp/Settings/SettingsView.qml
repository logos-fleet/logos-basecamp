import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Controls
import Logos.Theme

Rectangle {
    id: root

    property var    repositories:        []
    property bool   repositoriesLoading: false
    // Real Qt models (ModuleInstanceModel), owned by MainUIBackend and
    // populated on every uiModulesChanged/coreModulesChanged tick.
    property var    uiModulesModel:      null
    property var    coreModulesModel:    null
    property bool   modulesLoading:      false

    signal repositoryRefreshRequested()
    signal repositoryAddRequested(string url)
    signal repositoryRemoveRequested(string url)
    signal repositoryEnabledRequested(string url, bool enabled)
    signal repositoriesBecameVisible()

    signal appsRefreshRequested()
    signal appLoadRequested(string name)
    signal appUnloadRequested(string name)
    signal appsInspectorBecameVisible()

    signal modulesRefreshRequested()
    signal moduleLoadRequested(string name)
    signal moduleUnloadRequested(string name)
    signal moduleInspectorBecameVisible()

    function reportRepositoryResult(operation, url, success, error) {
        repositoriesView.reportOperationResult(operation, url, success, error)
    }

    function showRepositories() { d.selectedIndex = d.sectionRepositories }

    QtObject {
        id: d

        // Sub-views in the right pane. Order maps 1:1 to the StackLayout below.
        readonly property int sectionDashboard:       0
        readonly property int sectionAppsInspector:   1
        readonly property int sectionModuleInspector: 2
        readonly property int sectionRepositories:    3

        // `key` is an automation handle and nothing else: the labels are
        // translated and their order is a layout decision, so neither is
        // something a test may match on.
        readonly property var sections: [
            { key: "dashboard",         label: qsTr("Dashboard") },
            { key: "apps_inspector",    label: qsTr("Apps Inspector") },
            { key: "module_inspector",  label: qsTr("Module Inspector") },
            { key: "repositories",      label: qsTr("Package Repositories") }
        ]

        property int selectedIndex: 0

        // Search text is shared by the two inspectors. Reset whenever the
        // user switches away so stale queries don't leak between panels.
        property string searchText: ""

        readonly property bool searchable:
            selectedIndex === sectionAppsInspector ||
            selectedIndex === sectionModuleInspector

        onSelectedIndexChanged: searchText = ""

        // ─── Narrow (handset / tablet) layout ───
        // The desktop page is a 200-px section rail beside a pane, inside
        // 40-px insets: 252 px of the width is gone before the table starts,
        // and the inspectors' columns want ~700 more. On a phone — and on a
        // 13-inch iPad in portrait — that does not fit, and a Qt layout given
        // less than its minimum does not shrink, it OVERFLOWS: the pane ran
        // past the right edge of the screen, taking each row's Load/Unload
        // control with it (logos-workspace#84).
        //
        // Below this the rail becomes a strip above the pane and the insets
        // become a phone's, which hands the pane the whole width. Every text
        // on the page also declares a zero minimum, because one long label
        // with an implicit minimum is enough to push the page wide again.
        readonly property int narrowBelow: 900
        readonly property bool narrow: root.width > 0 && root.width < narrowBelow
        readonly property int pageMargin: narrow ? Theme.spacing.medium
                                                 : Theme.spacing.xxlarge
        readonly property int stripHeight: 40
    }

    Component {
        id: sectionsHeader

        LogosText {
            width: sectionsList.width
            topPadding: Theme.spacing.tiny
            bottomPadding: Theme.spacing.tiny
            text: qsTr("Sections")
            font.pixelSize: Theme.typography.subtitleText
            font.weight: Theme.typography.weightRegular
            color: Theme.palette.text
        }
    }

    color: Theme.palette.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: d.pageMargin
        spacing: d.narrow ? Theme.spacing.medium : Theme.spacing.xlarge

        // ─── Header ───
        // Title + subtitle on the left; when an inspector is selected, a
        // page-level search bar joins on the right — same layout as
        // AppManagerView so the two feel familiar side by side. Narrow, the
        // search bar drops to its own row underneath rather than squeezing
        // the title off the page.
        GridLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            columns: d.narrow ? 1 : 2
            columnSpacing: Theme.spacing.xlarge
            rowSpacing: Theme.spacing.medium

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: Theme.spacing.tiny

                LogosText {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Settings")
                    font.pixelSize: Theme.typography.pageTitleText
                    font.weight: Theme.typography.weightBold
                    color: Theme.palette.text
                    elide: Text.ElideRight
                }

                LogosText {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Manage modules, apps and dashboards.")
                    font.pixelSize: Theme.typography.primaryText
                    color: Theme.palette.textSecondary
                    elide: Text.ElideRight
                }
            }

            LogosSearchBar {
                id: searchBar
                objectName: "settings.searchField"
                visible: d.searchable
                Layout.alignment: d.narrow ? Qt.AlignLeft : Qt.AlignRight
                Layout.fillWidth: true
                Layout.preferredWidth: 605
                Layout.maximumWidth: 605
                Layout.minimumWidth: 0
                text: d.searchText
                placeholderText: d.selectedIndex === d.sectionAppsInspector
                                 ? qsTr("Search apps…")
                                 : qsTr("Search modules…")
                shortcutHint: "⌘K"
                onTextChanged: {
                    if (text !== d.searchText)
                        d.searchText = text
                }
            }

            Shortcut {
                sequence: "Ctrl+K"
                context: Qt.WindowShortcut
                enabled: root.visible && d.searchable
                onActivated: {
                    searchBar.textInput.forceActiveFocus()
                    searchBar.textInput.selectAll()
                }
            }
        }

        // Two cells side by side on the desktop; stacked, rail first, when
        // narrow. One GridLayout rather than two layouts behind a Loader, so
        // the sections list and the pane are the same items either way —
        // switching would rebuild the pane and lose the open section.
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            columns: d.narrow ? 1 : 2
            columnSpacing: Theme.spacing.medium
            rowSpacing: Theme.spacing.medium

            // ─── Sections: a rail on the desktop, a strip on a phone ───
            LogosListView {
                id: sectionsList

                orientation: d.narrow ? ListView.Horizontal : ListView.Vertical

                Layout.fillWidth: d.narrow
                Layout.fillHeight: !d.narrow
                Layout.minimumWidth: 0
                Layout.preferredWidth: d.narrow ? 0 : 200
                Layout.maximumWidth: d.narrow ? Number.POSITIVE_INFINITY : 200
                Layout.preferredHeight: d.narrow ? d.stripHeight : 0

                model: d.sections
                currentIndex: d.selectedIndex

                // The strip has no room for a heading, and the page title two
                // rows up already says where we are.
                header: d.narrow ? null : sectionsHeader

                delegate: LogosItemDelegate {
                    id: cell
                    objectName: "settings.section." + modelData.key
                    width: d.narrow ? implicitWidth : ListView.view.width
                    height: d.narrow ? d.stripHeight : implicitHeight
                    text: modelData.label
                    highlighted: ListView.isCurrentItem
                    radius: Theme.spacing.radiusLarge
                    highlightColor: Theme.palette.backgroundButton
                    hoverColor: "transparent"
                    textColor: (cell.highlighted || cell.hovered)
                                   ? Theme.palette.text
                                   : Theme.palette.textTertiary
                    onClicked: d.selectedIndex = index
                }
            }

            // ─── The pane ───
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Without this the pane keeps the desktop table's minimum and
                // the row of which it is a part overflows the page instead of
                // handing it what there is.
                Layout.minimumWidth: 0
                color: Theme.palette.surfaceRaised
                radius: Theme.spacing.radiusXlarge
                clip: true

                StackLayout {
                    anchors.fill: parent
                    currentIndex: d.selectedIndex

                    // 0 — Dashboard.
                    DashboardView {}

                    // 1 — Apps Inspector (UI plugins).
                    AppsInspectorView {
                        sourceModel: root.uiModulesModel
                        loading:     root.modulesLoading
                        searchText:  d.searchText

                        onReloadRequested: root.appsRefreshRequested()
                        onLoadRequested:   name => root.appLoadRequested(name)
                        onUnloadRequested: name => root.appUnloadRequested(name)
                        onVisibleChanged:  if (visible) root.appsInspectorBecameVisible()
                    }

                    // 2 — Module Inspector (core modules).
                    ModuleInspectorView {
                        sourceModel: root.coreModulesModel
                        loading:     root.modulesLoading
                        searchText:  d.searchText

                        onReloadRequested: root.modulesRefreshRequested()
                        onLoadRequested:   name => root.moduleLoadRequested(name)
                        onUnloadRequested: name => root.moduleUnloadRequested(name)
                        onVisibleChanged:  if (visible) root.moduleInspectorBecameVisible()
                    }

                    // 3 — Package Repositories.
                    RepositoriesView {
                        id: repositoriesView

                        repositories: root.repositories
                        loading:      root.repositoriesLoading

                        onRefreshRequested:    root.repositoryRefreshRequested()
                        onAddRequested:        url => root.repositoryAddRequested(url)
                        onRemoveRequested:     url => root.repositoryRemoveRequested(url)
                        onSetEnabledRequested: (url, enabled) =>
                                                   root.repositoryEnabledRequested(url, enabled)
                        onVisibleChanged:      if (visible)
                                                   root.repositoriesBecameVisible()
                    }
                }
            }
        }
    }
}
