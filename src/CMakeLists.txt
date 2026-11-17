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

if(SMALT_TARGET STREQUAL "kobo")
    add_subdirectory(nickel)      # 20, 21, 30, 31: NickelHook plugin, Kobo only
endif()
