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
| `COMMENTARY` | `COMMENTS` 189×648, 9 frames | (316, 106), alpha 0 | never shown, see §5.1 |
| `GAME_OVER` | `GAME_OVER` 234×116, 2 frames | (291, 264), alpha 0 | never shown, see §5.1 |
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
- `0x430800` (one word, `ev_43dda104…`): per letter an `ANAGRAM_TILE` sprite (20×40 sheet,
  frame 0 white, frame 1 yellow) and a `SMALL_LETTERS` sprite (`anagram_text.png`, 9 columns
  × 27 frames of 20×20; frame = letter index + 1, frame 0 blank) at `x = i * tile_width`
  (20), both anchor 4 (centre). The letter starts hidden; the slot flag `+0x174` starts at 0.
  The Anagram node never loads `SMALL_LETTERS_UNGUESSED`.
- Word-appear flip `0x4310f0(slot, together)` (CONFIRMED, asm `ev_e07c73dd…`): per letter
  `i` one chained sequence, each step a child of the previous one:
  1. `ModifierDelay` on the letter: `i * 100` ms (`0x4316c4` adds `0x64` per letter unless
     `together` is set, which keeps every delay at 0).
  2. `ModifierScale` on the tile, 150 ms (`0x96`), from `(1, 1)` to `(1, 0)`: the height
     squashes to nothing. Easing `0x4307e0` is `FLD [esp+4]; RET`, i.e. linear
     (`ev_6cdd1edc…`).
  3. Only when `+0x174 == 1`: `ModifierFrame` sets the tile to frame 1 (yellow).
  4. `ModifierFrame` sets the letter frame (`char - 0x60`; 27 for indices listed at
     `+0x1bc`, unused in Classic).
  5. Children of step 4, in parallel: tile scale `(1, 0)` → `(1, 1)` and letter scale
     `(1, 0)` → `(1, 1)`, 150 ms each, and `ModifierVisible(1)` on the letter.

  Callers: a guess (`0x432b20`, `push 0` at `0x432aa8`), so found words flip in letter by
  letter on **white** tiles; the round-end reveal `0x4333a0` (`ev_882be30c…`), which sets
  `+0x174 = 1` on every unfound slot and passes 0, so missed words flip in on **yellow**
  tiles; the saved-game restore `0x433090` (from `0x42aa56`, `push 1` at `0x43312b`), so
  restored words flip with all letters at once. Worked example: a 5-letter word settles
  after `4 * 100 + 150 + 150 = 700` ms; a restored one after 300 ms. The flip freezes with
  the scene while the pause menu is up.

Implementation: `layout_anagram_table()` in `game_scene.hpp`, tested in
`ui/tests/test_game_scene.cpp` with the 10-word and 48-word cases; the flip is
`anagram_tile_at()` / `SlotView::tile()`.

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
- `0x43ab90` (container ctor, vtable `LetterBox`): for each of the `n` cells it adds a
  `LetterBox` sprite (`letterBox.png` 69×138, `separateframes`, two 69×69 frames stacked),
  centred at `(i * 69 + 34.5, 34.5)`, then calls the frame setter (`+0x90`) with the ctor's
  last argument, which is also stored at `+0x1fc`. `0x4396e0` passes `0` for the played row
  and `1` for the available row. So the played row shows **frame 0, the blue socket**, and
  the available row shows **frame 1, a small gold dot** that only shows once its ball has
  left. The ctor adds the box sprites as children before any ball, so the balls draw on top
  (CONFIRMED, `ev_e11281bbae6a2b25910dd49db8eac3c1aa06a218e26450545d930b77278ce6f0`,
  `ev_1d91c94d9aa7fe85e2dfc5bb04c4d07a2a1d0ceec059ce96e912902af7609a98`,
  `ev_44cb03845ebe64efc330368a5848148a7b2810fc4eeb8f8f76df83569b8d5538`). The same setter
  takes `n + 3` on the 12-frame `IMAGE_CROSSWORDHIGHLIGHT`, which is why `+0x90` reads as
  "set frame". Crossword rows use `XwordBox` instead, and the ctor also builds hidden
  `IMAGE_BLITZDOT`, `IMAGE_XWORD_OVERLAY` and `IMAGE_HINT_LETTERS` sprites for the other
  modes. `letterBox_shadow.png` has no `resources.xml` entry, so the game never loads it.
- Gold dot during the deal (USER-OBSERVED, not traced): in the original the available-row
  dot stays hidden while its ball is still flipping in, and shows only once that ball has
  settled. No toggle for it was found around `0x4396e0`, `0x43bad0` or `0x43ab90`, so the
  port hides the dot from its own `LetterBall::revealing()` state instead.
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
  The easing `0x4513d0` (a label, so it does not decompile) is **smoothstep**
  `f(t) = (3 − 2t)·t²`. The x87 code loads the constant `3.0` from `0x59b830`
  (CONFIRMED, `ev_631caed6b8f3ba4084a84f4bd86e615093e5078fc61254fa98e784e7245cecc0`,
  `ev_395892991af74c77cf8ae3b0eb9abf6d259d408e23d7ed0aee81a9916e4901aa`). Example: at
  `t = 0.25` the ball has covered `(3 − 0.5)·0.0625 = 0.156` of the way.
- `0x427560` (keyboard, CONFIRMED, `ev_ca4cee865aca52e6b92b32c907bf9135d1fec10b0af23c915e3839f3b4d1e2c0`):
  it switches on the engine key code; the table at `0x5d3228` maps the codes to characters
  (`ev_7aa26075a742e6fda2ca8ef23ca3a5bcb876caf467e7ee456aad4216c33859b2`). Code 1 (`0x08`,
  Backspace) takes back the last letter (`0x424ec0`); code 4 (`0x7f`, Delete) is CLEAR
  (`0x4245c0`); code `0x11` (`0x20`, Space) is TWIST (`0x425240`); any other key types a letter
  (`0x424d60`). Code 5 (Return) reads the played row (`0x43c0c0`): with letters in it, Return
  is ENTER (`0x424730`); with the row empty, Return is LAST WORD (`0x425000`). So pressing
  Return twice puts the word just submitted back in the played row, ready to extend. The
  ENTER *button* (`0x425b60`) calls `0x424730` directly, so with nothing typed it does nothing.
- `0x427680` (mouse): a click on an available ball plays it (`SOUND_letter_place`); a click
  on the **last** played ball takes it back (`SOUND_letter_clear`).
- `0x427a60` (mouse move, CONFIRMED): the scene's mouse interface (vtable `0x590a50`: key
  `0x427560`, move `0x427a60`, down `0x427680`) hit-tests every `LetterBall` in the
  `GFVector<LetterBall*>` that `0x4396e0` builds (`+0x10` items, `+0x14` count; the
  `+0x4c` hit test), in either row and with no phase check, then the found words
  (`0x433500(0)`). On a hit it only sets the hot flag `+0x90` and bumps the hand-cursor count
  at `0x5fdf00`. **Balls have no hover frame, scale or sound**; the pointer becomes the hand
  and nothing else changes (`ev_3fba3d2df5a1d4006777eddf2eb5d182fe27690c37b83af5b5b8c39317e7c467`,
  `ev_44cb03845ebe64efc330368a5848148a7b2810fc4eeb8f8f76df83569b8d5538`).
- Found-word lookup (CONFIRMED, `0x433500`): for a slot that shows a word under the pointer, `0x433500(1)` (a click, from `0x427680`) copies the slot word (`+0x19c`)
  and opens `http://dictionary.reference.com/browse/` (`0x58feec`) plus the word with
  `ShellExecuteA("open")`; `0x433500(0)` (a move) only reports the hit, so found words get
  the hand cursor too (`ev_b15a2994b5caf2ff82d32d9b73a95b311a65fd6b423ce6e08abcd9136ac20cd3`).
  This is what popup 10 ("click a found word to look it up") points at. `+0x1b0` is the length
  of the slot's word string (`+0x19c` + `0x14`, MSVC `std::string`); both fill paths, the
  guess `0x432b20` and the round-end reveal `0x4333a0`, write it through `0x431880` only
  when it is 0, so **revealed words are links too** (`ev_0610cba93e93f292786cc731a1077352610db5abdd9fe19a15c1c791b98ce2e0`,
  `ev_a82e114324b902b512e6fa7a54c0c77159385b31d1ed2095242830e7c01b0132`). The hit test
  `0x431740` asks each of the slot's tile sprites (`+0x190` items, `+0x194` count), so the
  link is exactly the word's 20×20 tiles (`ev_1ad1091b984c1710a0261de8a812dcfec1fcc42275c16d332e45481f3cbfb93d`).
  Port: `GameScene::shown_slot_at()` and `Services::open_url` (`SDL_OpenURL`; a headless run
  only logs the URL). The port acts on mouse release, like its balls; the original acts on
  press.
- Score label (CONFIRMED, `0x424730`): after a correct word the score `+0x308` goes through
  `0x449c90` (`sprintf("%d")`) straight into `LABEL_Score` (`+0x23c`, cached by `0x423a20`)
  via its set-text slot `+0xc4`. There is **no count-up**: the label jumps to the new total
  (`ev_e7f08e2c9eafb9f3ee109e9c87349a34776e6be1dafadff3ee385925c698ea1a`,
  `ev_488899f05a1dfe7dbf4db291b05209127fbc1f4dfe54b84d557d940a73f9db53`).
- Twist: INFERRED as a `std::random_shuffle` of the available balls with the game LCG
  (`0x43d950`); counted in `TRACKER/TWISTS`.

## 5. Round flow

- `0x425c50` (round start, CONFIRMED): `+0x300 = round`, `+0x2fc = 120000` (2:00),
  `+0x304 = 10000` (countdown warning); round 1 also resets the score, the miss counter
  (`+0x1e8`) and the per-game trophy counters. The rack comes from the player's bag
  (`gameplay-notes.md` §2); the board from §2 above; `SOUND_letter_load` plays.
- Timer (CONFIRMED, `gameplay-notes.md` §3): only Timed / WOTD tick; the last 10 s play
  `SOUND_game_clock_countdown` once per second; 0 plays `SOUND_game_clock_timeup`.
- Guess (CONFIRMED, ENTER `0x424730` on the `0x432850` return code,
  `ev_e7f08e2c9eafb9f3ee109e9c87349a34776e6be1dafadff3ee385925c698ea1a`): not a word →
  `SOUND_word_reject` + popup 0; duplicate → `SOUND_word_reject` + popup 3; valid but no free
  slot → popup 5, or popup 8 when the board has no slot of that length at all, and **no
  sound**; accepted → `SOUND_word_correct`, `+10·len²`; any bingo → `SOUND_word_bingo`, and
  the first one (round not yet qualified) adds `+1000`, qualifies and queues popup 2 (the
  `IMAGE_POPUP_BINGO` banner); all words → `SOUND_game_all_words_submitted`, `+100·slots`,
  popup 1 (the `IMAGE_POPUP_ALLWORDS` banner), round ends. Any other accepted word (a
  plain word or a repeat bingo) queues popup 10 (`image_popup_10`, "click entered words for
  their definition") while the scene flag `+0x1f0` is clear, then sets it. Round start
  (`0x425c50`, `ev_9459800f…`) never clears `+0x1f0`, so the hint shows once per scene. Where
  the original zeroes it is UNCONFIRMED; the port resets it with each new `GameScene` (each
  game start or resume). The port shows the hint for a 1:1 match even though it has no
  definition lookup yet.

### 5.1 Popups (CONFIRMED, `0x428ec0`)

`0x428e20(kind)` appends to a queue (`+0x334`); the scene update `0x426860` starts the next
one only when no popup is up (`+0x348`), so popups show **one at a time**. Round start
`0x425c50` clears the queue (`0x42ace0`). Kind 2 loads `IMAGE_POPUP_BINGO`, kind 1
`IMAGE_POPUP_ALLWORDS`, any other kind `"IMAGE_IMAGE_POPUP_" + kind`
(`image_popup_<kind>.png`): 0 not in the dictionary, 3 already guessed, 4 crossword
intersection, 5 enough words of that length, 6 extra time, 7 hint letter, 8 no words of
that length, 9 no unplayed word fits, 10 definitions, 11 word of the day found.
Evidence: `ev_fee25d6c7c52d1b7d3176d91079a74a1e4e5191c146c3dc125f1fbd6731b35c6`,
`ev_2a0a784f296abacbdd700854063bd701eee8a8dd23e7cda3b9e635e12f78f042`,
`ev_faa42e184823a6f1cb814bcda1ce22fb7171ed5f5d9f154bb2232d012abcaa66`.

The popup is centred (anchor 4) at `(400, 150)` (`0x423a20`: `+0x34c = [0x59b984]`,
`+0x350 = [0x59b980]`, `ev_79be952146c636bd4321e15aa343e208c6b3be945c4d64b9e56d243ba488c1ca`,
`ev_df42ad34f8cafc42f596ce90c4fcadccdcca4ebcffe3414eda24444b50b4e2e3`) and starts at alpha 0,
scale 0. Its modifiers are chained through `0x4506b0`, each one a child of the previous, so
they run in order (durations are passed in `EDX` / `ESI`):

| Step | Modifier | Value |
| --- | --- | --- |
| 1 | `ModifierAlpha` 0 → 1 | 150 ms (`0x96`) |
| 2 | `ModifierScale` 0 → 1.2 | 250 ms (`0xfa`), easing `0x4513d0` (smoothstep); 1.2 is `[0x59b998]` |
| 3 | `ModifierScale` 1.2 → 1 | 100 ms (`0x64`), linear |
| 4a | banners: `ModifierFrame` one pass | 50 ms per frame (`0x32`), then `ModifierDelay` 1000 ms |
| 4b | messages: `ModifierDelay` | 900 ms (`0x384`) |
| 5 | `ModifierAlpha` 1 → 0 | 250 ms (`0xfa`) |
| 6 | method `0x4235c0` | clears `+0x348`, so the next popup can start |

The frame-step reading (one pass, 50 ms) matches the ball flip `0x43a820`, which fills the
same `ModifierFrame` fields with 1 and 60 ms
(`ev_7a7dd1dff5b4343cd7ef6ecd83fae05dca724cab28fd8459c9bdf8b4215ada0e`). Worked example: a
message is up for `150 + 250 + 100 + 900 + 250 = 1650` ms; the 15-frame bingo banner for
`150 + 250 + 100 + 750 + 1000 + 250 = 2500` ms.

The `COMMENTARY` (`COMMENTS`, `tt_commentary.png`) and `GAME_OVER` (`tt_gameover.PNG`)
sprites in `dialogs.xml` stay at `alpha="0"`: the exe has no `COMMENTS`, `COMMENTARY` or
`GAME_OVER` string to look them up with
(`ev_cc4ffff180f91c369e5edfda823138752aa26b1778e8ceac1fcb9233ccbeadeb`,
`ev_7deed8d25288c6415fd9568bd9abbf9dba4de2b65e19d98d452a12dc2f8155cf`; bounded to Ghidra's
defined strings). They are leftovers from the first game (`docs/design/original-look-brief.md`
§7), and the port draws neither.
- End of round: `0x428840` (all words) / `0x425420` (time up without a bingo): the missing
  words are revealed (`SOUND_word_pass_reveal`, `ModifierDelay(3000)`); a miss increments
  `+0x1e8` and plays `SOUND_game_lose_life`; the **third** miss marks the game over
  (`+0x260 = 1`, then `0x428c80(…, 1)`, which still reveals the words with
  `SOUND_word_pass_reveal` before game over `0x427d10`). Every reveal holds **3000 ms**.
- Pass / Give Up (CONFIRMED, REA batch B). The scene has one button spot at (317, 473)
  shared by NEXT ROUND, `BUTTON_Pass_2`, `BUTTON_Pass` and GIVE UP. The refresh `0x425710`
  shows exactly one of them outside Word of the Day:

  | State | Visible button |
  | --- | --- |
  | qualified (bingo found) | NEXT ROUND |
  | misses `+0x1e8` = 0 | `BUTTON_Pass_2` |
  | misses = 1 | `BUTTON_Pass` |
  | misses = 2 | GIVE UP |

  The dispatcher `0x425b60` sends Pass and Give Up to state 4. State 4 asks "PASS?"
  (`TEXT_DIALOG_PASS_HEADER` / `TEXT_DIALOG_PASS`) when `misses + 1 < 3`, otherwise "GIVE
  UP?" (`TEXT_DIALOG_GIVEUP_*`). YES (`0x428610` → `0x4253a0`) calls `0x425420(0)`:
  misses++, `SOUND_game_lose_life`. With misses < 3 the missing words are revealed
  (`SOUND_word_pass_reveal`, 3000 ms) and the next round starts (`0x425190`). On the
  third miss the save is deleted (`0x461840`) and `0x428c80(…, 1)` reveals the words
  (`0x4333a0`, `SOUND_word_pass_reveal`), waits 3000 ms and ends the game (`0x427d10`).
  REA batch E corrected an earlier "no reveal" note here
  (`ev_45045f11e6ba0aacf03c0fa5d768bd5f814fc325e3ef4d622269e266d77e708e`). NO (`0x428720`) resumes. Worked example: pass, pass, give up = misses
  1, 2, 3 → game over on the give up.
  Evidence: `ev_52333333f7d1d525a504b153b09bd1bfa66307e5ca52024a9d0472e70a683fb5`,
  `ev_104f9575430ab658e77ee43a6e339523b1acea4ee9872327d379a15d6fb3603f`,
  `ev_1e2c943eb1fd9e777628f98b61f09d7e3a266dcf9a80b15b9efb2db1895a6c4e`,
  `ev_e8a263cb4fe7f6b863b5487d9ecacf1cf315903d63832b732ccaa95b0e6200fc`,
  `ev_f5625215aea38c76cfd0a11f37c1024737f1adef063e566f82fe909c0346064b`.
- Scene states (`+0x20c`, run by `0x426860`, jump table `0x427058`, CONFIRMED): 1 and 2
  open `DIALOG_MENU`; 3 is round end (outside WOTD it plays
  `SOUND_game_next_round_acheived` and opens `DIALOG_NEXTROUND`); 4 is the Pass / Give Up
  question above; 5 opens `DIALOG_TUTORIAL`, which round start `0x425c50` selects on a
  player's first game (`ev_48575bcd821545fb8381f5848f6747d5854fdb32bc35c8f13f433c9eb23b69d8`);
  6 opens `DIALOG_PASSROUND`.
- `DIALOG_PASSROUND` is Timed only (CONFIRMED, REA batch B2). After the switch, the
  per-frame path at `0x426fc7` checks the mode against `CLASSIC_TIMED`; when the round's
  `[+0x1f4]+0x88` flag is clear it calls `0x425420(1)`. That is the time-up miss: reveal
  for 3000 ms, then `0x428a80` (`mov byte [ecx+0x2f9], 0; mov [ecx+0x20c], 6`) opens
  "Round Passed". The Untimed Pass uses `0x425420(0)` and goes straight to the next round.
  Evidence: `ev_1db2070b1f8957586c818109a077def962b1bde2347201f67832bdd159ab78a4`,
  `ev_145f168ef700c17cdca70da2f3fd7f8f846a5d95bb583403c0af92197781be55`,
  `ev_2d7452782ce28eaa417c1f96e2e9313a8beeb47f5695f60eeb2ebc9b140df993`,
  `ev_faa42e184823a6f1cb814bcda1ce22fb7171ed5f5d9f154bb2232d012abcaa66`.
- After the reveal (CONFIRMED): qualified → `DIALOG_NEXTROUND`; a miss with lives left →
  the next round starts by itself (`0x425190`); game over → `0x427d10` submits the score
  and opens `DIALOG_CONGRATS` when it ranks, else `DIALOG_ENDGAME`. Game over deletes the
  saved game (`0x427cc0`).
- NEXT ROUND (CONFIRMED, `0x425b60`) calls `0x428840(3000)`, which ends the round early.
- INFERRED: the clock labels are hidden in Untimed.
- Our port follows this table (plan phase 7): `BUTTON_Pass_2` (image `BUTTON_PASS2`,
  "PASSES: 2"), `BUTTON_Pass` and GIVE UP all count a miss through `RoundEnd::Passed`.
- High scores: `HighScoreManager::submit(mode, name, score, round)` on game over
  (`SOUND_game_high_score_achieved` when the entry makes the table), saved next to the
  profiles. Progress counters (`TRACKER/SOLVED`, `HARD_SOLVED`, `TWISTS`, rounds per mode)
  and the bag state are written back to the player profile after every change.

## 6. Dialogs (dialogs.xml, CONFIRMED unless marked)

All are `autocenter` over a 0.7 black backdrop (`DIALOG_BACKGROUND_ALPHA`).

| Dialog | Size | Contents |
| --- | --- | --- |
| `DIALOG_MENU` (pause) | 322×390 | `PAUSE_HEADER` "PAUSE MENU" at (163, 19); 181×208 buttons HOW TO PLAY (162, 87), SETTINGS (163, 155), STOP (163, 224), RESUME (163, 290) |
| `DIALOG_YESNO` | 400×180 min | `HEADER`, `MESSAGE` (`FONT_SETTINGSTEXT`, `width="390"`, `wrapstyle="wordwrap"`, line height 30); `0x40b470` moves YES / NO down by the message height and grows the **height** only |
| `DIALOG_NEXTROUND` | 380×320 | `IMAGE_BURST` sparkle (frametime 40, `#FFA800`, additive) at (191, 154); "Click to continue to the next round!"; OK at (190, 239) |
| `DIALOG_PASSROUND` | 380×280 | `TEXT_PASS_ROUND_TITLE` "Round Passed" (191, 16); `TEXT_PASS_ROUND_BODY` (192, 85); OK (189, 199) |
| `DIALOG_ENDGAME` | 380×260 | "GAME OVER" (190, 14); body (192, 88), line height 38; PLAY AGAIN (271, 173), MENU (109, 173) |
| `DIALOG_CONGRATS` | 380×320 | sparkle frame 4 at (191, 138); "Congratulations! You earned a new high score!" (193, 107) |

- `0x407f40` wires the pause menu. STOP opens `0x408320`: "Exit Game?" / "Are you sure you
  want to leave this game? Your game will be saved for next time." YES saves and leaves.
- `0x428b60` event `0x206` opens `DIALOG_MENU` and touches no sound or stream
  (`ev_761416f061c3d27fa5dbec9fe2fa719dfc783be3dcaa2821d1000c69b0284d43`); that the event is
  the window losing focus is INFERRED.
- **The pause menu leaves the music playing** (CONFIRMED, REA batch A):

  | Fact | Evidence |
  | --- | --- |
  | `DialogMenu` ctor `0x407f40` only wires the four buttons (HOW TO PLAY `0x408180`, SETTINGS `0x408250`, STOP `0x408320`, RESUME `0x408450`) | `ev_2a3a8994bcd25b92dbc965652fad45a654776d8ac2639b51fceb5c149c9ca61c` |
  | `DialogPause` (vtable `0x58eb6c`) overrides show (slot 44, `0x58ec1c`) with `0x409fa0`: `SOUND_PAUSE`, then `0x463f30(stream, 0)` silences the music | `ev_9c6afbad8e1644ab010630304d5872b82c9a608b7d67ba71c56f09e1a0ff50ce`, `ev_2d01cac98c38343f75de21ae5ec2a5584c9027eea792c190a57c3b498a8345e7` |
  | `DialogMenu` (vtable `0x58e2d4`) slot 44 is `0x40aa30`, which only calls the base show `0x40cba0` + `0x40ccc0` — no stream call | `ev_2f0422e109e808814453698910e79e374d781e3604c08e1dbf14fc920b730cd2`, `ev_949ede650c9f4be84039f4f0ae1f1427babecc03b9a2d15dd0083bb4176a8910` |
  | The restore `0x40a1f0` is referenced only from `DialogPause` code (`0x409f06`, `0x40a1d9`) | `ev_bd81e5dd7c4a468adbe11af004c45d7d9dd744eea34ac49a0f8e3695566e7898` |
  | Base show `0x40cba0` plays `SOUND_DIALOG_SHOW` when the dialog's `+0x1d8` flag is set; `0x40ccc0` fades the dialog in over `DIALOG_FADE_TIME`. Which dialogs set the flag is still untraced | `ev_d7d36e48c0f87c322b1186f9dc675605b267f0e5c54be1422130376ec3ec6534`, `ev_09e7db4e965c6ce1803195520b13466e61363f581b31c460a4dfae899e755586` |

- Buttons never play a hover sound (welcome-flow.md §3, button parser `0x48d390`).
- Label wrapping (CONFIRMED, `0x468ea0`,
  `ev_249d5e89a1ce5f847e34c5b0aa1043fb9534141758fc4c51af324e447919fb3d`): `wrapstyle` is
  stored at label `+0x20c` as `autowidth` = 0, `wordwrap` = 1, `truncate` = 2; `alignh` at
  `+0x208` as `left` = 0, `center` = 1, `right` = 2. So a long yes/no message wraps at
  390 px; our renderer drawing it on one line is a bug (plan phase 4).
- Word-wrap rule (CONFIRMED, REA batch B2). setText (`0x4698c0`, Label vtable `0x595df4`
  slot `+0xb0`) relayouts through `0x469a40`, a tokenizer: `\n`, `\r`, U+2028 and U+2029
  end the line; space, tab, NBSP, U+2000–U+200B, U+202F, U+205F and U+3000 end a word;
  `[style=…]` tags switch the style inline. Each finished word goes through `0x46a7b0`.
  With `wrapstyle` = 1 the word stays on the line when `x + wordWidth ≤ labelWidth`; else,
  when the line already has text and the word fits an empty line, the line breaks
  (`0x46a200`) and the word starts at `x = 0`; else the word is **dropped** (its run gets
  an empty string). The space advance after a word is added only when `x ≠ 0`, so a new
  line never starts with a space. Worked example at width 390: a line holding 300 px of
  text takes a 60 px word (`300 + 60 = 360 ≤ 390`) but breaks before a 100 px word
  (`400 > 390`). Evidence:
  `ev_9e5e892d09d11f7a2d0cd3888e01526982aad04c610aca57260a98466eb95fc7`,
  `ev_d519c62875e45b5f845329140c3439dd940ffaa22b74e9eace375be892159568`,
  `ev_b1d5061f07f9fffbc4895b1a4e76445b1d748023e05b0db173aaba82933f6918`,
  `ev_1abdcf6ac92d4139a100fd3b55d007eb07be3b8a2732bcd5700be5780a1b16a9`.
- `DIALOG_NEXTROUND` letterballs (CONFIRMED, `0x409100`,
  `ev_6a53b49b495b8b239bf5779bcd7e6fa734afd428cc313d95f38117b7752c2c2f`): the balls spell
  `TEXT_NEXT_ROUND_LETTERBALLS` ("Great Job"). Each non-space character `i` becomes a
  `LetterBall` on the next sprite slot `"1"`..`"8"`, so GREAT fills the top arc (1–5) and
  JOB the lower row (6–8). Ball `i` appears after `500 + 200·i` ms; the space still counts,
  so G = 500 ms, T = 1300 ms, J = 1700 ms, B = 2100 ms. Every ball also bursts 8
  `IMAGE_PARTICLEBALL` particles: random angle, distance `75 + rand % 50` px, alpha fade
  over `600 + rand % 90` ms, linear easing. Each ball runs the round-start flip `0x43a820`
  with its flag argument 1, so `SOUND_letter_load` (`0x43a940`) plays as each one lands;
  like the rack, a waiting ball shows its first (blank) frame. UNCONFIRMED: the particles'
  travel time and delay (two stack values the decompiler leaves uninitialised). Slot
  positions:

  | Slot | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | (x, y) | (60, 34) | (124, 3) | (192, −8) | (259, 1) | (321, 34) | (126, 75) | (192, 61) | (257, 74) |

- HOW TO PLAY and SETTINGS are decoded in `help-and-settings.md`.
- Not implemented: the next-round particle burst and `DialogPause` (`0x409e50`, key `p`,
  which clashes with typing).

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
| `0x43e070` | Recursive `.tree` node reader (letters, `0`/`1`, children); anchor check 2026-10-14 `ev_b0fd200dbf3523b14f18941871fe88b17c0aed2eac8ed5f6991650daf2055e70` |
| `0x425c50` | Round start: timer / round / score reset, rack pick, `AnagramTable` ctor |
| `0x426360` | After setup: timer label text, panel pieces fitted to the grid |
| `0x424730` | ENTER: guess, sounds, score, popup kind per result |
| `0x428e20` / `0x428e60` | Popup queue push / pop (`+0x334`) |
| `0x428ec0` | Popup: sprite by kind at (400, 150), fade / scale / hold / fade chain |
| `0x427680` | Mouse down on the letter rows (place / take back) |
| `0x431e50` | `AnagramTable` layout (columns / rows) |
| `0x430800` | One word of the grid (tiles + small letters) |
| `0x4396e0` | `AnagramTable` ctor: grid position, two `LetterballContainer`s, balls |
| `0x43ab90` | `LetterBox` ctor (the letter row container): cell sprites, frame by row |
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
| `0x426860` / `0x427058` | Scene state machine (`+0x20c`) / its jump table |
| `0x425b60` | Button dispatcher (Menu, NextRound, Pass / GiveUp, Clear, Enter, LastWord, Twist) |
| `0x425710` | Button refresh: NEXT ROUND / Pass_2 / Pass / GIVE UP |
| `0x4253a0` / `0x425420` | Pass / Give Up YES / miss path (reveal or game over) |
| `0x409100` | `DIALOG_NEXTROUND` letterballs and particles |
| `0x4513d0` | Smoothstep easing `(3 − 2t)·t²` |
| `0x468ea0` | Label XML parser (`wrapstyle`, `alignh`, `autofitheight`) |
| `0x468b20` / `0x595df4` | Label ctor / Label vtable |
| `0x4698c0` / `0x469a40` / `0x46a7b0` | Label setText / layout tokenizer / word flush with the wrap rule |
| `0x428a80` | Sets state 6 (`DIALOG_PASSROUND`) after a Timed time-up miss |
