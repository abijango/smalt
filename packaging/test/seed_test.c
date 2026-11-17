// Desktop test harness for toolchains/nickelhook/resources.c, the code that
// seeds the `uninstall` marker and config.ini.example on first load.
//
// The seeding runs in a constructor before main(), exactly as in libsmalt.so,
// against a fake user partition at SMALT_SEED_ROOT. packaging/test/seed-test.cmake
// runs this program several times and checks the files between runs.
//
//   seed_test            load once (the constructor seeds if needed)
//   seed_test uninstall  also do what NickelHook does after removing the mod
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <NickelHook.h>
#include <resources.h>

#include "embedded_resources.h"

#define SEED_CONFIG_DIR SMALT_SEED_ROOT "/.adds/bookorbit"

// Same values as src/nickel/install.c, under the fake root.
const struct nh_resources nh_resources = {
    .config_dir = SEED_CONFIG_DIR,
    .uninstall_file = SEED_CONFIG_DIR "/uninstall",
    .files = nh_embedded_resources,
    .initialized_file = NULL,
};

void nh_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fputs("[nh] ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "uninstall") == 0)
        nh_resources_uninstalled();
    return 0;
}
