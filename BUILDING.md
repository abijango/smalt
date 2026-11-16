# Building Smalt

## Desktop (Linux x86_64, cloud sessions)

```bash
cmake -S . -B build -G Ninja -DSMALT_TARGET=desktop
cmake --build build
ctest --test-dir build --output-on-failure
```

Needs CMake 3.22+, Ninja and the system Qt 5 (`qtbase5-dev`); `scripts/cloud-setup.sh` installs them. The desktop target builds every module except `src/nickel/` as a static library, plus the Qt Test programs under `tests/`. Tests run headless (`QT_QPA_PLATFORM=offscreen`).

## Kobo (firmware 4.x, Qt 5)

```bash
./build-kobo.sh
```

This produces `out/KoboRoot.tgz`. It runs CMake inside the prepared kobuild Qt5 image pinned in `toolchains/sdk-image.sh` (the same image Kobalt builds with), using Docker or Podman, so it needs network access to `ghcr.io` and `pkg-containers.githubusercontent.com` the first time. Inside that image it runs CMake directly.

Install: copy `KoboRoot.tgz` to `.kobo/` on the Kobo and eject. It contains:

| Path on the Kobo | What |
| --- | --- |
| `/usr/local/Kobo/imageformats/libsmalt.so` | The NickelHook mod |
| `/mnt/onboard/.adds/bookorbit/uninstall` | Delete it and restart to uninstall |
| `/mnt/onboard/.adds/bookorbit/config.ini.example` | Copy to `config.ini` and fill in |
| `/mnt/onboard/.adds/bookorbit/certs/ca-bundle.pem` | CA certificates (placeholder until task 11) |

## Code that runs on the Kobo: Qt 5.2 and GCC 4.9's library

The Kobo Qt5 SDK is **Qt 5.2** with **GCC 4.9's C++ standard library** (compiled by a newer GCC). The desktop build uses a newer Qt 5, so it won't catch these. Run `./build-kobo.sh` before you finish when you can; CI runs it on every PR.

- C++17 language features are fine. Standard library parts newer than GCC 4.9 are not: no `<optional>`, `<variant>`, `<string_view>`, `<any>`, `<filesystem>`, `<shared_mutex>` or `std::clamp`. C++14 library parts such as `std::make_unique` are fine.
- Only Qt APIs from Qt 5.2 or older. Common traps: `QTimer::singleShot` with a lambda (5.4), `QStorageInfo` (5.4), `Q_ENUM` (5.5; use `Q_ENUMS`), `QOverload` (5.7), `QDateTime::currentSecsSinceEpoch` and `toSecsSinceEpoch` (5.8), `QDeadlineTimer` (5.8), `QRandomGenerator`, `QThread::create` and `QStringView` (5.10). `QSaveFile` and `QLockFile` (5.1) are fine.
- The library must not need newer than glibc 2.19 or GCC 4.9's `GLIBCXX_3.4.20`. Check with the image's `readelf -V`.

## Adding code

- A module declares itself in its own folder with `smalt_add_module(<name> SOURCES ... DEPS ...)` (see `cmake/SmaltModules.cmake`). Every module is linked into `libsmalt.so` automatically.
- A test registers with `smalt_add_qtest(<area>.<name> SOURCES ... DEPS ...)`.
- `src/features/<x>/` and `tests/features/<x>/` are picked up automatically when they contain a `CMakeLists.txt`.
- `src/nickel/` picks up every `.cpp` and `.h` in the folder.

## Formatting

```bash
./scripts/check-format.sh        # check (CI runs this)
./scripts/check-format.sh --fix  # apply
```

`.clang-format` is Kobalt's (Qt project style). Vendored NickelHook in `toolchains/nickelhook/` is left as upstream wrote it.
