# Toolchains

| Path | What it is | Licence |
| --- | --- | --- |
| `kobo-qt5.cmake` | CMake toolchain for the Kobo target; includes the kobuild image's `/tc/qt5/toolchain.cmake` | MIT, adapted from Kobalt (`LICENSE.kobalt`) |
| `sdk-image.sh` | The kobuild Qt5 image, pinned by digest (same pin as Kobalt) | MIT, adapted from Kobalt (`LICENSE.kobalt`) |
| `nickelhook/` | NickelHook, vendored unchanged from [pgaskin/NickelHook](https://github.com/pgaskin/NickelHook) at `bcd90ba94d90d22562fb64ef2920ed3323c1dae5` | MIT (`nickelhook/LICENSE`) |
| `cache/` | Local cache (ignored by git) | |

Kobalt no longer has a root `toolchains/` folder or `build-kobo.sh`; its Kobo build lives in `platforms/kobo/` and runs inside the prepared [kobuild](https://github.com/nicoverbruggen/kobuild) images. Smalt follows that model: `./build-kobo.sh` runs CMake inside the pinned Qt5 image and never builds Qt or a compiler.

The Qt5 image is **Qt 5.2 with GCC 4.9's C++ library** (Kobalt `doc/building.md`). Code that goes into `libsmalt.so` must compile there: see `BUILDING.md`.
