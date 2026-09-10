# Generate BundledManifest.h from the module manifest, and put its directory on
# `target`'s include path.
#
# One function, called from both the iOS stage and the Android app, so the two
# builds cannot end up compiling different manifests into the same host.
include_guard(GLOBAL)

function(logos_bundled_manifest target manifest_file)
    if(NOT EXISTS "${manifest_file}")
        message(FATAL_ERROR "logos_bundled_manifest: no such manifest: ${manifest_file}")
    endif()
    file(READ "${manifest_file}" LOGOS_BUNDLED_MANIFEST_JSON)
    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/logos-bundled-manifest")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BundledManifest.h.in"
        "${_out_dir}/BundledManifest.h"
        @ONLY)
    # A manifest edit has to reconfigure, or the header goes stale silently.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest_file}")
    target_include_directories(${target} PRIVATE "${_out_dir}")
endfunction()
