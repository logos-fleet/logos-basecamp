import QtQuick
import QtQuick.Window
import QtTest

import Basecamp.AppManager

// THE CATALOG, ON A STORE SHELL'S SCREEN.
//
// The App Manager already decides every verdict in this suite -- availability
// is package_manager's, and the Platform floor's refusal is derived by the
// build and applied in StoreAppManager (logos-workspace#169). What it could not
// say until now is that a user ever SEES one: the verdict lived in
// `catalogEntries`, a model nothing on a phone was bound to, and the only place
// it appeared was a console line.
//
// So this is about the rendering rule and nothing else:
//
//   a refused row shows its reason IN THE ROW, and has NO install control
//   an installable row has one, and pressing it asks for that name
//   an installed row is neither -- it says what it is and offers nothing
//
// The three cases are one rule ("`canInstall` decides whether the control
// EXISTS") and it is asserted the way a finger meets it: the control is looked
// up by objectName, and its absence is checked rather than its `enabled`. A
// greyed-out button is not an answer to "why can I not install this".
TestCase {
    id: testCase
    name: "StoreCatalogView"
    when: windowShown

    // A stand-in for basecamp::appmanager::StoreAppManager, SHAPED LIKE THE
    // GATE and not like a button handler -- which is the thing this fake got
    // wrong, and the reason the suite was green while the press was dead
    // (logos-workspace#249).
    //
    // `beginInstall` does not install. The real one downloads the package, asks
    // package_manager who signed it and STOPS at stage 4 of five, publishing
    // `signerPrompt` and waiting for approveSigner() / rejectSigner()
    // (InstallGate.h). A fake whose beginInstall just recorded a name asserted
    // that the button emits -- which it always did -- and could not notice that
    // nothing on the page ever called the other two.
    QtObject {
        id: fakeAppManager

        property string catalogUnavailableReason: ""
        property var lastInstallRequest: ""
        // The two properties the page has to draw, in the shape
        // InstallGate::signerPrompt / refusedSigner publish them.
        property var signerPrompt: ({})
        property var refusedSigner: ({})
        property string lastError: ""
        property int installs: 0
        property string installed: ""
        // What signerTrust will say about the next package: approvable, or
        // refused over a key this device does not vouch for (ADR 0008).
        property bool signerIsInstallable: true

        property var catalogEntries: [
            {
                name: "web_counter_b", displayName: "Web Counter B", version: "1.0.0",
                description: "A counter whose UI is a page",
                available: true, unavailableReason: "", variant: "web",
                installed: false, installedVersion: "",
                canInstall: true, canReport: true, canOpenUniversalLink: true,
            },
            {
                // The Platform floor's refusal (#169): derived at build time,
                // and the sentence PlatformFloor::reasonFor produces.
                name: "chat_ui", displayName: "Chat", version: "2.0.0",
                description: "Chat, as a page",
                available: false,
                unavailableReason: "requires delivery_module, not in this build",
                variant: "", installed: false, installedVersion: "",
                canInstall: false, canReport: true, canOpenUniversalLink: false,
            },
            {
                // package_manager's own refusal, which reads the same way in a
                // row: this shell installs `web` variants and nothing else.
                name: "desktop_only", displayName: "Desktop Only", version: "2.1.0",
                description: "Ships darwin and linux variants",
                available: false,
                unavailableReason: "available on macOS and Linux, not in this build",
                variant: "", installed: false, installedVersion: "",
                canInstall: false, canReport: false, canOpenUniversalLink: false,
            },
            {
                name: "keystore_module", displayName: "Keystore", version: "1.0.0",
                description: "scrypt vaults and secp256k1 signing",
                available: true, unavailableReason: "", variant: "web",
                installed: true, installedVersion: "1.0.0",
                canInstall: false, canReport: false, canOpenUniversalLink: false,
            },
        ]

        function beginInstall(name) {
            lastInstallRequest = name
            lastError = ""
            refusedSigner = ({})
            if (!signerIsInstallable) {
                // The gate refuses before it prompts -- an Install button
                // beside a package that cannot be installed is the one thing
                // signerTrust exists to prevent -- and the IDENTITY survives
                // the refusal because the DID is the only way forward.
                signerPrompt = ({})
                lastError = "'" + name + "' is signed by a key your keyring "
                            + "does not vouch for"
                refusedSigner = ({
                    name: name, version: "1.0.0", signatureStatus: "signed",
                    signerName: "logos-catalog-test",
                    signerDid: "did:jwk:eyJjcnYiOiJFZDI1NTE5Ig",
                })
                return
            }
            signerPrompt = ({
                name: name, version: "1.0.0", installable: true,
                signatureStatus: "signed", policy: "require",
                signerName: "logos-catalog-test",
                signerDid: "did:jwk:eyJjcnYiOiJFZDI1NTE5Ig",
                trusted: true, trustedAs: "logos-catalog-test",
            })
        }

        function approveSigner() {
            if (Object.keys(signerPrompt).length === 0)
                return
            installed = signerPrompt.name
            ++installs
            signerPrompt = ({})
        }

        function rejectSigner() {
            if (Object.keys(signerPrompt).length === 0)
                return
            lastError = "you did not trust the signer of '"
                        + signerPrompt.name + "'"
            signerPrompt = ({})
        }
    }

    Component {
        id: hostComp

        Window {
            property alias catalog: view
            visible: true
            StoreCatalogView {
                id: view
                anchors.fill: parent
                appManager: fakeAppManager
            }
        }
    }

    // The screens a Store shell is read on. A phone is the narrow case and the
    // one the reason sentence has to survive: it is a sentence, not a badge,
    // and the row that carries it is 306 logical pixels wide once MainContainer
    // has taken the sidebar out of an iPhone 16 Pro (logos-workspace#87).
    function viewport_data() {
        return [
            { tag: "iphone-16-pro",        width: 306,  height: 834  },
            { tag: "ipad-air-13-portrait", width: 928,  height: 1326 },
            { tag: "desktop",              width: 1440, height: 900  },
        ];
    }

    function init() {
        fakeAppManager.signerPrompt = ({})
        fakeAppManager.refusedSigner = ({})
        fakeAppManager.lastError = ""
        fakeAppManager.lastInstallRequest = ""
        fakeAppManager.installs = 0
        fakeAppManager.installed = ""
        fakeAppManager.signerIsInstallable = true
    }

    function openCatalog(data) {
        var host = hostComp.createObject(null, { width: data.width, height: data.height });
        verify(host, "host window created");
        waitForRendering(host.contentItem);
        tryVerify(function() {
            return findChild(host.catalog, "storeCatalog.row.web_counter_b") !== null;
        }, 3000, "the catalog rows were instantiated");
        return host;
    }

    function test_a_refused_row_shows_its_reason_and_offers_no_install_data() {
        return viewport_data();
    }

    function test_a_refused_row_shows_its_reason_and_offers_no_install(data) {
        var host = openCatalog(data);

        var reason = findChild(host.catalog, "storeCatalog.reason.chat_ui");
        verify(reason, "the refused row carries a reason");
        verify(reason.visible, "and it is on screen");
        compare(reason.text, "requires delivery_module, not in this build",
                "in the words the floor refused it in");

        compare(findChild(host.catalog, "storeCatalog.install.chat_ui"), null,
                "a refused row has NO install control, not a disabled one");

        // The other refusal reads identically in the row: the App Manager
        // decides WHY, the row only shows it.
        var nativeOnly = findChild(host.catalog, "storeCatalog.reason.desktop_only");
        verify(nativeOnly, "the native-only row carries a reason too");
        compare(nativeOnly.text, "available on macOS and Linux, not in this build");
        compare(findChild(host.catalog, "storeCatalog.install.desktop_only"), null,
                "and no install control either");

        host.destroy();
    }

    function test_an_installable_row_offers_install_and_asks_by_name_data() {
        return viewport_data();
    }

    function test_an_installable_row_offers_install_and_asks_by_name(data) {
        var host = openCatalog(data);

        var install = findChild(host.catalog, "storeCatalog.install.web_counter_b");
        verify(install, "the installable row has an install control");
        verify(install.visible, "and it is on screen");

        // REACHABLE, not merely present. A Qt layout handed less room than its
        // minimum overflows rather than shrinking, and a control past the right
        // edge cannot be pressed by a finger at all (logos-workspace#84/#87).
        var origin = install.mapToItem(host.contentItem, 0, 0);
        verify(origin.x >= 0 && origin.x + install.width <= host.width,
               "the install control spans x " + Math.round(origin.x) + ".."
               + Math.round(origin.x + install.width) + ", outside the "
               + host.width + "-wide viewport");

        fakeAppManager.lastInstallRequest = "";
        mouseClick(install);
        tryCompare(fakeAppManager, "lastInstallRequest", "web_counter_b", 2000,
                   "pressing it asks the App Manager for that package");

        compare(findChild(host.catalog, "storeCatalog.reason.web_counter_b"), null,
                "and an available row carries no refusal");

        host.destroy();
    }

    function test_an_installed_row_offers_nothing_and_says_so_data() {
        return viewport_data();
    }

    function test_an_installed_row_offers_nothing_and_says_so(data) {
        var host = openCatalog(data);

        compare(findChild(host.catalog, "storeCatalog.install.keystore_module"), null,
                "an installed row is not offered again");
        var state = findChild(host.catalog, "storeCatalog.state.keystore_module");
        verify(state, "it says what it is instead");
        verify(state.text.indexOf("1.0.0") >= 0, "naming the version on the device");

        host.destroy();
    }

    // A shell whose Bundled set carries no package modules has no catalog at
    // all, and that is a normal state (ADR 0007). An EMPTY list would read as a
    // catalog with nothing in it, which is a different and more alarming thing.
    function test_a_shell_with_no_catalog_says_so_rather_than_showing_nothing() {
        var empty = Qt.createQmlObject(
            'import QtQuick; QtObject { property var catalogEntries: []; '
            + 'property string catalogUnavailableReason: '
            + '"package_manager is not in this build"; '
            + 'function beginInstall(n) {} }', testCase);
        var host = hostComp.createObject(null, { width: 306, height: 834 });
        host.catalog.appManager = empty;
        waitForRendering(host.contentItem);

        var message = findChild(host.catalog, "storeCatalog.unavailable");
        verify(message, "the page says why there is no catalog");
        verify(message.visible, "on screen");
        compare(message.text, "package_manager is not in this build");

        host.destroy();
        empty.destroy();
    }

    // ── THE PRESS, END TO END (logos-workspace#249) ────────────────────────
    //
    // An operator pressed Install on two rows this very model called
    // installable and got no install, no error and no visible change. The
    // press was fine: `beginInstall` ran, downloaded a megabyte, reached the
    // signer prompt and stopped there -- and NOTHING in any scene was bound to
    // `signerPrompt`, so stage 5 of the gate had no caller and the flow parked
    // for ever behind a page that looked idle.
    function test_pressing_install_puts_the_signer_gate_on_screen_data() {
        return viewport_data();
    }

    function test_pressing_install_puts_the_signer_gate_on_screen(data) {
        var host = openCatalog(data);

        compare(findChild(host.catalog, "storeCatalog.signer.install"), null,
                "no gate before the press");

        mouseClick(findChild(host.catalog, "storeCatalog.install.web_counter_b"));
        tryVerify(function() {
            return findChild(host.catalog, "storeCatalog.signer.title") !== null;
        }, 2000, "pressing Install puts the signer gate on screen");

        var title = findChild(host.catalog, "storeCatalog.signer.title");
        verify(title.text.indexOf("web_counter_b") >= 0,
               "naming the package being installed: " + title.text);

        // NAME AND DID TOGETHER. The name is self-asserted by the publisher;
        // the DID is what the keyring was checked against. Either alone tells
        // the user nothing they can verify.
        var identity = findChild(host.catalog, "storeCatalog.signer.identity");
        verify(identity, "the gate names the signer");
        verify(identity.text.indexOf("logos-catalog-test") >= 0,
               "by name: " + identity.text);
        verify(identity.text.indexOf("did:jwk:eyJjcnYiOiJFZDI1NTE5Ig") >= 0,
               "and by DID: " + identity.text);

        // REACHABLE, not merely present -- the same claim the row's own
        // control has to satisfy at a phone's width.
        var approve = findChild(host.catalog, "storeCatalog.signer.install");
        verify(approve, "and it offers a way to approve");
        var origin = approve.mapToItem(host.contentItem, 0, 0);
        verify(origin.x >= 0 && origin.x + approve.width <= host.width
               && origin.y >= 0 && origin.y + approve.height <= host.height,
               "the approve control spans x " + Math.round(origin.x) + ".."
               + Math.round(origin.x + approve.width) + " y "
               + Math.round(origin.y) + ".." + Math.round(origin.y + approve.height)
               + ", outside the " + host.width + "x" + host.height + " viewport");

        host.destroy();
    }

    function test_approving_the_gate_installs_data() {
        return viewport_data();
    }

    function test_approving_the_gate_installs(data) {
        var host = openCatalog(data);

        mouseClick(findChild(host.catalog, "storeCatalog.install.web_counter_b"));
        tryVerify(function() {
            return findChild(host.catalog, "storeCatalog.signer.install") !== null;
        }, 2000, "the gate is up");

        mouseClick(findChild(host.catalog, "storeCatalog.signer.install"));
        tryCompare(fakeAppManager, "installed", "web_counter_b", 2000,
                   "approving the signer is what installs the package");
        compare(fakeAppManager.installs, 1, "once");
        tryVerify(function() {
            return findChild(host.catalog, "storeCatalog.signer.install") === null;
        }, 2000, "and the gate comes back down");

        host.destroy();
    }

    function test_cancelling_the_gate_installs_nothing_and_says_so_data() {
        return viewport_data();
    }

    function test_cancelling_the_gate_installs_nothing_and_says_so(data) {
        var host = openCatalog(data);

        mouseClick(findChild(host.catalog, "storeCatalog.install.web_counter_b"));
        tryVerify(function() {
            return findChild(host.catalog, "storeCatalog.signer.cancel") !== null;
        }, 2000, "the gate is up");

        mouseClick(findChild(host.catalog, "storeCatalog.signer.cancel"));
        tryVerify(function() {
            return findChild(host.catalog, "storeCatalog.signer.cancel") === null;
        }, 2000, "cancelling takes the gate down");
        compare(fakeAppManager.installs, 0, "and installs nothing");

        // AND SAYS WHY, in the words the gate refused it in. A press whose
        // only outcome is a property nobody drew is the defect this whole
        // issue is.
        var shown = findChild(host.catalog, "storeCatalog.error");
        verify(shown, "the page carries the refusal");
        verify(shown.visible, "on screen");
        compare(shown.text, "you did not trust the signer of 'web_counter_b'");

        host.destroy();
    }

    // A REFUSAL THAT NEVER REACHES A PROMPT still has to be visible, and it is
    // the common one under a Store shell's `require` policy (ADR 0008):
    // package_manager refuses a package signed by a key the keyring does not
    // vouch for, and the DID is the only way forward from there -- it is what
    // would be anchored, and a phone has no `lgx keyring` to ask afterwards.
    function test_a_refused_signer_is_named_even_with_no_gate() {
        var host = openCatalog({ width: 306, height: 834 });
        fakeAppManager.signerIsInstallable = false;

        mouseClick(findChild(host.catalog, "storeCatalog.install.web_counter_b"));

        tryVerify(function() {
            var e = findChild(host.catalog, "storeCatalog.error");
            return e && e.visible && e.text !== "";
        }, 2000, "the refusal is on the page");
        compare(findChild(host.catalog, "storeCatalog.signer.install"), null,
                "and there is no Install control behind it");

        var who = findChild(host.catalog, "storeCatalog.refusedSigner");
        verify(who, "the refused key is named");
        verify(who.text.indexOf("did:jwk:eyJjcnYiOiJFZDI1NTE5Ig") >= 0,
               "by DID, which is the actionable half: " + who.text);
        compare(fakeAppManager.installs, 0, "nothing was installed");

        host.destroy();
    }
}
