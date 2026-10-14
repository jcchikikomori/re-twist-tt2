# Classic game scene (Untimed / Timed) — layout and flow

Evidence for `ui/include/tt2ui/game_scene.hpp`. Target: the installed `TextTwist2.exe`
(SHA-256 `0c6e962c…`), decompiled with Ghidra 12.1.4 through REA on 2026-10-13.
Labels: **CONFIRMED** = read in the decompiled code or the shipped data, **INFERRED** =
most likely reading, not fully traced.

The scene is `TextTwist2Scene` (one class for Untimed, Timed and Word of the Day). Its
components come from `properties/dialogs.xml` (`SCENE_GAME`), the images from
`properties/resources.xml` (section `gamescene`), and the geometry that the XML does not
carry is hard-coded in the exe. The `LETTERS_*` / `ANAGRAM_Height` / `ANAGRAM_LetterSpacing`
/ `ANAGRAM_GroupGap` values in `properties/default.xml` are **not referenced by the exe**
(the strings do not occur in the binary); only `ANAGRAM_ColumnGap` is read.

## 1. Screen components (dialogs.xml, CONFIRMED)

| Component | Image / font | Position | Notes |
| --- | --- | --- | --- |
| `UI_BAR` | `IMAGE_UI_BAR` 800×54 | (0, 547) | |
| `SCORE_LABEL` "SCORE:" | `FONT_UI_LABELS` | x 244, y 574, right-aligned | |
| `LABEL_Score` | `FONT_UI_NUMBERS` | x 251, y 574, left | |
| `LABEL_ROUND (copy)` "ROUND:" | `FONT_UI_LABELS` | x 537, right | |
| `LABEL_ROUND` | `FONT_UI_NUMBERS` | x 541, left | |
| `TIME_LABEL` "TIME:" | `FONT_UI_LABELS` | x 707, right | |
| `LABEL_TimerMinutes` "2:" | `FONT_UI_NUMBERS` | x 749, right | text `"<m>:"` (0x426360) |
| `LABEL_TimerSeconds` "00" | `FONT_UI_NUMBERS` | x 752, left | text `"%02d"` (0x426360) |
| `BUTTON_Menu` | `BUTTON_MENU` 119×200 (4 rows) | (10, 554) | |
| `BUTTON_Twist` / `Clear` / `LastWord` / `Enter` | 119×200 sheets | y 407, x 147 / 273 / 399 / 525 | |
| `BUTTON_NextRound` / `GiveUp` / `Pass` / `Pass_2` / `WOTD_DONE` | 157×208 sheets | (317, 473) | all share one spot |
| `COMMENTARY` | `COMMENTS` 189×648, 9 frames | (316, 106), alpha 0 | see §5 |
| `GAME_OVER` | `GAME_OVER` 234×116, 2 frames | (291, 264), alpha 0 | frame 0 "GAME OVER" |
| `PANEL_*` | `panel_*.png` | re-positioned at runtime | §3 |
| `ANAGRAMS` | code component | re-positioned at runtime | §2 |

Backgrounds: `BACKGROUND` (untimed), `BACKGROUND_TIMED`, `BACKGROUND_WOTD`, all 800×600.

## 2. Word grid ("AnagramTable", CONFIRMED)

- `0x4396e0` (table ctor): the grid node is placed at `x = 400 - width/2`, `y = 15`
  (`0x41700000`); the dialogs.xml `x="100" y="75"` is overridden.
- `0x431e50` (layout): with `n` slots,
  `columns = ceil(n / 8)`, clamped so that `columns < 5 → 4`;
  `rows = n / columns` (float); each column takes `floor(rows)` words and the fractional
  remainder accumulates — a column gets one extra word when the accumulator reaches 1.0
  (a `0.999` epsilon rounds the last step). Words are consumed in board order (length
  3..8, DFS order inside a length). Row pitch = tile height (20). A new column starts at
  `x += ANAGRAM_ColumnGap (3) + longest * 20 + 20`, where `longest` is the running maximum
  word length placed so far (starts at 3). The node records its width (`+0x1c4`, right
  edge of the widest column) and height (`+0x1c8`).
- `0x430800` (one word): per letter an `ANAGRAM_TILE` sprite (20×40 sheet, frame 0 empty,
  frame 1 found) and a `SMALL_LETTERS` sprite (`anagram_text.png`, 9 columns × 27 frames of
  20×20; frame = letter index + 1, frame 0 blank) at `x = i * tile_width` (20). Both start
  hidden. `SMALL_LETTERS_UNGUESSED` (`anagram_letters_revealed.png`, yellow tile with the
  letter) is the sheet for words revealed at the end of a lost round.

Implementation: `layout_anagram_table()` in `game_scene.hpp`, tested in
`ui/tests/test_game_scene.cpp` with the 10-word and 48-word cases.

## 3. Panel (CONFIRMED, 0x426360)

After the grid is built the panel pieces are fitted to it (`tx, ty, tw, th` = table rect):
`PANEL_LEFT` at `(tx - 10, 0)` stretched to height `ty + th`; `PANEL_LEFTCORNER` at
`(tx - 10, ty + th)`; `PANEL_RIGHT` at `(tx + tw, 0)`, same height; `PANEL_RIGHTCORNER` at
`(tx + tw, ty + th)`; `PANEL_BOTTOM` at `(tx, ty + th)` scaled to width `tw`; `PANEL_BG`
(800×195, anchored top-centre at x 400) scaled by `tw / 800` horizontally and
`(ty + th) / 195` vertically.

## 4. Letter rows ("LetterballContainer", CONFIRMED)

- `0x4396e0`: two containers; the **available** row at `y = 295` (`0x43938000`) and the
  **played** row at `y = 213` (`0x43550000`); both at `x = 400 - n * 69 / 2` for a rack of
  `n` letters. Every ball starts in the available row.
- `0x43bad0` / `0x43c370`: inside a container a ball sits at `(i * 69 + 34.5, 34.5)`; with
  index `-1` a ball goes to the **first empty cell** of the row.
- `0x439d00` (`LetterBall`): sprite `IMAGE_LETTERBALL_ANIM` (`letterball_anim.png`
  1020×1560, 17 columns × 26 rows of 60×60); the row is the letter, frame
  `(letter - 'a') * 17`. The ctor also builds an `IMAGE_LETTERBALL_50` / `_40` sprite and
  an `IMAGE_CHICKLET_SHEEN` overlay; the Classic scene does not show them (not drawn here).
- Round-start flip (CONFIRMED, `0x43c940` → `0x43a820`): ball `i` waits `i * 120` ms, then a
  `ModifierFrame` runs frames `first .. first + 11` at **60 ms** per frame and plays
  `SOUND_letter_load` when it ends (`0x43a940`). The ball **rests on frame 11**, the round
  pearl; frames 12–16 morph it into the square chicklet and are not used by this scene.
- Move (CONFIRMED, `0x43a4c0`): into a container a ball slides with `ModifierLocationLine`
  in **300 ms** (plus a `ModifierScale`); the twist relayout (`0x43c370`) uses **400 ms**.
  The easing function (label `0x4513d0`) does not decompile; linear is INFERRED.
- `0x427680` (mouse): a click on an available ball plays it (`SOUND_letter_place`); a click
  on the **last** played ball takes it back (`SOUND_letter_clear`).
- Twist: INFERRED as a `std::random_shuffle` of the available balls with the game LCG
  (`0x43d950`); counted in `TRACKER/TWISTS`.

## 5. Round flow

- `0x425c50` (round start, CONFIRMED): `+0x300 = round`, `+0x2fc = 120000` (2:00),
  `+0x304 = 10000` (countdown warning); round 1 also resets the score, the miss counter
  (`+0x1e8`) and the per-game trophy counters. The rack comes from the player's bag
  (`gameplay-notes.md` §2); the board from §2 above; `SOUND_letter_load` plays.
- Timer (CONFIRMED, `gameplay-notes.md` §3): only Timed / WOTD tick; the last 10 s play
  `SOUND_game_clock_countdown` once per second; 0 plays `SOUND_game_clock_timeup`.
- Guess (CONFIRMED return codes, `0x432850`): not a word → `SOUND_word_reject` + commentary
  frame 0; duplicate → frame 3; valid but no free slot → frame 5 (frame 8 when the board has
  no slot of that length at all); accepted → `SOUND_word_correct`, `+10·len²`; first bingo
  → `SOUND_word_bingo`, `+1000`, qualified, `IMAGE_POPUP_BINGO` (380×140, 15 frames) and
  commentary frame 2; all words → `SOUND_game_all_words_submitted`, `+100·slots`,
  `IMAGE_POPUP_ALLWORDS` (18 frames), commentary frame 1, round ends.
  `tt_commentary.png` rows: 0 "not one of the listed words", 1 "all the words", 2 "longest
  word / next round", 3 "already guessed", 4 crossword intersection, 5 "enough words of
  that length", 6 "correct, extra time" (Lightning), 7 "only the hinted letter" (Lightning),
  8 "don't need any words of that length".
- End of round: `0x428840` (all words) / `0x425420` (time up without a bingo): the missing
  words are revealed (`SOUND_word_pass_reveal`, `ModifierDelay(3000)`); a miss increments
  `+0x1e8` and plays `SOUND_game_lose_life`; the **third** miss marks the game over
  (`+0x260 = 1`, high-score save `0x428c80`). Every reveal holds **3000 ms**. Give Up asks
  first (`0x425b60` sets `+0x20c = 4`; `0x426860` case 4 opens the "Give Up?" yes/no).
- After the reveal (CONFIRMED): qualified → `DIALOG_NEXTROUND`; a miss with lives left →
  the next round starts by itself (`0x425190`); game over → `0x427d10` submits the score
  and opens `DIALOG_CONGRATS` when it ranks, else `DIALOG_ENDGAME`. Game over deletes the
  saved game (`0x427cc0`).
- INFERRED: NEXT ROUND / GIVE UP share one spot and swap on the qualified flag; pressing
  NEXT ROUND while qualified ends the round early; the clock labels are hidden in Untimed;
  the GAME OVER sprite fades in over 500 ms and a click returns to the menu; the
  commentary fades 250 ms in, holds 2 s, fades 250 ms out; popups run one pass at 60 ms per
  frame. The Pass buttons (`BUTTON_Pass`, `BUTTON_Pass_2`, `+0x20c = 5` on first play) are
  not implemented yet.
- High scores: `HighScoreManager::submit(mode, name, score, round)` on game over
  (`SOUND_game_high_score_achieved` when the entry makes the table), saved next to the
  profiles. Progress counters (`TRACKER/SOLVED`, `HARD_SOLVED`, `TWISTS`, rounds per mode)
  and the bag state are written back to the player profile after every change.

## 6. Dialogs (dialogs.xml, CONFIRMED unless marked)

All are `autocenter` over a 0.7 black backdrop (`DIALOG_BACKGROUND_ALPHA`).

| Dialog | Size | Contents |
| --- | --- | --- |
| `DIALOG_MENU` (pause) | 322×390 | `PAUSE_HEADER` "PAUSE MENU" at (163, 19); 181×208 buttons HOW TO PLAY (162, 87), SETTINGS (163, 155), STOP (163, 224), RESUME (163, 290) |
| `DIALOG_YESNO` | 400×180 min | `HEADER`, `MESSAGE` (line height 30); `0x40b470` moves YES / NO down by the message height and grows the **height** only |
| `DIALOG_NEXTROUND` | 380×320 | `IMAGE_BURST` sparkle (frametime 40, `#FFA800`, additive) at (191, 154); "Click to continue to the next round!"; OK at (190, 239) |
| `DIALOG_ENDGAME` | 380×260 | "GAME OVER" (190, 14); body (192, 88), line height 38; PLAY AGAIN (271, 173), MENU (109, 173) |
| `DIALOG_CONGRATS` | 380×320 | sparkle frame 4 at (191, 138); "Congratulations! You earned a new high score!" (193, 107) |

- `0x407f40` wires the pause menu. STOP opens `0x408320`: "Exit Game?" / "Are you sure you
  want to leave this game? Your game will be saved for next time." YES saves and leaves.
- `0x428b60` event `0x206` opens the pause menu; INFERRED to be the window losing focus.
- Not implemented: HOW TO PLAY / SETTINGS (they show "Not available"), the eight
  letterball sprites of `DIALOG_NEXTROUND`, `DialogPause` (`0x409e50`, key `p`, which
  clashes with typing), and the Pass dialog.

## 7. Saved games (CONFIRMED)

- `PlayerManager` (`0x460900`) keeps a `savegames` folder next to `players`. On scene start
  (`0x4242a0`) a save for the mode (`CLASSIC_TIMED`, `CLASSIC_UNTIMED`, `WORD_OF_THE_DAY`)
  is loaded with `0x42a460`; otherwise a fresh game starts (`0x425c50`).
- The menu asks first (`0x42dc80`): "Continue the game?" / "Do you want to continue your
  last played game? (Select 'No' to start again.)". NO deletes the save.
- Save (`0x429dd0`), called when the scene is left mid-round (`0x427b20`, only when
  `+0x264 == 0`): `WORD`, `ROUND`, `TIME`, `LONGESTWORD` (the qualified flag), `SCORE`,
  `DAY`, `FAILCOUNT` and a `WORD_LIST` of the found words (`0x432cb0`).
- Load rebuilds round 1 from `WORD`, overwrites the counters, places every listed word
  again without scoring (`0x433090` → `0x432b20`), then **deletes the file** (`0x461840`).
  A Word of the Day save from another `DAY` starts a new game.
- Our file is plain text (`tt2/saved_game.hpp`), one per player and mode, at
  `savegames/<player>/<MODE>.txt`; the original binary format is not reproduced.

## 8. Address index

| Address | What |
| --- | --- |
| `0x423a20` | `TextTwist2Scene` ctor: looks up the components, loads both `.tree` files |
| `0x425c50` | Round start: timer / round / score reset, rack pick, `AnagramTable` ctor |
| `0x426360` | After setup: timer label text, panel pieces fitted to the grid |
| `0x427680` | Mouse down on the letter rows (place / take back) |
| `0x431e50` | `AnagramTable` layout (columns / rows) |
| `0x430800` | One word of the grid (tiles + small letters) |
| `0x4396e0` | `AnagramTable` ctor: grid position, two `LetterballContainer`s, balls |
| `0x439d00` | `LetterBall` ctor: `IMAGE_LETTERBALL_ANIM` frame = letter × 17 |
| `0x43bad0` | Put a ball into a container cell (first free cell for index −1) |
| `0x43c370` / `0x43c940` | Container relayout / reveal animation |
| `0x43a820` / `0x43a940` | Ball flip (12 frames, 60 ms) / its `SOUND_letter_load` |
| `0x43a4c0` | Ball move: 300 ms into a container, 400 ms on relayout |
| `0x407f40` / `0x408320` | Pause menu ctor / "Exit Game?" confirmation |
| `0x40b470` | Yes/no dialog: buttons below the message, height grows |
| `0x4242a0` / `0x42dc80` | Scene start (load or new game) / "Continue the game?" prompt |
| `0x429dd0` / `0x42a460` | Save / load the running game |
| `0x427d10` | Game over: high score, then `DIALOG_CONGRATS` or `DIALOG_ENDGAME` |
