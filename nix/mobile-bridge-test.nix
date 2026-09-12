# THE PHONE CONTAINERS' BRIDGE, WITH A REAL BROWSER ON THE OTHER END.
#
# tests/mobile_web_bridge_test.cpp (in `unit-tests`) drives MobileWebBridge from
# C++ and covers everything the bridge DECIDES. What it cannot cover is the half
# the bridge SHIPS: the JavaScript shim it injects into every page, with a long
# poll loop, a chunked sender and a console wrapper in it. No desktop test
# otherwise runs a line of it, and it is the part a phone would find broken.
#
# So this runs it in Chromium. The adapter is the twenty lines each platform
# writes (a QWebEngineUrlSchemeHandler here, a WKURLSchemeHandler in
# IosWebPage.mm), and everything between the page and the host is the shipping
# code.
#
# IT LINKS NO CORE AND NO TRANSPORT, unlike web-container-test next door: what
# is under test is the channel, and a 26 MB QML runtime would only make its
# failures slower to reach.
{ pkgs, src }:

pkgs.stdenv.mkDerivation {
  pname = "logos-basecamp-mobile-bridge-test";
  version = "0.0.0";

  inherit src;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.qt6.wrapQtAppsHook
  ];

  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtdeclarative     # QtWebEngine's own dependency
    pkgs.qt6.qtwebengine
  ];

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild
    cmake -S tests/mobile-bridge -B build-mobile-bridge -GNinja \
      -DCMAKE_BUILD_TYPE=Release
    cmake --build build-mobile-bridge
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp build-mobile-bridge/mobile_bridge_test $out/bin/
    runHook postInstall
  '';

  # installCheck rather than check: wrapQtAppsHook has to have run first. A
  # WebEngine binary needs QTWEBENGINEPROCESS_PATH and the resource and locale
  # directories in its environment, and without them the renderer never starts
  # -- which reads exactly like a page that failed to load.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    export HOME=$TMPDIR
    export QT_QPA_PLATFORM=offscreen
    export QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu --disable-dev-shm-usage"
    $out/bin/mobile_bridge_test
    runHook postInstallCheck
  '';
}
