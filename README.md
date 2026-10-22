# re-twist-tt2

A native recreation of **Text Twist 2** (GameHouse, 2008) in portable C++17, for the desktop and Android.

The original shipped a 25 k-word common list and a 117 k-word full list, both locked inside a
Win32 install. This project decodes the game logic and the `.tree` dictionary format, and ships a
public-domain **2008 → 2026 dictionary update** (ENABLE2k, 173 528 words). Swapping dictionaries is
a file swap; the engine does not change.

It builds with CMake on Linux, macOS and Windows; the Android app wraps the same CMake build in
Gradle and the NDK. There is no web layer, no JavaScript, no Capacitor.

<img width="1282" height="752" alt="image" src="https://github.com/user-attachments/assets/759e3d30-e1ad-444e-b532-36081cb60f0e" />

## Tech stack

- **Language:** C++17 (GCC 9+, Clang 10+ or MSVC 19.20+).
- **Build:** CMake 3.16+.
- **Engine:** header-only, standard library only. No third-party code.
- **Desktop UI:** SDL3 (3.2+), SDL3_image and SDL3_mixer. `-DTT2_FETCH_SDL3=ON` downloads and builds
  the pinned releases (SDL 3.4.18, SDL_image 3.4.8, SDL_mixer 3.2.4) for distros that don't package
  them yet. When they are missing, CMake skips the UI and still builds the engine, the tools and the
  tests (`-DTT2_BUILD_UI=OFF` skips it on purpose).
- **Android:** Android 12+ on 64-bit ARM. The same C++ is compiled with the NDK (27.2) and loaded by SDL3's
  `SDLActivity`; a little Kotlin adds the ZIP importer and the splash screen. Building needs JDK 17.
- **Reverse engineering:** [REA](https://github.com/morluto/rea) and Ghidra, against a locally
  owned install. Nothing from that install is committed.

## Quick start

```bash
# Debian / Ubuntu: the headers SDL3 needs to build its video and audio backends
# (skip for an engine-only build)
sudo apt install cmake g++ pkg-config libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
  libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev \
  libgles2-mesa-dev libdrm-dev libgbm-dev libdbus-1-dev libibus-1.0-dev libudev-dev

cmake -S . -B build -DTT2_FETCH_SDL3=ON        # or drop the flag to use an installed SDL3
cmake --build build -j
ctest --test-dir build --output-on-failure    # engine + headless UI tests

./build/tt2                                    # play
```

**You also need your own copy of Text Twist 2.** The game asks for it on the first launch; see
[Original game assets (required for now)](#original-game-assets-required-for-now).

### Other binaries

```bash
./build/tt2_engine data/open-common.tree data/open-full.tree   # engine smoke test
./build/tt2-tree verify data/open-full.tree                    # dictionary tool
```

`tt2-tree` handles the `.tree` format: `decode`, `encode`, `merge`, `diff` and `verify`.

### Useful `tt2` flags

| Flag | What it does |
| --- | --- |
| `--save-dir DIR` | Keep profiles and saves in `DIR` instead of the per-user data directory |
| `--import DIR` | Import the original art from your install at `DIR`, then exit (0 = imported, 1 = nothing copied) |
| `--asset-dir DIR` | Import into and load from `DIR` instead of the per-user asset folder |
| `--assets DIR` | Load the art from `DIR` as is: no import, no checks, no prompt |
| `--asset-manifest FILE` | Check imports against `FILE` instead of `data/original-assets.sha256` |
| `--player NAME` | Add `NAME` if new and make it the current player |
| `--play untimed\|timed\|lightning\|wotd\|crossword` | Start a game as soon as the menu is up |
| `--speed N` | Run the clock `N` times faster |
| `--exit-after MS` | Quit after `MS` simulated milliseconds |
| `--dump-frames DIR` | Write `frame_NNNN.bmp` at `--fps N` (headless with `SDL_VIDEODRIVER=dummy`) |
| `--fixed-dt MS` | Advance every frame by exactly `MS` ms instead of the real time, so frame dumps repeat exactly |
| `--renderer NAME` | Try `auto`, `vulkan`, `opengl` or `software` first, for this run only (IMPROVEMENTS saves a choice); the log names the renderer that started and why others failed |
| `--probe-media FILE` | Decode `FILE` (image, `.ogg` or `.wav`) with the game's own loaders and exit; repeatable. Exit 1 if one fails |

## Original game assets (required for now)

This port's own placeholder art isn't made yet. Without the original game's art, fonts and sounds
it looks unfinished, so **for now you need a legitimately owned copy of Text Twist 2**. The game
copies what it needs from your install once; your copy stays untouched.

### Import in the game

1. Start the game. Before the logo plays, it asks:

   > **Original game required**
   >
   > This game needs the art, fonts and sounds from your own copy of Text Twist 2. The replacement
   > placeholder art isn't ready yet, so without them the game won't look or sound right. Import them
   > from your Text Twist 2 folder now?

1. Choose **YES**. Your system's folder picker opens: choose the folder Text Twist 2 is installed in,
   the one that holds `Text Twist 2.exe`.
1. The game checks every file, copies them into its asset folder (below), and switches to the
   original look. The next launch doesn't ask again.

**NO** keeps the placeholders for this session; the game asks again next time. To import later, use
**SETTINGS → ORIGINAL ASSETS → IMPORT** on the main menu.

### Import from a terminal

```bash
./build/tt2 --import "/path/to/Text Twist 2"                          # source build
./Re-Twist_TT2-v0.1.0-x86_64.AppImage --import "/path/to/Text Twist 2"   # Linux / Steam Deck package
```

```powershell
.\tt2.exe --import "C:\path\to\Text Twist 2"   # Windows package
```

It exits with 0 once every file is imported and checked, or 1 with nothing copied. Use this where no
folder picker exists, such as the Steam Deck's Game Mode (switch to Desktop Mode, or run it from
Konsole). In a source checkout, `scripts/import-original.sh` does the same.

### Where the files go

| System | Imported assets |
| --- | --- |
| Linux | `~/.local/share/re-twist-tt2/imported-assets/` |
| Steam Deck | `/home/deck/.local/share/re-twist-tt2/imported-assets/` |
| Windows | `%APPDATA%\re-twist-tt2\imported-assets\` |
| macOS (untested) | `~/Library/Application Support/re-twist-tt2/imported-assets/` |
| Android | the app's own storage (`files/re-twist-tt2/imported-assets/`); other apps can't see it |

The game prints the folder it uses at start (`assets: ...`). Profiles and saves live in the parent
folder (`--save-dir` moves them); `--asset-dir DIR` moves the imported assets.

### What is checked

Every file is checked against the SHA-256 checksums in `data/original-assets.sha256`, which holds
file names and checksums only. One missing or different file means nothing is copied, and a failed
import keeps the previous one. The game's DLL and `.nfo` files are never copied.

### After an update: import once more

An update can add files to that list. The one with Crossword does: it needs the six files in
`images/crosswordscene/`, which no 0.5.x import has. The game then treats your import as out of date.
It still loads the files you imported before, but asks on every launch until you import again, the same
way as the first time: your install folder on the desktop, or a ZIP of it on Android.

## Android

### Install

1. Download `re-twist-tt2-vX.Y.Z-android.apk` from the
   [Releases](https://github.com/jcchikikomori/re-twist-tt2/releases) page on the device. It needs Android 12 or
   newer on a 64-bit ARM (arm64-v8a) device.
1. Open it. Android asks once whether your browser or file manager may install apps; allow it, then install.
1. Later versions install over this one and keep your players, saves and imported assets: every release is signed
   with the same key ([docs/android-signing.md](docs/android-signing.md)).

### Import the original assets

On Android the game imports a **ZIP** of your Text Twist 2 folder, one file that is easy to copy to a device:

1. On a computer, zip the folder that holds `Text Twist 2.exe`. A ZIP of the folder itself or of its contents both
   work. Copy the ZIP to the device, for example into Downloads.
1. Start the game. It asks for the original game; choose **Choose ZIP** and pick the file.
1. The game checks every file inside the ZIP against the same checksums as on the desktop, copies them, and checks
   the copies. Nothing is kept unless every file checks out. The next launch doesn't ask again.

**Not now** keeps the placeholders for this session. **SETTINGS → ORIGINAL ASSETS → IMPORT** imports later. The
game keeps its own copy, so the ZIP can be deleted afterwards. Limits: the ZIP up to 1 GiB, each file up to
16 MiB, 256 MiB in all (the real game is about 24 MB).

### Build

```bash
cd android
./gradlew assembleDebug        # app/build/outputs/apk/debug/app-debug.apk
./gradlew testDebugUnitTest    # the Kotlin importer's tests, no device needed
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Gradle needs the Android SDK (platform 35) with NDK 27.2.12479018 and CMake 3.22.1, and JDK 17. Debug builds are
a separate app, "re-twist-tt2 (debug)" (`com.jcchikikomori.retwisttt2.debug`), so they install next to a release
instead of over it. Release builds are signed in CI; [docs/android-signing.md](docs/android-signing.md) covers
the key.

## CI and downloads

GitHub Actions runs two workflows:

- **CI** (`.github/workflows/ci.yml`), on every push to `main` and every pull request: the unit tests on Linux (GCC,
  warnings as errors, plus an AddressSanitizer / UBSan run) and on Windows (MSYS2 UCRT64), each followed by a headless
  smoke run of the game and a decode of `ui/tests/media/` (`--probe-media`). On Android: the Kotlin importer's
  tests and a debug APK.
- **Packages** (`.github/workflows/packages.yml`), on pushes to `main`, `v*` tags and by hand: a Linux **AppImage**,
  a Windows **zip** (`tt2.exe`, its DLLs and `data/`) and a signed Android **APK**. A `v*` tag also publishes all
  three on the [Releases](https://github.com/jcchikikomori/re-twist-tt2/releases) page; other runs keep them in
  the run's *Artifacts* section.

Both workflows also import a synthetic install made of random bytes, never real game files. Every APK is checked
by `scripts/ci-apk-check.sh`: no file in it may have the checksum of a file from the original game.

The packages carry only the open-license dictionaries, the asset checksums and the placeholder art; import the
original art as above. A packaged game finds its data next to itself (`data/` beside `tt2.exe`, or `share/tt2/data`
inside the AppImage); `--data-dir DIR` overrides that.

**Windows SmartScreen:** `tt2.exe` isn't code-signed yet, so Windows may say "Windows protected your PC" with an
unknown publisher. Choose **More info → Run anyway**, or before extracting, right-click the zip → **Properties** →
tick **Unblock**.

## Where things live

```text
.
├── engine/
│   ├── include/tt2/          # header-only engine: RNG, .tree codec, bag, board, scoring,
│   │                         #   Classic / Lightning / Crossword rounds, the Word of the Day
│   │                         #   clocks, profiles, trophies, settings, the resumable saved games
│   └── tests/                # engine unit tests (in-tree harness)
├── android/                  # the Android app (Gradle): MainActivity, the Kotlin ZIP importer
│                             #   and its tests; the native code is this repo's CMakeLists.txt
├── ui/
│   ├── include/tt2ui/        # headless scene model: sprites, modifiers, welcome flow,
│   │                         #   bitmap fonts, profile dialogs, the Classic / Word of the Day,
│   │                         #   Lightning and Crossword scenes and their dialogs, HOW TO PLAY,
│   │                         #   SETTINGS, IMPROVEMENTS, the asset import and its prompt
│   ├── src/main.cpp          # the SDL3 front end; audio.cpp plays sounds through SDL3_mixer
│   └── tests/                # headless UI tests
├── src/main.cpp              # engine driver / smoke test
├── tools/tree_cli.cpp        # tt2-tree dictionary tool
├── tools/gen_placeholders.py # writes data/placeholder/ (fonts, dialog frame, buttons)
├── tools/assets_cli.cpp      # tt2-assets: writes and checks the asset checksum list
├── tools/gen_android_icon.py # writes the Android launcher icon from packaging/linux/tt2.svg
├── third_party/              # tinyfiledialogs (the folder picker), the open fonts for the placeholders,
│                             #   the Super-xBR and Kotlin licence notices
├── data/                     # dictionaries (open-*.tree), original-assets.sha256, placeholder/ art
├── data-src/                 # raw word lists for rebuilding the open dictionaries
├── scripts/                  # import-original.sh (wraps tt2 --import), ci-import-smoke.sh,
│                             #   ci-apk-check.sh (what an APK may carry) and its test
├── packaging/linux/          # desktop entry and original icon for the AppImage
├── .github/workflows/        # CI (tests, debug APK) and Packages (AppImage, Windows zip, APK)
└── docs/
    ├── reverse-engineering/  # evidence from the original: format specs, scene layouts, timings
    ├── design/               # original-look brief, asset import design, credits
    ├── media/                # README media
    └── dictionary-sources.md
```

### How the parts connect

1. **`engine/`** holds the rules. It knows nothing about the screen, so every rule is unit-tested
   without a window.
2. **`ui/include/tt2ui/`** is the scene model: where each sprite sits, how it animates, and what a
   click or keypress does. It is also SDL-free, which is why the UI tests run headless.
3. **`ui/src/main.cpp`** is a thin SDL3 layer. It loads images, sounds and fonts, feeds input and
   time into the scene model, and draws whatever sprites the model reports.
4. **`docs/reverse-engineering/`** is the source of truth for every constant. Each behaviour cites
   the address in the original exe it was read from, or is marked as observed or unconfirmed.

## Status

| Area | State |
| --- | --- |
| Dictionary | Open `.tree` files committed. The codec round-trips both the original and the open files byte-for-byte and matches the Python reference decoder. The game prefers your own `data/common.tree` / `data/full.tree` (gitignored) and falls back to the open lists. |
| Engine | Classic (Timed, Untimed, Word of the Day), Lightning and Crossword rounds, profiles, high scores, trophies, settings and the saved game of every mode. All unit-tested. |
| Boot flow | Splash → loading → main menu with the original timings and fades. |
| Original assets | **Required for now.** Imported from your own install on first launch, checked against SHA-256 checksums. The replacement art isn't made yet. |
| Profiles | Welcome label, name input, player select and remove confirmation, saved to the per-user data directory. |
| **Untimed / Timed** | **Playable.** Word grid, pearl letter balls, twist / clear / last word / enter, the 2:00 clock, the in-game popups, pause menu, Exit / Give Up confirmations, round-end reveal, GAME OVER and new high score, resume on the next launch, HOW TO PLAY and SETTINGS. |
| **Lightning** | **Playable.** Five to ten bingo-only words per round, the 80.3 s clock that every solved word tops up by 8 s, automatic hints, GAME OVER when time runs out, resume on the next launch. |
| **Word of the Day** | **Playable.** One 2:00 round a day on the day's word (seed 1337, counted from 2008-09-03). GIVE UP until the first bingo, then DONE; the menu button greys out once the day's game is over, and the high score carries the date. **IMPROVEMENTS → DAILY WORD CLOCK** picks the original's clocks or your local day. With your own `common.tree` the word matches the 2008 game's; with the open list it differs. |
| **Crossword** | **Playable.** An 18×11 grid of crossing words, all spelled from one root word: pick a word with the arrows or a click and type it, with TWIST, ZOOM, CLEAR and LAST WORD. No clock: solve every word for +1000 and the next puzzle; GIVE UP ends the game. Resume on the next launch. The grids differ from the original's, which seeds them from the clock. |
| Trophies | All 24, with the original's rules ([trophies.md](docs/reverse-engineering/trophies.md)): the in-game popup and the trophy room. Well Rounded, Maniac and Fanatic count finished rounds in all five modes. |
| Android | Android 12+ tablets and phones (arm64): OpenGL ES first, the ZIP importer, adaptive launcher icon, splash screen, and a signed release APK built in CI. Tested on a Galaxy Tab A7 Lite (Android 14). |

Current work is the animation polish for Untimed (easing, fades, letter flips). The bigger picture
is in [`ROADMAP.md`](ROADMAP.md).

## License

- **Code** (engine, UI, tools, scripts, docs): [MIT](LICENSE).
- **Dictionaries** (`data/open-*.tree`): `open-full.tree` is public domain and `open-common.tree`
  is CC BY-SA 4.0. See [`data/LICENSE.md`](data/LICENSE.md) for attribution.
- **Third-party code** keeps its own licence: Super-xBR (MIT, ported in `ui/include/tt2ui/upscale.hpp`;
  [`third_party/super-xbr/LICENSE`](third_party/super-xbr/LICENSE)), tinyfiledialogs (zlib), SDL3 (zlib) and the
  placeholder fonts (Apache 2.0, OFL). [`docs/design/asset-credits.md`](docs/design/asset-credits.md) lists them all.
- **Packages** carry every notice in a `licenses/` folder (`share/licenses/tt2/` inside the AppImage), including
  the MinGW runtime DLLs' in the Windows zip.

## Legal

This is an independent fan project. It is not affiliated with, endorsed by, or sponsored by
GameHouse. "Text Twist" is a trademark of its respective owner, and the licenses above grant no
rights to it.

The look and feel may imitate the original; the assets may not. The original `.tree` files, the
executable, and its images, fonts and sounds are never committed or packaged; the repository holds
only their checksums (`data/original-assets.sha256`). The importer copies them only on your own
machine, from your own install, and whether your license allows that is your responsibility. The
DRM components (`texttwist2_RADRM.dll`, the trial code, the `.nfo` files) are not analyzed.
