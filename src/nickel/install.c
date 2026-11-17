// Seeds Smalt's support files on user storage, the way Kobalt does
// (platforms/kobo/common/install.c, MIT, see toolchains/LICENSE.kobalt).
//
// toolchains/nickelhook/resources.c runs before NickelHook checks the
// uninstall marker. On the first load after an install it writes the files
// embedded from packaging/resources/ (the `uninstall` marker and
// config.ini.example) into .adds/bookorbit/, never replacing a file that is
// already there, and then a receipt. So:
//   - installing only KoboRoot.tgz is enough; the marker no longer has to come
//     from the archive;
//   - deleting `uninstall` (or the whole .adds/bookorbit folder) later still
//     uninstalls, because the receipt says the files were already seeded;
//   - after an uninstall NickelHook clears the receipt, so reinstalling seeds
//     the marker again.
//
// The receipt is .adds/.bookorbit.initialized, outside the config folder, so a
// first install never depends on that folder existing (Kobalt keeps its
// receipt inside its folder and relies on its ZIP to create it).
#include <stddef.h>

#include <resources.h>

#include "embedded_resources.h"

#define SMALT_CONFIG_DIR "/mnt/onboard/.adds/bookorbit"

const struct nh_resources nh_resources = {
    .config_dir = SMALT_CONFIG_DIR,
    .uninstall_file = SMALT_CONFIG_DIR "/uninstall",
    .files = nh_embedded_resources,
    .initialized_file = NULL, // .adds/.bookorbit.initialized
};
