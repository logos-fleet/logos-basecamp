# WHAT A PHONE APP CARRIES SO THAT A `web` MODULE CAN RUN AT ALL: the Qt-wasm
# QML runtime every `ui_qml` `web` variant loads, and the Downloaded modules
# this build ships with.
#
# ARCHITECTURE-FREE BY CONSTRUCTION. Everything here is wasm and JavaScript, so
# it is taken from the BUILD platform's package set and is byte-identical on
# both phones — which is the point: the desktop container, the simulator and the
# Samsung serve the same bytes off the same URL shape (app/web/LogosWebPaths.h),
# and a `web` variant that renders on one has no way to differ on another.
#
#   $out/logos-runtime/       the runtime's www/, served under `/logos-runtime/`
#   $out/web-modules/<name>/  one directory per module, laid out as lgpm
#                             installs one: the package files with manifest.json
#                             at the top, which is what the core's discovery
#                             reads
#
# TWO MODULES, BECAUSE ONE CANNOT SHOW A BUDGET. The live-runtime budget is only
# observable with two Downloaded modules -- one page has nothing to be evicted
# for -- and logos-module-builder exports the same instrumented fixture built
# twice for that reason (`web-view-counter` and `web-view-counter-b`). It has to
# be two BUILDS: a variant's identity is compiled into its Qt-wasm backend image,
# so a page told to publish a name its backend does not answer to times out at
# the container's contract query.
{ pkgs
, # logos-view-module-runtime's qml-runtime-wasm, or null while a pin predates
  # it. Null is a real state — the app builds and opens pages, and a variant
  # whose loader asks for a runtime then fails IN THE PAGE naming what is
  # missing (MobileWebContainerBackend::install).
  qmlRuntimeWasm ? null
, # name -> the `web` output of a ui_qml module. Each is a directory holding one
  # `<name>_web/` package.
  webVariants ? { }
}:

let
  inherit (pkgs) lib;

  # A variant's package directory inside its output. logos-module-builder names
  # it `<module>_web`; read rather than assumed, so a layout change here is a
  # build failure and not a phone that finds no module.
  copyVariant = name: drv: ''
    src=$(echo ${drv}/*_web)
    [ -d "$src" ] || { echo "error: ${drv} holds no <module>_web directory" >&2; exit 1; }
    mkdir -p $out/web-modules/${name}
    cp -r "$src"/. $out/web-modules/${name}/
    chmod -R u+w $out/web-modules/${name}

    # ASSERTED, not assumed: the directory a module is installed in is the name
    # the core discovers it under, and a package whose manifest says something
    # else would load as a module the container never asked for.
    grep -q '"name":"${name}"' $out/web-modules/${name}/manifest.json || {
      echo "error: ${name}'s manifest does not name it" >&2
      exit 1
    }
    echo "==> web module ${name}: $(du -sk $out/web-modules/${name} | cut -f1) KB"
  '';
in

pkgs.runCommand "logos-mobile-web-assets" { } (''
  mkdir -p $out/web-modules
'' + lib.optionalString (qmlRuntimeWasm != null) ''
  mkdir -p $out/logos-runtime
  cp -r ${qmlRuntimeWasm}/www/. $out/logos-runtime/
  chmod -R u+w $out/logos-runtime
  echo "==> bundled QML runtime: $(du -sk $out/logos-runtime | cut -f1) KB"
'' + lib.concatStrings (lib.mapAttrsToList copyVariant webVariants) + ''
  echo "==> mobile web assets: $(du -sk $out | cut -f1) KB total"
'')
