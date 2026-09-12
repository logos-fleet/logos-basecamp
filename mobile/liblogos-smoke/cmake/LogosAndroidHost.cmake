# What the two Android hosts do IDENTICALLY, once.
#
#   logos_android_link_logos(<target> <roots> <out_apk_libs>)
#   logos_android_package_source_dir(<target>)
#
# The probe and the Shell differ in their UI and in nothing below it, so they
# link the same libraries out of the same prefixes and hand androiddeployqt the
# same assembled directory. Kept here rather than in each CMakeLists because a
# rule that drifts between the two is a difference between two APKs that are
# meant to have none -- and a path spelled twice is a path that can go stale in
# one copy.
include_guard(GLOBAL)

# The Logos libraries a host links, and the whole set its APK carries.
#
# Every .so under <roots> travels in the APK; only the ones the host calls
# directly go on the link line. A shared library that is merely dlopened by
# another one still has to be packaged, which is what the two lists separate --
# <out_apk_libs> is the packaging one, for QT_ANDROID_EXTRA_LIBS.
function(logos_android_link_logos target roots out_apk_libs)
    set(_extra "")
    foreach(_root IN LISTS roots)
        file(GLOB _found "${_root}/lib/*.so" "${_root}/lib/*.so.*")
        list(APPEND _extra ${_found})
    endforeach()
    list(REMOVE_DUPLICATES _extra)

    set(_link "")
    set(_spdlog "")
    foreach(_so IN LISTS _extra)
        if(_so MATCHES "liblogos_core\\.so$|liblogos_qt_host\\.so$|liblogos_protocol\\.so$")
            list(APPEND _link ${_so})
        endif()
        # The SAME libspdlog.so liblogos_core links: the host attaches a logcat
        # sink to the core's channels (BundledSetRunner), and that only reaches
        # them if there is one registry in the process.
        if(_so MATCHES "libspdlog\\.so$")
            set(_spdlog ${_so})
        endif()
    endforeach()
    if(NOT _link MATCHES "liblogos_core")
        message(FATAL_ERROR "liblogos_core.so not found under LOGOS_LIB_ROOTS: ${roots}")
    endif()
    if(NOT _spdlog)
        message(FATAL_ERROR "libspdlog.so not found under LOGOS_LIB_ROOTS: ${roots}")
    endif()
    message(STATUS "Logos link: ${_link}")
    message(STATUS "APK extra libs: ${_extra}")

    target_link_libraries(${target} PRIVATE ${_link} ${_spdlog})
    # Compiled-lib mode is what puts spdlog's registry in the .so rather than
    # in each image that includes it.
    target_compile_definitions(${target} PRIVATE SPDLOG_COMPILED_LIB)

    set(${out_apk_libs} "${_extra}" PARENT_SCOPE)
endfunction()

# THE ANDROID PACKAGE SOURCE DIR IS ASSEMBLED, not named: androiddeployqt takes
# exactly one, and a host here needs three things in it -- its own platform/
# (the manifest), the launcher icon every Android host in this repo shares
# (mobile/android/res), and the Web container's Java (mobile/webview/android).
# Copying the Java beats moving it under a host's platform/, because the same
# class is the Android half of the container for every host and belongs next to
# the C++ it is paired with.
function(logos_android_package_source_dir target)
    set(_package "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/platform/ DESTINATION ${_package})
    file(COPY ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../android/res DESTINATION ${_package})
    file(COPY ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../webview/android/src
         DESTINATION ${_package})
    set_property(TARGET ${target} PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${_package}")
endfunction()
