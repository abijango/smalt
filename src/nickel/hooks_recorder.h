// NickelHook entries for task 20's part of the bridge. Owned by task 20; see
// bridge.h. plugin.cpp splices these into NickelHook's tables, so each macro
// expands to zero or more complete, comma-terminated entries:
//
//   #define SMALT_RECORDER_HOOKS \
//       { "Nickel_symbol", "smalt_hook_name", "libnickel.so.1.0.0", nh_symoutptr(orig), "why", false },
//   #define SMALT_RECORDER_DLSYMS \
//       { "Nickel_symbol", nh_symoutptr(fn), "why", true },
//
// Hook functions are extern "C" with default visibility, defined in the
// task's own .cpp files in src/nickel/. Prefer optional entries: a missing
// symbol must leave the feature off, never stop the mod from loading.
#pragma once

#define SMALT_RECORDER_HOOKS
#define SMALT_RECORDER_DLSYMS
