# THE WEB CONTAINER, END TO END: a `ui_qml` module's `web` variant loaded
# through the real core, rendered in a real webview, and clicked.
#
# What it proves, and why nothing else can:
#
#   * logos-module-builder's `wasm/browser-e2e` boots the same variant in
#     headless Chrome against a STUB container. It proves the variant.
#   * logos-view-module-runtime's `WebRuntimeTests` drive the runtime's bridge
#     on a desktop with no browser at all. They prove the runtime.
#   * This proves the CONTAINER — basecamp's `logos:` scheme, basecamp's
#     QWebChannel channel, basecamp's widget, and the real liblogos core behind
#     them — which is the half the other two stand in for.
#
# It is a NIX CHECK and the browser one is not, which is the whole difference
# between the two: Qt WebEngine is a Qt package this build already has, so the
# browser is an input rather than something the sandbox must find.
#
# THE FIXTURES ARE OTHER REPOS', exported as packages rather than copied here.
# The variant is the only `web` one whose QML reports what it did — where its
# button is, when its replica arrived, what the property change did — and a
# second copy in this repo would be a second thing to keep in step with the
# loader, the manifest keys and the runtime's context properties. `greeter` is
# the module that variant's QML calls by name, and is the builder's for the same
# reason: the browser harness stubs the same name and method, and the two assert
# the same string only while one fixture is behind both.
#
# THREE MODULES IN THE DIRECTORY, AND EACH IS LOAD-BEARING. The variant is what
# is under test; `greeter` is what proves `logos.callModuleAsync` leaves the page
# and lands somewhere real; and capability_module is what makes that call LEGAL.
# The page's calls go out as the module's own identity on an isolated token store
# (WebContainer::launch), whose first call to any target runs
# `capability_module.requestModule` — so with no broker loaded the call is
# refused at the target's empty-token check and "reached a native module" could
# not be true of any wiring. All three are Bare or web artifacts, so this check
# is ONE process with no subprocess host to find.
{ pkgs, src, liblogos, logosCppSdk, webVariant, qmlRuntime, nativeModule
, capabilityModule }:

let
  # A Bare image carries the platform's own shared-object extension, and the
  # `_bare` STEM is what stamps the format at discovery — so the extension is
  # free to differ and the name in the manifest has to follow the platform.
  libExt = if pkgs.stdenv.hostPlatform.isDarwin then "dylib" else "so";
in

pkgs.stdenv.mkDerivation {
  pname = "logos-basecamp-web-container-test";
  version = "0.0.0";

  inherit src;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.qt6.wrapQtAppsHook
  ];

  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtdeclarative     # QtWebEngineQuick's own dependency
    pkgs.qt6.qtwebengine
    pkgs.qt6.qtwebchannel
    # logos-cpp-sdk's exported config does find_dependency on all three, so they
    # have to be findable here even though the driver links none of them
    # directly — it takes the seam header and the core's C ABI and nothing else.
    pkgs.nlohmann_json
    pkgs.openssl
    pkgs.boost
  ];

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild
    cmake -S tests/web-container -B build-web-container -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DLOGOS_LIBLOGOS_ROOT="${liblogos}" \
      -DLOGOS_CPP_SDK_ROOT="${logosCppSdk}"
    cmake --build build-web-container
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp build-web-container/web_container_test $out/bin/
    runHook postInstall
  '';

  # installCheck rather than check: wrapQtAppsHook has to have run first. A
  # WebEngine binary needs QTWEBENGINEPROCESS_PATH and the resource and locale
  # directories in its environment, and without them the renderer never starts
  # — which reads exactly like a page that failed to load.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck

    export HOME=$TMPDIR
    export QT_QPA_PLATFORM=offscreen
    # Chromium with no GPU and no sandbox. SwiftShader is not optional here:
    # Qt for WebAssembly draws through WebGL2, so without a rasteriser the
    # runtime boots and renders nothing — and a view that renders nothing is
    # exactly what this check exists to catch.
    export QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --enable-unsafe-swiftshader --use-gl=angle --use-angle=swiftshader --disable-dev-shm-usage"

    # The three packages, laid out as lgpm installs one: a directory per module,
    # named for the module, with its manifest at the top.
    #
    # The manifests for the two Bare modules are written HERE because a `bare`
    # output is the image alone — the artifact a host dlopens, with no package
    # around it (that is what `nix build .#install` adds). Discovery reads
    # `main`, and the `_bare` stem is what stamps the format, so these two keys
    # are the whole of what each needs.
    mkdir -p $TMPDIR/modules/web_counter
    cp -r ${webVariant}/web_counter_web/. $TMPDIR/modules/web_counter/

    mkdir -p $TMPDIR/modules/greeter
    cp ${nativeModule}/lib/greeter_bare.${libExt} $TMPDIR/modules/greeter/
    printf '%s\n' '{"name":"greeter","version":"1.0.0","type":"core","description":"the native module a web variant calls by name","main":"greeter_bare.${libExt}","dependencies":[]}' \
      > $TMPDIR/modules/greeter/manifest.json

    mkdir -p $TMPDIR/modules/capability_module
    cp ${capabilityModule}/lib/capability_module_bare.${libExt} $TMPDIR/modules/capability_module/
    printf '%s\n' '{"name":"capability_module","version":"1.0.0","type":"core","description":"the capability broker","main":"capability_module_bare.${libExt}","dependencies":[]}' \
      > $TMPDIR/modules/capability_module/manifest.json

    chmod -R u+w $TMPDIR/modules

    $out/bin/web_container_test \
      --modules-dir $TMPDIR/modules \
      --runtime-dir ${qmlRuntime}/www \
      --module web_counter \
      --native-module greeter

    runHook postInstallCheck
  '';
}
