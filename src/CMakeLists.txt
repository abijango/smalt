# Each folder below is owned by one task (CLAUDE.md, "Repo layout") and
# declares its own module with smalt_add_module(). Order matters only for
# dependencies: a module must be added after the modules it links.
add_subdirectory(common)          # 00 scaffold: smalt::log, Paths, interface meta-objects
add_subdirectory(core/ledger)     # 10
add_subdirectory(core/outbox)     # 10
add_subdirectory(bookorbit)       # 11
add_subdirectory(safety)          # 12
add_subdirectory(ui/kit)          # 13
add_subdirectory(recorder)        # 20
add_subdirectory(sync)            # 21
add_subdirectory(ui/screens)      # 22
add_subdirectory(features)        # 31-34: one subfolder each, picked up automatically
# 23: added once each folder has a CMakeLists.txt. src/stats/ builds the
# aggregation and dashboard parsing (including src/bookorbit/dashboard.*),
# src/ui/screens/stats/ the Stats widgets; neither is linked by the shell
# (it gets them through ShellDeps callbacks), only by the composition root.
foreach(smalt_stats_dir stats ui/screens/stats)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${smalt_stats_dir}/CMakeLists.txt")
        add_subdirectory(${smalt_stats_dir})
    endif()
endforeach()

if(SMALT_TARGET STREQUAL "kobo")
    add_subdirectory(nickel)      # 20, 21, 30, 31: NickelHook plugin, Kobo only
endif()
