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
# THE FIXTURE IS logos-module-builder's, exported as a package rather than
# copied here. It is the only `web` variant whose QML reports what it did —
# where its button is, when its replica arrived, what the property change did —
# and a second copy in this repo would be a second thing to keep in step with
# the loader, the manifest keys and the runtime's context properties.
{ pkgs, src, liblogos, logosCppSdk, webVariant, qmlRuntime }:

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

    # The variant, laid out as lgpm installs a package: one directory per
    # module, named for the module, with its manifest at the top.
    mkdir -p $TMPDIR/modules/web_counter
    cp -r ${webVariant}/web_counter_web/. $TMPDIR/modules/web_counter/
    chmod -R u+w $TMPDIR/modules

    $out/bin/web_container_test \
      --modules-dir $TMPDIR/modules \
      --runtime-dir ${qmlRuntime}/www \
      --module web_counter

    runHook postInstallCheck
  '';
}
