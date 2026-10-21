# Asset and Third-Party Credits

What ships with re-twist-tt2 that someone else made, and under which license. The dictionaries have their own
page: [dictionary-sources.md](../dictionary-sources.md).

## Third-Party Code

| Component | Version | License | Where | Used for |
| --- | --- | --- | --- | --- |
| [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/) by Guillaume Vareille | 3.21.5 | zlib | `third_party/tinyfiledialogs/` | The "choose your Text Twist 2 folder" dialog when importing the original assets |
| [SDL3, SDL3_image, SDL3_mixer](https://www.libsdl.org/) | 3.4.18, 3.4.8, 3.2.4 | zlib | system or MSYS2 package, or built from the release tarball (`TT2_FETCH_SDL3`); not vendored | Window, input, rendering, images, audio |
| [Super-xBR](https://github.com/libretro/slang-shaders/tree/master/crt/shaders/crt-super-xbr) by Hyllian | libretro slang-shaders, 2015 | MIT | ported to C++ in `ui/include/tt2ui/upscale.hpp`; notice in `third_party/super-xbr/` | The optional 2x texture upscaler (UPSCALER in IMPROVEMENTS) |
| [stb_image, stb_vorbis](https://github.com/nothings/stb) by Sean Barrett | as bundled in SDL3_image / SDL3_mixer | public domain or MIT | inside SDL3_image and SDL3_mixer | Decoding PNG / JPEG and Ogg Vorbis |
| [Kotlin standard library](https://kotlinlang.org/) by JetBrains, with `org.jetbrains:annotations` | 2.0.21, 13.0 | Apache 2.0 | Gradle dependency (`android/gradle/libs.versions.toml`); notice in `third_party/kotlin/` | The Android app's Kotlin code (APK only) |

- tinyfiledialogs is vendored **unaltered**. `third_party/tinyfiledialogs/VERSION` records the upstream commit and
  the SHA-256 of each file; `LICENSE` holds its notice.
- It is compiled as its own C target, outside the project's warning flags (`CMakeLists.txt`, `tt2_tinyfd`).
- Super-xBR is a port, not a copy: the shader math rewritten for the CPU. The MIT notice is kept in full at the
  top of `upscale.hpp`. Art it upscales exists only in memory while the game runs; nothing is written to disk.
  `third_party/super-xbr/LICENSE` holds the notice verbatim and `VERSION` the upstream commit and shader hashes.

## What the Packages Carry

Each package installs these notices into `licenses/` (the Windows zip), `share/licenses/tt2/` (the AppImage) or
`assets/licenses/` (the APK, written by the `bundleLicenses` Gradle task):

| File | Covers | License |
| --- | --- | --- |
| `re-twist-tt2.txt` | This project's code | MIT |
| `super-xbr.txt` | The upscaler port | MIT |
| `tinyfiledialogs.txt` (not the APK), `SDL3.txt`, `SDL3_image.txt`, `SDL3_mixer.txt` | Code linked in statically | zlib |
| `kotlin-stdlib.txt` (APK only) | The Kotlin standard library and JetBrains annotations in the app's code | Apache 2.0 |
| `libwinpthread.txt`, `gcc-runtime-exception.txt` (zip only) | The MinGW runtime DLLs next to `tt2.exe` | MIT and BSD; GPL 3 with the GCC Runtime Library Exception |
| `appimage-runtime.txt` (AppImage only) | The AppImage runtime stub | MIT, with the libraries it links listed inside |

- stb_image and stb_vorbis are taken under their public-domain option, so they need no file.
- The fonts' licences sit next to the fonts in `data/placeholder/`, and the dictionaries' in `data/LICENSE.md`.
- `packages.yml` fails a build that is missing any of these files; for the APK, `scripts/ci-apk-check.sh` does
  the check, in CI too.
- The APK takes the SDL texts from the same pinned tarballs CMake builds from, so they can't drift from the code.

## Art, Fonts and Sounds

- **Shipped:** none from Text Twist 2. `data/placeholder/` holds generated stand-ins for the fonts, the dialog
  frame and the dialog buttons, written by `tools/gen_placeholders.py`. The shapes are new; only the colour values
  were sampled from the original. Anything else still draws as a procedural placeholder in code.
  `data/placeholder/2x/` is the same set drawn again at twice the size (the fonts rendered from the same open
  fonts at twice the point size), loaded on GPU renderers; same sources, same licences.

| Font | License | Where | Used for |
| --- | --- | --- | --- |
| [Luckiest Guy](https://fonts.google.com/specimen/Luckiest+Guy) by Astigmatic | Apache 2.0 | `third_party/fonts/luckiest-guy/` | Header-style bitmap fonts (`font_uibar`, `font_dialog_headers*`, `font_uibar_labels`) |
| [Arimo](https://fonts.google.com/specimen/Arimo) by Steve Matteson | SIL OFL 1.1, no Reserved Font Name | `third_party/fonts/arimo/` | Text bitmap fonts and the button labels |

- Both are unaltered copies from the google/fonts repository; `third_party/fonts/README.md` records the commits.
- **Icons:** `packaging/linux/tt2.svg` (the desktop icon) and the Android launcher icon are this project's own
  drawing, a pearl letter ball with a "T" on a blue tile. `tools/gen_android_icon.py` turns the SVG's geometry
  into the Android vector drawables; no outside source.
- The bitmap sheets are renderings of these fonts, so their licence files ship next to them in `data/placeholder/`.
- **Imported by the player:** the original art, fonts and sounds come from the player's own install, copied on
  their machine by `tt2 --import` or the in-game importer (`docs/design/import-assets.md`). They are never
  committed or packaged. `data/original-assets.sha256` holds only their relative paths and SHA-256 hashes.
