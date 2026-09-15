import QtQuick
import QtQuick.Window
import QtTest

import Basecamp.Settings

// The Settings inspectors at handset and tablet widths.
//
// The Shell's tables were the desktop's: about a thousand logical pixels of
// columns, with the row's Load/Unload the LAST of them. On a phone — and on a
// 13-inch iPad in portrait — that column was off the right edge, and Qt
// delivers a press by coordinate, so the control could not be reached at all
// (logos-workspace#84). The iOS acceptance driver measured it and worked
// around it by activating the row's own control instead of pressing it, which
// kept its verdicts green while the UI was unusable by hand.
//
// So this suite asserts the geometry, and then PRESSES: mouseClick() goes to
// the item's coordinates in its window, so a control that sits past the right
// edge gets no click and the signal spy stays at zero. Nothing here inspects
// the column set — how a view gets its action on screen is its own business.
TestCase {
    id: testCase
    name: "SettingsMobileLayout"
    when: windowShown

    // Three core modules and one that is Basecamp itself: main_ui never gets a
    // toggle, so a row for it must NOT be looked for.
    ListModel {
        id: coreModules

        ListElement {
            name: "bare_counter"; label: "Bare Counter"; statusText: "Not loaded"
            description: "A counter with no UI"; version: "1.0.0"
            isLoaded: false; isMainUi: false; cpu: 0.0; memory: 0.0
            statsMeasured: false
        }
        ListElement {
            name: "view_counter"; label: "View Counter"; statusText: "Loaded"
            description: "A counter with a view"; version: "1.0.0"
            isLoaded: true; isMainUi: false; cpu: 1.5; memory: 12.5
            statsMeasured: true
        }
        ListElement {
            name: "main_ui"; label: "Basecamp Shell"; statusText: "Loaded"
            description: "This app"; version: "1.0.0"
            isLoaded: true; isMainUi: true; cpu: 3.0; memory: 40.0
            statsMeasured: true
        }
        // LOADED, AND NOTHING COULD ACCOUNT FOR IT -- the third stats state,
        // and the one that had no fixture: liblogos reports null rather than 0
        // for a module its container cannot measure (logos-workspace#86), so
        // the row is loaded and still owes no figure. See stats_rows().
        ListElement {
            name: "web_counter"; label: "Web Counter"; statusText: "Loaded"
            description: "A counter whose UI is a page"; version: "1.0.0"
            isLoaded: true; isMainUi: false; cpu: 0.0; memory: 0.0
            statsMeasured: false
        }
    }

    ListModel {
        id: uiModules

        ListElement {
            name: "counter_ui"; label: "Counter"; statusText: "Not loaded"
            description: "Counter's interface"; version: "1.0.0"
            isLoaded: false; isMainUi: false; cpu: 0.0; memory: 0.0; iconPath: ""
            statsMeasured: false
        }
    }

    Component {
        id: hostComp

        Window {
            property alias settings: view
            visible: true
            SettingsView {
                id: view
                anchors.fill: parent
                coreModulesModel: coreModules
                uiModulesModel: uiModules
            }
        }
    }

    // The screens this has been measured on, in the logical pixels the Shell's
    // content surface reported on each, plus a desktop width so a fix that only
    // ever collapses is caught too. The 724 is a physical iPad Air (4th gen):
    // it is the one that found the first threshold wrong, because its pane is
    // exactly the desktop columns' minimum total and the row overflowed anyway.
    //
    // THE PANE, NOT THE SCREEN. MainContainer spends 96 px of the width on the
    // sidebar and its insets and about 40 of the height on the tab bar before
    // a Settings view sees any of it -- 1024x1366 of iPad Air 13 arrives here
    // as 928x1326, and 820x1180 of iPad Air 4 as 724x1140. The iPhone entry
    // used to be the SCREEN's 402x874, which is 96 px more than the view ever
    // gets; it read as the most generous handset case rather than the real
    // one. 402-96 = 306 (logos-workspace#87).
    function viewport_data() {
        return [
            { tag: "iphone-16-pro",        width: 306,  height: 834  },
            { tag: "ipad-air-13-portrait", width: 928,  height: 1326 },
            { tag: "ipad-air-4-portrait",  width: 724,  height: 1140 },
            { tag: "desktop",              width: 1440, height: 900  },
        ];
    }

    // The subset of those that are below the compact threshold, for the
    // verdicts that are about the collapsed layout itself.
    function compact_viewport_data() {
        return [
            { tag: "iphone-16-pro",        width: 306, height: 834  },
            { tag: "ipad-air-13-portrait", width: 928, height: 1326 },
        ];
    }

    // Opens a Settings section and returns its table, once that table has the
    // width its column set will be laid out in. The settling is the point: the
    // width cascades down a StackLayout, a Flickable and a ListView and each
    // hop costs a polish pass, so a measurement taken straight after the click
    // reads the table at its implicit 32 px and every verdict below it is
    // about nothing. Polled rather than asserted, so that a table which never
    // settles is still reported by the geometry check that follows — the one
    // that can say what actually went wrong.
    function openSection(host, key, viewName, tableName) {
        var entry = findChild(host.settings, "settings.section." + key);
        verify(entry, "the Settings sidebar offers '" + key + "'");
        // Emitted, not pressed: the section strip scrolls, and which sections
        // are on screen is not what this suite is about.
        entry.clicked();

        var view = findChild(host.settings, viewName);
        verify(view, viewName + " is in the Settings stack");
        tryVerify(function() { return view.visible; }, 2000, viewName + " came to the front");

        var table = findChild(host.settings, tableName);
        verify(table, tableName + " exists");
        for (var i = 0; i < 100; ++i) {
            if (table.height > 0 && table.width === table.parent.width) break;
            wait(10);
        }
        waitForRendering(host.contentItem);
        return table;
    }

    function assertReachable(host, handle, spy, expectedArg) {
        var control = findChild(host.settings, handle);
        verify(control, handle + " exists");
        verify(control.visible, handle + " is visible");

        var origin = control.mapToItem(host.contentItem, 0, 0);
        var right = origin.x + control.width;
        var bottom = origin.y + control.height;
        verify(origin.x >= 0 && right <= host.width,
               handle + " spans x " + Math.round(origin.x) + ".." + Math.round(right)
               + ", outside the " + host.width + "-wide viewport");
        verify(origin.y >= 0 && bottom <= host.height,
               handle + " spans y " + Math.round(origin.y) + ".." + Math.round(bottom)
               + ", outside the " + host.height + "-tall viewport");

        // And it answers a press at those coordinates.
        spy.clear();
        mouseClick(control);
        tryCompare(spy, "count", 1, 2000, handle + " did not answer a tap");
        compare(spy.signalArguments[0][0], expectedArg);
    }

    function test_module_inspector_load_toggle_is_on_screen_data() { return viewport_data(); }

    function test_module_inspector_load_toggle_is_on_screen(data) {
        var host = hostComp.createObject(null, { width: data.width, height: data.height });
        verify(host, "host window created");
        waitForRendering(host.contentItem);

        openSection(host, "module_inspector", "moduleInspectorView",
                    "moduleInspector.table");

        var loadSpy = loadSpyComp.createObject(testCase, { target: host.settings });
        var unloadSpy = unloadSpyComp.createObject(testCase, { target: host.settings });

        tryVerify(function() {
            return findChild(host.settings, "moduleRow.loadToggle.bare_counter") !== null;
        }, 3000, "the rows were instantiated");

        // The unloaded row and the loaded one: the loaded one also carries the
        // Interface drill-down, so its action cluster is the wider of the two.
        assertReachable(host, "moduleRow.loadToggle.bare_counter", loadSpy, "bare_counter");
        assertReachable(host, "moduleRow.loadToggle.view_counter", unloadSpy, "view_counter");

        loadSpy.destroy();
        unloadSpy.destroy();
        host.destroy();
    }

    function test_apps_inspector_load_toggle_is_on_screen_data() { return viewport_data(); }

    function test_apps_inspector_load_toggle_is_on_screen(data) {
        var host = hostComp.createObject(null, { width: data.width, height: data.height });
        verify(host, "host window created");
        waitForRendering(host.contentItem);

        openSection(host, "apps_inspector", "appsInspectorView",
                    "appsInspector.table");

        var spy = appLoadSpyComp.createObject(testCase, { target: host.settings });

        tryVerify(function() {
            return findChild(host.settings, "moduleRow.loadToggle.counter_ui") !== null;
        }, 3000, "the rows were instantiated");

        assertReachable(host, "moduleRow.loadToggle.counter_ui", spy, "counter_ui");

        spy.destroy();
        host.destroy();
    }

    // Desktop keeps the full column set — a fix that collapsed everywhere
    // would pass the reachability tests above and still be a regression.
    function test_desktop_keeps_the_full_column_sets() {
        var host = hostComp.createObject(null, { width: 1440, height: 900 });
        waitForRendering(host.contentItem);

        var modules = openSection(host, "module_inspector", "moduleInspectorView",
                                  "moduleInspector.table");
        compare(modules.columns.length, 5,
                "desktop Module Inspector: Module / Status / CPU / Memory / actions");

        var apps = openSection(host, "apps_inspector", "appsInspectorView",
                               "appsInspector.table");
        compare(apps.columns.length, 5,
                "desktop Apps Inspector: App / Version / Status / Description / actions");

        host.destroy();
    }

    // ...and the mobile widths do collapse, rather than reaching the geometry
    // above by some other means (a narrower button, a scrolled viewport).
    function test_mobile_widths_collapse_to_the_row_and_its_action_data() {
        return compact_viewport_data();
    }

    function test_mobile_widths_collapse_to_the_row_and_its_action(data) {
        var host = hostComp.createObject(null, { width: data.width, height: data.height });
        waitForRendering(host.contentItem);

        var modules = openSection(host, "module_inspector", "moduleInspectorView",
                                  "moduleInspector.table");
        compare(modules.columns.length, 2, "Module Inspector keeps module + action");

        var apps = openSection(host, "apps_inspector", "appsInspectorView",
                               "appsInspector.table");
        compare(apps.columns.length, 2, "Apps Inspector keeps app + action");

        host.destroy();
    }

    // What the compact layout switches on. The threshold is a number in the
    // view, and the number it has to be is the width the desktop column set
    // asks for -- so read that set and add it up. A column added to either
    // table, or a preferredWidth changed, moves the answer and fails here
    // rather than three screens later.
    //
    // The PREFERRED total, not the minimum one: a RowLayout squeezed between
    // the two does not shrink every column proportionally, so the row overflows
    // well before the minimums bite. That is exactly what the iPad Air (4th
    // gen) showed -- a 700-px pane, the minimum total to the pixel, with the
    // toggle at x=710 in a 724-wide viewport.
    function test_the_compact_threshold_is_what_the_desktop_columns_ask_for_data() {
        return [
            { tag: "modules", view: "moduleInspectorView", table: "moduleInspector.table",
              key: "module_inspector" },
            { tag: "apps",    view: "appsInspectorView",   table: "appsInspector.table",
              key: "apps_inspector" },
        ];
    }

    function test_the_compact_threshold_is_what_the_desktop_columns_ask_for(data) {
        var host = hostComp.createObject(null, { width: 1440, height: 900 });
        waitForRendering(host.contentItem);
        var table = openSection(host, data.key, data.view, data.table);
        var view = findChild(host.settings, data.view);

        var wanted = 0;
        for (var i = 0; i < table.desktopColumns.length; ++i)
            wanted += table.desktopColumns[i].preferredWidth;

        compare(view.desktopColumnsWidth, wanted,
                data.tag + ": the threshold is the desktop columns' preferred total");
        host.destroy();
    }

    // The badge each row carries its module's name on is how the Shell's iOS
    // driver counts the rows on screen (ShellModulesDriver). It lives in the
    // Status column on the desktop and folds into the module cell when
    // compact, so a collapse that dropped it would blind that check.
    function test_the_compact_row_keeps_its_status_handle() {
        var host = hostComp.createObject(null, { width: 306, height: 834 });
        waitForRendering(host.contentItem);
        openSection(host, "module_inspector", "moduleInspectorView",
                    "moduleInspector.table");

        tryVerify(function() {
            return findChild(host.settings, "moduleInspector.status.bare_counter") !== null;
        }, 3000, "the compact row still carries moduleInspector.status.<name>");
        verify(findChild(host.settings, "moduleInspector.status.view_counter"),
               "for every row, not just the first");

        host.destroy();
    }

    // ─── the stats the compact row shows, and the ones it must not ───
    //
    // WHY THIS IS A TEST AND NOT A DEVICE RUN. The rule was only ever asserted
    // by ShellModulesDriver on a booted simulator, and every part of it has
    // been wrong at least once: the cells read 0.0% / 0.0 MB for modules
    // nothing had measured (logos-workspace#86), the driver demanded a figure
    // of rows that legitimately had none and of only one row that did
    // (logos-workspace#149 and #172), and logos-workspace#159 was filed
    // against the assertion rather than the UI.
    //
    // The three states a stats cell can be in, and what each one owes. Both
    // layouts are read against this one list, so a fix applied to the compact
    // row and not the desktop cells (or the other way round) fails here.
    //
    //   view_counter  loaded and measured     a figure, and its OWN: a cell
    //                                         rendering another row's reading
    //                                         must not pass on a lucky " MB"
    //   bare_counter  not loaded              nothing: a row claiming 0.0 MB
    //                                         for it invents a measurement
    //   web_counter   loaded, never measured  nothing either -- a `web`
    //                                         module's page is the platform's
    //                                         process, so its container has
    //                                         no reading to give. This is the
    //                                         state logos-workspace#172
    //                                         turned the driver's expectation
    //                                         around on.
    function stats_rows() {
        return [
            { row: "view_counter", measured: true,  figure: "12.5" },
            { row: "bare_counter", measured: false, figure: "" },
            { row: "web_counter",  measured: false, figure: "" },
        ];
    }

    // ONE LINE, NOT TWO CELLS, at a phone's width: CPU and Memory fold into
    // the module cell there (logos-workspace#84), which is why the driver has
    // to know both handles.
    function test_the_compact_row_shows_a_figure_only_where_there_is_one_data() {
        return compact_viewport_data();
    }

    function test_the_compact_row_shows_a_figure_only_where_there_is_one(data) {
        var host = hostComp.createObject(null, { width: data.width, height: data.height });
        waitForRendering(host.contentItem);
        openSection(host, "module_inspector", "moduleInspectorView",
                    "moduleInspector.table");

        tryVerify(function() {
            return findChild(host.settings, "moduleInspector.stats.view_counter") !== null;
        }, 3000, "the compact row carries moduleInspector.stats.<name>");

        var rows = stats_rows();
        for (var i = 0; i < rows.length; ++i) {
            var expected = rows[i];
            var where = data.tag + " " + expected.row + ": ";

            // `visible` is the only thing that can be asked of a row with no
            // reading: the binding still evaluates while the line is hidden,
            // so its `text` reads "0.0%  ·  0.0 MB" for a row that is
            // rendering nothing at all.
            var line = findChild(host.settings, "moduleInspector.stats." + expected.row);
            verify(line, where + "the row carries the stats handle");
            compare(line.visible, expected.measured, where + "shows a stats line");
            if (!expected.measured)
                continue;

            // The driver reads the rendered text, so "1.5%  ·  12.5 MB" is
            // the claim.
            verify(line.text.indexOf(" MB") !== -1,
                   where + "the line carries a memory figure, not '" + line.text + "'");
            verify(line.text.indexOf(expected.figure) !== -1,
                   where + "the row's own figure, not another row's: " + line.text);
        }

        host.destroy();
    }

    // The desktop half of the same rule: two cells rather than one line, and
    // an em dash rather than a hidden item, because the column has to stay
    // aligned.
    function test_the_desktop_cells_em_dash_what_was_never_measured() {
        var host = hostComp.createObject(null, { width: 1440, height: 900 });
        waitForRendering(host.contentItem);
        openSection(host, "module_inspector", "moduleInspectorView",
                    "moduleInspector.table");

        tryVerify(function() {
            return findChild(host.settings, "moduleInspector.memory.view_counter") !== null;
        }, 3000, "the desktop row carries moduleInspector.memory.<name>");

        var dash = "\u2014";
        var rows = stats_rows();
        for (var i = 0; i < rows.length; ++i) {
            var expected = rows[i];
            var where = expected.row + ": ";
            var cpu = findChild(host.settings, "moduleInspector.cpu." + expected.row);
            var mem = findChild(host.settings, "moduleInspector.memory." + expected.row);
            verify(cpu && mem, where + "has both desktop stats cells");
            compare(cpu.hasFigure, expected.measured, where + "cpu cell hasFigure");
            compare(mem.hasFigure, expected.measured, where + "memory cell hasFigure");
            if (expected.measured) {
                verify(mem.text.indexOf(" MB") !== -1,
                       where + "a measured memory cell reads a figure, got '"
                       + mem.text + "'");
            } else {
                compare(cpu.text, dash, where + "cpu cell is an em dash");
                compare(mem.text, dash, where + "memory cell is an em dash");
            }
        }

        host.destroy();
    }

    Component { id: loadSpyComp;    SignalSpy { signalName: "moduleLoadRequested" } }
    Component { id: unloadSpyComp;  SignalSpy { signalName: "moduleUnloadRequested" } }
    Component { id: appLoadSpyComp; SignalSpy { signalName: "appLoadRequested" } }
}
