{
  description = "Logos Basecamp - Qt application with UI plugins";

  inputs = {
    # THE MOBILE CHAIN'S INPUTS ARE LOCKED TO THE logos-fleet FORKS, not to
    # these URLs: the smoke host needs logos-nix's mobile pseudo-systems,
    # logos-liblogos's lib.mkMobileChains, and the cross-build CMake options
    # in logos-protocol, logos-plugin-qt, logos-module, logos-package and
    # logos-package-manager -- none of which are upstream yet. `nix flake
    # update` would move them back and the mobile outputs would stop
    # evaluating; re-pin with
    #   nix flake lock --override-input <input> github:logos-fleet/<repo>/<rev>
    logos-nix.url = "github:logos-co/logos-nix";
    # Follow the same nixpkgs as logos-nix
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-protocol.url = "github:logos-co/logos-protocol";
    logos-plugin-qt.url = "github:logos-co/logos-plugin-qt";
    logos-qt-sdk.url = "github:logos-co/logos-qt-sdk";
    logos-module.url = "github:logos-co/logos-module";
    logos-module-loader-qt.url = "github:logos-co/logos-module-loader-qt";
    logos-liblogos.url = "github:logos-co/logos-liblogos";
    # ONE logos-protocol, and ONE logos-qt-host, in what the app stages.
    # logos-qt-host bakes sizeof(LogosAPIClient) into its own `operator new`
    # while logos-protocol DEFINES that constructor, so a second protocol is an
    # 8-byte heap overrun on every getClient() -- silent on macOS, where it
    # rounds up into the next malloc size class, and fatal on glibc. Without
    # these the app linked one protocol and qt-host was built against another.
    logos-cpp-sdk.inputs.logos-protocol.follows = "logos-protocol";
    logos-plugin-qt.inputs.logos-protocol.follows = "logos-protocol";
    logos-qt-sdk.inputs.logos-protocol.follows = "logos-protocol";
    logos-qt-sdk.inputs.logos-plugin-qt.follows = "logos-plugin-qt";
    logos-qt-sdk.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    logos-liblogos.inputs.logos-protocol.follows = "logos-protocol";
    logos-liblogos.inputs.logos-plugin-qt.follows = "logos-plugin-qt";
    logos-liblogos.inputs.logos-qt-sdk.follows = "logos-qt-sdk";
    logos-liblogos.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    logos-package-manager.url = "github:logos-co/logos-package-manager";
    # liblogos_core links libpackage_manager_lib, so liblogos otherwise puts
    # its OWN older liblgx in the bundle's flat lib/ — where the module's
    # newer copy can never win on macOS, and package_manager crashes.
    logos-liblogos.inputs.logos-package-manager.follows = "logos-package-manager";
    # ...and ONE lgx source tree and ONE logos-module. liblogos's mobile chain
    # builds lgx (and logos_module) FROM SOURCE off these inputs, so without
    # the follows the smoke host links a different lgx tree than the one this
    # flake ships, and a standalone `nix build` of the mobile outputs resolves
    # them upstream, where the cross-build options do not exist yet.
    logos-liblogos.inputs.logos-package.follows = "logos-package";
    logos-liblogos.inputs.logos-module.follows = "logos-module";
    logos-package-manager-module.url = "github:logos-co/logos-package-manager-module";
    logos-package-downloader-module.url = "github:logos-co/logos-package-downloader-module";
    # ...and their BUILDER is this flake's, for the same reason every other
    # Bundled-set member's is (see the mobile block below): a Bare image is
    # stamped with the logos-protocol version it compiled against and the host
    # gates that stamp at load. These two are now mobile catalog members, so a
    # builder of their own would be a second protocol in the app image.
    logos-package-manager-module.inputs.logos-module-builder.follows = "logos-module-builder";
    logos-package-downloader-module.inputs.logos-module-builder.follows = "logos-module-builder";
    # The capability broker, and a MEMBER of the mobile dev catalog
    # (mobileCatalogFor below): the catalog carries its `bare` output, reached
    # as `legacyPackages.<buildSystem>.mobile.<target>.bare`. A module-to-module
    # call on a phone mints its token through this module, so the networking set
    # cannot run without it.
    #
    # LOCKED TO THE logos-fleet FORK, for the same reason and with the same
    # consequence as logos-module-builder below: upstream publishes no mobile
    # keys and no Bare build of its own (it included boost/uuid until the fork),
    # so a bare `nix flake update` walks the lock back to logos-co and the
    # mobile outputs stop EVALUATING ("attribute 'legacyPackages' missing").
    # Re-pin with
    #   nix flake lock --override-input logos-capability-module \
    #     github:logos-fleet/logos-capability-module/<rev>
    logos-capability-module.url = "github:logos-co/logos-capability-module";
    # ONE builder in the closure, for the reason the logos-module-builder block
    # below states for bare_counter and which now applies here too: the app
    # embeds capability_module's `bare` artifact, a Bare module is stamped with
    # the logos-protocol version it was compiled against, and the host gates
    # that stamp at load. Two builders means two protocol pins and an app that
    # refuses its own bundled module -- and without the follows the mobile keys
    # do not exist at all, because this module's OWN pin predates them
    # ("attribute 'legacyPackages' missing").
    logos-capability-module.inputs.logos-module-builder.follows = "logos-module-builder";
    logos-modules-state-module.url = "github:logos-co/logos-modules-state-module";
    logos-package.url = "github:logos-co/logos-package";
    logos-package-manager-ui.url = "github:logos-co/logos-package-manager-ui";
    # The UI otherwise brings its own package_manager and package_downloader,
    # so the closure carries two of each and the UI that drives installs sits
    # on the older one — the one with no VersionMismatch, and without the
    # signer-binding fix.
    logos-package-manager-ui.inputs.package_manager.follows = "logos-package-manager-module";
    logos-package-manager-ui.inputs.package_downloader.follows = "logos-package-downloader-module";
    # The BUNDLED module's builder. mobile/bare-counter is built through
    # `logos-module-builder.lib.mkLogosModule`, and the app embeds its `bare`
    # output -- an iOS framework, an Android .so.
    #
    # Every shared input is cut to this flake's copy, and the reason is the
    # same one the block above states for logos-qt-host: a Bare module is
    # compiled against logos-protocol's headers and stamped with the protocol
    # version it saw, and the host gates that stamp at load
    # (bareModuleProtocolCompatible). Two protocol pins in one closure means
    # the app either refuses its own bundled module or -- worse, on a MINOR
    # skew -- loads it and disagrees about the wire.
    #
    # LOCKED TO THE logos-fleet FORK, not to this URL: `bareCounter` reaches for
    # `legacyPackages.<buildSystem>.mobile.<target>.bare`, which upstream does
    # not publish. A bare `nix flake update` walks the lock back to logos-co and
    # the mobile smoke apps stop EVALUATING. Re-pin with
    #   nix flake lock --override-input logos-module-builder \
    #     github:logos-fleet/logos-module-builder/<rev>
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-module-builder.inputs.logos-nix.follows = "logos-nix";
    logos-module-builder.inputs.logos-protocol.follows = "logos-protocol";
    logos-module-builder.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    logos-module-builder.inputs.logos-qt-sdk.follows = "logos-qt-sdk";
    logos-module-builder.inputs.logos-plugin-qt.follows = "logos-plugin-qt";
    # Not a typo: the builder carries a SECOND alias of the same repo
    # (github:logos-co/logos-plugin-qt) under the pre-rename name, and it must
    # land on the same rev as the alias above or the two backends disagree.
    logos-module-builder.inputs.logos-plugin-core.follows = "logos-plugin-qt";
    logos-module-builder.inputs.logos-module.follows = "logos-module";
    logos-module-builder.inputs.nix-bundle-logos-module-install.follows =
      "nix-bundle-logos-module-install";
    # ONE VIEW RUNTIME IN THE CLOSURE, for the reason every other `follows` here
    # exists and for one more that is specific to it.
    #
    # The builder exports a `ui_qml` module's `web` variant (`web-view-counter`,
    # and every real ui_qml module's `web` output) only when ITS
    # logos-view-module-runtime publishes `qml-runtime-wasm`. Without this line
    # that is the builder's own lock -- which at the time of writing predates the
    # output -- so no `web` variant existed in this flake at all: `web-container-
    # test` was silently ABSENT from `nix flake show .#checks`, and the mobile
    # apps shipped no module for their Web container to load. The workspace flake
    # already declares this follows and saw a different world from the repo's own
    # (logos-fleet/logos-workspace#91).
    #
    # And the second reason: the app SERVES the runtime it bundles
    # (nix/app.nix stages `qmlRuntimeWasm` under share/logos-runtime, the mobile
    # apps carry it in nix/mobile-web-assets.nix). If the builder linked a
    # variant against a different one, the page would load a backend image built
    # against a runtime this build does not ship.
    logos-module-builder.inputs.logos-view-module-runtime.follows =
      "logos-view-module-runtime";
    logos-design-system.url = "github:logos-co/logos-design-system";
    logos-view-module-runtime.url = "github:logos-co/logos-view-module-runtime";
    # ui-host links the same qt-host and protocol the app does.
    logos-view-module-runtime.inputs.logos-protocol.follows = "logos-protocol";
    logos-view-module-runtime.inputs.logos-plugin-qt.follows = "logos-plugin-qt";
    logos-view-module-runtime.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    nix-bundle-logos-module-install.url = "github:logos-co/nix-bundle-logos-module-install";
    # The PUBLISH half of the mobile catalog: nix-bundle-lgx owns `.lgx`, so it
    # owns restaging a cross-built module image as a variant payload, signing
    # it, indexing it and pinning a release. This flake is the CONSUMER --
    # nix/bundled-set.nix resolves and admits -- and the two halves live in
    # different repos on purpose: a Store shell build consumes a pinned release
    # it did not produce, and a publisher that ships inside its only consumer
    # can only ever publish that consumer's own modules.
    #
    # LOCKED TO THE logos-fleet FORK: `lib.<system>.mkMobileCatalog` is not
    # upstream yet. A bare `nix flake update` walks it back to logos-co and the
    # mobile outputs stop EVALUATING. Re-pin with
    #   nix flake lock --override-input nix-bundle-lgx \
    #     github:logos-fleet/nix-bundle-lgx/<rev>
    nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
    nix-bundle-lgx.inputs.logos-nix.follows = "logos-nix";
    # ONE lgx source tree in the closure: the publisher writes the manifest and
    # the Merkle tree that nix/verify-lgx-member.sh then re-checks, and a
    # package written by one lgx and admitted by another is two
    # implementations agreeing by luck.
    nix-bundle-lgx.inputs.logos-package.follows = "logos-package";
    nix-bundle-lgx.inputs.nix-bundle-dir.follows = "nix-bundle-dir";
    # ── the networking modules the mobile catalog publishes ────────────────
    # libp2p_module, delivery_module and chat_module, as Bare images for the
    # three mobile targets. They are here for the same reason bare-counter is
    # built here: the app's Bundled set is resolved out of a CATALOG, and the
    # dev catalog has to be built from something.
    #
    # EVERY shared input follows this flake's copy, and logos-module-builder
    # above all: a Bare module is compiled against logos-protocol's headers and
    # stamped with the protocol version it saw, and the host gates that stamp
    # at load (bareModuleProtocolCompatible). Each of the three pins its own
    # builder rev upstream, and three builders in one closure means three
    # protocols -- the app would refuse its own bundled modules, or worse, load
    # them and disagree about the wire.
    #
    # LOCKED TO THE logos-fleet FORKS, like the rest of the mobile chain: the
    # `bare` outputs for the mobile pseudo-systems do not exist upstream.
    logos-libp2p-module.url = "github:logos-co/logos-libp2p-module";
    logos-libp2p-module.inputs.logos-module-builder.follows = "logos-module-builder";
    logos-delivery-module.url = "github:logos-co/logos-delivery-module";
    logos-delivery-module.inputs.logos-module-builder.follows = "logos-module-builder";
    logos-delivery-module.inputs.nix-bundle-lgx.follows = "nix-bundle-lgx";
    logos-chat-module.url = "github:logos-co/logos-chat-module";
    logos-chat-module.inputs.logos-module-builder.follows = "logos-module-builder";
    # chat_module DEPENDS on delivery_module (metadata.json), and the .lidl
    # contract it generates against has to be the one the delivery image in the
    # same Bundled set actually implements.
    logos-chat-module.inputs.logos-delivery-module.follows = "logos-delivery-module";
    # chat_ui: the REAL Chat app, and the mobile catalog's second `ui_qml`
    # member. view_counter is a fixture that proves the shape; this is the
    # module a user means by "Chat" -- `src/qml/ChatView.qml` over a
    # `ChatBackend.rep`, the same tree the desktop plugin is built from.
    #
    # Its iOS `view` framework is built by THIS flake's logos-module-builder,
    # which is why every shared input is cut to this flake's copy: the
    # framework is stamped with the logos-protocol version it compiled against
    # and the host gates that stamp at load, exactly as it does for a Bare
    # module. chat_ui's own flake follows its builder THROUGH chat_module, so
    # pointing chat_module here is what moves the builder too.
    #
    # LOCKED TO THE logos-fleet FORK, like the rest of the mobile chain:
    # `packages.aarch64-ios.view` only exists on a builder new enough to
    # publish the mobile keys, and upstream's pin is not. Re-pin with
    #   nix flake lock --override-input logos-chat-ui \
    #     github:logos-fleet/logos-chat-ui/<rev>
    logos-chat-ui.url = "github:logos-co/logos-chat-ui";
    logos-chat-ui.inputs.chat_module.follows = "logos-chat-module";
    logos-chat-ui.inputs.logos-delivery-module.follows = "logos-delivery-module";
    nix-bundle-dir.url = "github:logos-co/nix-bundle-dir";
    # LOCKED TO THE logos-fleet FORK, not to this URL: the test framework's
    # per-app inspector port (launchAppWithInspector) is not upstream yet, and
    # without it integration-test, host-services-test and shutdown-test all
    # serve their app's inspector on 3768 -- so whichever two nix happens to
    # build in parallel end up driving the same app. Re-pin with
    #   nix flake lock --override-input logos-qt-mcp \
    #     github:logos-fleet/logos-qt-mcp/<rev>
    logos-qt-mcp.url = "github:logos-co/logos-qt-mcp";
    nix-bundle-appimage.url = "github:logos-co/nix-bundle-appimage";
    nix-bundle-macos-app = {
      url = "github:logos-co/nix-bundle-macos-app";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.nix-bundle-dir.follows = "nix-bundle-dir";
    };
  };

  nixConfig = {
    extra-substituters = [ "https://cache.nix.logos.co/public" ];
    extra-trusted-public-keys = [ "public:l4HrXgL4nw246+LBh2SOJyhz64BoGegOYLheT/iIAPU=" ];
  };

  outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-protocol, logos-plugin-qt, logos-qt-sdk, logos-module, logos-module-loader-qt, logos-liblogos, logos-libp2p-module, logos-delivery-module, logos-chat-module, logos-chat-ui, logos-package-manager, logos-package-manager-module, logos-package-downloader-module, logos-capability-module, logos-modules-state-module, logos-package, logos-package-manager-ui, logos-design-system, logos-view-module-runtime, logos-module-builder, logos-qt-mcp, nix-bundle-logos-module-install, nix-bundle-lgx, nix-bundle-dir, nix-bundle-appimage, nix-bundle-macos-app }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      # Build info (version + commit hashes) baked into the app binary so
      # the Dashboard can render it. Commits come from the flake inputs'
      # locked revs; self's rev is "dirty" when the checkout has uncommitted
      # changes or is overridden via a path input.
      revOf = input: input.rev or input.dirtyRev or "dirty";
      buildInfo = {
        # VERSION is only present on release branches. On master (pre-release
        # CI builds) there is no VERSION file, so fall back to a
        # "pre-release-{sha7}" string derived from self.rev — available on
        # every clean CI checkout. Dirty local builds lack self.rev and get
        # an empty string, which hides the badge (intentional for dev).
        version = if builtins.pathExists ./VERSION
          then nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION)
          else if (self ? rev) then "pre-release-${builtins.substring 0 7 self.rev}" else "";
        commits = [
          { name = "logos-basecamp"; commit = revOf self; }
          { name = "logos-nix"; commit = revOf logos-nix; }
          { name = "logos-cpp-sdk"; commit = revOf logos-cpp-sdk; }
          { name = "logos-module"; commit = revOf logos-module; }
          { name = "logos-liblogos"; commit = revOf logos-liblogos; }
          { name = "logos-package-manager"; commit = revOf logos-package-manager; }
          { name = "logos-package-manager-module"; commit = revOf logos-package-manager-module; }
          { name = "logos-package-downloader-module"; commit = revOf logos-package-downloader-module; }
          { name = "logos-capability-module"; commit = revOf logos-capability-module; }
          { name = "logos-modules-state-module"; commit = revOf logos-modules-state-module; }
          { name = "logos-package"; commit = revOf logos-package; }
          { name = "logos-package-manager-ui"; commit = revOf logos-package-manager-ui; }
          { name = "logos-design-system"; commit = revOf logos-design-system; }
          { name = "logos-view-module-runtime"; commit = revOf logos-view-module-runtime; }
          { name = "logos-qt-mcp"; commit = revOf logos-qt-mcp; }
          { name = "nix-bundle-logos-module-install"; commit = revOf nix-bundle-logos-module-install; }
          { name = "nix-bundle-dir"; commit = revOf nix-bundle-dir; }
          { name = "nix-bundle-appimage"; commit = revOf nix-bundle-appimage; }
          { name = "nix-bundle-macos-app"; commit = revOf nix-bundle-macos-app; }
        ];
      };
      # The BUILD platform for a given target. Bundlers and code generators RUN
      # during the build, so on the x86_64-windows cross target they must come
      # from the build system -- taking them from packages.x86_64-windows would
      # hand the builder a PE it cannot execute.
      buildSystemFor = target:
        if target == "x86_64-windows" then "x86_64-linux" else target;

      # forAllSystems, plus the "x86_64-windows" pseudo-system. A cross
      # derivation's `system` attr is its BUILD platform, so the Windows
      # attributes evaluate anywhere and realise on x86_64-linux. Keying it as a
      # system rather than a package-name suffix is what lets the 34
      # `dep.packages.${system}.x` interpolations below stay untouched.
      forAllSystems = f: logos-nix.lib.forAllTargets ({ system, pkgs }:
        let buildSystem = buildSystemFor system; in f {
        inherit system pkgs;
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        # The SAME output carries BOTH the target headers/CMake package AND the
        # logos-cpp-generator binary, so this is a SPLIT, not a swap: keep
        # logosSdk for -DLOGOS_CPP_SDK_ROOT and use logosSdkBuild wherever the
        # generator must RUN. Getting it backwards succeeds on native and, under
        # cross, puts a PE on the builder's PATH -- the symptom is
        # "logos-cpp-generator: command not found".
        logosSdkBuild = logos-cpp-sdk.packages.${buildSystem}.default;
        logosProtocolPkg = logos-protocol.packages.${system}.default;
        logosQtHost = logos-plugin-qt.packages.${system}.logos-qt-host;
        # HEADERS ONLY -- the Qt<->lp seam headers the generated dependency
        # wrapper in app/generated/ includes. Nothing links this.
        logosQtSdk = logos-qt-sdk.packages.${system}.default;
        logosModule = logos-module.packages.${system}.default;
        logosLiblogos = logos-liblogos.packages.${system}.default;
        logosPackageManagerLibrary = logos-package-manager.packages.${system}.lib;
        logosPackageManagerModule = logos-package-manager-module.packages.${system}.default;
        logosPackageManagerModuleLib = logos-package-manager-module.packages.${system}.lib;
        logosPackageDownloaderModule = logos-package-downloader-module.packages.${system}.default;
        logosPackageDownloaderModuleLib = logos-package-downloader-module.packages.${system}.lib;
        logosLiblogosPortable = logos-liblogos.packages.${system}.portable;
        logosPackageManagerModuleLibPortable = logos-package-manager-module.packages.${system}.lib-portable;
        logosCapabilityModule = logos-capability-module.packages.${system}.default;
        logosModulesStateModule = logos-modules-state-module.packages.${system}.default;
        logosPackageLib = logos-package.packages.${system}.lib;
        # The `lgx` CLI, keyed by BUILD system: it is the tool the Bundled-set
        # build RUNS (create, add, sign, verify, extract), never something a
        # target links. Taking it from ${system} would hand an iOS binary to the
        # builder.
        logosLgx = logos-package.packages.${buildSystem}.lgx;
        # Headers-only output (include/ with logos/semver.hpp + semver/, no
        # library). The app's AppsModel includes the shared semver comparator;
        # it links nothing from lgx, so the headers output keeps liblgx out of
        # the app entirely.
        logosPackageHeaders = logos-package.packages.${system}.headers;
        logosPackageManagerUI = logos-package-manager-ui.packages.${system}.default;
        logosDesignSystem = logos-design-system.packages.${system}.default;
        logosViewModuleRuntime = logos-view-module-runtime.packages.${system}.default;
        # The app's bundled Qt-wasm QML runtime, served to every `web` variant's
        # page (ADR 0004).
        #
        # null on Windows, where the whole Web-container path is — and null
        # while this repo's lock predates the runtime, which is the same
        # condition `webContainerFixture` reads for the check. The app still
        # builds and still opens a page; a `web` variant whose manifest asks for
        # the qml runtime then fails IN THE PAGE with a message naming what is
        # missing, which is the honest answer for a build that shipped without
        # one (see WebContainerBackend::install).
        qmlRuntimeWasm =
          if pkgs.stdenv.hostPlatform.isWindows then null
          else (logos-view-module-runtime.packages.${system} or {}).qml-runtime-wasm or null;
        # logos-qt-mcp is the QML inspector used by the UI test harness. It has
        # no Windows target and is not needed to RUN the app -- nix/app.nix
        # already takes `logosQtMcp ? null` and gates the inspector on it -- so
        # Windows builds simply go without it. The inspector-dependent outputs
        # (integration-test, shutdown-test, mcp-server) are correspondingly
        # absent from the Windows package set; see the `packages` block.
        logosQtMcp =
          if system == "x86_64-windows" then null
          else logos-qt-mcp.packages.${system}.default;
        logosCppSdkSrc = logos-cpp-sdk.outPath;
        logosLiblogosSrc = logos-liblogos.outPath;
        logosPackageManagerModuleSrc = logos-package-manager-module.outPath;
        logosCapabilityModuleSrc = logos-capability-module.outPath;
        # Bundlers run ON the builder, so they are keyed by buildSystem, not by
        # the target. nix-bundle-dir in particular is ELF/Mach-O only (its
        # bundle.sh branches `file -b` -> Mach-O | ELF with no PE case), so on
        # Windows it must not be invoked at all -- see nix/app.nix.
        # Keyed by the TARGET, not buildSystem: the install bundler now does its
        # own host/target split internally -- it takes lgpm from the build
        # system (it runs there) and the .lgx bundler from the target (which
        # decides the variant name and library extension). Keying the whole
        # thing by buildSystem made it label a Windows package "linux-amd64"
        # and look for a .so payload that was really a .dll.
        installDev = nix-bundle-logos-module-install.bundlers.${system}.dev;
        installPortable = nix-bundle-logos-module-install.bundlers.${system}.portable;
        dirBundler = nix-bundle-dir.bundlers.${buildSystem}.qtApp;
      });

      # The key the LOCAL catalogs in this repo are signed with -- the fixture
      # catalog the Bundled-set tests build, and the mobile dev catalog the
      # simulator app is assembled from. It is a TEST key: its secret half is in
      # the repo, so a signature by it proves only that the catalog builder ran,
      # and nothing in this tree ever adds it to a device keyring. A published
      # catalog is signed by a key that is not in a git repository.
      catalogTestKey = {
        name = "logos-catalog-test";
        jwk = ./mobile/catalog/keys/logos-catalog-test.jwk;
        did = nixpkgs.lib.fileContents ./mobile/catalog/keys/logos-catalog-test.did;
      };

      # ── Mobile: the liblogos smoke host ───────────────────────────────────
      # liblogos_core running on a phone with nothing loaded. The core, and
      # the eight repos it links, are cross-built by logos-liblogos
      # (lib.mkMobileChains); this flake adds the host that starts it and the
      # runners that put it on a simulator, an iPhone/iPad or an Android
      # device.
      #
      # Mobile pseudo-systems are opt-in in logos-nix (an iOS host is
      # stdenv.isDarwin, so folding them into forAllTargets misroutes every
      # `if isDarwin` above) and are merged onto `packages` the same way
      # x86_64-windows is.
      #
      # androidBuildSystem: the Android derivations' `system` is their BUILD
      # platform, and the canonical one is x86_64-linux, which a Mac cannot
      # realise even though it builds the identical closure. legacyPackages
      # below is where a Mac asks for the Android APK.
      #
      # The smoke host builds against the package set the chain itself was
      # built from (`chain.pkgs`) rather than instantiating a second one.
      #
      # bareCounter is the app's ONE Bundled module (mobile/bare-counter), and
      # the host embeds its `bare` output: an iOS framework in
      # <App>.app/Frameworks/, an Android .so in the native library directory.
      # It is built by logos-module-builder from THIS flake's logos-protocol
      # and logos-cpp-sdk (see the `follows` block at the top), so the protocol
      # version stamped into the artifact is the one the host gates it against.
      bareCounter = logos-module-builder.lib.mkLogosModule {
        src = ./mobile/bare-counter;
        configFile = ./mobile/bare-counter/metadata.json;
      };

      # viewCounter is the app's ONE Bundled VIEW module (mobile/view-counter):
      # a `type: ui_qml` module whose iOS `view` output is one embedded
      # framework carrying its Qt backend and its QML, with Qt and LogosAPI
      # bound upward into the app. Same builder, same follows block, same
      # protocol pin as bareCounter.
      viewCounter = logos-module-builder.lib.mkLogosQmlModule {
        src = ./mobile/view-counter;
        configFile = ./mobile/view-counter/metadata.json;
        # The two modules it calls, by the name metadata.json declares them
        # under (snake_case, not the flake spelling) -- that is what
        # collectAllModuleDeps matches on. Each publishes a LIDL contract, which
        # is what a declared dependency has to do since the copy-headers-out-of-
        # the-built-plugin fallback was removed.
        flakeInputs = {
          bare_counter = bareCounter;
          capability_module = logos-capability-module;
        };
      };

      # ── the Bundled set ───────────────────────────────────────────────────
      # Which modules the app carries is a LIST, and a list cannot be a flake
      # attribute name: `--bundle a,b` and `--bundle b,a` would be two outputs
      # of a flake that can enumerate neither. So `ws build --bundle` reaches
      # here through the environment instead, read under `--impure`.
      #
      # In a PURE evaluation getEnv returns "", so `nix flake check`, CI and a
      # plain `nix build` all see the fixed default below. The flag widens what
      # a developer can ask for; it never makes the default build irreproducible.
      #
      # AC 5, and the reason this is an env read rather than codegen: adding an
      # app to --bundle changes no source file. The set is resolved from the
      # catalog, and the host reads it from a manifest at runtime.
      requestedBundle = default:
        let e = builtins.getEnv "LOGOS_BUNDLE_APPS"; in
        if e == "" then default
        else builtins.filter (a: a != "") (nixpkgs.lib.splitString "," e);

      # WHICH `web` MODULES THE APP IMAGE CARRIES. The same mechanism and the
      # same rules as `--bundle` above: a list cannot be a flake attribute name,
      # `getEnv` is "" in a pure evaluation, and the default below is what every
      # check and every plain `nix build` sees.
      #
      # It exists for the one thing the default build cannot show. A Store shell
      # installing a module from a catalog has to be a build that does NOT
      # already ship that module -- otherwise "it installed" and "it came in the
      # app image" are the same observation, and the Modules tab would call it
      # `embedded` because it would BE embedded. So the acceptance run ships
      # `web_counter` and installs `web_counter_b`:
      #
      #   LOGOS_SHELL_WEB_MODULES=web_counter nix run --impure .#run-basecamp-shell-ios-sim
      requestedWebModules = default:
        let e = builtins.getEnv "LOGOS_SHELL_WEB_MODULES"; in
        if e == "" then default
        else builtins.filter (a: a != "") (nixpkgs.lib.splitString "," e);

      # The dev catalog: the two mobile modules in this repo, published as
      # signed .lgx packages with per-target variants, exactly as a release
      # catalog publishes them. `ws build --target ... --bundle ...` resolves
      # against THIS, so the path a Store shell build takes is the path the
      # smoke app takes -- there is no second, shorter route that only the app
      # uses and only the app tests.
      #
      # THREE PACKAGES, TWO ROOTS. capability_module is the third, and it is a
      # REAL cross build of repos/logos-capability-module rather than a fixture:
      # the trust root every module-to-module call mints its token through
      # (logos-protocol's LogosAPIClient auto-`requestModule` path), published
      # here so the set the app carries is not limited to the two demo modules
      # that live in this repo. It cross-builds because logos-module-builder now
      # puts a module's own `nix.packages` on the mobile Bare build's CMake
      # roots and include path -- capability_module's only third-party use is
      # header-only boost/uuid, which logos-nix does cross-build for both mobile
      # sets, and nothing was naming it to the compile.
      #
      # bare_counter and view_counter stay two ROOTS rather than gaining a
      # dependency edge between them: the view counter does not call the bare
      # counter, and writing a dependency into a signed manifest to make a
      # closure come out the right size would be a lie the core would later act
      # on. A real multi-level closure is exercised by nix/bundled-set-test.nix.
      mobileCatalogFor = { system, androidBuildSystem }:
        let
          chain = (logos-liblogos.lib.mkMobileChains { inherit androidBuildSystem; }).${system};
          buildPkgs = chain.pkgs.pkgsBuildBuild;
          buildSystem = chain.pkgs.stdenv.buildPlatform.system;
          lgx = logos-package.packages.${buildSystem}.lgx;
          catalogLib = nix-bundle-lgx.lib.${buildSystem}.mkMobileCatalog { inherit lgx; };
          bundledSetLib = import ./nix/bundled-set.nix {
            pkgs = buildPkgs; inherit lgx; publisher = catalogLib;
          };

          target = bundledSetLib.variantForSystem.${system};
          isAndroid = system == "aarch64-android";
          signingKey = { inherit (catalogTestKey) name jwk; };

          # One catalog entry per Bare module, and they are all the same shape:
          # the `mobile.<target>.bare` image the module's OWN flake cross-builds,
          # published under `<name>_bare` and signed with the dev key. Nothing
          # here knows that one of them carries a nim libp2p, another a nim
          # delivery core plus zerokit's rln, and the third a Rust chat core.
          #
          # `legacyPackages.<buildSystem>.mobile`, not `packages.<system>`: a
          # cross derivation's `system` is its BUILD platform, and the Android
          # leg has to be the one this machine can realise.
          #
          # `dependencies` is the module's own metadata.json answer, not a
          # convenience: the Bundled set resolves a CLOSURE out of it, so
          # `--bundle chat_module` has to bring delivery_module along without
          # naming it.
          mkBareSpec = { name, version, category, description, module, dependencies ? [ ] }: {
            inherit name version category description dependencies signingKey;
            type = "core";
            variants.${target} = catalogLib.mkVariantPayload {
              drv = module.legacyPackages.${androidBuildSystem}.mobile.${system}.bare;
              stem = "${name}_bare";
              inherit target;
            };
          };

          viewPayload = catalogLib.mkVariantPayload {
            drv = viewCounter.packages.${system}.view;
            stem = "view_counter_view";
            inherit target;
            # The QML the host renders comes out of the framework's qrc (ADR
            # 0006); this is the same source file, shipped so the PACKAGE is a
            # valid ui_qml one and a reader that never dlopens the image can
            # still see the view it declares.
            extraFiles."qml/Main.qml" = ./mobile/view-counter/src/qml/Main.qml;
          };

          # The same shape for the REAL chat app. Its QML tree is a directory
          # (ChatView.qml plus the ChatUi/ module beside it) and all of it
          # travels inside the framework's qrc; the entry document is shipped
          # beside the image for the same reason view_counter's is.
          chatUiPayload = catalogLib.mkVariantPayload {
            drv = logos-chat-ui.packages.${system}.view;
            stem = "chat_ui_view";
            inherit target;
            extraFiles."qml/ChatView.qml" = "${logos-chat-ui}/src/qml/ChatView.qml";
          };

          specs = {
            bare_counter = mkBareSpec {
              name = "bare_counter";
              version = "1.0.0";
              category = "testing";
              description = "The counter, as a Bundled Bare module for the mobile host";
              module = bareCounter;
            };

            # The capability broker. Not a networking module, and here for what
            # the three below NEED: a module-to-module call mints its token
            # through `capability_module` (LogosAPIClient::mintAndCacheToken),
            # and without it in the set the call goes out with no token, the
            # target's ModuleProxy refuses it and the caller is told "token not
            # recognized". Measured on the iOS simulator: chat_module's
            # `delivery_module.createNode` failed exactly that way and the chat
            # core came up with delivery_state "error".
            #
            # liblogos already knows this module by name -- the in-process
            # container grants it `token_registry` / `token_delivery`
            # (hostServicesJsonFor) -- so bundling it is the whole of the wiring.
            capability_module = mkBareSpec {
              name = "capability_module";
              version = logos-capability-module.config.version;
              category = "security";
              description = "Coordinates permissions between modules";
              module = logos-capability-module;
            };

            # ── the networking set (slice 21) ─────────────────────────────
            libp2p_module = mkBareSpec {
              name = "libp2p_module";
              version = "1.0.0";
              category = "protocol";
              description = "nim-libp2p's C bindings as a Bundled Bare module";
              module = logos-libp2p-module;
            };
            delivery_module = mkBareSpec {
              name = "delivery_module";
              version = "0.2.1";
              category = "protocol";
              description = "logosdelivery + rln as a Bundled Bare module";
              module = logos-delivery-module;
            };
            chat_module = mkBareSpec {
              name = "chat_module";
              version = "0.2.2";
              category = "messaging";
              description = "The Rust chat core as a Bundled Bare module";
              module = logos-chat-module;
              dependencies = [ "delivery_module" ];
            };
          } // nixpkgs.lib.optionalAttrs (!isAndroid) {
            # ── the two package modules (slice 29) ────────────────────────
            # What turns `ShellStoreBackend::hasCatalog()` from false into a
            # catalog: the App Manager browses through package_downloader and
            # judges availability through package_manager, and a Store shell
            # carries them the only way a phone allows -- inside the app image,
            # at build time (ADR 0007).
            #
            # iOS ONLY, and the refusal on the other side is the honest one.
            # Both libraries reach a phone as STATIC archives (logos-package's
            # lgx, logos-package-manager's lgpm, logos-package-downloader's lgpd
            # with curl and OpenSSL folded in), which is what iOS wants anyway.
            # The same libraries cross-compile for Android as SHARED objects,
            # and a Bare module linking liblgx.so would need liblgx.so in the
            # APK beside it -- a second, unbundled soname that the Android
            # DT_NEEDED gate refuses by design. So `--bundle package_manager
            # --target android-arm64` is refused BY NAME rather than half-built,
            # which is the same answer `--bundle view_counter` gets there.
            package_manager = mkBareSpec {
              name = "package_manager";
              version = logos-package-manager-module.config.version;
              category = "management";
              description = "Plugin manager for the Logos system";
              module = logos-package-manager-module;
            };
            package_downloader = mkBareSpec {
              name = "package_downloader";
              version = logos-package-downloader-module.config.version;
              category = "management";
              description = "Online package catalog and download service";
              module = logos-package-downloader-module;
            };

            view_counter = {
              name = "view_counter";
              version = "1.0.0";
              type = "ui_qml";
              category = "misc";
              description = "A QML counter over a .rep backend, as one embedded framework";
              view = "qml/Main.qml";
              icon = ./mobile/catalog/icon.png;
              # THE CLOSURE, and it is the module's own -- read off
              # mobile/view-counter/metadata.json rather than listed here, so
              # the signed manifest cannot drift from what the code does.
              # ViewCounterPlugin renders bare_counter's count by calling it,
              # and a module-to-module call mints its token through
              # capability_module. `--bundle view_counter` therefore resolves
              # all three.
              dependencies = viewCounter.config.dependencies;
              variants.${target} = viewPayload;
              inherit signingKey;
            };

            # ── the milestone's own app (slice 22) ────────────────────────
            # `--bundle chat_ui` is what the slice names, and this is the
            # entry that makes it resolve. Same shape as view_counter and a
            # different KIND of member: view_counter is a fixture built out of
            # this repo, chat_ui is an external `type: ui_qml` repo whose
            # desktop plugin already ships -- so the catalog is now carrying a
            # real app rather than only the demo that proved the shape.
            #
            # ITS CLOSURE IS ITS OWN, read off logos-chat-ui/metadata.json:
            # chat_ui -> chat_module -> delivery_module. capability_module and
            # libp2p_module are NOT in it and are not added here -- neither is
            # a chat_ui dependency (the first is how any module-to-module call
            # mints its token, the second is the transport delivery dials),
            # and writing ambient infrastructure into a signed manifest would
            # be a claim the core would later act on. They are named on
            # --bundle beside chat_ui, which is what the two `ws build`
            # commands in the README do.
            chat_ui = {
              name = "chat_ui";
              version = logos-chat-ui.config.version;
              type = "ui_qml";
              category = "chat";
              description = "Chat App for Logos - Private messaging interface";
              view = "qml/ChatView.qml";
              icon = ./mobile/catalog/icon.png;
              dependencies = logos-chat-ui.config.dependencies;
              variants.${target} = chatUiPayload;
              inherit signingKey;
            };
          };

          drvs = nixpkgs.lib.mapAttrs (_: catalogLib.mkPackage) specs;
        in
        {
          inherit target bundledSetLib;
          catalog = catalogLib.mkCatalog {
            release = "logos-basecamp-mobile-dev";
            signers = [ catalogTestKey.did ];
            packages = nixpkgs.lib.mapAttrsToList
              (n: spec: { inherit spec; drv = drvs.${n}; }) specs;
          };
          # On iOS ONE name is enough: view_counter's own declared
          # dependencies resolve the rest, so the set comes out as
          # view_counter -> bare_counter -> capability_module.
          #
          # Android's Qt is shared objects, so a ui_qml module there is a
          # different artifact that logos-module-builder does not publish yet.
          # The two core members are named directly instead, and
          # `--bundle view_counter --target android-arm64` is exactly the case
          # the set must refuse by name rather than half-build.
          defaultApps =
            if isAndroid then [ "capability_module" "bare_counter" ] else [ "view_counter" ];
        };

      mobileBundledSetFor = { system, androidBuildSystem }:
        let c = mobileCatalogFor { inherit system androidBuildSystem; }; in
        c.bundledSetLib.mkBundledSet {
          inherit (c) catalog target;
          apps = requestedBundle c.defaultApps;
          pname = "liblogos-smoke-bundled-set";
        };

      # THE `web` HALF OF A PHONE APP: the Qt-wasm QML runtime and the
      # Downloaded `web` modules this build ships (nix/mobile-web-assets.nix).
      # Architecture-free wasm and JavaScript, so it is keyed off the BUILD
      # platform and both phones carry the same bytes.
      #
      # `web-view-counter` and `web-view-counter-b` are the only `ui_qml` `web`
      # variants that exist -- the same instrumented fixture built twice under
      # two names, which logos-module-builder exports precisely so a container
      # with a live-runtime budget can be shown enforcing it. Both are absent
      # while a pin predates the Qt-for-WebAssembly outputs they need, which is
      # a pin rollout rather than a defect: `or null` each, and the app then
      # ships whichever exist.
      mobileWebAssetsFor = { chain, androidBuildSystem }:
        let
          builderPkgs = logos-module-builder.packages.${androidBuildSystem} or { };
          wanted = requestedWebModules [ "web_counter" "web_counter_b" ];
          named = name: attr:
            nixpkgs.lib.optionalAttrs
              (builtins.elem name wanted && builderPkgs ? ${attr})
              { ${name} = builderPkgs.${attr}; };
        in
        import ./nix/mobile-web-assets.nix {
          pkgs = chain.pkgs.pkgsBuildBuild;
          qmlRuntimeWasm =
            (logos-view-module-runtime.packages.${androidBuildSystem} or { }).qml-runtime-wasm
              or null;
          webVariants = named "web_counter" "web-view-counter"
                     // named "web_counter_b" "web-view-counter-b";
        };

      mkMobileSmoke = { androidBuildSystem ? "x86_64-linux" }:
        nixpkgs.lib.mapAttrs
          (system: chain:
            let isAndroid = system == "aarch64-android"; in
            import (if isAndroid then ./nix/android-apps.nix else ./nix/ios-apps.nix) {
              inherit (chain) pkgs;
              inherit chain;
              src = ./.;
              # The app's Bundled set: the modules named on --bundle, resolved
              # out of the mobile catalog, signature- and Merkle-checked, and
              # laid out in the one directory this platform's loader will open
              # them from. The host reads bundled-set.json out of it and knows
              # nothing else about what it carries.
              bundledSet = mobileBundledSetFor { inherit system androidBuildSystem; };

              # The app's `web` half: the bundled QML runtime and the
              # Downloaded `web` modules it ships. See mobileWebAssetsFor
              # above. Both phones, keyed off the build platform alone --
              # wasm and JavaScript are architecture-free.
              webAssets = mobileWebAssetsFor { inherit chain androidBuildSystem; };

              # LogosViewPlugin.h — the HOST side of the view-plugin
              # interface, header-only. The runtime's library and its ui-host
              # binary are a desktop concern; on a phone the host holds the
              # view object itself and only needs the declaration to cast to.
              #
              # Both phones, even though only iOS has a view module to mount:
              # the mobile catalog publishes no `ui_qml` variant for Android,
              # but BundledSetShellHost is the same file on both and it is
              # what mounts one.
              viewRuntimeSrc = logos-view-module-runtime;

              # The Shell's own images: the design system and main_ui, cross
              # built as static archives. Pure UI -- no logos input reaches
              # them -- so they are keyed off the chain's package set alone.
              # Two files rather than one because the platforms differ in what
              # they do with the archives, not in which ones they are: see the
              # header of each.
              shellUi = import
                (if isAndroid then ./nix/shell-ui-android.nix else ./nix/shell-ui-ios.nix)
                {
                  inherit (chain) pkgs;
                  src = ./.;
                  # Only `.version` is read; no logos input reaches these
                  # stages, and the nulls are what says so.
                  version = (import ./nix/default.nix {
                    inherit (chain) pkgs;
                    logosSdk = null;
                    logosProtocolPkg = null;
                    logosQtHost = null;
                    logosModule = null;
                    logosLiblogos = null;
                  }).version;
                  designSystemSrc = logos-design-system;
                };
            })
          (logos-liblogos.lib.mkMobileChains { inherit androidBuildSystem; });

      # One smoke set per Android build platform; `packages`, `apps` and
      # `legacyPackages` below are views of these, so nothing is instantiated
      # twice.
      mobileSmokeFor = nixpkgs.lib.genAttrs logos-nix.lib.androidBuildSystems
        (androidBuildSystem: mkMobileSmoke { inherit androidBuildSystem; });
      # Flake `packages` carry the canonical (x86_64-linux) Android build platform.
      mobileSmoke = mobileSmokeFor.x86_64-linux;
    in
    {
      packages = forAllSystems ({ pkgs, system, logosSdk, logosSdkBuild, logosProtocolPkg, logosQtHost, logosQtSdk, logosModule, logosLiblogos, logosLiblogosPortable, logosPackageManagerLibrary, logosPackageManagerModule, logosPackageManagerModuleLib, logosPackageManagerModuleLibPortable, logosPackageDownloaderModule, logosPackageDownloaderModuleLib, logosPackageLib, logosPackageHeaders, logosLgx, logosPackageManagerUI, logosCapabilityModule, logosModulesStateModule, logosDesignSystem, logosViewModuleRuntime, qmlRuntimeWasm, logosQtMcp, installDev, installPortable, dirBundler, ... }:
        let
          # Common configuration
          common = import ./nix/default.nix {
            inherit pkgs logosSdk logosProtocolPkg logosQtHost logosModule logosLiblogos;
          };
          src = ./.;

          # Basecamp's own UI shell: a privilege-free plugin that links Qt and
          # nothing else from this workspace, which nix/symbol-gate.nix enforces
          # across the in-process image set. It builds from the SAME `src` as
          # the app; its CMakeLists lives in src/ and can only see
          # app/interfaces/, so it cannot include a host header even by accident.
          mainUIPlugin = import ./nix/main-ui.nix {
            inherit pkgs common src logosDesignSystem;
          };

          packageManagerUIPlugin = logosPackageManagerUI;

          # Pre-installed modules/plugins (bundle + lgpm install in one step).
          # Dev build: raw derivation (depends on /nix/store at runtime).
          # Distributed build: portable self-contained bundle. Off Windows that
          # portability comes from nix-bundle-dir; on Windows nix-bundle-lgx
          # takes its own `mkWindowsPayload` path instead and does not call
          # nix-bundle-dir at all. That is nix-bundle-lgx's business, not this
          # flake's -- a MODULE must not carry the Qt/OpenSSL/runtime DLLs the
          # host already ships in bin/, so the PE path needs a hostLibs strip
          # that nix-bundle-dir does not have yet. Unrelated to binBundleDir
          # below, which is the APP and therefore is the thing that ships them.
          installedDev = map installDev [
            logosPackageManagerModuleLib
            logosPackageDownloaderModuleLib
            logosCapabilityModule
            # The module lifecycle registry. liblogos feeds it load/unload/crash
            # as sequenced facts, which is what lets a consumer stop polling --
            # PackageCoordinator's settle-timer inference is what this replaces.
            # Optional by construction: absent, the feed never arms.
            logosModulesStateModule
            packageManagerUIPlugin
          ];
          installedDistributed = map installPortable [
            logosPackageManagerModuleLibPortable
            logosPackageDownloaderModuleLib
            logosCapabilityModule
            logosModulesStateModule
            packageManagerUIPlugin
          ];

          # App package (development build)
          app = import ./nix/app.nix {
            inherit pkgs common src logosModule logosLiblogos logosSdk logosProtocolPkg logosQtHost logosQtSdk logosDesignSystem logosViewModuleRuntime logosPackageManagerModule logosPackageDownloaderModule logosPackageHeaders buildInfo logosSdkBuild;
            inherit logosQtMcp mainUIPlugin qmlRuntimeWasm;
            installedModules = installedDev;
          };

          mockTests = import ./nix/mock-tests.nix { inherit pkgs src; };

          # The UI shell alone, against a fixture, with no Logos code linked.
          # Lowest-coupling way to run the UI -- and the first thing worth
          # standing up on a platform the runtime has not been ported to.
          shellPreview = import ./nix/shell-preview.nix {
            inherit pkgs common src mainUIPlugin;
          };
          appMock = import ./nix/app.nix {
            inherit pkgs common src logosModule logosLiblogos logosSdk logosProtocolPkg logosQtHost logosQtSdk logosDesignSystem logosViewModuleRuntime logosPackageManagerModule logosPackageDownloaderModule logosPackageHeaders buildInfo logosSdkBuild;
            inherit mainUIPlugin;
            # The SAME plugins the real dev build stages. PMUI is real code
            # loaded from disk here — only the modules it talks to are faked —
            # so it has to actually be in the bundle.
            installedModules = installedDev;
            enableInspector = false;
            useMockBackend = true;
          };

          # App package (distributed build for DMG/AppImage)
          # Uses portable-compiled liblogos for portable variant selection
          appDistributed = import ./nix/app.nix {
            inherit pkgs common src logosModule logosSdk logosProtocolPkg logosQtHost logosQtSdk logosDesignSystem logosViewModuleRuntime logosPackageManagerModule logosPackageDownloaderModule logosPackageHeaders buildInfo logosSdkBuild;
            inherit mainUIPlugin;
            logosLiblogos = logosLiblogosPortable;
            installedModules = installedDistributed;
            portable = true;
            enableInspector = false;
          };

          # PORTABLE mock: the same fixture-backed backend, built the way the
          # shipped bundles are (portable liblogos, portable module variants,
          # no /nix/store references after bundling).
          appMockPortable = import ./nix/app.nix {
            inherit pkgs common src logosModule logosSdk logosProtocolPkg logosQtHost logosQtSdk logosDesignSystem logosViewModuleRuntime logosPackageManagerModule logosPackageDownloaderModule logosPackageHeaders buildInfo logosSdkBuild;
            inherit mainUIPlugin;
            logosLiblogos = logosLiblogosPortable;
            installedModules = installedDistributed;
            portable = true;
            enableInspector = false;
            useMockBackend = true;
          };

          # Distributed build with inspector enabled (for macOS integration tests)
          appDistributedWithInspector = import ./nix/app.nix {
            inherit pkgs common src logosModule logosSdk logosProtocolPkg logosQtHost logosQtSdk logosDesignSystem logosViewModuleRuntime logosPackageManagerModule logosPackageDownloaderModule logosPackageHeaders buildInfo logosSdkBuild;
            inherit logosQtMcp mainUIPlugin;
            logosLiblogos = logosLiblogosPortable;
            installedModules = installedDistributed;
            portable = true;
            enableInspector = true;
          };

          # macOS app for testing (distributed build with inspector enabled)
          macosAppTest = if pkgs.stdenv.isDarwin then
            nix-bundle-macos-app.lib.${system}.mkMacOSApp {
              drv = appDistributedWithInspector;
              name = "LogosBasecamp";
              bundle = dirBundler appDistributedWithInspector;
              icon = ./app/macos/logos.icns;
              infoPlist = ./app/macos/Info.plist.in;
              entitlements = ./app/macos/LogosBasecamp.entitlements;
            }
          else null;

          macosApp = if pkgs.stdenv.isDarwin then
            nix-bundle-macos-app.lib.${system}.mkMacOSApp {
              drv = appDistributed;
              name = "LogosBasecamp";
              bundle = dirBundler appDistributed;
              icon = ./app/macos/logos.icns;
              infoPlist = ./app/macos/Info.plist.in;
              entitlements = ./app/macos/LogosBasecamp.entitlements;
            }
          else null;

          # (There is no ./nix/appimage.nix binding here. The shipped AppImage is
          # the `bin-appimage` output further down, built by nix-bundle-appimage.
          # A dead `appImage = import ./nix/appimage.nix ...` binding survived
          # here long after that file was removed, evaluating only because
          # nothing ever forced it.)

          # Self-contained directory bundle: appDistributed modules expect host Qt
          # via @rpath; qtApp copies Qt frameworks into lib/ and rewrites the binary.
          # (appDistributed alone is an intermediate used by AppImage / .app wrappers.)
          withMainProgram = drv: drv.overrideAttrs (old: {
            meta = (old.meta or {}) // {
              mainProgram = "LogosBasecamp";
            };
          });
          # dirBundler on EVERY platform, Windows included.
          #
          # The `winBundler = drv: drv` / `bundleFor` bypass that used to sit
          # here was placed on an explicit condition: "the real fix is a PE
          # branch in nix-bundle-dir's bundle.sh that skips relocation and keeps
          # Qt staging; when that lands, DELETE bundleFor". It has landed, and
          # this flake's root `nix-bundle-dir` input already resolves to it
          # (f843b8ec, which is `main`), so the condition is met and the bypass
          # is gone.
          #
          # Why it was never a working alternative: nix-bundle-dir does two
          # separable jobs. (a) RELOCATION -- rewriting rpaths / install names
          # so binaries stop pointing into /nix/store. (b) Qt STAGING -- the Qt
          # plugin scan, the QML module scan and qt.conf generation; that half
          # is FORMAT-AGNOSTIC and Windows needs it exactly as much as anywhere
          # else, because Qt plugins and QML module DLLs are LoadLibrary'd and
          # nothing in the import table reveals them.
          #
          # The bypass was argued for on the grounds that a PE needs none of
          # (a). Only the rpath REWRITING half of that is true, and the
          # measurement below is what corrects it: the un-bundled tree reaches
          # a third of its bin/ through 12 SYMLINKS into /nix/store, which no
          # amount of "PE imports are base names" makes portable. Phase 1's
          # `cp -aL` is what dereferences them, and skipping the bundler
          # skipped that as surely as it skipped (b).
          #
          # Measured, not assumed. Both trees were realised on x86_64-linux
          # from ONE tree -- same appDistributed derivation, the only variable
          # being whether dirBundler is applied -- and compared entry by entry:
          #
          #                              bypass(drv:drv)   dirBundler
          #     entries                              85         1740
          #     regular files                        59         1657
          #     symlinks                             12            0
          #     bytes                            259 MB       479 MB
          #     *.dll in bin/                        33           88
          #     bin/qt.conf                     MISSING      present
          #     lib/qt-6/…/platforms/qwindows.dll
          #                                     MISSING      present
          #     lib/qt-6 (plugins + qml)        MISSING   1533 files
          #     nix closure refs                     12            1
          #
          # Those 12 symlinks are the part that matters most, and they are why
          # "the bypass at least shipped the app" was never true. bin/Qt6Core
          # .dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Network.dll, Qt6RemoteObjects
          # .dll, libssl/libcrypto, libpng16, libzstd, libb2, pcre2 and
          # double-conversion were SYMLINKS into /nix/store. Copy that tree to
          # a Windows box -- the entire point of a portable bundle -- and every
          # one of them dangles: 0xC0000135, no output, before main(). The
          # bundled tree resolves all 12 into real files and has no symlink
          # left. Its single remaining nix reference is an inert /nix string
          # embedded in a PE's data, which imports nothing.
          #
          # 55 DLLs exist only in the bundled bin/: the Qt Quick / Controls /
          # Labs set the fixpoint sweep pulls in once the QML modules are
          # staged, plus libcurl and its TLS/HTTP2 chain mirrored beside the
          # package_downloader module that imports them.
          #
          # The one thing the bundler does NOT carry over: README.txt and
          # share/ (a .desktop file and a hicolor icon, 9 paths). That is not a
          # Windows regression -- bundle.sh Phase 1 copies bin/, lib/ and
          # extraDirs on EVERY platform, so the shipping Linux and macOS
          # bundles have never had them either; the bypass "kept" them only by
          # doing nothing at all. Both are dead weight off-store anyway:
          # README.txt is a build-info file whose every line is a /nix/store
          # path, and a .desktop file does nothing on Windows.
          #
          # Getting here also required three additions to the app's
          # passthru.extraClosurePaths (see nix/app.nix) -- qtdeclarative,
          # libjpeg.bin, sqlite.bin. Each was a build the bundler FAILED,
          # naming the missing DLL and the plugin that imported it, rather than
          # shipping a tree that dies before main(). That is the behaviour the
          # bypass was hiding.
          #
          # NOT demonstrated, stated plainly:
          #
          #  * None of this has been RUN on Windows, by this change or by CI,
          #    which has never executed a Windows binary. Every claim above is
          #    build-time and tree-shape only.
          #  * The two trees measured came from a harness that dropped ONE
          #    entry from installedDistributed -- packageManagerUIPlugin --
          #    because logos-package-manager-ui did not cross-compile at the rev
          #    this flake pinned then: its generated logos_sdk.h included
          #    package_manager_api.h, which was not produced for the Windows
          #    target. There was also a separate EVAL-time blocker, a
          #    logos-package-downloader-module with no x86_64-windows target.
          #
          #    BOTH have since been fixed upstream. `.#packages.x86_64-windows.*`
          #    now evaluates AND builds, and the resulting bundle DOES include
          #    package_manager_ui -- so the numbers above describe a tree that
          #    was missing a plugin which is no longer missing. Re-measure
          #    before quoting them.
          #
          #    (The cross build needs an x86_64-linux builder: logos_build_info.h
          #    is an x86_64-linux derivation, so it cannot run on an
          #    aarch64-darwin host without one.)
          binBundleDir = withMainProgram (dirBundler appDistributed);
          binBundleDirMock = withMainProgram (dirBundler appMockPortable);
          binBundleDirInspector = withMainProgram (dirBundler appDistributedWithInspector);

          # Catalog-driven Bundled set (ADR 0007, slice 20). The library is
          # instantiated here so `packages` can expose the test; the mobile app
          # builds its own set from the same two files against a cross package
          # set (nix/ios-apps.nix).
          bundledSetPublisher =
            nix-bundle-lgx.lib.${system}.mkMobileCatalog { lgx = logosLgx; };

          # ONE instantiation, handed to both consumers below. The test and the
          # release must be the same catalog -- a release published from a
          # second, parallel set of fixtures would pin bytes nothing tests.
          bundledSetFixture = import ./nix/bundled-set-fixture.nix {
            inherit pkgs;
            catalog = bundledSetPublisher;
            testKey = catalogTestKey;
            icon = ./mobile/catalog/icon.png;
          };

          bundledSetTests = import ./nix/bundled-set-test.nix {
            inherit pkgs;
            lgx = logosLgx;
            bundledSet = import ./nix/bundled-set.nix {
              inherit pkgs; lgx = logosLgx; publisher = bundledSetPublisher;
            };
            fixture = bundledSetFixture;
            testKey = catalogTestKey;
            # A release somebody else published: the index and the .lgx bytes
            # are committed, so the `url` + `sha256` + `rootHash` path is
            # exercised over bytes this build did not produce. Regenerate with
            # `nix build .#bundled-set-release` (see mobile/catalog/README.md).
            pinnedRelease = ./mobile/catalog/pinned-release;
          };

          # The fixture catalog, published. Not a check -- it is the PRODUCER
          # of mobile/catalog/pinned-release/, run by hand when the fixtures
          # change, and its output is committed so the test consumes bytes
          # rather than rebuilding them.
          bundledSetRelease =
            bundledSetPublisher.mkRelease { catalog = bundledSetFixture.local; };

          # A REPOSITORY a developer can serve, over the same `.lgx` files.
          # Different consumer, different index (nix/local-catalog.nix): the
          # Bundled-set release is read by a BUILD, this one by
          # logos-package-downloader on a phone at run time.
          #
          # Two rows, and the second is not filler. `web_counter_b` is the one
          # this build can install -- a `web` variant, which is all a Store shell
          # may download (ADR 0003) -- and `desktop_only` ships darwin and linux
          # variants and nothing else, which is what makes "listed as unavailable
          # with the reason, and NO install control" observable against a real
          # catalog instead of only in a unit test.
          #
          # `web_counter_b` rather than `web_counter`: the app ships the first of
          # the two (nix/mobile-web-assets.nix), and installing something the
          # image already carries would prove nothing about installing.
          localCatalog = import ./nix/local-catalog.nix {
            inherit pkgs;
            icon = ./mobile/catalog/icon.png;
            lgx = logosLgx;
            catalog = bundledSetPublisher;
            testKey = catalogTestKey;
            webVariants =
              let builderPkgs = logos-module-builder.packages.${system} or { }; in
              nixpkgs.lib.optionalAttrs (builderPkgs ? web-view-counter-b)
                { web_counter_b = builderPkgs.web-view-counter-b; };
            prebuilt.desktop_only = bundledSetFixture.drvs.desktop_only;
          };

          # Hoisted so shutdown-test can read the elapsed time for the combined PR-gate budget.
          integrationTest = import ./nix/integration-test.nix { inherit pkgs src logosQtMcp; appPkg = app; };
          integrationTestBundle = import ./nix/integration-test.nix {
            inherit pkgs src;
            appPkg = macosAppTest;
            inherit logosQtMcp;
            appBin = "${macosAppTest}/LogosBasecamp.app/Contents/MacOS/LogosBasecamp";
          };

          # ── the Web container's end-to-end check, and why it is optional ──
          #
          # It loads a `ui_qml` module's `web` variant through the real core
          # into a real webview and clicks it (nix/web-container-test.nix). Its
          # fixture is a CROSS-REPO package: logos-module-builder exposes
          # `web-view-counter` — the only `web` variant whose QML reports what
          # it did — only from the revisions carrying slice 27's fifth
          # increment, and that package exists there only when the builder's own
          # logos-view-module-runtime publishes `qml-runtime-wasm`.
          #
          # ABSENT rather than broken while this repo's lock predates both, and
          # the workspace runs it meanwhile — its `follows` put ONE builder and
          # ONE runtime in the closure:
          #
          #     ws test logos-basecamp --local logos-module-builder
          #
          # The day the pin lands, the check appears with no edit here.
          #
          # Windows is excluded for the reason the whole Web-container path is:
          # the view backend is Qt WebEngine and POSIX (logoscore's page host
          # says the same, about the same thing).
          webContainerFixture =
            (logos-module-builder.packages.${system} or {}).web-view-counter or null;
          # The runtime the APP bundles, not a second resolution of it: the
          # check has to serve the page the same image `nix/app.nix` stages
          # under share/logos-runtime, or it would be measuring a runtime this
          # build does not ship.
          webContainerRuntime = qmlRuntimeWasm;
          # The other two modules in that check's directory: the native module
          # the view calls by name, and the broker that makes the call legal.
          # Both are Bare images, so the check stays one process.
          webContainerNativeModule =
            (logos-module-builder.packages.${system} or {}).bare-greeter or null;
          webContainerCapabilityModule =
            (logos-capability-module.packages.${system} or {}).bare or null;
          hasWebContainerTest =
            !pkgs.stdenv.hostPlatform.isWindows
            && webContainerFixture != null
            && webContainerRuntime != null
            && webContainerNativeModule != null
            && webContainerCapabilityModule != null;
        in
        {
          # Individual outputs.
          main-ui-plugin = mainUIPlugin;
          package-manager-ui-plugin = packageManagerUIPlugin;
          app = app;

          # Basecamp against the fixture-backed mock — no Logos runtime at all.
          # Run: nix run .#app-mock        (see mock/README.md)
          app-mock = appMock;

          # The same, bundled portable: a self-contained directory with no
          # /nix/store references, no Logos runtime and no network. This is the
          # one to hand to someone who just wants to run the UI.
          #   nix build .#bin-bundle-dir-mock && ./result/bin/LogosBasecamp
          bin-bundle-dir-mock = binBundleDirMock;

          # Correctness gate for the mock itself. Cheap (no app build), and the
          # thing that stops mock/ from rotting: it compiles against Basecamp's
          # own sources and against MockStore from logos-protocol, so an SDK bump
          # can break it silently otherwise. Like symbol-gate, this needs an
          # explicit `nix build .#mock-tests -L` step in CI — the checks entry
          # below does not run on its own.
          mock-tests = mockTests;

          # The Bundled-set pipeline over a local fixture catalog: closure,
          # signature and Merkle admission, the embedded layout, and the
          # refusals. Seconds, and no cross toolchain -- see
          # nix/bundled-set-test.nix for why it uses fixture payloads.
          bundled-set-tests = bundledSetTests;

          # The iOS runners rendered over fixture Bundled sets and linted.
          # Seconds, no cross toolchain, and the only thing in this repo that
          # renders a runner at all -- see nix/ios-runner-lint.nix.
          ios-runner-lint = import ./nix/ios-runner-lint.nix { inherit pkgs; };

          # `nix build .#bundled-set-release` -> the fixture catalog as a
          # PINNED release: the same index with every member's sha256 and
          # Merkle root filled in. Copy the result over
          # mobile/catalog/pinned-release/ when the fixtures change.
          bundled-set-release = bundledSetRelease;

          # The same packages as a REPOSITORY, ready to be served:
          # `logos-repo.json.in` + `index.json.in` (a `@BASEURL@` placeholder,
          # because a URL carries a port nothing knows at build time) beside the
          # `.lgx` files. `nix run .#serve-local-catalog` is what fills it in.
          local-catalog-release = localCatalog.release;

          # ...and the server that fills the placeholder in and listens. Exposed
          # as a package so `apps` can point at it: an attribute of `apps` cannot
          # reach into the per-system `let` the release is built in.
          serve-local-catalog = localCatalog.serve;

          # nix run .#shell-preview   (see shell-preview/README.md)
          shell-preview = shellPreview;

          # Self-contained flat directory (bin/ + lib/ with Qt).
          # Run: nix run .#bin-bundle-dir
          bin-bundle-dir = binBundleDir;

          # Test-only twin of bin-bundle-dir WITH the QML inspector compiled in,
          # so logos-qt-mcp can connect and drive the UI headlessly. Identical to
          # the shipping bundle in every other respect.
          #
          # The inspector is a compile-time feature and is deliberately OFF in the
          # shipping bin-bundle-dir / appimage / macos outputs — we do NOT ship
          # the inspector in release builds. This output exists purely so the
          # package-manager doc-test can install and exercise modules through
          # the real bundled UI; it is not a release artifact.
          # Build: nix build .#bin-bundle-dir-inspector
          bin-bundle-dir-inspector = binBundleDirInspector;

          # QML Inspector MCP server: nix build .#mcp-server -o result-mcp
          mcp-server = logos-qt-mcp.packages.${system}.mcp-server;

          # Full logos-qt-mcp package (includes test-framework, mcp-server, qt-plugin)
          # Use: nix build .#logos-qt-mcp -o result-mcp
          # Then: LOGOS_QT_MCP=./result-mcp node tests/ui-tests.mjs --ci ./result/bin/LogosBasecamp
          logos-qt-mcp = logosQtMcp;

          # Smoke test (also exposed as a package so it can be built standalone)
          smoke-test = import ./nix/smoke-test.nix { inherit pkgs; appPkg = app; };

          # One-runtime symbol gate. Asserts the logos C++ runtime (TokenManager,
          # StoreRegistry, LogosAPI, LogosAPIClient) is DEFINED exactly once across
          # the images that share one process. The assertion is exactly-one and
          # deliberately does NOT name an owner: liblogos_core stopped being the
          # provider when the runtime became real shared libraries, and it is
          # liblogos_protocol and liblogos_qt_host that define these types today.
          # A second definition is a second TokenManager and every cross-module
          # call is refused at runtime with no build diagnostic.
          # Build: nix build .#symbol-gate  (CI: the "One-runtime symbol gate"
          # step in test-linux, test-macos AND build-windows runs this and its
          # negative control -- Windows being the one where a duplicate is fatal,
          # since PE has no symbol interposition to collapse it)
          symbol-gate = import ./nix/symbol-gate.nix { inherit pkgs; appPkg = app; };

          # Negative control for the above. Plants a REAL duplicate runtime where
          # an in-process consumer goes and asserts the gate REJECTS it. Ship both
          # or neither: an absence assertion that has never been seen to fail is
          # indistinguishable from a broken one.
          # Build: nix build .#symbol-gate-negative
          symbol-gate-negative = import ./nix/symbol-gate.nix {
            inherit pkgs; appPkg = app; negativeControl = true;
          };

          # One library name staged twice -- lib/ and beside a module -- must
          # mean the same build: macOS binds to lib/, Linux to the sibling.
          # Over the BUNDLE, since the duplication is the bundler's doing.
          # Build: nix build .#link-gate  (CI: build-appimage, build-macos-app)
          link-gate = import ./nix/link-gate.nix {
            inherit pkgs; bundlePkg = binBundleDir;
          };

          # Negative control for the above. Ship both or neither.
          # Build: nix build .#link-gate-negative
          link-gate-negative = import ./nix/link-gate.nix {
            inherit pkgs; bundlePkg = binBundleDir; negativeControl = true;
          };

          # ui_qml sandbox-escape regression test (F-008). Focused C++ unit test:
          # builds a real malicious QML plugin and asserts the production sandbox
          # refuses to load it. Build: nix build .#sandbox-test
          sandbox-test = import ./nix/sandbox-test.nix { inherit pkgs src; };

          # Pure-model unit tests (AppsModel install-status logic, etc.). Same
          # shape as sandbox-test — standalone QtTest project, no app launch,
          # no IPC. Build: nix build .#unit-tests
          unit-tests = import ./nix/unit-tests.nix {
            inherit pkgs src logosPackageHeaders;
            logosViewModuleRuntimeSrc = logos-view-module-runtime;
            # SOURCE TREES, not the built packages: the mobile Web container
            # tests take two SEAM headers from them and link neither, so a
            # source path keeps a check that runs in seconds from depending on
            # a core and a transport build.
            logosLiblogosSrc = logos-liblogos.outPath;
            logosProtocolSrc = logos-protocol.outPath;
          };

          # QML component tests (Qt Quick Test)
          qml-tests = import ./nix/qml-tests.nix {
            inherit pkgs src logosPackageHeaders logosDesignSystem;
          };

          # Coverage report for the unit-test suite: same targets as
          # .#unit-tests, compiled with --coverage and reported via gcovr.
          # Report-only for now (failUnderLine = 0) — raise the threshold as
          # the test plan phases land to make it a gate.
          # Build: nix build .#coverage -L && open result/coverage.html
          coverage = import ./nix/coverage.nix {
            inherit pkgs src logosPackageHeaders;
            logosViewModuleRuntimeSrc = logos-view-module-runtime;
            failUnderLine = 0;
          };

          # Integration test (UI tests via Qt Inspector)
          integration-test = integrationTest;

          # Host-services grant guard. Asserts that a NON-"core" identity
          # (ui-host running package_manager_ui) actually completes a
          # capability-gated call chain — i.e. that capability_module really
          # received its token_registry/token_delivery grant from the loader
          # basecamp pins, rather than failing closed. See
          # nix/host-services-test.nix and tests/host-services-assert.mjs.
          # Build: nix build .#host-services-test
          host-services-test = import ./nix/host-services-test.nix {
            inherit pkgs src logosQtMcp; appPkg = app;
          };

          # Two apps at once, one inspector each. The guard that the
          # app-driving checks above can run in parallel -- which is how nix
          # runs them. Build: nix build .#inspector-isolation-test
          inspector-isolation-test = import ./nix/inspector-isolation-test.nix {
            inherit pkgs src logosQtMcp; appPkg = app;
          };

          # Shutdown tests (SIGTERM, SIGINT, Ctrl+Q / ⌘Q). Spawns a fresh
          # app per case and asserts orderly exit (code 0).
          shutdown-test = import ./nix/shutdown-test.nix {
            inherit pkgs src logosQtMcp;
            appPkg = app;
            uiTestRun = if pkgs.stdenv.isDarwin then integrationTestBundle else integrationTest;
          };

          # Default package
          default = app;
        } // pkgs.lib.optionalAttrs (!pkgs.stdenv.hostPlatform.isWindows) {
          # The phone containers' bridge, driven by a real browser. Qt WebEngine
          # is a Qt package this build already has, so the browser is an input
          # rather than something the sandbox must find -- see the file. Absent
          # on Windows for the reason the whole Web-container path is.
          mobile-bridge-test = import ./nix/mobile-bridge-test.nix {
            inherit pkgs src;
          };
        } // pkgs.lib.optionalAttrs hasWebContainerTest {
          web-container-test = import ./nix/web-container-test.nix {
            inherit pkgs src;
            liblogos = logosLiblogos;
            logosCppSdk = logosSdk;
            webVariant = webContainerFixture;
            qmlRuntime = webContainerRuntime;
            nativeModule = webContainerNativeModule;
            capabilityModule = webContainerCapabilityModule;
          };
        } // pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
          bin-appimage = nix-bundle-appimage.lib.${system}.mkAppImage {
            drv = appDistributed;
            name = "logos-basecamp";
            bundle = dirBundler appDistributed;
            desktopFile = ./assets/logos-basecamp.desktop;
            icon = ./app/icons/logos.png;
          };
        } // pkgs.lib.optionalAttrs pkgs.stdenv.isDarwin {
          bin-macos-app = macosApp;
          smoke-test-bundle = import ./nix/smoke-test.nix {
            inherit pkgs;
            appPkg = macosApp;
            appBin = "${macosApp}/LogosBasecamp.app/Contents/MacOS/LogosBasecamp";
          };
          integration-test-bundle = import ./nix/integration-test.nix {
            inherit pkgs src;
            appPkg = macosAppTest;
            inherit logosQtMcp;
            appBin = "${macosAppTest}/LogosBasecamp.app/Contents/MacOS/LogosBasecamp";
          };
          host-services-test-bundle = import ./nix/host-services-test.nix {
            inherit pkgs src;
            appPkg = macosAppTest;
            inherit logosQtMcp;
            appBin = "${macosAppTest}/LogosBasecamp.app/Contents/MacOS/LogosBasecamp";
          };
        }
      ) // mobileSmoke;

      # nix run .                   → dev build  (depends on /nix/store at runtime)
      # nix run .#bin-bundle-dir    → self-contained bundle (Qt frameworks in lib/)
      apps =
        let
          desktopApps = forAllSystems ({ system, ... }: {
            default = {
              type = "app";
              program = "${self.packages.${system}.app}/bin/LogosBasecamp";
            };
            bin-bundle-dir = {
              type = "app";
              program = "${self.packages.${system}.bin-bundle-dir}/bin/LogosBasecamp";
            };
            # A local catalog release, served on loopback:
            #   nix run .#serve-local-catalog [-- <port>]
            # An app rather than a package because the URLs in the index carry a
            # port, and a port exists only once something is listening on it.
            serve-local-catalog = {
              type = "app";
              program = "${self.packages.${system}.serve-local-catalog}/bin/serve-local-catalog";
            };
          });
        in
        desktopApps
        # The mobile runners are build-platform scripts, so they belong to the
        # system that RUNS them, not to the pseudo-system they target:
        #   nix run .#run-liblogos-smoke-ios-sim
        #   LOGOS_IOS_TEAM_ID=... LOGOS_IOS_DEVICE=... nix run .#run-liblogos-smoke-ios-device
        #   nix run .#run-liblogos-smoke-android
        #   nix run .#run-basecamp-shell-android
        #
        # The left operand is named rather than read back off `self.apps`: an
        # attribute of `apps` cannot refer to `apps` itself, `or { }` does not
        # break the cycle, and the result is an infinite recursion the moment
        # anything asks for apps.aarch64-darwin.
        // {
          aarch64-darwin = (desktopApps.aarch64-darwin or { }) // {
            # THIS MAC'S SET, not the canonical Linux one, and only because of
            # the `web` half. An iOS cross build is keyed off the host that runs
            # Xcode whichever of these is asked, but nix/mobile-web-assets.nix is
            # keyed off `androidBuildSystem` -- so with mobileSmoke here a Mac
            # would be asked to build an x86_64-linux wasm derivation to fill its
            # own app, and refuse ("platform mismatch"). Both iOS apps carry the
            # Qt-wasm QML runtime now (the Shell RUNS what a user installed into
            # the Web container), so both runners are bound here.
            run-liblogos-smoke-ios-sim = {
              type = "app";
              program = "${mobileSmokeFor.aarch64-darwin.aarch64-ios-simulator.run-liblogos-smoke-ios-sim}/bin/run-liblogos-smoke-ios-sim";
            };
            run-liblogos-smoke-ios-device = {
              type = "app";
              program = "${mobileSmokeFor.aarch64-darwin.aarch64-ios.run-liblogos-smoke-ios-device}/bin/run-liblogos-smoke-ios-device";
            };
            # Basecamp's real UI shell on a phone, over the same Bundled set:
            #   nix run .#run-basecamp-shell-ios-sim
            run-basecamp-shell-ios-sim = {
              type = "app";
              program = "${mobileSmokeFor.aarch64-darwin.aarch64-ios-simulator.run-basecamp-shell-ios-sim}/bin/run-basecamp-shell-ios-sim";
            };
            run-basecamp-shell-ios-device = {
              type = "app";
              program = "${mobileSmokeFor.aarch64-darwin.aarch64-ios.run-basecamp-shell-ios-device}/bin/run-basecamp-shell-ios-device";
            };
            run-liblogos-smoke-android = {
              type = "app";
              program = "${mobileSmokeFor.aarch64-darwin.aarch64-android.run-liblogos-smoke-android}/bin/run-liblogos-smoke-android";
            };
            # ...and the Shell on the other phone. main_ui is a static archive
            # on Android too (nix/shell-ui-android.nix), so the two runs are
            # the same app over the same host.
            run-basecamp-shell-android = {
              type = "app";
              program = "${mobileSmokeFor.aarch64-darwin.aarch64-android.run-basecamp-shell-android}/bin/run-basecamp-shell-android";
            };
          };
          x86_64-linux = (desktopApps.x86_64-linux or { }) // {
            run-liblogos-smoke-android = {
              type = "app";
              program = "${mobileSmoke.aarch64-android.run-liblogos-smoke-android}/bin/run-liblogos-smoke-android";
            };
            run-basecamp-shell-android = {
              type = "app";
              program = "${mobileSmoke.aarch64-android.run-basecamp-shell-android}/bin/run-basecamp-shell-android";
            };
          };
        };

      # The mobile artifacts keyed by the platform that BUILDS them; see
      # mkMobileSmoke for why Android needs this and packages does not suffice.
      legacyPackages = nixpkgs.lib.mapAttrs (_: mobile: { inherit mobile; }) mobileSmokeFor;

      checks = forAllSystems ({ pkgs, system, ... }: {
        smoke-test = self.packages.${system}.smoke-test;
        sandbox-test = self.packages.${system}.sandbox-test;
        unit-tests = self.packages.${system}.unit-tests;
        qml-tests = self.packages.${system}.qml-tests;
        integration-test = self.packages.${system}.integration-test;
        shutdown-test = self.packages.${system}.shutdown-test;
        host-services-test = self.packages.${system}.host-services-test;
        inspector-isolation-test = self.packages.${system}.inspector-isolation-test;
        symbol-gate = self.packages.${system}.symbol-gate;
        symbol-gate-negative = self.packages.${system}.symbol-gate-negative;
        mock-tests = self.packages.${system}.mock-tests;
        bundled-set = self.packages.${system}.bundled-set-tests;
        ios-runner-lint = self.packages.${system}.ios-runner-lint;
      } // pkgs.lib.optionalAttrs (self.packages.${system} ? mobile-bridge-test) {
        mobile-bridge-test = self.packages.${system}.mobile-bridge-test;
      } // pkgs.lib.optionalAttrs (self.packages.${system} ? web-container-test) {
        # The Web container, end to end. Absent only while this repo's lock
        # predates the fixture it loads — see the binding in `packages`.
        web-container-test = self.packages.${system}.web-container-test;
      } // pkgs.lib.optionalAttrs (!pkgs.stdenv.hostPlatform.isWindows) {
        link-gate = self.packages.${system}.link-gate;
        link-gate-negative = self.packages.${system}.link-gate-negative;
      } // pkgs.lib.optionalAttrs pkgs.stdenv.hostPlatform.isDarwin {
        # The mobile Shell host, LINKED. Everything above it -- the Xcode step,
        # the .app, the install -- needs a device and cannot be a check (ADR
        # 0002), but the static archive that carries main_ui, the design system
        # and the Native container is a derivation, and until now nothing built
        # it: a break there showed up only in a hand-run `ws run <repo>
        # --target ios-sim-arm64 --app shell`.
        #
        # Darwin only, because it is an iOS cross build: the value is not
        # evaluated at all on a Linux host.
        ios-shell-host = mobileSmoke.aarch64-ios-simulator.basecamp-shell-host-ios;
      } // pkgs.lib.optionalAttrs (builtins.elem system logos-nix.lib.androidBuildSystems) {
        # The Android Shell's own two stages, CROSS BUILT. The APK above them
        # needs gradle and a 300 MB download-free sandbox and is built by `ws
        # build logos-basecamp#basecamp-shell-android --target android-arm64`;
        # these two are what break when main_ui gains a Qt module the Android
        # set does not carry, or when the design system's QML stops compiling
        # for another platform -- and until now nothing built them at all.
        #
        # Only on a build platform an Android cross set exists for; the value
        # is not evaluated elsewhere.
        android-shell-ui = pkgs.linkFarmFromDrvs "basecamp-android-shell-ui" [
          mobileSmokeFor.${system}.aarch64-android.main-ui-plugin
          mobileSmokeFor.${system}.aarch64-android.design-system
        ];
      });

      devShells = forAllSystems ({ pkgs, logosSdk, logosProtocolPkg, logosQtHost, logosModule, logosLiblogos, logosPackageManagerLibrary, logosPackageManagerModule, logosCapabilityModule, logosPackageLib, logosDesignSystem, logosCppSdkSrc, logosLiblogosSrc, logosPackageManagerModuleSrc, logosCapabilityModuleSrc, ... }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
            pkgs.zstd
            pkgs.krb5
            pkgs.abseil-cpp
          ];
          
          shellHook = ''
            # Nix package paths (pre-built for host system)
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_PROTOCOL_ROOT="${logosProtocolPkg}"
            export LOGOS_QT_HOST_ROOT="${logosQtHost}"
            export LOGOS_MODULE_ROOT="${logosModule}"
            export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
            export LOGOS_PACKAGE_MANAGER_ROOT="${logosPackageManagerLibrary}"
            export LOGOS_CAPABILITY_MODULE_ROOT="${logosCapabilityModule}"
            export LGX_ROOT="${logosPackageLib}"
            export LOGOS_DESIGN_SYSTEM_ROOT="${logosDesignSystem}"
            
            # Source paths for iOS builds (from flake inputs)
            export LOGOS_CPP_SDK_SRC="${logosCppSdkSrc}"
            export LOGOS_LIBLOGOS_SRC="${logosLiblogosSrc}"
            export LOGOS_PACKAGE_MANAGER_MODULE_SRC="${logosPackageManagerModuleSrc}"
            export LOGOS_CAPABILITY_MODULE_SRC="${logosCapabilityModuleSrc}"
            
            echo "Logos Basecamp development environment"
            echo ""
            echo "Nix packages (host builds):"
            echo "  LOGOS_CPP_SDK_ROOT: $LOGOS_CPP_SDK_ROOT"
            echo "  LOGOS_MODULE_ROOT: $LOGOS_MODULE_ROOT"
            echo "  LOGOS_LIBLOGOS_ROOT: $LOGOS_LIBLOGOS_ROOT"
            echo "  LOGOS_PACKAGE_MANAGER_ROOT: $LOGOS_PACKAGE_MANAGER_ROOT"
            echo "  LOGOS_CAPABILITY_MODULE_ROOT: $LOGOS_CAPABILITY_MODULE_ROOT"
            echo "  LGX_ROOT: $LGX_ROOT"
            echo "  LOGOS_DESIGN_SYSTEM_ROOT: $LOGOS_DESIGN_SYSTEM_ROOT"
            echo ""
            echo "Source paths (for iOS builds):"
            echo "  LOGOS_CPP_SDK_SRC: $LOGOS_CPP_SDK_SRC"
            echo "  LOGOS_LIBLOGOS_SRC: $LOGOS_LIBLOGOS_SRC"
            echo "  LOGOS_PACKAGE_MANAGER_MODULE_SRC: $LOGOS_PACKAGE_MANAGER_MODULE_SRC"
            echo "  LOGOS_CAPABILITY_MODULE_SRC: $LOGOS_CAPABILITY_MODULE_SRC"
          '';
        };
      });
    };
}
