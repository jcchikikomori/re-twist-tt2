# re-twist-tt2 - Roadmap

This document describes the current status and the upcoming milestones of this project.

## Updated

`Tue, 13 Oct 2026 14:10:00 GMT+8`

## Milestones (so far)

- Began the decompilation using [REA](https://github.com/morluto/rea)
- Began the Ghidra setup later on
- Implemented the engine

## Ongoing Goals

1. Successful initial boot
2. The boot sequence must be 1:1 with the original game
3. Focus on first 2 modes (Untimed & Timed)
4. Resume system for Untimed
5. Other game modes such as Lightning, Bingo, etc.
6. On Linux, the build must be on a `AppImage` file
7. The game feedback look & feel (focus on Untimed first)
8. Clickable links on the succeeded words (redirects to the definition of the word from the dictionary online)
9. Pause menu & confirmation screen when exiting the game
10. "How to Play" screen

## Identified bugs

1. The word (that can advance to the next round) during starting the level (or next level) was shown, ended up cheating.
2. No animations during the game.
3. When hovering the items on the main menu, it triggers a sound effect which is not on a original game (no sound effect).

## Success stories (what we made, so far)

- Header-only C++17 engine (`engine/include/tt2/`) with the `.tree` dictionary codec round-tripping
  the original word lists byte-for-byte, the Java-LCG and MSVC CRT random generators, and the
  Classic, Lightning and Crossword rounds, all unit tested.
- Crossword mode reconstructed from confirmed decompilation (grid fit/commit rules, 10,000-attempt
  placement loop).
- Welcome flow (GameHouse splash, loading screen, main menu) decoded from the exe with exact timings
  and layout: `docs/reverse-engineering/welcome-flow.md`.
- First playable UI layer (`ui/`): SDL2 front end that boots splash, loading and the main menu 1:1
  with the original timings, using the user's own imported assets or procedural placeholders.
- Profile & progression system decoded and implemented in the engine (`profile.hpp`,
  `profile_store.hpp`): player manager rules, per-player progression fields, the five high-score
  tables with the original ordering/trim/default-fill logic, and plain-text persistence. Spec:
  `docs/reverse-engineering/profiles.md`. Trophy unlock conditions are next.
- Profile UI: the GF2 bitmap-font format (`fonts/*.xml` + padded PNG layers) decoded and rendered
  (`bitmap_font.hpp`), the welcome label on the main menu, and the name-input / player-select /
  remove-confirmation dialogs with the original layout, 15-character limit, duplicate check and
  reselect rule (`profile_dialogs.hpp`), all headless-tested. Profiles save to the per-user data
  directory as soon as they change.
- Classic game scene (Untimed & Timed, goal 3): the word grid, letter rows, panel and UI bar laid out
  from the decompiled `TextTwist2Scene` (`docs/reverse-engineering/game-scene.md`), typing / clicking
  / twist / clear / last word / enter, the 2:00 clock with the 10 s countdown, bingo and all-words
  popups, the three-miss game over and high-score submission, wired to the player's bag and
  progression counters (`game_scene.hpp`, headless-tested). Found and fixed on the way: the C++
  `.tree` codec read the grammar in the wrong order and produced garbage racks from the real
  dictionaries; it now matches the exe (`0x43e070`) and the Python reference decoder byte-for-byte.

## Future Goals

1. Widescreen support
2. User will be able to use custom dictionaries, for fun!
3. Automatically make a build for Linux using GitHub Actions
4. Being able to play the game on a Web Browser
