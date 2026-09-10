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
  # The Bundled Bare module for aarch64-android: lib/lib<name>_bare.so
  # (logos-module-builder's `bare` output on the Android package set). It
  # travels in the APK like every other Logos .so and lands in the app's
  # native library directory, which since API 29 is the only place Android
  # will dlopen from at all.
  bareModule,
}:

let
  inherit (pkgs) lib;
  packageName = "co.logos.liblogos.smoke";
  activity = "org.qtproject.qt.android.bindings.QtActivity";

  # The Bundled Bare module's file name. `lib` prefix because an APK carries
  # only lib*.so; `_bare` stem suffix because that is how liblogos tells a Bare
  # module from a Qt plugin.
  bareModuleSo = "libbare_counter_bare.so";

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
  # spdlog too: the iOS chain carries it in `all`, the Android one does not,
  # and the host compiles against it to hang a logcat sink on the core's
  # channels (BundledModuleRunner). Same prefix the chain linked, so there is
  # no second spdlog.
  # lib.getDev: nixpkgs' spdlog splits its headers into a `dev` output, and the
  # default one carries lib/ alone -- passing it here adds an include root that
  # exists and holds nothing.
  # ...and fmt beside it: this spdlog is built against an external fmt
  # (SPDLOG_FMT_EXTERNAL), so spdlog/fmt/fmt.h includes <fmt/format.h>.
  includeRoots = chain.all ++ map lib.getDev [ pkgs.spdlog pkgs.fmt ];
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

    # ── the Bundled module, PACKAGED BUT NOT AUTO-LOADED ────────────────────
    # It is dropped into androiddeployqt's output directory AFTER the tool has
    # written res/values/libs.xml, so gradle packages it (jniLibs.srcDirs is
    # `libs`) and QtLoader never hears about it.
    #
    # This is not tidiness. QT_ANDROID_EXTRA_LIBS is a LOAD list: QtLoader
    # System.load()s every entry on the qtMainLoopThread before any Logos code
    # runs. A Bare module leaves every `lp_*` undefined by design, and that
    # early load resolves them against nothing -- measured on an SM-G990B, the
    # app died before its first frame with
    #   java.lang.UnsatisfiedLinkError: dlopen failed: cannot locate symbol
    #   "lp_token_save" referenced by ".../libbare_counter_bare.so"
    # The module must be opened by the Native container, once the core has
    # registered it.
    nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ pkgs.pkgsBuildBuild.patchelf ];

    preBuild = (old.preBuild or "") + ''
      install -Dm755 ${bareModule}/lib/${bareModuleSo} \
        android-build/libs/${pkgs.androidPkgs.abi}/${bareModuleSo}

      # The module names the host's protocol image in its own DT_NEEDED
      # (logos-module-builder does that at link time -- on Android bionic
      # offers a dlopen'd library no other way to reach an app library's
      # symbols). Assert both halves here, because the APK is where they have
      # to meet: a module whose dependency the APK does not carry is
      # unloadable, and the device says so only at load.
      needed=$(patchelf --print-needed \
        android-build/libs/${pkgs.androidPkgs.abi}/${bareModuleSo})
      grep -qx liblogos_protocol.so <<< "$needed" || {
        echo "error: ${bareModuleSo} does not name liblogos_protocol.so in DT_NEEDED;" >&2
        echo "its lp_* would resolve against nothing on this platform. NEEDED was:" >&2
        printf '  %s\n' $needed >&2
        exit 1
      }
      [ -f "android-build/libs/${pkgs.androidPkgs.abi}/liblogos_protocol.so" ] || {
        echo "error: liblogos_protocol.so is not in the APK, and ${bareModuleSo} needs it" >&2
        exit 1
      }

      # libs.xml is what QtLoader reads. If the module ever appears in it, the
      # app is back to the crash above -- and it would look like a Qt problem.
      if grep -q "${bareModuleSo}" android-build/res/values/libs.xml; then
        echo "error: ${bareModuleSo} is in libs.xml; QtLoader would load it eagerly" >&2
        exit 1
      fi
      echo "==> bundled Bare module packaged, not auto-loaded: ${bareModuleSo}"
    '';
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
