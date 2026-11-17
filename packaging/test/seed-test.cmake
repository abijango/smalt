# Runs packaging/test/seed_test through an install's life and checks the
# files on the fake user partition after each step:
#   cmake -DEXE=<seed_test> -DROOT=<fake onboard root> -P seed-test.cmake
set(dir "${ROOT}/.adds/bookorbit")
set(receipt "${ROOT}/.adds/.bookorbit.initialized")

function(load)
    execute_process(COMMAND "${EXE}" ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "seed_test ${ARGN} failed: ${result}")
    endif()
endfunction()

function(expect path present why)
    if(present AND NOT EXISTS "${path}")
        message(FATAL_ERROR "${why}: expected ${path}")
    elseif(NOT present AND EXISTS "${path}")
        message(FATAL_ERROR "${why}: expected no ${path}")
    endif()
endfunction()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

# 1. First load after installing only KoboRoot.tgz: no .adds folder at all.
load()
expect("${dir}/uninstall" TRUE "first load seeds the marker")
expect("${dir}/config.ini.example" TRUE "first load seeds the example config")
expect("${receipt}" TRUE "first load records a receipt")
file(READ "${dir}/uninstall" text)
if(NOT text MATCHES "delete this file")
    message(FATAL_ERROR "the seeded marker should explain itself, got: ${text}")
endif()

# 2. The user edits a seeded file; a restart keeps the edit.
file(WRITE "${dir}/config.ini.example" "edited by the user\n")
load()
file(READ "${dir}/config.ini.example" text)
if(NOT text STREQUAL "edited by the user\n")
    message(FATAL_ERROR "a restart must not replace existing files")
endif()

# 3. The user deletes `uninstall` to remove Smalt: it must not come back, so
#    NickelHook sees it missing and uninstalls.
file(REMOVE "${dir}/uninstall")
load()
expect("${dir}/uninstall" FALSE "a deleted marker is not seeded again")

# 4. Deleting the whole folder also uninstalls.
file(REMOVE_RECURSE "${dir}")
load()
expect("${dir}/uninstall" FALSE "a deleted folder is not seeded again")

# 5. NickelHook removes the library and clears the receipt...
load(uninstall)
expect("${receipt}" FALSE "uninstalling clears the receipt")

# 6. ...so reinstalling seeds the marker again.
load()
expect("${dir}/uninstall" TRUE "a reinstall seeds the marker again")
expect("${receipt}" TRUE "a reinstall records a receipt")

# 7. The user's own config.ini is never touched.
file(WRITE "${dir}/config.ini" "url = https://example.invalid\n")
load()
file(READ "${dir}/config.ini" text)
if(NOT text STREQUAL "url = https://example.invalid\n")
    message(FATAL_ERROR "config.ini must never be touched")
endif()
message(STATUS "Seeding behaves as an install, uninstall and reinstall need")
