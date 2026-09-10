# The liblogos smoke host as an Android APK (pkgs.mkQtAndroidApk), plus
# run-liblogos-smoke-android: adb install + launch on the attached device.
#
# One derivation, unlike iOS: androiddeployqt and gradle both run inside the
# sandbox.
{
  pkgs,
  # logos-liblogos's mobile chain for the aarch64-android set.
  chain,
  src,
}:

let
  inherit (pkgs) lib;
  packageName = "co.logos.liblogos.smoke";
  activity = "org.qtproject.qt.android.bindings.QtActivity";

  # lib.getLib: nixpkgs' openssl, fmt and icu default to their bin or dev
  # output, and an APK built from those carries no .so at all -- mkQtAndroidApk
  # then fails with "these DT_NEEDED sonames are neither in the APK nor
  # provided by Android".
  # `chain.all` carries the third-party tail liblogos_core links; the extra
  # entries are what those link in turn (fmt, icu, zlib) and libsodium, which
  # only lgx uses.
  libRoots = lib.unique (
    map lib.getLib (
      chain.all
      ++ [
        pkgs.fmt
        pkgs.libsodium
        pkgs.icu
        pkgs.zlib
      ]
    )
  );
  includeRoots = chain.all;
  joined = lib.concatMapStringsSep ";" toString;

  # An APK carries only files named lib<name>.so, and nixpkgs' cross libraries
  # are versioned (libspdlog.so.1.17, libssl.so.3, libicuuc.so.76) with the
  # Logos libraries referencing those sonames. Flatten: copy under the
  # unversioned name and rewrite SONAME and every DT_NEEDED to match. Qt's own
  # libraries are androiddeployqt's business and are excluded.
  apkLibs = pkgs.pkgsBuildBuild.runCommand "liblogos-smoke-apk-libs" {
    nativeBuildInputs = [ pkgs.pkgsBuildBuild.patchelf ];
  } ''
    # The unversioned name a library travels under in the APK. Android ships
    # private libicu*, libssl and libcrypto; a DT_NEEDED by the same name
    # resolves to THOSE, not to ours, and the app dies at dlopen with a
    # missing C++-mangled ICU symbol. Rename ours so no collision is possible.
    apk_name() {
      local stem="''${1%%.so*}"
      case "$stem" in libicu*|libssl|libcrypto) stem="''${stem}_lg" ;; esac
      echo "$stem.so"
    }
    mkdir -p $out/lib
    for root in ${lib.concatStringsSep " " (map toString libRoots)}; do
      for f in "$root"/lib/lib*.so*; do
        [ -e "$f" ] || continue
        [ -L "$f" ] && continue
        name=$(basename "$f")
        case "$name" in libQt6*|*.a|*.la) continue ;; esac
        base=$(apk_name "$name")
        # Several prefixes stage the same library (liblogos and the package
        # manager copy their closure beside themselves); one copy is enough.
        [ -e "$out/lib/$base" ] && continue
        cp "$f" "$out/lib/$base"
        chmod u+w "$out/lib/$base"
        patchelf --set-soname "$base" "$out/lib/$base"
      done
    done
    for l in $out/lib/*.so; do
      for n in $(patchelf --print-needed "$l"); do
        base=$(apk_name "$n")
        [ "$n" = "$base" ] || patchelf --replace-needed "$n" "$base" "$l"
      done
    done
    ls -la $out/lib
  '';

  apk = (pkgs.mkQtAndroidApk {
    pname = "liblogos-smoke-android";
    version = "0.1.0";
    inherit src packageName;
    target = "LiblogosSmoke";
    qtModules = with pkgs.qt6; [
      qtbase
      qtremoteobjects
    ];
    buildInputs = libRoots ++ [ apkLibs ];
    cmakeFlags = [
      "-DCMAKE_BUILD_TYPE=Release"
      "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=${pkgs.qt6.qtremoteobjects}"
      "-DLOGOS_LIB_ROOTS=${apkLibs}"
      "-DLOGOS_INCLUDE_ROOTS=${joined includeRoots}"
    ];
    meta.description = "liblogos_core smoke host, packaged as an Android APK";
  }).overrideAttrs (old: {
    setSourceRoot = "sourceRoot=$(echo */mobile/liblogos-smoke/android)";
    gradleFlags = (old.gradleFlags or [ ]) ++ [ "--stacktrace" ];
  });
  apkFile = "${apk}/${apk.apkName}";
  adb = "${pkgs.androidPkgs.androidsdk}/bin/adb";

  runner = pkgs.buildPackages.writeShellScriptBin "run-liblogos-smoke-android" ''
    set -euo pipefail
    adb=${adb}; apk=${apkFile}; pkg=${packageName}
    export ANDROID_SERIAL="''${ANDROID_SERIAL:-$("$adb" devices | awk 'NR > 1 && $2 == "device" { print $1; exit }')}"
    echo "run-liblogos-smoke-android: installing $apk on $ANDROID_SERIAL ($(du -k "$apk" | cut -f1) KB)"
    "$adb" install -r "$apk" >/dev/null || { "$adb" uninstall "$pkg" >/dev/null; "$adb" install -r "$apk"; }
    "$adb" logcat -c || true
    "$adb" shell am start -W -n "$pkg/${activity}" >/dev/null
    pid=$("$adb" shell pidof "$pkg" | tr -d '\r')
    echo "run-liblogos-smoke-android: $pkg pid $pid; console follows (Ctrl-C to stop)"
    "$adb" logcat --pid="$pid" -v raw '*:V' \
      | grep --line-buffered -E '^\[smoke\]|^\[qt\] .*(warning|error|fatal|Fatal)|libc|DEBUG|FATAL'
  '';
in
{
  liblogos-smoke-android = apk;
  run-liblogos-smoke-android = runner;
}
