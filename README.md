# re-twist-tt2

A native desktop recreation of **Text Twist 2** (GameHouse, 2008) in portable C++17.

The original shipped a 25 k-word common list and a 117 k-word full list, both locked inside a
Win32 install. This project decodes the game logic and the `.tree` dictionary format, and ships a
public-domain **2008 → 2026 dictionary update** (ENABLE2k, 173 528 words). Swapping dictionaries is
a file swap; the engine does not change.

It builds with CMake on Linux, macOS and Windows. There is no web layer, no JavaScript, no
Capacitor.

![Boot sequence: GameHouse splash, loading screen, main menu](docs/media/boot-sequence.gif)

The splash, loading screen and main menu are rebuilt 1:1 from the decompiled original (timings in
`docs/reverse-engineering/welcome-flow.md`), recorded with `tt2 --dump-frames` at 4x speed. The art
in the GIF comes from a legitimately owned copy imported with `scripts/import-original.sh`; the
repository itself ships only procedural placeholders.

## Tech stack

- **Language:** C++17 (GCC 9+, Clang 10+ or MSVC 19.20+).
- **Build:** CMake 3.16+.
- **Engine:** header-only, standard library only. No third-party code.
- **Desktop UI:** SDL2 (2.0.10+), SDL2_image and SDL2_mixer. When they are missing, CMake skips the
  UI and still builds the engine, the tools and the tests (`-DTT2_BUILD_UI=OFF` skips it on
  purpose).
- **Reverse engineering:** [REA](https://github.com/morluto/rea) and Ghidra, against a locally
  owned install. Nothing from that install is committed.

## Quick start

```bash
# Debian / Ubuntu: the UI dependencies (skip for an engine-only build)
sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev

cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure    # engine + headless UI tests

./build/tt2                                    # play
```

Optional, and only with a copy you legally own: import the original art, fonts and sounds so the
game looks like the 2008 release. Without this step the game boots with placeholders.

```bash
./scripts/import-original.sh "/path/to/Text Twist 2"
```

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
| `--assets DIR` | Read the art from `DIR` instead of `imported-assets/` |
| `--player NAME` | Add `NAME` if new and make it the current player |
| `--play untimed\|timed` | Start a game as soon as the menu is up |
| `--speed N` | Run the clock `N` times faster |
| `--exit-after MS` | Quit after `MS` simulated milliseconds |
| `--dump-frames DIR` | Write `frame_NNNN.bmp` at `--fps N` (headless with `SDL_VIDEODRIVER=dummy`) |

## CI and downloads

GitHub Actions runs two workflows:

- **CI** (`.github/workflows/ci.yml`), on every push to `main` and every pull request: the unit tests on Linux (GCC,
  warnings as errors, plus an AddressSanitizer / UBSan run) and on Windows (MSYS2 UCRT64), each followed by a headless
  smoke run of the game.
- **Packages** (`.github/workflows/packages.yml`), on pushes to `main`, `v*` tags and by hand: a Linux **AppImage**
  and a Windows **zip** (`tt2.exe`, its DLLs and `data/`). Download them from the run's *Artifacts* section.

The packages carry only the open-license dictionaries and the placeholder art. A packaged game finds its dictionaries
next to itself (`data/` beside `tt2.exe`, or `share/tt2/data` inside the AppImage); `--data-dir DIR` overrides that.

## Where things live

```text
.
├── engine/
│   ├── include/tt2/          # header-only engine: RNG, .tree codec, bag, board, scoring,
│   │                         #   Classic / Lightning / Crossword rounds, profiles, settings,
│   │                         #   the resumable saved game
│   └── tests/                # engine unit tests (in-tree harness)
├── ui/
│   ├── include/tt2ui/        # headless scene model: sprites, modifiers, welcome flow,
│   │                         #   bitmap fonts, profile dialogs, game scene and its dialogs,
│   │                         #   HOW TO PLAY, SETTINGS
│   ├── src/main.cpp          # the SDL2 front end (the only file that touches SDL)
│   └── tests/                # headless UI tests
├── src/main.cpp              # engine driver / smoke test
├── tools/tree_cli.cpp        # tt2-tree dictionary tool
├── data/                     # runtime dictionaries (open-*.tree committed)
├── data-src/                 # raw word lists for rebuilding the open dictionaries
├── scripts/import-original.sh
├── packaging/linux/          # desktop entry and original icon for the AppImage
├── .github/workflows/        # CI (tests) and Packages (AppImage, Windows zip)
└── docs/
    ├── reverse-engineering/  # evidence from the original: format specs, scene layouts, timings
    ├── design/               # original-look brief
    ├── media/                # README media
    └── dictionary-sources.md
```

### How the parts connect

1. **`engine/`** holds the rules. It knows nothing about the screen, so every rule is unit-tested
   without a window.
2. **`ui/include/tt2ui/`** is the scene model: where each sprite sits, how it animates, and what a
   click or keypress does. It is also SDL-free, which is why the UI tests run headless.
3. **`ui/src/main.cpp`** is a thin SDL2 layer. It loads images, sounds and fonts, feeds input and
   time into the scene model, and draws whatever sprites the model reports.
4. **`docs/reverse-engineering/`** is the source of truth for every constant. Each behaviour cites
   the address in the original exe it was read from, or is marked as observed or unconfirmed.

## Status

| Area | State |
| --- | --- |
| Dictionary | Open `.tree` files committed. The codec round-trips both the original and the open files byte-for-byte and matches the Python reference decoder. The game prefers your own `data/common.tree` / `data/full.tree` (gitignored) and falls back to the open lists. |
| Engine | Classic (Timed, Untimed, Word of the Day), Lightning and Crossword rounds, profiles, high scores, settings and the Untimed saved game. All unit-tested. |
| Boot flow | Splash → loading → main menu with the original timings and fades. |
| Profiles | Welcome label, name input, player select and remove confirmation, saved to the per-user data directory. |
| **Untimed / Timed** | **Playable.** Word grid, pearl letter balls, twist / clear / last word / enter, the 2:00 clock, the in-game popups, pause menu, Exit / Give Up confirmations, round-end reveal, GAME OVER and new high score, resume on the next launch, HOW TO PLAY and SETTINGS. |
| Lightning, Crossword, Word of the Day | Engine done; the menu entries are still stubs. |
| Trophies | Slots saved; unlock conditions not reconstructed yet. |

Current work is the animation polish for Untimed (easing, fades, letter flips). The bigger picture
is in [`ROADMAP.md`](ROADMAP.md).

## License

- **Code** (engine, UI, tools, scripts, docs): [MIT](LICENSE).
- **Dictionaries** (`data/open-*.tree`): `open-full.tree` is public domain and `open-common.tree`
  is CC BY-SA 4.0. See [`data/LICENSE.md`](data/LICENSE.md) for attribution.

## Legal

This is an independent fan project. It is not affiliated with, endorsed by, or sponsored by
GameHouse. "Text Twist" is a trademark of its respective owner, and the licenses above grant no
rights to it.

The look and feel may imitate the original; the assets may not. The original `.tree` files, the
executable, and its images, fonts and sounds are never committed. The importer copies them only on
your own machine, from your own install, and whether your license allows that is your
responsibility. The DRM components (`texttwist2_RADRM.dll`, the trial code, the `.nfo` files) are
not analyzed.
