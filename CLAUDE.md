# CLAUDE.md — re-twist-tt2

A native desktop recreation of **Text Twist 2** (GameHouse, 2008), written
in portable C++17. The whole game fits in one CMake project; there is no
web, no JavaScript, no Capacitor.

The **main goal is updating the dictionary**: the original game's `.tree`
word lists are fully decoded, and this project can read and write them
byte-for-byte. The shipped open-license dictionaries (ENABLE2k, public
domain) replace the original 2008 GameHouse word list without changing
the engine.

## Stack

- **C++17** (any of GCC 9+, Clang 10+, MSVC 19.20+).
- **CMake 3.16+** for the build.
- **No third-party libraries in the engine.** The engine is header-only;
  the tree CLI uses only the C++ standard library. The desktop UI
  (`ui/`) needs SDL2, SDL2_image and SDL2_mixer; it is skipped
  automatically when they are missing (`TT2_BUILD_UI=OFF` to disable).
- **No container required.** The project compiles on the host directly;
  `podman` / `docker` is optional for reproducing the same toolchain.

## Commands

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure    # unit tests
./build/tt2_engine data/open-common.tree data/open-full.tree
./build/tt2-tree   verify data/open-full.tree
```

## Layout

| Path | Owner / purpose |
| --- | --- |
| `engine/include/tt2/` | The engine (header-only). `random.hpp`, `word_tree.hpp`, `bag.hpp`, `board.hpp`, `scoring.hpp`, `round_classic.hpp`, `round_lightning.hpp`, `round_crossword.hpp`, `profile.hpp` (players, progression, high scores; spec in `docs/reverse-engineering/profiles.md`), `profile_store.hpp` (plain-text save files). |
| `engine/tests/` | Unit tests, one file per header plus `test_main.cpp` (the in-tree test harness). |
| `src/main.cpp` | The driver / smoke test for the engine. |
| `tools/tree_cli.cpp` | Dictionary tool: `decode`, `encode`, `merge`, `diff`, `verify`. Replaces the old `tools/tree-cli.ts`. |
| `data/` | Runtime dictionaries. The open-license `open-common.tree` + `open-full.tree` are committed; the original GameHouse `common.tree` + `full.tree` are gitignored. |
| `data-src/` | Raw word lists for rebuilding `open-*.tree` (ENABLE2k, frequency lists). |
| `docs/reverse-engineering/` | Evidence from the original game: binary fingerprint, gameplay notes, `.tree` format spec, reference Python decoder/encoder. |
| `docs/dictionary-sources.md` | Sources, licenses, access dates for the open dictionaries. |
| `CMakeLists.txt` | The build. |

## Legal boundary (important)

- **Never commit GameHouse assets**: no `.tree` dictionaries, images,
  sounds, fonts, or the exe/DLL. The original `common.tree` / `full.tree`
  (when present) are gitignored; the symlink `Text Twist 2.exe` is
  never committed.
- **What ships in the repo:** the open-license dictionaries
  (`data/open-*.tree`) and nothing else from the game. The 2026 update is
  literally a file swap.
- **Look and feel may imitate the original; assets may not.** Layouts,
  color language, and motion inspired by the 2008 game are fine.
  Copying/tracing its PNG art, fonts, or audio is not.
- **Do not analyze, describe, or touch DRM/licensing**: `texttwist2_RADRM.dll`,
  trial/unlock logic, or the `.nfo` files in the install folder.

### Placeholder assets vs. importing from the original install

The UI will need images, fonts, and sounds. There are two paths; pick
the one that fits the deployer.

1. **Default — open / royalty-free placeholder assets.** This is
   what `main` ships with. Use procedurally drawn or open-license
   substitutes:
   - **Fonts:** system fonts the OS already provides (e.g. the
     `DejaVu`, `Liberation`, `Noto`, or `Inter` families — all
     SIL OFL or similar permissive licenses, no attribution
     required in app UI but credit in `docs/`). No bitmap-font
     PNGs from the original install.
   - **Images / icons:** procedural (drawn with the GPU/CPU
     renderer in the same warm colour palette as the original
     but with new geometry). When a stock illustration is
     appropriate, pull from open-license sets (e.g. public
     domain / CC0 / CC-BY) and credit the source in
     `docs/design/asset-credits.md`.
   - **Sound:** generate or commission original SFX, or use
     CC0/CC-BY SFX packs (e.g. freesound.org, OpenGameArt).
     The original game uses `.ogg` Vorbis through Miles Sound
     System; the replacement can use any free format
     (WAV/OGG/Opus) the chosen audio backend supports.
2. **End-user's own original install — the end-user's legal
   liability.** Some users own a legitimate copy of Text Twist 2
   and want the game to *look* like the original. The build
   supports an opt-in importer:

   ```bash
   # Example: copy the original install's images and sounds into
   # the build tree. ONLY do this with a copy you legally own.
   ./scripts/import-original.sh /path/to/Text\ Twist\ 2/install
   cmake --build build
   ```

   What this is and what it isn't:
   - The repository **never** ships with imported assets. The
     importer is a one-shot local tool; the user runs it on
     their own machine against their own install.
   - The user is **solely responsible** for confirming they
     have the right to copy those assets out of their install.
     The license to use the executable does not, in every
     jurisdiction, extend to extracting its art assets.
   - The CMake build detects the imported assets and uses them
     when present; otherwise it falls back to the placeholders.
   - Imported assets are gitignored. `git status` after running
     the importer should not show them.

   The importer is intentionally not automated in CI. It is a
   manual, user-initiated step.

## Reference: how the original works

Every engine constant comes from `docs/reverse-engineering/`. The high-level
summary is in `docs/reverse-engineering/architecture.md`; the binary
fingerprint and class hierarchy are in `docs/reverse-engineering/binary-fingerprint.md`;
the address-level walkthrough of the gameplay code is in
`docs/reverse-engineering/gameplay-notes.md`.

### Dictionary files (`.tree`)

- Grammar: `Node := [a-z]* ('0'|'1') Node*`. A node's child letters
  (sorted) come first, then `1` = "a word ends here" or `0` = "not a
  word", then each child's subtree, depth-first. No header.
- `TextTwist2Words.tree` (original) → **common** list (25,004 words,
  3–15 letters). Supplies the rack words and the on-screen word slots.
- `DamBeaversWords.tree` (original) → **full** list (116,757 words,
  2–16 letters). Used **only to validate guesses**.
- `encode(decode(x)) === x` byte-for-byte for both files. Any change to
  `engine/include/tt2/word_tree.hpp` must keep that round trip intact.

### Classic rules (Timed / Untimed)

- RNG: Java-style LCG (`0x5DEECE66D`, `0xB`, 48-bit). Shuffling uses
  `std::random_shuffle` semantics.
- Rack word: common words of 5–8 letters, drawn from a persistent
  shuffled bag (seed + index saved per player). A word qualifies only
  if it has **≥15 sub-anagrams** in the common list.
- Target length is `min(rand 5..8, maxLen setting 6/7/8)`: skip longer
  words, make up to 5 attempts, keep the closest shorter one.
- Word slots: common sub-anagrams of length 3..8. If there are more
  than 48, each length gets the quota `max(1, trunc(count*48/total))`.
- Guess: must be in the **full** list, then fills the first empty slot
  of the same length. A guess that isn't in the common list counts as
  "uncommon".
- Scoring: **10 × len²** per word; the **first bingo** (a word using
  every letter) in a round adds **+1000** and qualifies you; **finding
  every word** adds **+100 × slot count** and ends the round. There is
  no time bonus.
- Timed rounds last **120 s**, with a 10 s warning countdown. When time
  runs out you advance if qualified; otherwise it's game over.

### Lightning / Blitz

- Words per round: `n = min(10, floor(5 + 0.2·(round−1)))`.
- Timer: 80.3 s, +8 s per word, capped at 80.3 s.
- Score per word: `10·len·(len−hints)`. Only bingos count.

### Word of the Day

- Seed 1337, index = days since 2008-09-03, at most 5 slots per length.

### Crossword

- 18×11 grid. The root word must have exactly one full-length anagram.
  10,000 placement attempts.

## Working rules

- Keep `engine/` pure and fully unit-tested. Every change ships with
  tests (positive and negative cases).
- Match the original rules above unless a deviation is deliberate.
  Document any deviation in code comments and in the PR.
- **Engine ownership.** `WordTree`, `Bag`, `ClassicRound`,
  `LightningRound` store **non-owning pointers** into the `WordTree`
  instances they were given. The trees must outlive the round — keep
  them in local variables or members, never pass temporaries:

  ```cpp
  // GOOD: trees outlive the round.
  auto com = common();
  auto ful = full();
  Bag bag(com);
  ClassicRound round(com, ful, ClassicRound::Config{});

  // BAD: temporaries die after the constructor returns; pointers
  // dangle. AddressSanitizer will catch this on the first use.
  Bag bag(common());
  ClassicRound round(common(), full(), ClassicRound::Config{});
  ```

  This is enforced by the tests (see `engine/tests/test_round_classic.cpp`)
  and the CMake build's ASan run.
- UI work (layout, styling, motion, responsiveness) goes through the
  designer lane. The UI lives in `ui/`: `ui/include/tt2ui/` is the
  headless scene model (`sprite.hpp`, `modifier.hpp`,
  `welcome_flow.hpp`), `ui/src/main.cpp` is the SDL2 front end, and
  `ui/tests/` holds the headless tests. Timings and layout for the
  welcome flow come from `docs/reverse-engineering/welcome-flow.md`;
  keep the model free of SDL so it stays testable. Original assets are
  never committed: `scripts/import-original.sh` copies them into the
  gitignored `imported-assets/` on the user's machine only.
- Commits follow Conventional Commits. **The AI only stages changes and
  drafts commit messages; the user commits and pushes.**
- **AI attribution is mandatory on every AI-generated commit.** Every
  commit message drafted by an LLM must end with a `Co-authored-by`
  trailer naming the actual model that generated the change, e.g.:

  ```text
  Co-authored-by: Claude Opus 5.5 <noreply@anthropic.com>
  ```

  For non-Anthropic models, use the model name without email address
  (non Git format, not going to use Co-Author). Commits made directly
  by the user do not need this trailer.
- Don't commit AI tracking files or the local `.opencode/` config.
