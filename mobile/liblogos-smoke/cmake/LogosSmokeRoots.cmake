# Shared by the iOS stage and the Android app: the Logos stages install their
# headers under different subdirectories of their prefixes (the repos disagree,
# and this is not the place to change that), so add every one that exists.
#
#   logos_smoke_add_include_roots(<target> <PUBLIC|PRIVATE> <roots>)
#
# <roots> is a ;-list of prefixes, passed by nix.
function(logos_smoke_add_include_roots target visibility roots)
    foreach(_root IN LISTS roots)
        # include/implementations/web is where logos-protocol puts
        # message_channel.h, which liblogos' web_module_view.h includes by bare
        # name -- so the Web container's seam header does not compile without it.
        foreach(_inc include include/cpp include/core include/implementations/plain
                     include/implementations/web
                     include/logos_container include/logos_module_loader include/process_stats)
            if(IS_DIRECTORY "${_root}/${_inc}")
                target_include_directories(${target} ${visibility} "${_root}/${_inc}")
            endif()
        endforeach()
    endforeach()
endfunction()
