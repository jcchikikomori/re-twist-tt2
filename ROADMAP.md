# re-twist-tt2 - Roadmap

This document describes the current status and the upcoming milestones of this project.

## Updated

`Thu, 22 Oct 2026 23:45:00 GMT+8`

## Milestones (so far)

- Began the decompilation using [REA](https://github.com/morluto/rea)
- Began the Ghidra setup later on
- Implemented the engine
- Moved the UI from SDL2 to SDL3 (Vulkan, OpenGL or software rendering)
- Ported the game to Android (0.6.0)
- Added the last two modes, Word of the Day and Crossword (0.6.0)

## Ongoing Goals

1. Run the Android device checklist (fresh install, every mode, background / foreground, Back button,
   renderers) for 0.6.0 on a real tablet
2. Check the AppImage on a Steam Deck
3. Decide whether Crossword needs a touch-specific change on Android
4. Finish the open placeholder art, so the game no longer needs the player's own import to start

## Reached Goals

All eleven of the original goals are done:

1. Successful initial boot
2. The boot sequence is 1:1 with the original game (splash, loading screen, main menu, with the decompiled
   timings)
3. Untimed & Timed
4. Resume system, for every mode
5. Every game mode of the original: Untimed, Timed, Lightning, Word of the Day and Crossword. There is no
   Bingo mode: the original never had one, so the old "Lightning, Bingo, etc." goal meant these five
6. On Linux, the build ships as an `AppImage`
7. The game feedback look & feel (pearl letter balls, slot flips, reveal, sparkles, popups)
8. Clickable links on the found words (they open the word's definition online)
9. Pause menu & confirmation screen when exiting the game, plus the main menu's QUIT confirmation
10. "How to Play" screen
11. Achievement system: trophies pop up in the middle of the game, and the trophy room lists them

## Identified bugs

All four are fixed:

1. The word (that can advance to the next round) during starting the level (or next level) was shown, ended up cheating.
   Fixed: the rack is now dealt like the original's `0x43c370`, which reshuffles until the letters differ from
   both the word and the previous order (`game-scene.md` §4).
2. No animations during the game.
   Fixed: every in-game animation of the original is built; the full inventory is in `game-scene.md` §9.
3. When hovering the items on the main menu, it triggers a sound effect which is not existing on a original game.
   Fixed: a button only plays a hover sound when its `oversound` attribute asks for one, and no menu button does.
4. No music during pause screen. It should continue playing in the background.
   Fixed: the pause menu leaves the music playing, as the original does (`game-scene.md` §6).

## Success stories (what we made, so far)

- Header-only C++17 engine (`engine/include/tt2/`) with the `.tree` dictionary codec round-tripping
  the original word lists byte-for-byte, the Java-LCG and MSVC CRT random generators, and the
  Classic, Lightning and Crossword rounds, all unit tested.
- Crossword mode reconstructed from confirmed decompilation (grid fit/commit rules, 10,000-attempt
  placement loop).
- Welcome flow (GameHouse splash, loading screen, main menu) decoded from the exe with exact timings
  and layout: `docs/reverse-engineering/welcome-flow.md`.
- First playable UI layer (`ui/`): an SDL3 front end that boots splash, loading and the main menu 1:1
  with the original timings, using the user's own imported assets or procedural placeholders.
- Profile & progression system decoded and implemented in the engine (`profile.hpp`,
  `profile_store.hpp`): player manager rules, per-player progression fields, the five high-score
  tables with the original ordering/trim/default-fill logic, and plain-text persistence. Spec:
  `docs/reverse-engineering/profiles.md`. Trophy unlock conditions followed in `trophies.hpp`.
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
- Untimed polish (goals 4, 7 and 9): the resume system (`saved_game.hpp`, the menu's "Continue the
  game?" prompt, save on STOP / window close, delete on load and on game over), the pause menu with its
  "Exit Game?" confirmation and the "Give Up?" confirmation (`game_dialogs.hpp`), pearl letter balls
  that flip in one by one and slide at the decompiled speeds, and the round-end feedback: reveal,
  next-round sparkle, GAME OVER and new-high-score dialogs. Spec: `game-scene.md` §4–§7.
- Lightning (`blitz_scene.hpp`, spec `lightning.md`): the 80.3 s clock, automatic hints and bingo-only
  scoring.
- Word of the Day (`game_scene.hpp`, spec `game-scene.md` §8): the daily rack from the original's seed and
  day count, one round per day, and the greyed-out menu button until the next day.
- Crossword (`crossword_scene.hpp`, spec `crossword.md`): the 18×11 grid with its zoom, pan and turn, the
  locked crossing letters, and save / resume.
- Settings, high scores, trophy room, credits and the port's own IMPROVEMENTS dialog (window modes, a
  Super-xBR upscaler, side bars on wide screens).
- Android port (0.6.0): the same C++ build inside a Gradle app, with a ZIP importer for the player's own
  install. CI builds the AppImage, the Windows zip and a signed APK.
- The open dictionaries (`data/open-*.tree`, ENABLE2k) now keep slurs and vulgar words out of the
  racks and answer slots, while still accepting them as typed guesses (`docs/dictionary-sources.md`).

## Future Goals

1. Widescreen support. Done: generated side bars and the Borderless / Native window modes
2. Gamepad support (SDL3 has the API for it)
3. User will be able to use custom dictionaries, for fun! Partly there: `tt2-tree encode` turns any word list
   into a `.tree`, and the game loads `data/common.tree` / `data/full.tree` in place of the open ones
4. Automatically make a build for Linux using GitHub Actions. Done: every `v*` tag publishes the AppImage
   and the Windows zip
5. Being able to play the game on a Web Browser
