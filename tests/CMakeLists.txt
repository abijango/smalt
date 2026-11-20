# Desktop tests, mirroring src/ (CLAUDE.md, "Repo layout"). Each folder is
# owned by the same task as the code it tests and registers its tests with
# smalt_add_qtest(<area>.<name> ...). Run: ctest --test-dir build
add_subdirectory(fakes)       # 00: FakeNickel, shared by every test that needs Nickel
add_subdirectory(common)      # 00
add_subdirectory(core)        # 10
add_subdirectory(bookorbit)   # 11
add_subdirectory(safety)      # 12
add_subdirectory(ui/kit)      # 13
add_subdirectory(recorder)    # 20
add_subdirectory(sync)        # 21
add_subdirectory(ui/screens)  # 22
add_subdirectory(nickel)      # 20, 30: desktop-testable parts, against FakeNickel
add_subdirectory(features)    # 31-34: one subfolder each, picked up automatically
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stats/CMakeLists.txt")
    add_subdirectory(stats)   # 23
endif()
