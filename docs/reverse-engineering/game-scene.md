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
| `LABEL_ROUND (copy)` "ROUND:" | `FONT_UI_LABELS` | x 537, right | no code touches it; shown in every mode (§8.10) |
| `LABEL_ROUND` | `FONT_UI_NUMBERS` | x 541, left | text = round number `+0x300` (0x426360); WOTD shows 1 (§8.10) |
| `TIME_LABEL` "TIME:" | `FONT_UI_LABELS` | x 707, right | hidden with the clock in Untimed only (0x426360, §8.10) |
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
  restored words flip with all letters at once.
- Which word a revealed slot shows (CONFIRMED, 2026-10-15): not its own answer. A guess
  fills the first empty slot of its length (`0x432b20`), so a later answer found early sits
  in an earlier slot. For each empty slot in order, `0x4333a0` calls `0x433320`
  (`ev_ea585423d1eae4512d66b8ee0122a1b0e550c9ef72d4f36ab3dc07789f8dbc8f`). It walks the
  board list (`+0x180`, `+0x184` entries of 0x1c bytes) for the first word of the slot's
  length (`+0x178`) that `0x433180`
  (`ev_ecdd7b2159c8408898edaa90a3c1bb82c29f8fb26604d3a9c636c0111aae29e6`) finds in no slot.
  `0x431880` (`ev_9a5628986878e0269af125f61084a14b43bd25c30ecde9f6f256b058d7afb0a5`)
  writes it to `+0x19c` at once, so later slots skip it. `+0x1b0` is that string's
  length (`0x4310f0` reads it as the letter count), so "empty slot" means `+0x1b0 == 0`.
  No word shows twice. Port: `tt2::reveal_words` (`board.hpp`); before 2026-10-15 the
  port revealed each slot's own answer and could show a found word twice. Worked example: a 5-letter word settles
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
- Round-start flip (CONFIRMED, `0x43c940` → `0x43a820`, asm
  `ev_9f22df41a5ed62aa5bf0d677398226019b913c49dc3ae323e413c9ad9593d8ef`): ball `i` waits
  `i * 120` ms (a `ModifierDelay`), then a `ModifierFrame` runs frames `first .. first + 11`
  at **60 ms** per frame. When the flag argument is 1 (the round start `0x425c50` pushes 1 at
  `0x426264`), a `ModifierMethod` calling `0x43a940` (`SOUND_letter_load`) is added under
  the **same** delay as the frames (`EDI` still holds the delay at `0x43a91e`), so the two run
  side by side: the sound plays as the flip **starts**, at `i * 120` ms, not when it ends.
  An earlier reading here said "when it ends", which put the last of 8 balls' sound
  `720 + 7 × 120 = 1560` ms late. The ball **rests on frame 11**, the round pearl; frames
  12–16 morph it into the square chicklet and are not used by this scene.
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
- `0x425000` (LAST WORD, CONFIRMED, `ev_ec1755c442f2e335bdc5ba491ef25668f8b5ff34a71bb6ab8c2b02321dc59c4a`):
  it plays `SOUND_word_last` **once** at full volume (`0x4634b0(1.0, 0, 1.0, 0)`), then, for
  each letter of the last word, finds an available ball (`0x43be50`) and moves it with
  `0x43bad0(1, -1, 1.0)`. `0x43bad0` plays no sound
  (`ev_e5a4433055832cdec9d43fd8150d632c77b7c0275dea9bba703d89fc1fa48da9`), and none of the six
  `SOUND_letter_place` references (`ev_fe370170576423088a978c2c168211ce1e9c348269268606147ece3379939264`)
  is inside `0x425000`, so a recall is one whoosh, not a click per letter. It ends with the
  button refresh `0x425710`. Its callers are the button dispatcher `0x425b60` and the Return
  key `0x427560`, which calls it with no last-word check, so Return on an empty row with nothing
  to recall still plays the whoosh. The LAST button is disabled then, so only the key does it.
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
  press. Deliberate deviation: the old `dictionary.reference.com` address only redirects
  now, so the port opens `https://www.dictionary.com/browse/<word>` directly.
- Letter buttons (CONFIRMED, listing of `0x425710`,
  `ev_da78430350f5563c2d366b5f0ab813b4ebe44f7cd3e87fa0b07d0ad8d7e134f7`). The refresh runs
  after ENTER and after every ball click. When `+0x260` (game over) or `+0x2f9` (a reveal
  is running) is set, all nine buttons are disabled. Otherwise MENU is on, and the rest
  follow the two rows: `0x43c0c0` reads the played row, `0x43c170` the available row.

  | Button | Enabled when | Address |
  | --- | --- | --- |
  | TWIST | more than one ball in the available row | `0x4257c3` (`cmp eax, 1; setg`) |
  | ENTER | at least one letter played | `0x425807` (`test; setg`) |
  | LAST WORD | nothing played and the round's last word is not empty | `0x42580f`–`0x42582e` |
  | CLEAR | at least one letter played | `0x425858` (`test; setg`) |

  So at a round start TWIST is the only one on. The last word is the string at `+0x64` of
  the round object (`+0x1f4`), which `0x4396e0` builds empty every round, so LAST starts
  off each round. Port deviation: ENTER waits for 3 letters (`ENTER_MIN_LETTERS`), because
  no board word is shorter; Return on the keyboard still submits any non-empty row
  (`0x427560`).
- Score label (CONFIRMED, `0x424730`): after a correct word the score `+0x308` goes through
  `0x449c90` (`sprintf("%d")`) straight into `LABEL_Score` (`+0x23c`, cached by `0x423a20`)
  via its set-text slot `+0xc4`. There is **no count-up**: the label jumps to the new total
  (`ev_e7f08e2c9eafb9f3ee109e9c87349a34776e6be1dafadff3ee385925c698ea1a`,
  `ev_488899f05a1dfe7dbf4db291b05209127fbc1f4dfe54b84d557d940a73f9db53`).
- Twist (CONFIRMED, `0x425240` → `0x43c370` → `0x43c280`,
  `ev_bfa0944e936a11f348afc9cdac9a277ab17e10857d8a5de693f0051c9eb97b3d`,
  `ev_292da96dc327f2dfd9d293bf89fe259e8daf6902abcaa53a56ccabe8f0584d9c`): the key (`0x427560`,
  key code `0x11`) and the button (`0x425b60`) both call `0x425240`, which plays
  `SOUND_word_twist` **first** and then asks `0x43c280` whether the available row may move.
  It may not when fewer than two letters are available, when every available letter is the
  same, or when **any available ball is still sliding**: `0x43a4c0` keeps the running move in
  the ball's `+0x184` and chains `0x43a2f0` after it, whose bytes are
  `MOV dword [ECX+0x184], 0` (`ev_3de15ad48fac8e2d6ad29d2fedb98d731b178ada69e4e0dad31d19f6ff146fea`).
  There is no timer: a held key (the key translator `0x484770` passes every `WM_KEYDOWN`
  repeat through) moves the rack once per finished 400 ms slide and sounds on every repeat.
  When allowed, `0x43c370` shuffles (`0x439370`) and shuffles again while the new order equals
  the order before or the rack word `table+0x1c` (CONFIRMED, §4.1-§4.2; this replaces an
  earlier INFERRED reading). The shuffle draws the CRT `_rand()`, not the game LCG (CONFIRMED,
  §4.1). Counted in `TRACKER/TWISTS` (the port counts only twists that move the rack).
  **Port deviation:** the port plays `SOUND_word_twist` only when the rack moves, so a held
  key doesn't click on every repeat while the balls slide.
- Sound playback (CONFIRMED, `0x4634b0`,
  `ev_d92c44a24f8b39b08f4a98e21e4f6a48caf6f2a9f1b520a78e3c8c24a3984158`): every `SOUND_*` is
  played on a **free voice** (`-1`), with volume `0..255`, pan `128 ± 127` and the frequency
  clamped to `100..705600`. These match FMOD 3's `FSOUND_PlaySoundEx(FSOUND_FREE, …)` /
  `FSOUND_SetVolume` / `FSOUND_SetPan` / `FSOUND_SetFrequency` (INFERRED from the argument
  shapes; the library is linked statically). A sound never waits for or cuts off an earlier
  copy of itself, which is what SDL_mixer's `Mix_PlayChannel(-1, …)` does too.

### 4.1 Round-start deal order (REA batch D, 2026-10-22)

Claude traced this for T9.3 (O5) of `docs/plans/wotd-crossword.md` (`untimed-polish.md` task 9.1, decision F10).
Same binary, Ghidra 12.1.4 through REA, one session; the call sites were also read in a local `objdump` listing of
the exe. Evidence ids DL1-DL10 are listed in §4.4 (`DL`, because `D1` / `D6` already name port deviations). Tags as
at the top of this file; **UNCONFIRMED** = searched for and not found, with the search trail.

The deal (CONFIRMED, DL1-DL9):

```text
0x425c50 round start
  table = 0x4396e0(scene, rack, common, full, 0, 0, 0)              call @0x4261c1-0x4261cd
    ball[i] = LetterBall(code point i of rack), i = 0..n-1           @0x43988b-0x4398a8
    0x43bad0(0, -1, 1.0) for ball[0..n-1]: first free cell, a snap   @0x439af5-0x439b1a, row +0xb0
  0x43c370(row = [table+0xb0], word = table+0x1c, slide = 0)        @0x426242-0x426253
    before = the row's letters                                       0x43c170 @0x43c3c8
    if 0x43c280(before):                                             2+ letters, not all equal, none moving
      do    0x439370(cells, cells + n)                               MSVC random_shuffle on CRT _rand()
      while row == before or row == word                             compares @0x43c43d, @0x43c49a
      cell i -> (i*69 + 34.5, 34.5), set at once (slide = 0)        vtable +0x34 @0x43c61b-0x43c636
  0x43c940(1)                                                        the flip-in (§4 above, §9.2 row 4)
```

- **Before the shuffle the balls are in rack-word order.** The ctor reads code point `i` of the rack string
  (`0x45f8c0` gives its byte offset, `0x460410` decodes it) and builds ball `i` from it. Then it puts ball `i` into the
  first free cell of the available row: `+0xb0`, because the ctor's last argument is 0 (@`0x439ac8`). `0x43bad0`
  with a first argument of 0 sets the position through vtable `+0x34` and never calls `0x43a4c0`, so nothing slides
  (DL1, DL2, DL5).
- **The RNG is the CRT `_rand()`, not the game LCG.** `0x439370` is MSVC `std::random_shuffle(first, last)`: for
  `i = 1 .. n-1`, `j = rand() % (i + 1)`, then `swap(a[i], a[j])`. It takes extra 15-bit draws only above 32,767
  elements, so a pass costs `n - 1` draws. That is `tt2::CrtRandom::shuffle` exactly (DL4). The bag (`0x43d550`) and
  the target length draw from the Java LCG `0x5fde90` (W66), so the deal does **not** share the bag's stream. The CRT
  state is process-wide and clock-seeded (`crossword.md` §9), shared with the twist, Crossword and the menu's idle
  timers (`welcome-flow.md`).
- **A guard with no cap: the original never deals the rack word in order.** The loop is a do-while. It shuffles at
  least once, then again while the row's letters equal the order before the call (first compare) or the rack word
  `table+0x1c` (second compare). At round start both strings are the rack word. Both compares are a byte compare
  (`0x402da0`) plus equal sizes, on UTF-8 strings that `0x43c170` rebuilds from each ball's `+0x188` code point
  (`0x422aa0`). The ball ctor stores its letter argument there unchanged (@`0x439e33`), and `0x460410` changes no
  case, so a row that spells the word always matches, including an order that only swaps two equal letters (DL4,
  DL6, DL7, DL9). Expected passes are `1 / (1 - m / n!)`, with `m` the arrangements that spell the word: for `stone`,
  `120 / 119 ≈ 1.008`.
- **Nothing skips the shuffle at round start.** `0x43c280` refuses with fewer than 2 letters, with every letter the
  same, or while a ball's move `+0x184` is set. A rack has 5-8 letters, the ball ctor zeroes `+0x184`
  (@`0x439e2d`), and the snap above starts no move (DL5, DL9).
- **The first frame is already shuffled.** `slide = 0` places the balls at once, and only then does `0x43c940(1)`
  start the flip-in, so the word order is never drawn (DL3, DL6).
- **A resumed game is dealt afresh.** The save keeps `WORD` but no ball order (§7), and the load passes `WORD` back
  to `0x425c50` (W36).

### 4.2 Every deal through `0x43c370` (CONFIRMED, DL8-DL9)

| Caller | Mode, moment | Row / word / slide | Site |
| --- | --- | --- | --- |
| `0x425c50` | Classic Untimed / Timed and WOTD, round start | `[table+0xb0]` / `table+0x1c` / 0 | @`0x426242`-`0x426253` |
| `0x425240` | the same, TWIST | `[table+0xb0]` / `table+0x1c` / 1 (400 ms, §9.2 row 6) | @`0x42532e`-`0x425347` |
| `0x414340` | Lightning, each word's build | `[word+0xb0]` / the word passed to `0x4396e0` / 0 | @`0x4143f3`-`0x414400` |
| `0x413090` | Lightning, TWIST | `[word+0xb0]` / `word+0x1c` / 1 | @`0x41317e`-`0x413197` |
| `0x434f40` | Crossword word object, no restore string (@`0x435303`-`0x435312`) | `[word+0xb4]` / `EDI` (INFERRED `word+0x18`) / 0 | @`0x4353f5`-`0x4353fd` |
| `0x41b060` | Crossword, TWIST | `[word+0xb4]` / `word+0x18` / 1 | @`0x41b14e`-`0x41b160` |

- WOTD runs the same `0x425c50`, so its deal is the Classic one. Its reseed of the LCG `0x5fde90` (§8.2) does not
  reach the CRT.
- TWIST uses the same shuffle and the same guard, so a twist always changes the order and never spells the word.
- The shuffle runs over **all** `n` cells (`[+0x184, +0x184 + 4·+0x188)`, @`0x43c52f`-`0x43c595`), and the layout
  steps 69 px per cell, empty or not. INFERRED: a ball in the played row leaves its available cell empty, so a twist
  mid-word also moves the gaps. A pass that only moves gaps leaves the letters equal to `before` and shuffles again.
- The Crossword deal's word string is INFERRED from register flow: `EDI = word+0x18` @`0x434f7a`, and the same
  `EDI` bounds the ball loop @`0x4352f6`. Its TWIST passes `word+0x18` explicitly.

Worked example (CONFIRMED arithmetic, DL10): `srand(1)` draws `41, 18467, 6334, 26500, 19169`. With the rack
`garden`:

| i | draw | j = draw % (i + 1) | row after `swap(a[i], a[j])` |
| --- | --- | --- | --- |
| 1 | 41 | 1 | `garden` |
| 2 | 18467 | 2 | `garden` |
| 3 | 6334 | 2 | `gadren` |
| 4 | 26500 | 0 | `eadrgn` |
| 5 | 19169 | 5 | `eadrgn` |

`eadrgn` differs from `garden`, so one pass deals it (5 draws). With `srand(195)` and `stone`, the first pass gives
back `stone`; the guard rejects it, and the second pass deals `snteo` (8 draws).

Port (T9.3b): `tt2::deal_order(before, word, crt)` in `engine/include/tt2/random.hpp` is `0x43c280`'s refusal
plus `0x43c370`'s uncapped do-while on `CrtRandom::shuffle`. Every deal and TWIST goes through it: `GameScene` (round
start, resume, TWIST), `BlitzScene` (each word, TWIST) and `CrosswordScene` (each word's rack with the root as the
word, TWIST). All three borrow `main.cpp`'s one process-wide `CrtRandom` (D-11), so the deal no longer draws the
game LCG: for a given `--seed`, later rounds' target lengths and bag picks differ from older builds, as they do in
the exe. The engine tests replay the worked examples above. Differences that remain:

- A TWIST with letters typed deals only the balls still in the row, so its gaps never move; the exe shuffles all
  `n` cells (INFERRED above). Each pass therefore draws one less per gap than the exe's.
- A ball's identity: the port maps the dealt letters back to balls left to right, so two equal letters never trade
  places in a twist. The letters each cell shows are the same as the exe's.
- Port guard: two different letters whose swap is the word leave no order to deal (the exe would loop forever); the
  row stays as it is. Every rack and Lightning word has 5+ letters, so play never reaches it.
- A resumed Crossword round deals every rack again. The exe deals only the words with no `PLAYED` string
  (@`0x435303`-`0x435312`), so a resume with solved words draws less there (INFERRED; the load was not traced).
- The menu's idle timers (`welcome-flow.md` §3) still have their own `CrtRandom` in `WelcomeFlow`, not the shared
  one.

### 4.3 Search trails

| Item | Searched | Result |
| --- | --- | --- |
| A deal path that skips `0x43c370` | callers of `0x43c370` (DL8); the round start after the ctor (DL3, DL9) | none: every new rack in every mode goes through it |
| A word-order deal | the loop's exit condition @`0x43c4a1`-`0x43c4bf`; `0x43c280`'s refusals (DL5, DL6) | none at round start; a single-letter or one-letter-repeated rack would skip the shuffle, and no rack word is like that |

### 4.4 Evidence ids (batch D)

| Id | Call | evidence_id |
| --- | --- | --- |
| DL1 | pseudo `0x4396e0` (`AnagramTable` ctor: balls, rows, snap loop) | `ev_44cb03845ebe64efc330368a5848148a7b2810fc4eeb8f8f76df83569b8d5538` |
| DL2 | asm `0x4396e0` (ball `i` = code point `i`; row `+0xb0`; `0x43bad0(0, -1, 1.0)`) | `ev_81a2810316ba8e08cbdbedd605c4445faace9458b216040f9d1efe10e96e2ef3` |
| DL3 | pseudo `0x425c50` (ctor, then `0x43c370(table+0x1c, 0)`, then `0x43c940(1)`) | `ev_48575bcd821545fb8381f5848f6747d5854fdb32bc35c8f13f433c9eb23b69d8` |
| DL4 | pseudo `0x43c370`, `0x439370` | `ev_257816e08a54c3c2815677b7960884c7d42e708c0d0839bad1ae3b98ffaec007` |
| DL5 | pseudo `0x43bad0`, `0x43c280`, `0x43c170` | `ev_f066cd594ef478c5828d7a18989acaf8a68aaa9180fa7327f901520f107890f8` |
| DL6 | asm `0x43c370` (the do-while, both compares, the shuffle range, snap or slide) | `ev_7d3a55bd29efde3b573689704f2847fd42d7cc88dc3fc7e99fc0c347f559d9f4` |
| DL7 | pseudo `0x45f8c0`, `0x460410`, `0x422aa0`, `0x45fa60`, `0x402da0` (UTF-8 string helpers, compare) | `ev_62fa905eadfee0aac7890c4decc681748a6148c1e1f574760a37a0dbd42733a4` |
| DL8 | callers `0x43c370` (`0x413090`, `0x414340`, `0x41b060`, `0x425240`, `0x425c50`, `0x434f40`) | `ev_18e281dc5da19ca204249fde5928de2be3bb45e5c50fe8bd079a40dfc1285e75` |
| DL9 | `objdump` of `0x426150`-`0x426271`, the §4.2 call sites, and `0x439d00` @`0x439d2b`-`0x439e33` (`+0x184 = 0`, `+0x188` = the letter) | none (local listing) |
| DL10 | simulation of `0x43c370` + `0x439370`, checked against `tt2::CrtRandom` (`engine/include/tt2/random.hpp`) | none (local) |

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

- End of round: `0x428840` (all words, NEXT ROUND, time up with a bingo) / `0x425420` (time up without a bingo,
  Pass): the missing words are revealed (`SOUND_word_pass_reveal`, then a `ModifierDelay`); a miss increments
  `+0x1e8` and plays `SOUND_game_lose_life`; the **third** miss marks the game over
  (`+0x260 = 1` @`0x4256ab`, then `0x428c80(…, 1)`, which still reveals the words with
  `SOUND_word_pass_reveal` before game over `0x427d10`). `0x428840` holds the length its caller passes
  (`[EAX+0x44] = [EBP+8]` @`0x428a02`): **1000 ms** after finding every word (`0x424730` @`0x42493c`:
  `push 0x3e8`, no mode test, so Timed and Untimed too), **3000 ms** for NEXT ROUND and a qualified time-up.
  `0x425420` and `0x428c80` always hold **3000 ms**. (W5 audit, §8.11; an earlier "every reveal holds 3000 ms"
  here was wrong for the all-words reveal.)
- Pass / Give Up (CONFIRMED, REA batch B). The scene has one button spot at (317, 473)
  shared by NEXT ROUND, `BUTTON_Pass_2`, `BUTTON_Pass` and GIVE UP. The refresh `0x425710`
  shows exactly one of them outside Word of the Day:

  | State | Visible button |
  | --- | --- |
  | qualified (bingo found) | NEXT ROUND |
  | misses `+0x1e8` = 0 | `BUTTON_Pass_2` |
  | misses = 1 | `BUTTON_Pass` |
  | misses = 2 | GIVE UP |

  While the block flag `+0x260` or the reveal flag `+0x2f9` is set, `0x425710` takes another branch: it calls
  only setEnabled(0) (vtable `+0xb8`) on `+0x210`..`+0x230` and never setVisible (`+0x5c`). So the button on the
  spot when the round ended stays **on screen, greyed** (its disabled frame), through the reveal and behind the
  dialog or end card that follows, until the next round's refresh (W5 audit, §8.11).

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
  `SOUND_game_next_round_acheived` and opens `DIALOG_NEXTROUND`; WOTD: §8.5); 4 is the Pass / Give Up
  question above; 5 opens `DIALOG_TUTORIAL`, which round start `0x425c50` selects on a
  player's first game (`ev_48575bcd821545fb8381f5848f6747d5854fdb32bc35c8f13f433c9eb23b69d8`);
  6 opens `DIALOG_PASSROUND`.
- Tutorial (CONFIRMED, REA batch G). The fresh-game start `0x425c50` (not the resume
  `0x42a460`) reads the current player's byte at profile `+0xc0 + mode` (`TRACKER/0..4`,
  mode = global `0x5fdea8`, 0 Timed, 1 Untimed, 4 Word of the Day), sets it to 1, and when
  it was 0 sets state 5 (`0x425d0b`). The dispatcher `0x426860` sets the block flag
  `+0x260` for every dialog state, so the game waits. `0x40e040` fills `HEADER` /
  `BODY_TEXT` from `TEXT_TUTORIAL_TITLE_<mode>` / `TEXT_TUTORIAL_BODY_<mode>`. The ctor wires
  `BUTTON_HELP` (READ MORE) to `0x40e1f0`, which opens `DIALOG_HELP` over the tutorial, and
  `BUTTON_OK` (PLAY) to `0x40e1c0`: owner slot `+0xe0` (`GameScene` `0x427b10`: clear
  `+0x260`, refresh buttons) then hide. No sound plays.
  Evidence: `ev_696c1eec946fd882da1004c3caa5472df86e869d992e6266982a512597efe619`,
  `ev_d5cddee1e91269b84e960bb0d6ae972266cbd9b74f86789cb3d16430ea972126`,
  `ev_6abbbc7a006f8a101cb1f06fdc54304116b153a16d66ad38fb244a3891bee54b`; the flag test and
  state store come from the `objdump` listing of `0x425cd7`-`0x425d0b`.
  Port: `GameDialog::Tutorial`, `build_tutorial()`, `PlayerProfile::mode_flags`. INFERRED:
  Esc does not skip it (no Esc handler was traced).
- `DIALOG_PASSROUND` is Timed only (CONFIRMED, REA batch B2). After the switch, the
  per-frame path at `0x426fc7` checks the mode against `CLASSIC_TIMED`; when the round's
  `[+0x1f4]+0x88` flag is clear it calls `0x425420(1)`. That is the time-up miss: reveal
  for 3000 ms, then `0x428a80` (`mov byte [ecx+0x2f9], 0; mov [ecx+0x20c], 6`) opens
  "Round Passed". The Untimed Pass uses `0x425420(0)` and goes straight to the next round.
  Evidence: `ev_1db2070b1f8957586c818109a077def962b1bde2347201f67832bdd159ab78a4`,
  `ev_145f168ef700c17cdca70da2f3fd7f8f846a5d95bb583403c0af92197781be55`,
  `ev_2d7452782ce28eaa417c1f96e2e9313a8beeb47f5695f60eeb2ebc9b140df993`,
  `ev_faa42e184823a6f1cb814bcda1ce22fb7171ed5f5d9f154bb2232d012abcaa66`.
- Round Passed OK (CONFIRMED, REA batch F). The `DialogPassRound` ctor (`0x409c3a`) wires
  `BUTTON_OK` to `0x409d10`, which calls its owner's vtable slot `+0xd8` and then hides the
  dialog (`0x40ce80`). On `GameScene` (vtable `0x590894`) that slot is `0x428aa0`: it hides
  `DIALOG_NEXTROUND` if present, clears the game-over flag `+0x260`, refreshes the buttons
  (`0x425710`) and starts the next round (`0x425190`). State 6 opens the dialog through the
  shared tail `0x426917` and plays no sound. `0x425420` itself: misses `< 3` → reveal,
  3000 ms `ModifierDelay`, then `0x428a80` when called with 1, else `0x425190`; the third
  miss is game over either way (`0x428c80(…, 1)`).
  Evidence: `ev_dd6fcc5bbf07f471abb5218f8c147769ea8ef09ded367d9ebcf518d79e4af5b5`,
  `ev_6473a7a56b0a87a6bf9d732fd0a324322b9fbb31bee5d40b02eab9ab9bb98a23`,
  `ev_815617902c71054da54351ed56ef50d162a05796a71500f528f900d8ce2238ab`,
  `ev_2ab2e50901303d81dbf09d837cba05df2a1c2f3cabfbffc6968e974ea3e55d41`.
  Port: `GameDialog::PassRound`, `build_pass_round()`; Return presses OK, Esc does nothing.
- After the reveal (CONFIRMED): qualified → `DIALOG_NEXTROUND`; a Timed time-up miss with
  lives left → `DIALOG_PASSROUND`; a Pass with lives left → the next round starts by itself
  (`0x425190`); game over → `0x427d10` submits the score
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

All are `autocenter` and `dodarkenbackground="true"`.

- **Backdrop colour (CONFIRMED).** The `DialogTextTwist2` ctor `0x40ca20` stores
  `dodarkenbackground` at `+0x1d9`. Show `0x40cba0` then calls `0x40d190`, which clears
  `movable` (`+0x1c2`) and sets the colour at `+0x19c` to `Color(0.5, 0, 0.25, 0.6)`
  (constants `0x59b858`, `0x59b854`, `0x59b850`). GF2 colours are stored alpha first:
  `0x44ed00` unpacks `0xAARRGGBB` into `[+0]` A, `[+4]` R, `[+8]` G, `[+0xc]` B. So the
  backdrop is **RGB (0, 64, 153) at alpha 128**, a dark blue. Each channel becomes
  `c * 255 + 0.5`, truncated (`0x44ed70`): `0.25 → 64`, `0.6 → 153`, `0.5 → 128`.
  `default.xml`'s `DIALOG_BACKGROUND_ALPHA` (0.7) is dead data: the exe has no such string.
  Evidence: `ev_05cefb67a7ecfbfc9dbdebdc119a0734f4955e0a6b7d76960148e9e3cf4232c3`,
  `ev_2d994ae51ccd0e3451bb1d2778a9ea54fbedd49b83f064f83f768b18312047a1`,
  `ev_d27af62cb7ae791812309c013423e2a06082dee61a6e806497c7c1164ec19ab1`,
  `ev_08dc4b7130e2f26b9f92fa27ee59be9e91e5217178cabe5b314f7f2aeabd1b70`; the colour
  layout comes from the `objdump` listing of `0x44ed00`.
- **Show / hide fade (CONFIRMED).** Show `0x40ccc0` adds `ModifierAlpha(0 → 1,
  DIALOG_FADE_TIME)`; hide `0x40ce80` adds `ModifierAlpha(1 → 0, DIALOG_FADE_TIME)`, then
  removes the dialog (`0x43fcd0`), runs the optional callback at `+0x1d4`, and plays
  `SOUND_DIALOG_HIDE` when `+0x1d8` is set. `DIALOG_FADE_TIME` is 200 ms (`default.xml`);
  `ModifierAlpha` `0x451030` is linear.
  Evidence: `ev_05cefb67a7ecfbfc9dbdebdc119a0734f4955e0a6b7d76960148e9e3cf4232c3`,
  `ev_b4574330eabe40f6150a171a57aec7c07b75f9e5cf3c6e85e36900bb54a3fd3d`.
- INFERRED: the backdrop fades with its dialog. The routine that draws the `+0x19c` colour
  was not found (bounded search: every `+0x19c` / `+0x1ac` / `+0x1b0` access in the
  disassembly), so the port assumes it follows the dialog's alpha.
- Port: `dialog_fade.hpp` (`DialogFade`, `darken_color`). The renderer draws each dialog
  slot into its own texture; a closing dialog fades out from its last frame. Deviation:
  the port acts on a button at the click, not after the 200 ms hide fade.
- **A dialog under another never fades** (CONFIRMED, re-read 2026-10-15): the fade lives
  only in show `0x40ccc0` (`ModifierAlpha(0 → 1)` on the dialog being shown,
  `ev_09e7db4e965c6ce1803195520b13466e61363f581b31c460a4dfae899e755586`) and hide
  `0x40ce80` (`ModifierAlpha(1 → 0)` on the dialog being hidden,
  `ev_bcc7575af53b901909a0c731bf9672eaf083f8b2501217b681479ade36194634`), and a dialog
  opened over another skips the hide (see "Dialogs stack" below). The darken colour is set
  once by `0x40d190` (`ev_1abf1dd47f2ab8689ea665a55dad1e3e922644f27cc61a24969d9a147f72a0bd`).
  So on a push only the new dialog and its backdrop fade in; on a pop only the top one
  and its backdrop fade out. **Port:** the dialog under the top one lives in its own
  slot, so a push or pop moves it between slots; `hand_off()` carries its alpha across
  instead of fading it out of one slot and in to the other (the old out-then-in flicker),
  and a popped dialog is drawn above the one coming back, over its own fading backdrop
  (`closing_above()`).

| Dialog | Size | Contents |
| --- | --- | --- |
| `DIALOG_MENU` (pause) | 322×390 | `PAUSE_HEADER` "PAUSE MENU" at (163, 19); 181×208 buttons HOW TO PLAY (162, 87), SETTINGS (163, 155), STOP (163, 224), RESUME (163, 290) |
| `DIALOG_YESNO` | 400×180 min | `HEADER`, `MESSAGE` (`FONT_SETTINGSTEXT`, `width="390"`, `wrapstyle="wordwrap"`, line height 30); `0x40b470` moves YES / NO down by the message height and grows the **height** only, with no margin (decompiled 2026-10-15: no constant is added), so the buttons end on the frame's bottom edge. **Port deviation:** the message is 332 px wide (34 px inside each side) and the dialog grows 34 px more, so text and buttons stay inside the frame's window (`game_dialogs.hpp`, `YESNO_TEXT_INSET` / `YESNO_BOTTOM_PAD`) |
| `DIALOG_NEXTROUND` | 380×320 | `IMAGE_BURST` sparkle (frametime 40, `#FFA800`, additive) at (191, 154); "Click to continue to the next round!"; OK at (190, 239) |
| `DIALOG_TUTORIAL` | 444×320 | `HEADER` `FONT_DIALOG_HEADERS2` (221, 16); `BODY_TEXT` `FONT_TUTORIAL` centred at (224, 155), line height 32 (the whole block is centred, so 4 lines sit at 107..203; INFERRED, line 4 would otherwise fall below the buttons at 235); READ MORE `IMAGE_BUTTON_READMORE` (135, 235); PLAY `IMAGE_BUTTON_PLAY` (317, 235) |
| `DIALOG_PASSROUND` | 380×280 | `TEXT_PASS_ROUND_TITLE` "Round Passed" (191, 16); `TEXT_PASS_ROUND_BODY` (192, 85); OK (189, 199) |
| `DIALOG_ENDGAME` | 380×260 | "GAME OVER" (190, 14); body (192, 88), line height 38; PLAY AGAIN (271, 173), MENU (109, 173) |
| `DIALOG_CONGRATS` | 380×320 | sparkle frame 4 at (191, 138); "Congratulations! You earned a new high score!" (193, 107); "Awesome" letterballs; OK opens `DIALOG_HIGHSCORES` (`profiles.md` §4.4) |

- `0x407f40` wires the pause menu. STOP opens `0x408320`: "Exit Game?" / "Are you sure you
  want to leave this game? Your game will be saved for next time." YES saves and leaves.
- **Dialogs stack** (CONFIRMED, `ev_efc05cab711733699a313abb52b1106b294383583419898c25de77480f41e176`,
  `ev_cd8a7b2ab31b2eccb64ad0ca43bf775c4797da0b9091d436ea7b304ff43551f8`): STOP (`0x408320`) builds the
  YES / NO with `0x40b310` and shows it (vtable `+0xb0`); SETTINGS (`0x408250`) does the same. Neither hides
  the pause menu. YES (`0x408430`) hands the answer to the owner (`[+0x1dc]`, vtable `+0xdc`); NO is
  `0x408c00`, which stores the dialog in its own `+0x1d0` and jumps to the hide `0x40ce80`
  (`ev_de068f09a96bb022c74a2525455f1ee9257e840ceec28c2a690ff06bfa498c9d`,
  `ev_4162cf97aa1db90036c5cb4fee799f641d06f8262f3d765d4073627ca1a83d3d`), so only the top dialog closes and
  the menu is uncovered. HOW TO PLAY's OK is the same `0x408c00`. Each dialog darkens what is behind it
  (`dodarkenbackground`), so the menu shows dimmed under its confirm. **Port:** `GameScene::push_dialog()` /
  `pop_dialog()` keep the dialogs underneath (`under_dialog()`), and `main.cpp` draws the one under the top
  on its own layer with its own backdrop. Only the top dialog takes input.
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
  with its flag argument 1, so `SOUND_letter_load` (`0x43a940`) plays as each one appears;
  like the rack, a waiting ball shows its first (blank) frame. Slot positions:

  | Slot | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | (x, y) | (60, 34) | (124, 3) | (192, −8) | (259, 1) | (321, 34) | (126, 75) | (192, 61) | (257, 74) |

- `DIALOG_NEXTROUND` particle burst (CONFIRMED from the listing of `0x409100`,
  `ev_6d98d1f974a40d4e765a68fc65c5a399c3df22821369dbd49eaae6c33319d31e`; the decompiler
  output leaves both durations uninitialised). Per ball, 8 particles (`[esp+0x3c] = 8` at
  `0x40935b`), each an `IMAGE_PARTICLEBALL` sprite (`particleball.png`, 18×18) at the ball's
  centre with alpha 1, linked into the ball's parent just before the ball (`0x409531`–
  `0x409579`), so it draws under it. A `ModifierDelay` holds it for the **ball's own delay**
  (`0x4096d9` copies `[esp+0x24]`, the `500 + 200·i` counter). Two modifiers chained after
  that delay then run together:
  - `ModifierLocationLine` (ctor `0x486560`, `EDX = 0x2bc` at `0x4098bf`): **700 ms**,
    linear (`0x4307e0`), to `centre + ((75 + rx)·cos a, (75 + ry)·sin a)`;
  - `ModifierAlpha` 1 → 0 over `600 + rand % 90` ms (`0x4097b9`–`0x409807`), linear.
  The draws come from the global LCG (`0x5fde90`) in this order: the angle
  `a = next(24) / 2^24 · 2π` (`0x40959c`, `CL = 0x18`), `rx = rand % 50`, `ry = rand % 50`,
  then the fade. The fade (at most 689 ms) always ends before the 700 ms trip, so a particle
  is gone before it stops. Worked example: ball G (delay 500) with `rx = 20` and `a = 0`
  starts moving at 500 ms and would reach `x + 95` at 1200 ms, but is invisible from at most
  1189 ms. Port: `Particle` / `next_round_particles()`, drawn under the dialog balls. The
  port draws from its own LCG stream so the burst does not shift the deal and twist
  randomness (a cosmetic deviation). The anchor of `0x442be0` is not traced; the port
  centres the particle like the balls.
- HOW TO PLAY and SETTINGS are decoded in `help-and-settings.md`.
- Not implemented: `DialogPause` (`0x409e50`, key `p`,
  which clashes with typing).

## 7. Saved games (CONFIRMED)

- `PlayerManager` (`0x460900`) keeps a `savegames` folder next to `players`. On scene start
  (`0x4242a0`) a save for the mode (`CLASSIC_TIMED`, `CLASSIC_UNTIMED`, `WORD_OF_THE_DAY`)
  is loaded with `0x42a460`; otherwise a fresh game starts (`0x425c50`).
- "Continue the game?" / "Do you want to continue your last played game? (Select 'No' to
  start again.)" is built by `0x42dc80` (`CTEXT_START_SAVEGAME`,
  `ev_2153c453dd0d668a6bc2cf751712e82483b7b7dbc569a82f7beffe1c52ddbc59`). UNKNOWN whether the
  original ever shows it: `0x42dc80` has no direct caller, only a vtable slot at `0x591220`
  (`ev_b32b0f5d2218417c64055a3a3d03b00fbdcf6d381bb513d73ea95eedb9d7488c`), next to the menu's
  profile check `0x42d9b0` at `0x59121c`. Play-testing the original (2026-10-15), the prompt
  never appears: a save simply resumes, and the only way to start again is to pass and GIVE UP.
  **Port improvement (2026-10-15):** the port asks with this dialog whenever a save exists;
  NO deletes the save and starts a new game. IMPROVEMENTS -> ASK TO RESUME (`resume_prompt`
  in `improvements.txt`, on by default) turns it off, which resumes the save without asking,
  as the original does.
- **Port deviation:** removing a player deletes their saves (`savegames/<player>/`), and
  adding a player deletes any left under that name, so a new player never inherits a
  removed one's game (`ProfileStore::remove_saved_games`, `ProfileFlow::Services::forget_player`).
  Before this, a new profile reusing a removed player's name was asked to continue the
  old game. What the original does with a removed player's saves is not traced.
- Save (`0x429dd0`), called when the scene is left mid-round (`0x427b20`, only when
  `+0x264 == 0`): `WORD`, `ROUND`, `TIME`, `LONGESTWORD` (the qualified flag), `SCORE`,
  `DAY`, `FAILCOUNT` and a `WORD_LIST` of the found words (`0x432cb0`).
- Load rebuilds round 1 from `WORD`, overwrites the counters, places every listed word
  again without scoring (`0x433090` → `0x432b20`), then **deletes the file** (`0x461840`).
  A Word of the Day save from another `DAY` starts a new game.
- Our file is plain text (`tt2/saved_game.hpp`), one per player and mode, at
  `savegames/<player>/<MODE>.txt`; the original binary format is not reproduced.

## 8. Word of the Day (REA batch W, 2026-10-22)

Same binary, Ghidra 12.1.4 through REA, one session. Word of the Day (WOTD) is `TextTwist2Scene` with the mode
string `scene+0x30c == "WORD_OF_THE_DAY"` (`0x5906c8`; the menu global `0x5fdea8 == 4`, `0x4242a0`). Every check
below is a string compare on that field through `0x4972a0`. Evidence ids W1-W78 are listed in §8.9 (W44-W54
belong to the UI bar addendum, §8.10; W55-W78 to the W5 audit, §8.11). Tags as at the top of this file;
**UNCONFIRMED** = searched for and not found, with the search trail.

### 8.1 Three clocks, three day numbers (CONFIRMED)

| What | Where | Formula | Clock |
| --- | --- | --- | --- |
| Rack word | `0x43d690` | `pool[(uint32)time() / 86400 - 14125]` | UTC |
| Scene day `+0x1ec` | round start `0x425c50` @`0x425cb9`-`0x425cec` | `((uint32)(_time64() - tz * 60)) / 86400 - 14125` | local standard time |
| Menu gate | `0x42f1a0` | `player.WOD !=` the same formula | local standard time |
| Resume check | load `0x42a460` | saved `DAY !=` the same formula → delete the save, new game | local standard time |
| High-score date | `0x43ff60` | `GetSystemTime` → `GetDateFormatA(..., "yyyy'-'MM'-'dd")` | UTC calendar date |

- `tz` is the `timezone` field of `struct __timeb64` (`MOVSX ECX, word ptr [ESP+0x4a]`, offset `+0xa` of the
  buffer passed to `_ftime64_s` @`0x425cb9`; W6). The `dstflag` field (`+0xc`) is **never read**, in `0x425c50`,
  `0x42f1a0` or `0x42a460` (W4, W6, W36). So in summer the scene / menu day flips at 01:00 local daylight time.
  Worked example: Berlin in July (`timezone = -60` minutes, `dstflag = 1`), 00:30 CEST is 23:30 local standard
  time, so the menu still shows yesterday's day number until 01:00.
- The divisions are unsigned on the low 32 bits of the time (`MUL` by `0xc22e4507`, `SHR EDX, 0x10`).
- The scene day is computed **once per round start** (every mode, not only WOTD) and kept in `+0x1ec`; the load
  path overwrites it with the saved `DAY` (W36).

### 8.2 Rack word (`0x43d690`, CONFIRMED; corrects `gameplay-notes.md` §2)

- The only caller is round start `0x425c50` (W3), and only when no resume word was passed and the mode is WOTD
  (`0x425fd0` → `0x4261d4`). The classic target-length loop that reads the player's 6/7/8 setting
  (`player+0x3c`, `0x426008`-`0x426104`) is skipped, so **WOTD ignores MAX WORD LENGTH** (W6).
- `0x43d690` builds the 5-8 letter pool (`0x43dcb0`), reseeds the **global** Java LCG at `0x5fde90` with
  `1337 ^ 0x5DEECE66D`, shuffles (`0x43d950`, a `random_shuffle` with a generator callback, W43), then takes
  `pool[day]` **directly**: `LEA EAX, [EDX*8]; SUB EAX, EDX; LEA EAX, [base + EAX*4]` = `base + day * 0x1c`
  (W2). There is **no modulo and no bounds check**, and **no 15-sub-anagram test**: the sub-anagram list
  (`0x43dd80`) is built but its size is not compared. That test lives only in the Classic bag `0x43d550`.
- Consequences, with the original `TextTwist2Words.tree` (pool = **22,048** words of 5-8 letters, counted with
  `docs/reverse-engineering/tree_decode.py`): day 0 is 2008-09-03; from day 22,048 (**2069-01-14**) the read goes
  past the end of the vector; before 2008-09-03 the index is negative. About **26 %** of the pool has fewer than
  15 common sub-anagrams (5,775 of 22,048; same count with the open list: 3,964 of 19,256 = 21 %), so on about
  one day in four the exe hands out a word the Classic bag would refuse. INFERRED (estimate, assumes the day
  indices hit the pool evenly).
- CONFIRMED (W5 audit): the reseed changes **no gameplay draw**. The global LCG `0x5fde90` has four readers in
  scene code (W66): the Classic target-length loop `0x426002` (skipped in WOTD), the Classic bag `0x43d5b6`, the
  WOTD reseed itself `0x43d6bd`, and the shuffle callback `0x43dad0` used only inside `0x43d690` (W68, W70).
  `0x436090`, the other reader, belongs to Crossword (its only caller is `0x41ba40`, W67, W69). The deal and the
  twist shuffle with the CRT `_rand()`: `0x43c370` calls `0x439370(begin, end)`, which draws `_rand()` and
  reshuffles while the letters still spell the rack word (W71, W72). INFERRED: the other `0x5fde90` readers are
  cosmetic (`DIALOG_CONGRATS` / `DIALOG_NEXTROUND` particles at `0x401xxx` / `0x409xxx`, GF2 effects), so after
  a WOTD round their positions follow the reseeded state. The port draws those from its own generator; no change.
- A resumed game does not call `0x43d690`: `0x42a460` passes the saved `WORD` to `0x425c50` (W36).
- Port deviation **D1** (`Bag::word_of_the_day_for_day`, `docs/design/wotd-crossword.md`): the pool is built from
  whichever common list the game loaded. With the shipped `open-common.tree` (19,256 words of 5-8 letters) the word
  for a date differs from the 2008 game's; with the player's own `common.tree` it is the same every day. Worked
  example, 2026-10-22 (word day 6623): `analogue` with the open list, `airport` with the original (§8.11).

### 8.3 Board (`0x4262b0`, CONFIRMED)

`0x425c50` enumerates the rack against the common tree (`+0x268`) and the full tree (`+0x2ac`) (`0x43d7b0`
twice), then **only in WOTD** calls `0x4262b0` on the common list (EAX = the first result, @`0x426193`), before the
`AnagramTable` ctor `0x4396e0` (W6). `0x4262b0` keeps a counter per length, `[0, 0, 0, 5, 5, 5, 5, 5, 5]` for
lengths 0-8, walks the list in order and erases (`0x418840`) every word whose length counter is already 0
(W41). So:

- the board gets the **first 5 words of each length 3-8 in sub-anagram (DFS) order**, at most 30 slots;
- the cap runs **before** the 48-slot quota of `0x432330`, which therefore never trims a WOTD board (30 < 48);
- the full-tree list is not capped: any valid word still counts as a guess.
- CONFIRMED (W5 audit): a common word cut by the cap counts as **uncommon**. `0x4396e0` passes the capped common
  list and the full list to `0x432330` (W74), which empties the table list at `+0x180` (count `+0x184`) and
  refills it with the quota-selected board words only, grouped by length (W75). `0x432850` sets the "uncommon"
  out-flag when a find over `[+0x180, +0x180 + count·0x1c)` misses (W73), and `+0x184` is also the slot count the
  all-words check compares with `+0x178` (`0x424730` @`0x4248f7`). So "uncommon" means **not one of the board's
  words**: a full-only word, or a common word the WOTD cap (or the Classic 48-slot quota) left off. It still
  fills the first free slot of its length and feeds the Human Dictionary counter. This corrects
  `gameplay-notes.md` and `architecture.md`, which call `+0x180` the common sub-anagram list.

Port note: `Board::build(rack, common, 5)` used to apply the 48 quota first and truncate to 5 afterwards; Phase 2
(T2.1) moved the cap first. The W5 audit made `GuessProcessor::apply` flag a guess uncommon when it is not on the
board (`Board::on_board`), instead of when it is not in the common list.

### 8.4 Buttons (CONFIRMED)

`dialogs.xml` (`SCENE_GAME`), read-only from the install:

| Component | Image | Position | Other attributes |
| --- | --- | --- | --- |
| `BUTTON_WOTD_DONE` | `BUTTON_Done` (`images/gamescene/button_done.png`, `rowcount="4"`) | (317, 473) | `command="BUTTON_NextRound"`, `tooltip="CTOOLTIP_NextRound"`, `clicksound="SOUND_BUTTON"`, `setup="DEFAULT_BUTTON"` |
| `BUTTON_PLAY_WOD` (main menu) | `IMAGE_BUTTON_WOTD` (`button_wotd.png`, 4 frames) | (85, 378) | `tooltip="CTOOLTIP_SELECTION_PLAY"`, `clicksound="SOUND_BUTTON"`, `disabledsound="SOUND_button_wordoftheday"`, `hitmaskthreshold="75"` |

- The exe looks `BUTTON_WOTD_DONE` up by name once (`0x425e37`, the only xref to the string, W12) and keeps it at
  `+0x1fc`; the NEXT ROUND pointer is saved in `+0x1f8`. Round start then makes `+0x224` (the "next round" slot
  the refresh drives) point at **`WOTD_DONE` in WOTD** and at NEXT ROUND otherwise (`0x425e97`-`0x425f34`, W6).
- The dispatcher `0x425b60` has no `BUTTON_WOTD_DONE` case: it matches commands, and the button's command is
  `BUTTON_NextRound`, so a click runs `0x428840(3000)`, the NEXT ROUND path (W8, W9, W10).
- Refresh `0x425710` in WOTD (W7): `BUTTON_Pass` and `BUTTON_Pass_2` are always hidden and disabled; GIVE UP is
  shown while the round is **not** qualified (`[+0x1f4]+0x88 == 0`); `WOTD_DONE` is shown once it **is** (first
  bingo found). Worked example: start → GIVE UP; first bingo → DONE replaces it; nothing else changes.
- The tooltip text is `CTOOLTIP_NextRound` ("Advance to the next round"), a leftover the original never changed.

### 8.5 Round flow and the single round (CONFIRMED; answers OI-5)

- Timer: WOTD ticks like Timed (`0x426d59`/`0x426d67`: CLASSIC_TIMED or WORD_OF_THE_DAY), from 120,000 ms with
  the 10 s warning (W17, W18).
- First bingo: `+1000` and qualified, as in Classic, but the popup is **kind 11** (`IMAGE_IMAGE_POPUP_11`) instead
  of kind 2 (the BINGO banner): `0x424730` tests the WOTD string @`0x424aa1` and queues `0x428e20(0xb)` (W37). A
  repeat bingo or a plain word queues nothing new (the one-time popup 10 rule is unchanged). The guess that fills
  every slot queues popup 1 (all words) instead, in every mode. CONFIRMED (W5 audit): kind 11 uses the message
  timing (1650 ms, §5.1): `0x428ec0` @`0x42934c` tests `kind == 2`, then `kind == 1`, for the banner chain, and
  every other kind takes `ModifierDelay(0x384)` = 900 ms (`ev_2a0a784f…`, §5.1).
- Round end paths in WOTD:

  | Trigger | Code | Then |
  | --- | --- | --- |
  | DONE clicked (qualified) | `0x425b60` → `0x428840(3000)` | reveal, 3000 ms, state 3 |
  | All words found | `0x424730` → `0x428840(1000)` | reveal, 1000 ms, state 3 |
  | Time up, qualified | `0x426860` @`0x42701b` → `0x428840(3000)` | reveal, 3000 ms, state 3 |
  | Time up, not qualified | `0x426860` @`0x426ff4`-`0x427014` | `+0x260 = 1`, `0x427cc0`, `0x428c80(…, 1)`: reveal, 3000 ms, game over |
  | GIVE UP → YES | state 4 → `0x428610` → `0x4253a0` | `+0x264 = 2`, delete save, `WOD = +0x1ec`, `0x428c80(…, 1)` |

  `0x428840` ends with the delay callback `0x428a60` = `mov byte [ecx+0x2f9], 0; mov dword [ecx+0x20c], 3`
  (W15, W16). The time-up branch picks Timed vs WOTD at `0x426fc7`; WOTD never calls the miss path `0x425420`
  (W18).
- **No lives, no Pass:** state 4 in WOTD always asks the GIVE UP question (`TEXT_DIALOG_GIVEUP_HEADER` /
  `TEXT_DIALOG_GIVEUP`, "Give Up?"), whatever the miss counter (`0x426bac` → `0x426c3e`, W18, W19, W20); YES ends
  the game at once (`0x4253a0`, W21). `DIALOG_PASSROUND` never opens in WOTD.
- **State 3 in WOTD** (`0x426a27` → `0x426b56`, W17, W18): `+0x264 = 1`, block `+0x260 = 1`, no sound, no
  `DIALOG_NEXTROUND` here, then `count_round(player, 4)` (`mov eax, 4` @`0x426b74`, `call 0x40f2b0`) with its
  trophy popup (`0x429480`), `0x427cc0` (delete the save, write `WOD`), and `0x428c80(…, 0)` (no second reveal,
  3000 ms, W40) → game over `0x427d10`. Nothing in WOTD reaches the next-round starter `0x425190`, so **a WOTD game is
  exactly one round**.
- Game over `0x427d10` in WOTD (W24, W25):
  1. `score > 0`: insert into table **`"wotd"`** (`0x5919c8`, W28) with the date text (§8.7). If it ranks (W29):
     `SOUND_game_high_score_achieved` and `DIALOG_CONGRATS`.
  1. Otherwise, qualified: `SOUND_game_next_round_acheived` and **`DIALOG_NEXTROUND`** after all, but `0x4099c0`
     sets its WOTD flag (`dialog+0x1f4 = 1`) and the body to `TEXT_NEXT_ROUND_BODY_WOTD` ("Click to return to
     the main menu!") (W30). Its OK (`0x409a70`) then calls owner slot `+0xdc` instead of `+0xd8`; on the game
     scene vtable `0x590894` that is `0x427b20` (leave the scene; no save because `+0x264 != 0`), not `0x428aa0`
     (next round) (W32, W33, W34).
  1. Otherwise (not qualified): `DIALOG_ENDGAME` with `TEXT_END_ROUND_BODY_WOTD` ("Sorry, you didn't find the
     Word of The Day."), `BUTTON_REPLAY` hidden and disabled, and `BUTTON_OK` (the menu button) moved to
     x = 192.0 (`0x43400000` through vtable `+0x28`; INFERRED setX) (`0x404540`, W31).

  So OI-5's answer is yes: `DIALOG_NEXTROUND` does appear in WOTD, but only as an end-of-game "back to the menu"
  card, never to start a second round.
- Tutorial: round start reads `player+0xc0+mode` with mode 4 and shows `DIALOG_TUTORIAL` the first time, with
  `TEXT_TUTORIAL_TITLE_4` / `TEXT_TUTORIAL_BODY_4` (§5, W6). A resume (`0x42a460`) still passes through
  `0x425c50`, but the flag was set on the first game, so it does not show again.

### 8.6 `player.WOD` and the save `DAY` (CONFIRMED; answers OI-2 and OI-10)

- **Save `DAY`** (`0x429dd0`, key `DAY` at `0x590860`) = scene `+0x1ec`, the **local standard-time** day taken at
  round start (W35). The save is written only when the scene is left with `+0x264 == 0` (`0x427b20`, W34).
- **Load** (`0x42a460`, WOTD only): reads `DAY`, recomputes today with the same local formula, and on a mismatch
  deletes the save (`0x461840`) and starts a fresh game (`0x425c50(1, 0)`) with no prompt (W36). The scene start
  `0x4242a0` picks load vs. fresh game (W38).
- **`player.WOD` (`+0x38`) writers**, all `= scene+0x1ec` (the same local day), all at **game end**:

  | Writer | When | Reached in WOTD |
  | --- | --- | --- |
  | `0x427cc0` (only caller `0x426860`, W22, W23) | state 3 round end, and time up without a bingo | yes |
  | `0x4253a0` (W21) | GIVE UP → YES | yes |
  | `0x425420` third miss (W39) | Pass / time-up miss path | no (WOTD never calls it) |
  | `0x411ef0` (W42) | Blitz game-over reveal, gated by a WOTD string compare | no (Blitz mode string) |

  `0x411ef0` is the `player+0x38` copy in `lightning.md` §4.2 (once tagged UNKNOWN there): a leftover from the
  shared code, dead in Blitz.
- **Not written** at round start (`0x425c50` only computes `+0x1ec`, W6), on leave (`0x427b20` saves, W34), or in
  the save writer (W35). So a mid-round leave keeps the menu button **enabled** on the same day, and clicking it
  resumes the save. The OI-10 risk (a same-day save behind a closed gate) does not exist in the exe: `WOD` is only
  written by paths that also delete the save (`0x427cc0` and `0x4253a0` both call `0x461840` first).
- INFERRED edge (not play-tested): after a round-end reveal starts (`+0x264 = 1`) and before state 3 runs, the
  pause menu can still exit the scene (`0x425b60` sets state 1 for `BUTTON_Menu` whatever `+0x264` is). That exit
  writes no save and no `WOD`, so the same day can be played again. W5 audit, more static support: the dispatch
  opens state 1 only while `+0x260 == 0` (`0x426860` top, W17), and a `0x428840` reveal sets only `+0x2f9` /
  `+0x264` (W14), so the menu does open there; `0x427b20` skips the save when `+0x264 != 0` (W34) and
  `0x427b40` replaces the scene with `SCENE_SELECTION` (W56). Still not play-tested on the original.
- Day rollover mid-game: the rack word and `+0x1ec` are fixed at round start, so a game that crosses midnight
  keeps its word and writes the **start** day into `WOD`; the button is enabled again the next time the menu
  is drawn. A save that crosses midnight is discarded on load (above).

### 8.7 High-score entry (`0x427d6c`-`0x427ec9`, CONFIRMED; answers OI-4)

- Table key `"wotd"` (`0x5919c8`, 4 chars), the same `0x4108a0(name, score, 0, +0x2fc, extra)` call shape as
  Timed (W24, W25).
- `extra` = `0x43ff60()` cut at the first `' '` (`0x5907d4`, W27): `0x43ff60` returns
  `GetDateFormatA(LOCALE_SYSTEM_DEFAULT, 0, utc, "yyyy'-'MM'-'dd") + " " + GetTimeFormatA(..., "HH'-'mm'-'ss")`
  from **`GetSystemTime`** (UTC) (W26). So the stored text is the **UTC date as `YYYY-MM-DD`**. Worked example:
  a game finished at 07:30 on 2026-10-23 in Manila (UTC+8) is stored as `2026-10-22`. The HIGH SCORES dialog
  shows it under `TEXT_HIGHSCORE_DATE` ("Date:", `profiles.md` §4.1).

### 8.8 UNCONFIRMED

| Item | Search trail |
| --- | --- |
| Exit during the round-end reveal (§8.6 edge) | Static only: `0x425b60` state 1 is unconditional; the dispatch needs `+0x260 == 0`, which a `0x428840` reveal leaves (W14, W17); `0x427b20` skips the save when `+0x264 != 0` (W34); `0x427b40` leaves (W56); not play-tested on the original |

Resolved by the W5 audit (§8.11): kind 11 popup timing (message, §8.5), the global LCG after the reseed (no
gameplay draw, §8.2), and the "uncommon" flag on cap-trimmed words (set, §8.3).

### 8.9 Evidence ids (batch W)

| Id | Call | evidence_id |
| --- | --- | --- |
| W1 | pseudo `0x43d690` | `ev_b836e1193a21e92dc6308cc5b099556a244b94882418ec0e20a2e8811836c70a` |
| W2 | asm `0x43d690` | `ev_a6497a4234824599c8d2727818ee0ccc5502eb79cae6a288c2e386bb15d734a6` |
| W3 | callers `0x43d690` | `ev_ad110912cb7b268a907e0ace6c829bc82b2a46dcc267c7b335365952b8079b82` |
| W4 | pseudo `0x42f1a0` | `ev_6a9bb451625e15c4bcc93a6230ef2d2232b33c3cb13610990508a036ff4af9d4` |
| W5 | pseudo `0x425c50` | `ev_48575bcd821545fb8381f5848f6747d5854fdb32bc35c8f13f433c9eb23b69d8` |
| W6 | asm `0x425c50` | `ev_9459800fd26fae8313cfb42fbeb4ac74b8b3d2b94a27c727c5e707f2623e17cb` |
| W7 | pseudo `0x425710` | `ev_52333333f7d1d525a504b153b09bd1bfa66307e5ca52024a9d0472e70a683fb5` |
| W8 | pseudo `0x425b60` | `ev_104f9575430ab658e77ee43a6e339523b1acea4ee9872327d379a15d6fb3603f` |
| W9 | asm `0x425b60` | `ev_5fa16e103908ed44ad634ebca65eb5520489ad508c340a7e77e390ddec315185` |
| W10 | bytes `0x58fcdc` (button commands) | `ev_30c41d1568af11a2b8805c3c7109cd98787fb317813bb0682751b6b7f5ab275d` |
| W11 | bytes `0x5906b8` (mode strings, button names) | `ev_cb634dec01a5c2feb9f391c5b0fee4316aded4cbed64972aa5c241b4142598df` |
| W12 | xrefs `0x590744` (`BUTTON_WOTD_DONE`) | `ev_5685f5a0e6bea6baa81eb61cd61f7ff4682a046546d8404b7f00a336baa60290` |
| W13 | xrefs `0x5906c8` (`WORD_OF_THE_DAY`) | `ev_e6883cb045453146d0ad92be5a5c20b2a9e138d7295065e359554c12cde8c205` |
| W14 | pseudo `0x428840` | `ev_9abb52f29a32ae443791c04de1c4a9e216cc2c7814841d6e8f002dcfc1a9865b` |
| W15 | asm `0x428840` | `ev_3eedebc8567245135e24eba1fbb142f4f3242768d73d4ed950e888319ccf019e` |
| W16 | bytes `0x428a60` (sets state 3) | `ev_e455874aa7f03665668f87a148311c4308b217004973cb856d1a213233a4963a` |
| W17 | pseudo `0x426860` | `ev_f5625215aea38c76cfd0a11f37c1024737f1adef063e566f82fe909c0346064b` |
| W18 | asm `0x426860` | `ev_faa42e184823a6f1cb814bcda1ce22fb7171ed5f5d9f154bb2232d012abcaa66` |
| W19 | bytes `0x58fe00` (GIVE UP header) | `ev_ba5ce98c0c702a8d36774c7c72a39215865841d08a18b84fbcfff7358f37f8a3` |
| W20 | bytes `0x591860` (PASS / GIVEUP texts) | `ev_462b82291dea7f2a4898365570f8391679cca02ef275dd41c7ecd842683b610c` |
| W21 | pseudo `0x4253a0` | `ev_1e2c943eb1fd9e777628f98b61f09d7e3a266dcf9a80b15b9efb2db1895a6c4e` |
| W22 | pseudo `0x427cc0` | `ev_b54b4fe4628d8df262162f02979b003e80b0024601e8add11272f760cc4fd8d2` |
| W23 | callers `0x427cc0` | `ev_3aa2949b32134c747ee9e860130372444c47f704c9b4be6cdc54e09eca0c00d1` |
| W24 | pseudo `0x427d10` | `ev_4550c53f3139bcacec5d0ebe253ce9e96125d587c124ceec170bf22e5d4a3004` |
| W25 | asm `0x427d10` | `ev_cd5e659b3524eae90deb2df9c8d3d5d3469849c9936ff358fe47012f5bc20cd7` |
| W26 | pseudo `0x43ff60` | `ev_b307cac04dd07e02ba4eff244ce8c26bca41a585303787c64d780f97a5cb176a` |
| W27 | bytes `0x5907c8` (`" "` at `0x5907d4`) | `ev_d55c8154269902a3bfbca930597a47c4f92a56370d9875b0f611c67342f8f941` |
| W28 | bytes `0x591950` (dialog ids, table keys) | `ev_9ef2d4ca45e3569d7521007611b84a93188617b4c000e724baf19bca14d39687` |
| W29 | bytes `0x58fe78` (high-score sound) | `ev_5867eb8888bfd955182528817b53183a1c8cecf4f38bf5ece7760ab9990de765` |
| W30 | pseudo `0x4099c0` | `ev_6a9ee0507b98dc3449ac5fe9a3d5c32da7aa4192da9b901041de00e27dcc3377` |
| W31 | pseudo `0x404540` | `ev_44297a4362aeed2953bf3f9cc1d97f5979cebc79390b55fc64981fbfae5239ec` |
| W32 | pseudo `0x409a70` | `ev_429e531f96a80928008c46701b3880de1f7fd711951e8fc81c997587b0d39e8c` |
| W33 | bytes `0x590964` (vtable slots `+0xd0`-`+0xdc`) | `ev_1598c772ac76ee0c8e6e386ae7625eee300e3ef40f059fb3d0af9f7b60cdb043` |
| W34 | pseudo `0x427b20` | `ev_02555fcc3867e692d3011976f57a18d02a05569524377f9e28ea8f4a4e7ece1a` |
| W35 | pseudo `0x429dd0` | `ev_feae34df9f8dba04115aaff060c35dbfb3f34c8c666590329500c7c324f00a34` |
| W36 | pseudo `0x42a460` | `ev_0f083bb9728be876db346ce803d8fdc257f6c478f94a6f29005ce0891e227fd2` |
| W37 | pseudo `0x424730` | `ev_e7f08e2c9eafb9f3ee109e9c87349a34776e6be1dafadff3ee385925c698ea1a` |
| W38 | pseudo `0x4242a0` | `ev_839112ddb8536b7f891651bd06a1e26fc178b01e6a76306db3377a58769cddca` |
| W39 | pseudo `0x425420` | `ev_12a29f6e7b7936f9e1ec43c0b1cfe9f71fe8405ad500536a162ce52b8c6a99af` |
| W40 | pseudo `0x428c80` | `ev_a28bbd4f868f3bb994da7c816409e253bdd452a8799360d21d05bc56985997df` |
| W41 | pseudo `0x4262b0` | `ev_b20255d271f0d3f487d5013980aaf9c5b14bc70b29db3225f8c64bbf7460c5cc` |
| W42 | pseudo `0x411ef0` | `ev_c791c69f18e9efee08fdef54deb62fe4ff8ab9fee5699062aa866b69c0f6aed5` |
| W43 | pseudo `0x43d950` | `ev_dc616f0f7776793550c6e1690a5476af0d4df870615de763181d4d443ce798ce` |
| W44 | strings `LABEL_ROUND` (`0x5905d8`) | `ev_e176c32ce8e97baae46e720a037d1eb02ddaf3797f22716638fd0b0af282ac2e` |
| W45 | xrefs `0x5905d8` (only `0x423d65`) | `ev_54b77a5c7fe3275ad03b0d06333d846e6d13557f6abb44f1f6740e49f72850ed` |
| W46 | asm `0x423a20` (ctor lookups) | `ev_79be952146c636bd4321e15aa343e208c6b3be945c4d64b9e56d243ba488c1ca` |
| W47 | bytes `0x5905a0` (UI bar names) | `ev_5c0aeb5a9561cbd6a7a56b78547211861880eacdb139726dbc99c5da37b891a2` |
| W48 | strings `(copy)` (no hit) | `ev_5e9b49c98c07bf6b95e4aa9e5e76d6b88fe8c03ce08c59a053b77f85abf6f9c5` |
| W49 | strings `UIBAR` (no hit) | `ev_6dd2b4f2e786271648bbbfd9ddfb8a8baad1c2c20b148840d3bb3b330ec4bb5c` |
| W50 | asm `0x426360` | `ev_cbe57e77ac9223065ba14826217f260de9df12e42c8d196967c1acd592ceb5f4` |
| W51 | pseudo `0x426360` | `ev_40e4facb171c6f6f6966ab1dc35c17995a24203c131882ebb5729cc94efc48a5` |
| W52 | bytes `0x58fde0` (`CLASSIC_UNTIMED`) | `ev_c09ec63baef2bc25ccabc6ec9f6052f38c82273d23e622c746199b5875e1c4b6` |
| W53 | xrefs `0x426360` (2 callers) | `ev_2de80fc408d0102b1fc593418b875145ad3c53f9949f6b8b0bf0d2c24cd6d657` |
| W54 | pseudo `0x425190` (next round) | `ev_c9a84041b977f1cb3e1b5d9769cd007d4b57f6074088f0feaba43b0eeb96e70f` |
| W55 | asm `0x424730` (all words: `push 0x3e8` @`0x42493c`; `+0x178 == +0x184` @`0x4248f7`) | `ev_dbf47191191768ed5f06295c84cdfb66bc0e49d4f75c410e34117d6872f3b1e1` |
| W56 | pseudo `0x427b40` (leave: `SCENE_SELECTION`) | `ev_f1de22d5308d13e9283081256d4ef1f72c189a8e8747ffa5375b22bcecf8ad0c` |
| W57 | xrefs `0x404540` (`DIALOG_ENDGAME` OK) | `ev_8c3744ff594b082c27508cc9a996e0b68a1a59caa867b36191740a7f7bbc7696` |
| W58 | pseudo `0x428540` (open `DIALOG_HIGHSCORES`) | `ev_24bda08bc6360dcb4dde12d8fb97858c74bd121ec85a3c1b6838f81797e63a84` |
| W59 | pseudo `0x401b50` (`DIALOG_CONGRATS` OK) | `ev_a8a4a413adbd8325f3725fa4f236e929860f09812e62e80517814903bc2bf78f` |
| W60 | pseudo `0x40ce80` (dialog hide) | `ev_bcc7575af53b901909a0c731bf9672eaf083f8b2501217b681479ade36194634` |
| W61 | asm `0x423640` (scene ctor: `+0x260 = 0`, trophy queue emptied) | `ev_cbad70304506d28b0c8dea18caa2868400793ff7775482330efef811c8154153` |
| W62 | xrefs `0x427b10` | `ev_2d62823e6f66d550bd2057ae1d3e4f4903455979a3fe3ceddd7e510e88ad66ac` |
| W63 | pseudo `0x428720` (PASS / GIVE UP NO) | `ev_3f11c6307180b276a5e527b2110889d1455524cab3c76d14865ffed338852fd0` |
| W64 | bytes `0x40e1c0` (tutorial PLAY: owner `+0xe0`) | `ev_019f9de989f7fdd8b3e991f7a329abb226deacba62aae1789174c58d120ae68f` |
| W65 | bytes `0x40a380` (pause menu handler: owner `+0xe0`) | `ev_1101aee0fbd19576850a14607133918fdc9283a839d38d55212c22fbd6473d14` |
| W66 | xrefs `0x5fde90` (global Java LCG) | `ev_7f2ec0edc4fa3d24eca653aa62be553279708f370d3f33efb756f639fde3e3a0` |
| W67 | containing procedure of the `0x4363f9`-`0x436838` readers (`0x436090`) | `ev_a9779afab381ff9f702cab3338afa3daec1517072490ca82a249b71c544c974e`, `ev_9baa9cf964d3a470d42603341d44adfa0c285fb74e1f9ef139a82b9d51b4005b` |
| W68 | containing procedure of `0x43dad5` (none: a callback body) | `ev_ec2ed54e09572a33b437473f2e5aa6aaf0f2583b330adffabcb3ed63355db5d2` |
| W69 | callers `0x436090` (only `0x41ba40`, Crossword) | `ev_87a9a746a1266830a86af9779faaaaa9175087a9619cc66433fa5cc18c94b148` |
| W70 | bytes `0x43dad0` (shuffle generator callback) | `ev_2e9d53f951b0da71e3b14c740ac383d93e1055641172e6bcef9d2da86f07932d` |
| W71 | pseudo `0x43c370` (deal / twist relayout) | `ev_77522ecfe58a8322486e92312c1a939f6b05d7ff0274e280a2abaea297f85b7d` |
| W72 | pseudo `0x439370` (rack shuffle, CRT `_rand()`) | `ev_294aa5ef7b8cf98cf6fedcdc7c52be677aac88c1c7160d629f0041a5aa458960` |
| W73 | pseudo `0x432850` (guess check, "uncommon" out-flag) | `ev_c2e4a806af43ecd60e7258f087917bf20f10e346e7ca2c2289b1387b3d2aacc2` |
| W74 | asm `0x4396e0` (`AnagramTable` ctor: lists passed to `0x432330`) | `ev_81a2810316ba8e08cbdbedd605c4445faace9458b216040f9d1efe10e96e2ef3` |
| W75 | pseudo `0x432330` (table list `+0x180` rebuilt from the board words) | `ev_ce1122d744f8c585ae0815707bc0ffcf4d16c1a6d00e585b1145e194056f9a2a` |
| W76 | pseudo `0x428610` (PASS / GIVE UP YES) | `ev_83f76bc94d0779c51f90d3c7fc92056d82928918b4a984b0aca172f48a6965c9` |
| W77 | bytes `0x426c90` (dispatch tail: refresh, trophy / popup dequeue, clock gate) | `ev_f53240fd7f75e6a297fe162d99b37f87bd2ced11f3bac5e642188631cf749630` |
| W78 | local byte scan of `.text` for writes to `+0x260` (`C6 /0 imm8`, `88 /r`) | none (Python over the exe; results in §8.11) |

### 8.10 UI bar: ROUND and TIME in WOTD (CONFIRMED; REA addendum, 2026-10-22)

Question from a play test: the port shows "ROUND: 1" in the bottom bar during WOTD. Does the exe hide, empty or
replace the round label there? **No.** The exe shows "ROUND: 1" in WOTD; the port hides it on purpose (D6, below).

- **The "ROUND:" caption is never touched by code.** The exe holds no `LABEL_ROUND (copy)` string (no `(copy)`
  string at all, W48; the name table at `0x5905a0` has `LABEL_ROUND` but no copy, W47) and no `TEXT_UIBAR_*`
  string (W49). So the caption is drawn as `dialogs.xml` sets it: `textid="TEXT_UIBAR_ROUND"` ("ROUND:" in
  `text.xml`), no `visible` attribute, in every mode. `text.xml` has no WOTD-specific UI bar text either.
- **The round number label.** `LABEL_ROUND` (`0x5905d8`) has one xref, the ctor lookup at `0x423d65` (W44, W45),
  which stores it in `+0x240` next to `LABEL_Score` `+0x23c`, the timer labels `+0x234` / `+0x238` and
  `TIME_LABEL` `+0x244` (W46). The only code that uses `+0x240` is `0x426360` (searched: `0x4242a0`, `0x425c50`,
  `0x425190`, `0x425710`, `0x426860`, `0x424730`, `0x42a460`, `0x428840`, `0x428c80`; W5, W7, W14, W17, W36-W38,
  W40, W54). There, `@0x4263ee`-`0x426417`, it sets the text to `to_string(+0x300)` through the label vtable
  `+0xc4` (setText) with **no mode check** (W50, W51). Its callers are round start (`0x42626c`) and load
  (`0x42aa84`) (W53).
- **The value is 1 in WOTD.** `+0x300` is the first argument of round start `0x425c50` (W5). WOTD starts with
  `0x425c50(1, 0)` (`0x4242a0`, W38; also the stale-save path of `0x42a460`, W36), a resume restores the saved
  `ROUND` (W36), and the only path to round n+1, `0x425190` = `0x425c50(+0x300 + 1, 0)` (W54), is never reached in
  WOTD (§8.5). So the label reads "1" for the whole game.
- **Only Untimed hides anything.** `@0x426535` compares the mode with `0x58fe00` = `"CLASSIC_UNTIMED"` (W52):
  true → `setVisible(0)` (vtable `+0x5c`) on `+0x234`, `+0x238` and `+0x244`; false → `setVisible(1)` (W50). WOTD
  is not Untimed, so the clock and "TIME:" show, as in Timed.

Worked example, a fresh WOTD game: `SCORE: 0`, `ROUND: 1`, `TIME: 2:00`. Untimed round 3: `ROUND: 3`, no clock.

Port check: `ui/src/main.cpp` `draw_game` draws the clock when `timed()`, which `game_scene.hpp` makes true for
Timed and WOTD, and "ROUND:" with `round_text()` when `round_visible()`.
The port hides "ROUND:" and its number in WOTD, deviation D6 (user decision 2026-10-22, `wotd-crossword.md`).

### 8.11 W5 audit against the exe (2026-10-22)

Claude ran this audit for T3.1 of `docs/plans/wotd-crossword.md`. Method: frame dumps of the port's WOTD
(`tt2 --play wotd --fixed-dt 20 --fps 4 --dump-frames`, driven with `--type`: tutorial, play, first bingo with
popup 11, DONE, both end cards, HIGH SCORES with the date, the greyed menu button), compared with §8 and the
install's `dialogs.xml` / `text.xml`; then REA reads for every difference. The gap list with decisions is
`docs/plans/wotd-gaps.md` (local, gitignored). Evidence W55-W78 (§8.9).

- **The spot button while the scene is blocked** (W7, W14, W21, W39, W76). See §5: `0x425710` only disables
  when `+0x260` or `+0x2f9` is set. `0x428840` also disables `+0x224` itself before its refresh; `0x4253a0`,
  `0x428610` and `0x425420` never call setVisible (`0x425420` ends with the refresh). So DONE, or GIVE UP after
  YES, stays on screen, greyed, through the reveal, state 3 and behind the end card; in Classic the same holds
  for NEXT ROUND and the PASS buttons. Port: it hid them; fixed in `GameScene::refresh_buttons`.
- **The all-words reveal is 1000 ms in every mode** (W15, W55; §5). Port: Classic held 3000 ms; fixed
  (`ALL_WORDS_REVEAL_MS`).
- **Speed Speller is checked on every `0x428840` call** (W14: `114999 < +0x2fc < 120001`, no caller test), so
  NEXT ROUND and DONE win it too. Port: it skipped NEXT ROUND; fixed.
- **Trophies during a reveal** (W17, W77). With no state pending, `0x426860` jumps straight to the dequeue
  (@`0x426cd9`): trophies (@`0x426ce6`) and popups (@`0x426d05`) need only `+0x260 == 0`; the clock needs `+0x260`,
  `+0x2f9` and `+0x264` all clear (@`0x426d24`-`0x426d44`). `0x428840` and a time-up miss with lives left
  (`0x425420`, which sets `+0x260` only on the third miss, @`0x4256ab`) leave `+0x260` at 0, so a trophy already
  won shows **during** their reveal. Pass and GIVE UP come from a dialog whose YES (`0x4253a0`) does not clear
  it, and the WOTD time-up miss sets it @`0x427005`, so their trophies wait. Port: it held every trophy until the
  round-end steps were done; fixed (`reveal_unblocked_`). The Classic round count moved to the moment
  `DIALOG_NEXTROUND` opens, so its popup still shows after that dialog, as when the original counts on its OK.
- **The WOTD round's own trophy never shows** (W17, W18, W24, W34, W40, W56-W65, W78). State 3 sets
  `+0x260 = 1` @`0x426b5c` before `count_round(4)` pushes its id. The scene code writes 0 to `+0x260` at: the
  ctor `0x423640` @`0x4236e4` (which also empties the trophy queue, W61), scene start `0x4242f6` / `0x42430f`,
  round start `0x425c96`, the dispatch default `0x426cc4`, `0x427b10` @`0x427b11` (reached only through vtable
  `+0xe0`: the pause menu `0x40a3a3` and the tutorial `0x40e1d1` / `0x40e2ce`; W62, W64, W65), PASS / GIVE UP NO
  `0x428720` @`0x428818` (W63) and the Classic next round `0x428aa0` @`0x428b38`. None runs after state 3 in WOTD:
  `0x428c80` → `0x427d10` opens the end card (W24, W40); CONGRATS OK `0x401b50` → `0x428540` opens HIGH SCORES
  (W58, W59), whose hide `0x40ce80` does not touch the owner (W60); NEXTROUND / ENDGAME OK → owner `+0xdc` =
  `0x427b20` → `0x427b40`, a new `SCENE_SELECTION` (W34, W56, W57). So Well Rounded (or a round tier) is **won and
  saved**, shown as new in the trophy room, but its popup never plays: not before, over, or after the end card.
  Port: the same.
- **"Uncommon" means not on the board** (W73-W75), §8.3. Port: fixed in `GuessProcessor::apply`.
- **Popup 11 uses the message timing**, §8.5. Port: already so.
- **The WOTD reseed changes no gameplay draw** (W66-W72), §8.2. Port: no change.
- **D1 on a frame dump.** With the player's own `common.tree` the port's word for 2026-10-22 (word day 6623) is
  `airport`, the word the 2008 game picks (§8.2: same pool, same seed, same index); with the shipped
  `open-common.tree` it is `analogue`. The port keeps D1 (`Bag::word_of_the_day_for_day`).
- Everything else on the dumps matched §8: `BACKGROUND_WOTD`, TIME from 2:00 with ROUND hidden (D6), GIVE UP then
  DONE at (317, 473), popup 11 at (400, 150), the GAME OVER card with MAIN MENU centred and no REPLAY, the
  NEXTROUND "back to the menu" card, the `2026-10-22` date in HIGH SCORES, and the greyed WOTD menu button
  afterwards ("word of the day already played today").

## 9. Animation inventory (REA batch O4, 2026-10-22)

Claude ran this batch for T9.1 of `docs/plans/wotd-crossword.md` (task 8.1 of `untimed-polish.md`). Same binary,
Ghidra 12.1.4 through REA, one session, plus two local reads of the exe with `/usr/bin/python3` and `objdump`: an
RTTI walk that maps every `GF2::Modifier*` class to its vtable (AN28), and a scan of the whole `.text` listing for
every vtable store and constructor call of those classes (AN29, 155 raw matches). The REA xref and caller lists
(AN7-AN18) match that scan site for site. Evidence ids AN1-AN30 are listed in §9.5. Tags as at the top of this file;
**UNCONFIRMED** = searched for and not found, with the search trail.

Short answer: every animation the exe builds for the Classic scene and the dialogs over it is already in the port.
Of the rows the plan expected to be missing, the slot fill is built, and the score count-up, a ball hover / press
effect and a GAME OVER animation do not exist in the original. Only `DialogPause` (key P) is not ported, by choice
(§6).

### 9.1 Modifier classes and their duration fields (CONFIRMED)

A GF2 modifier is a small object. The code passes its duration in a register, and the constructor stores it in a
field. Most classes derive from `ModifierFraction` (vtable `0x597624`). Its update `0x47ed40` (AN2) adds the frame
time to `+0x48`, ends the modifier when `+0x48 >= +0x44` (after `+0x4c` passes; 1 = once), and stores
`+0x50 = ease(+0x48 / +0x44)` through the function pointer at `+0x54`. So for every Fraction subclass **`+0x44` is
the duration in ms and `+0x54` the easing**. The base constructor `0x47ece0` takes the duration in `ESI` and the
easing in `EDX`; `EDX = 0` stores the linear `0x4307e0` instead (AN1). Smoothstep `0x4513d0` is in §4.

| Class (vtable) | Built by | Duration | Easing | Other fields | Evidence |
| --- | --- | --- | --- | --- | --- |
| `ModifierFraction` (`0x597624`) | base ctor `0x47ece0` | `ESI` → `+0x44` | `EDX` → `+0x54`; 0 = linear `0x4307e0` | `+0x48` elapsed, `+0x4c` passes (1), `+0x50` eased value | AN1, AN2 |
| `ModifierDelay` (`0x5943bc`) | `0x450b10`, or inlined | `EDX` → `+0x44`, counted down by the update `0x450c00` (file read) | none | `+0x48` / `+0x4c`: optional node to wait for instead | AN1, AN13 |
| `ModifierAlpha` (`0x59440c`) | `0x451030`, or inlined | `EDX` → `+0x44` | always linear | from `[esp+4]` → `+0x58`, to `[esp+8]` → `+0x5c` | AN1, AN16 |
| `ModifierScale` (`0x59700c`) | always inlined: `0x47ece0`, then the vtable store | `ESI` | `EDX`: smoothstep `0x4513d0`, or 0 = linear | from (x, y) `+0x58` / `+0x5c`, to `+0x60` / `+0x64` | AN1, AN14 |
| `ModifierLocation` (`0x597f24`) | `0x486560` | `EDX` → `+0x44` | linear | start (x, y) from the two stack floats | AN1, AN9 |
| `ModifierLocationLine` (`0x59446c`) | `0x486560`, then the vtable store (inlined), or the XML helper `0x451400` | `EDX` | the caller writes `+0x54`; `0x451400` picks smoothstep when its 4th argument is set | target (x, y) `+0x70` / `+0x74` | AN1, AN17 |
| `ModifierFrame` (`0x5971dc`) | `0x4793c0` (whole sheet), `0x479420` (range), or inlined | first stack argument → `+0x48`, **ms per frame**; 0 = set the frame at once and stop | none | `EDX` → `+0x44`, **passes** (1 = once, ≤ 0 = loop); `+0x4c` first, `+0x50` last (−1 = sheet end), `+0x5c` ping-pong | AN1, AN3, AN15 |
| `ModifierVisible` (`0x59768c`) | inlined (`0x47ef30` for XML) | instant | none | `+0x44` visible flag | AN4, AN18 |
| `ModifierClipRect` (`0x5970c4`) | `0x478390` | `ESI`, passed on to `0x47ece0` | **always linear**: `0x478390` starts with `XOR EDX, EDX` | from rect `[EBX]`, to rect `[EDI]` | AN1, AN30 |
| `ModifierSimpleAlpha` (`0x591a8c`) | `0x4304d0` (Lightning only) | fixed **400 ms** | linear | from, to | AN2 |
| `ModifierRotation` (`0x59714c`) | `0x478b70` (XML), or inlined (Crossword `0x41c9d6`) | `+0x50`; the update `0x478bf0` divides the elapsed `+0x4c` by it | byte `+0x54`: 1 = smoothstep (inline `(3 − 2t)·t²`), 0 = linear | from angle `+0x44`, to `+0x48`; byte `+0x55` = endless spin | file read |
| `ModifierLetterballContainer` (`0x591a14`) | `0x430320` | none | none | a named group (`"LB_ANIM"`) that holds a ball's move chain at ball `+0x184` | AN2, AN6 |

Two readings change:

- `0x47ece0` is the **`ModifierFraction` base constructor**, not a `ModifierScale` one. `ModifierScale` has no
  constructor of its own: every site calls `0x47ece0` and then stores the vtable `0x59700c` inline (AN14). The
  layout is the same, so the durations read earlier from `ESI` (§5.1, `crossword.md`, `lightning.md`) stand.
- `ModifierFrame` takes **`EDX` = passes** and the **first stack argument = ms per frame**, not "`EDX` = ms per
  frame" as the table in `welcome-flow.md` §0 has it. The update `0x479710` (AN3) steps the frame each time `+0x54`
  reaches `+0x48`. At the last frame it ends when `+0x44 == 1`, counts `+0x44` down when it is larger, and loops
  when it is 0 or less; `+0x48 = 0` sets the frame at once. Every caller in game code passes `EDX = 1` (the XML
  sprite parser aside, §9.4), with 50, 60, 70 or 100 ms on the stack (`0x4293a3`, `0x43a8d9`, `0x42debb`,
  `0x42bc40`). The frame times in this file and in the
  flows of `welcome-flow.md` already follow this reading; only that table is off.

### 9.2 Classic scene animations

One row per animation the Classic scene (Untimed, Timed, Word of the Day) and the dialogs over it can show. "built
(matches)" means the original has no animation there and the port adds none. Port paths are under
`ui/include/tt2ui/`.

| # | Target | Modifier chain | Duration / easing | Address | Port status | Tag | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Found word, per letter (a guess) | Delay → Scale tile y 1 → 0 → Frame letter (set) → Scale tile y 0 → 1 ∥ Scale letter y 0 → 1 ∥ Visible(1) | Delay `i·100`; each Scale 150 ms (`+0x44 = 0x96`), linear (`+0x54 = 0x4307e0`); Frame 0 ms | `0x432b20` → `0x4310f0(slot, 0)` | built, `game_scene.hpp:60-93` (`anagram_tile_at` :78) | CONFIRMED | AN4, AN19, §2 |
| 2 | Missed word, per letter (round-end reveal) | as 1, plus Frame tile → 1 (yellow) after the squash | as 1 | `0x4333a0` → `0x4310f0(slot, 0)` | built, `game_scene.hpp:86` | CONFIRMED | AN4, AN19, §2 |
| 3 | Restored word (resumed game) | as 1, every letter at once | 300 ms for the whole word | `0x433090` → `0x432b20` → `0x4310f0(…, 1)` | built, `game_scene.hpp:71-80` | CONFIRMED | AN4, AN19, §2 |
| 4 | Rack ball, round start | Delay → Frame first .. first + 11 ∥ Method `0x43a940` (`SOUND_letter_load`) | Delay `i·120`; 60 ms per frame (`+0x48 = 0x3c`), one pass (`+0x44 = 1`): 720 ms | `0x425c50` → `0x43c940` → `0x43a820` | built, `game_scene.hpp:173-174`, `:238-268`, `:1046` | CONFIRMED | AN5, AN20, §4 |
| 5 | Ball placed or taken back (click, typing, Backspace, CLEAR, LAST WORD) | Container `"LB_ANIM"` → LocationLine ∥ Scale → MethodParam `0x43a420` (re-parent) → Method `0x43a2f0` (clears `+0x184`) | slide 300 ms (`EDX = 0x12c` @`0x43a5eb`), smoothstep; Scale 300 ms (`ESI = 0x12c` @`0x43a65e`), linear, to the caller's scale: Classic passes 1.0 (`fld1` @`0x4246e6`, `0x424e6f`, `0x424fca`, `0x425142`, `0x427838`, `0x4278c3`, forwarded @`0x43bd2a`), so it does not show | `0x43bad0` → `0x43a4c0` (container set) | built, `game_scene.hpp:175`, `:239-242`, `:1513`; the 1.0 → 1.0 scale is not modelled | CONFIRMED | AN5, AN6, AN21 |
| 6 | Twist relayout | LocationLine → Method `0x43a2f0` | 400 ms (`EDX = 0x190` @`0x43a781`), smoothstep | `0x425240` → `0x43c370` → `0x43a4c0` (no container) | built, `game_scene.hpp:176`, `:1609` | CONFIRMED | AN6, AN21 |
| 7 | Ball hover | none; the hand cursor only | none | `0x427a60` | built (matches), `game_scene.hpp:746-750` | CONFIRMED | §4, AN29 |
| 8 | Ball press | none of its own; it starts row 5 | none | `0x427680` | built (matches) | CONFIRMED | §4, AN29 |
| 9 | Score label | none; setText jumps to the new total | none | `0x424730` | built (matches), `game_scene.hpp:635` | CONFIRMED | W37, W55, AN29 |
| 10 | Clock, last 10 s | none; one `SOUND_game_clock_countdown` per second | none | `0x426860` | built (matches), `game_scene.hpp:930-931` | CONFIRMED | W17, AN29 |
| 11 | Message popup (kinds 0, 3, 5, 8, 10, 11) | Alpha 0 → 1 → Scale 0 → 1.2 → Scale 1.2 → 1 → Delay → Alpha 1 → 0 → Method `0x4235c0` | 150 linear; 250 smoothstep; 100 linear; 900; 250 linear: 1650 ms | `0x428ec0` | built, `scene_popups.hpp:53-60`, `:131-140` | CONFIRMED | §5.1, AN1, AN7, AN8, AN10 |
| 12 | Banner popup (kind 1 ALL WORDS, kind 2 BINGO) | as 11, with Frame (whole sheet) → Delay in place of the 900 ms Delay | Frame 50 ms per frame (`push 0x32` @`0x4293a3`), one pass (`EDX = 1` @`0x4293a5`); Delay 1000: 2500 ms for 15 frames | `0x428ec0` @`0x42934f`-`0x4293e2` | built, `scene_popups.hpp:57-58`, `:135-136` | CONFIRMED | AN3, AN11, §5.1 |
| 13 | Trophy popup, trophy image | Alpha 0 → 1 → Scale 0.05 → 0.6 → Scale 0.6 → 0.5 → Delay → Alpha 1 → 0 → Method `0x4235d0` | 200 linear; 500 smoothstep; 300 linear; 2000 (`0x7d0` @`0x429b7c`); 500 linear: 3500 ms | `0x429510` | built, `scene_popups.hpp:162-163`, `:215-217`; `trophy_room.hpp:93-103` | CONFIRMED | `trophies.md` E2-E4, AN7, AN8, AN13 |
| 14 | Trophy popup, title | as 13, scale 0.05 → 1.2 → 1.0 | as 13 | `0x429510` | built, `scene_popups.hpp:224-226` | CONFIRMED | `trophies.md` E3, AN14 |
| 15 | Round end: reveal hold | Delay → Method `0x428a60` | the caller's: 1000 (all words) or 3000 | `0x428840` @`0x428a02` | built, `game_scene.hpp:395-399`, `:1239` | CONFIRMED | W14, W15, W55, AN13 |
| 16 | Miss (Pass, time-up miss): reveal hold | Delay | 3000 (`0xbb8` @`0x425653`) | `0x425420` | built, `game_scene.hpp:395`, `:1239` | CONFIRMED | W39, AN13 |
| 17 | Third miss: reveal hold before game over | Delay → `0x427d10` | 3000 (`0xbb8` @`0x428dc4`) | `0x428c80` | built, `game_scene.hpp:395`, `:1239` (Classic), `:1331` (WOTD) | CONFIRMED | W40, AN13 |
| 18 | GAME OVER | none; no modifier in `0x427d10`, the `GAME_OVER` sprite is never looked up, and the end card uses the dialog fade (row 20) | none | `0x427d10` | built (matches: not drawn), `scene_popups.hpp:25-27` | CONFIRMED | W24, W25, AN29, §5.1 |
| 19 | Commentary | none; `COMMENTARY` / `COMMENTS` are never looked up | none | none | built (matches: not drawn), `scene_popups.hpp:25-27` | CONFIRMED | §5.1 |
| 20 | Any dialog, show / hide | Alpha 0 → 1 / 1 → 0 | `DIALOG_FADE_TIME` 200 ms in `EDX`, linear | `0x40ccc0` / `0x40ce80` | built, `dialog_fade.hpp:30` (acts on the click, not after the fade: §6) | CONFIRMED | §6, AN1, AN7 |
| 21 | HOW TO PLAY, page change | Alpha 1 → 0 ∥ Alpha 0 → 1 | 200 ms each (`EDX = 0xc8`), linear | `0x404f50` | built, `help_dialog.hpp:41-42` | CONFIRMED | `help-and-settings.md`, AN7 |
| 22 | HOW TO PLAY, open | Delay on the dialog, nothing chained to it | 200 ms (`+0x44 = 0xc8` @`0x404ea9`) | `0x404e30` | built (matches: nothing to draw) | INFERRED (no visible effect, §9.4) | AN13, AN29 |
| 23 | `DIALOG_NEXTROUND` / `DIALOG_CONGRATS` letterballs | Delay → Frame, as row 4 | Delay `500 + 200·i`, then as row 4 | `0x409100` / `0x401290` → `0x43a820` | built, `game_scene.hpp:282-316`, `:1426-1434` | CONFIRMED | §6, AN20 |
| 24 | Same dialogs, particles (8 per ball) | Delay → LocationLine ∥ Alpha 1 → 0 | the ball's delay; 700 ms (`EDX = 0x2bc`), linear; `600 + rand % 90` ms, linear | `0x409100` @`0x409708`-`0x4098d6`; `0x401290` @`0x401898`-`0x401a66` | built, `game_scene.hpp:333-337`, `:361` (own random stream: §6) | CONFIRMED | §6, AN9, AN13, AN16, AN17 |
| 25 | Same dialogs, `SPARKLE` (`IMAGE_BURST`) | XML `frametime="40"` → Frame range from the sprite parser | 40 ms per frame, looping | `0x444e70` @`0x445f08` | built, `game_dialogs.hpp:92-99` | 40 ms CONFIRMED (`dialogs.xml`); the loop INFERRED (§9.4) | AN12, AN26 |
| 26 | `DialogPause` (key P), show | Delay → Method `0x40a1c0` | 100 ms (`+0x44 = 0x64` @`0x40a116`) | `0x409fa0` | different: not ported (§6) | CONFIRMED; `0x40a1c0`'s effect INFERRED (§9.4) | AN13, AN29 |

Worked example, a bingo on the last free 6-letter slot: the six letters flip in from 0 to `5·100 + 300 = 800` ms
(row 1), while the BINGO banner grows in over `150 + 250 + 100 = 500` ms and shows its 15 frames for 750 ms (row 12).

Bounded negative: the `TextTwist2Scene` methods themselves build modifiers only in `0x425420`, `0x428840`,
`0x428c80`, `0x428ec0` and `0x429510`. There is none in the ctor `0x423a20`, round start `0x425c50`, ENTER
`0x424730`, the refresh `0x425710`, the input handlers `0x427560` / `0x427680` / `0x427a60`, the update `0x426860`
or game over `0x427d10` (AN13-AN18, AN29). Everything else the scene shows moving comes from the AnagramTable flip
`0x4310f0` and the LetterBall helpers `0x43a4c0` / `0x43a820`.

### 9.3 Every construction site, by caller

Every site from AN29, grouped by the function that builds it; the constructors' own vtable stores are left out. The
Classic rows of §9.2 come from the first ten callers, plus the XML sprite parser in the last row (row 25).

| Caller | What it animates | Sites (class @address) | Documented in |
| --- | --- | --- | --- |
| `0x4310f0` | word flip (rows 1-3; Crossword reaches it through `0x436ff0`) | Delay @`0x4311ce`; Scale @`0x43129f`, `0x4314f2`, `0x4315bf`; Frame @`0x43135f`, `0x431414`; Visible @`0x43165d` | §2 |
| `0x43a820` | ball flip (rows 4, 23) | Delay @`0x43a869`; Frame @`0x43a8cc` | §4 |
| `0x43a4c0` | ball move (rows 5, 6; Lightning's reveal through `0x43c6a0`) | Container @`0x43a55b`; LocationLine @`0x43a60e`, `0x43a7a4`; Scale @`0x43a673` | §4 |
| `0x425420`, `0x428840`, `0x428c80` | reveal holds (rows 15-17) | Delay @`0x42564d`, `0x4289fc`, `0x428dbe` | §5 |
| `0x428ec0` | popups (rows 11, 12; every game scene) | Alpha @`0x42926f`, `0x42941c`; Scale @`0x4292b9`, `0x429316`; Delay @`0x42936c`, `0x4293dd`; Frame @`0x4293aa` | §5.1 |
| `0x429510` | trophy popup (rows 13, 14; every game scene) | Alpha @`0x429a39`, `0x429bbb`, `0x429c1e`, `0x429d8e`; Scale @`0x429a9b`, `0x429b02`, `0x429c7c`, `0x429cdf`; Delay @`0x429b76`, `0x429d4b` | `trophies.md` §1 |
| `0x409100` / `0x401290` | `DIALOG_NEXTROUND` / `DIALOG_CONGRATS` particles (row 24) | Delay @`0x409708` / `0x401898`; Alpha @`0x40981e` / `0x4019ae`; LocationLine @`0x4098d6` / `0x401a66` | §6 |
| `0x40ccc0` / `0x40ce80` | dialog show / hide (row 20) | Alpha @`0x40cd8d` / `0x40cf3f` | §6 |
| `0x404e30` / `0x404f50` | HOW TO PLAY open / page change (rows 21, 22) | Delay @`0x404ea3`; Alpha @`0x404f7f`, `0x404fbf` | `help-and-settings.md` |
| `0x409fa0` | `DialogPause` show (row 26) | Delay @`0x40a110` | §6 |
| `0x43a310` (only from `0x43c890`) | pearl ↔ square tile flip, 60 ms per frame, 5 frames | Frame range @`0x43a33f`, `0x43a392`, `0x43a3d2` | Lightning and Crossword only (AN22); `lightning.md` §1.1 |
| `0x43c990`, `0x43cb60` | hint letter (Lightning, from `0x4131f0`) | Frame @`0x43ca37`; Alpha @`0x43cb1c`; Visible @`0x43cbb7` | `lightning.md` §2.5 |
| `0x43cc30`, `0x43ce60`, `0x43d310` | Crossword letters and reveal (from `0x41c740`, `0x41fed0`) | Alpha @`0x43ccc9`, `0x43cef5`, `0x43d158`; Visible @`0x43cd2d`, `0x43d1ee`; Delay @`0x43cfdf`; Frame @`0x43d06e`, `0x43d38f` | `crossword.md` |
| `0x4116e0`, `0x411e10`, `0x411ef0`, `0x414780` | Lightning: +8 s bar, lose, reveal, word switch | ClipRect @`0x411b77`; Delay @`0x411eac`, `0x411f87`, `0x4129d8`, `0x4147fd`; Alpha @`0x41217f`, `0x4122c7`, `0x412752`; Scale @`0x4123a2`, `0x41249f`, `0x412827`, `0x41291a`, `0x41492f`, `0x414bad`; LocationLine @`0x4148ba`, `0x414ac4`; SimpleAlpha @`0x414a09`, `0x414c12` | `lightning.md` |
| `0x41b1c0`, `0x41c910`, `0x41cab0`, `0x41d160`, `0x41d390`, `0x41e930`, `0x41fed0` | Crossword: zoom out, turn, pans, zoom in, the 700 ms advance, reveal | Scale @`0x41b292`, `0x41d7ce`, `0x41ffba`; Delay @`0x41c960`, `0x41ea56`, `0x420175`, `0x42032b`; Rotation @`0x41c9d6`; LocationLine @`0x41cc6e`, `0x41d327` | `crossword.md` §3 |
| `0x42b080` (a `LoadScene` vtable slot), `0x42c760`, `0x42dd30`, `0x42fe30` | loading screen, menu fades, logo, trophy room | Delay, Alpha, Frame, Scale | `welcome-flow.md`, `trophies.md` |
| GF2: `0x444e70`, `0x4433e0`, `0x4462b0`, `0x441bb0`, `0x45bb60`, `0x46c2f0`-`0x46c8ac`, `0x477410`, `0x478b70`, `0x47e6e0`, `0x47e7e0`, `0x47ef30`, `0x484d00` | XML sprite attributes (`frametime`, row 25), the text blink (callers `0x4068d0`, `0x45e080`), fade-to-black and `SceneTransitionAlpha` scene changes, XML rotation / sound / visible / skew | all classes | §9.1, `welcome-flow.md` §0 |

### 9.4 Search trails (INFERRED items; none UNCONFIRMED)

No row in §9.2 is UNCONFIRMED, so F9 of the plan does not trigger: no parameter is left for the user to decide.

| Item | Searched | Result |
| --- | --- | --- |
| `SPARKLE` loop count (row 25) | `dialogs.xml` gives `frametime="40"`; the sprite parser `0x444e70` builds the Frame @`0x445f08` with `EDX` from a stack local filled by the attribute reads @`0x445cdc`-`0x445e98` (file read), not traced to one attribute | INFERRED loop while the dialog is up; the port loops |
| HOW TO PLAY open delay (row 22) | `0x404e30` adds a 200 ms `ModifierDelay` to the dialog through `0x448cf0` and chains nothing after it (file read) | INFERRED no visible effect |
| `DialogPause` method (row 26) | `0x40a1c0` is `mov eax, [0x5fde9c]; mov byte [eax+0x18], 1; jmp 0x463fc0`; `0x463fc0` not decompiled | INFERRED: pauses the engine 100 ms after the dialog opens |
| The ball move's scale (row 5) | Classic callers of `0x43bad0` pass 1.0 (row 5); `0x43a4c0` adds extra methods only when the scale is 0.5 (`0x43a6ac`, Crossword) | CONFIRMED no visible change in Classic |

Port notes (no change made here): `modifier.hpp:72` calls `0x47ece0` the `ModifierScale` constructor; it is the
`ModifierFraction` base, and the port's semantics are right. Comments that still call the Crossword durations
INFERRED (`crossword_scene.hpp:82-103`) can now say CONFIRMED.

### 9.5 Evidence ids (batch AN)

| Id | Call | evidence_id |
| --- | --- | --- |
| AN1 | pseudo `0x450b10`, `0x451030`, `0x47ece0`, `0x486560`, `0x451400`, `0x4793c0`, `0x479420`, `0x478390` (constructors) | `ev_558f1f3837d36efe5daf961c72f92e5c99fa9c0acd9fd3b74d4453144c828bf2` |
| AN2 | pseudo `0x47ed40` (Fraction update), `0x4304d0` (SimpleAlpha), `0x430320` (LetterballContainer) | `ev_56ffa74e8913f01573021daeea885da85a0dfd21da59a3e82fdc13f1e2aff723` |
| AN3 | asm `0x479710` (ModifierFrame update) | `ev_36ee274ce8940a6381f0d065a336abc21a1eb621801bbcf938da815f52b6364a` |
| AN4 | pseudo `0x4310f0` (word flip) | `ev_42bd88dc8f9f50aaba1be94d8bd5a3174390cdd65c3dbbabf202e87f13ea2cd3` |
| AN5 | pseudo `0x43a820` (ball flip), `0x43a4c0` (ball move) | `ev_a13e9af9f2096beac0ae69cf4cc8ffd7d854b5e2088d1d08b1a3f6f5a7945632` |
| AN6 | asm `0x43a4c0` (300 / 400 ms, easing, scale) | `ev_15bcab3045c66e3e536a066dcdc6da52bca82f35a50b96f78b59ac9ea79ee2a1` |
| AN7 | callers `0x451030` (ModifierAlpha) | `ev_133b82239b0c3646b0311e5b65be14c43dc4d1516de5b16cb0404fc27429c813` |
| AN8 | callers `0x47ece0` (ModifierFraction) | `ev_905ac0c3430ff4aeeeae5d13de238c420bc6e92f64a3f0b983d311089dbd034e` |
| AN9 | callers `0x486560` (ModifierLocation) | `ev_6fd46a130470aa38b95c3887b97703e36edba634007fef8b791e59357e26991b` |
| AN10 | callers `0x450b10` (ModifierDelay) | `ev_8cc65fcf167a37a08eda4acfbe908b104c1badcc0318bae1cbb414aaf9bf7e38` |
| AN11 | callers `0x4793c0` (ModifierFrame, whole sheet) | `ev_a92025c9bb571fb8580781360b0b1048651e028d3f51fa55e6d8a5da1560bd58` |
| AN12 | callers `0x479420` (ModifierFrame, range) | `ev_e268d1d69f7588b7d007a66f54ef3b132093a41629778d365b478b7b121cb294` |
| AN13 | xrefs `0x5943bc` (ModifierDelay vtable, 24 sites) | `ev_39f204df721e4df0c6799440226d8fb836f4322ac394be2b0ddbfa1f73c516ac` |
| AN14 | xrefs `0x59700c` (ModifierScale vtable, 22 sites) | `ev_3dfba6dabf406bf65c529b992afda569190dc9b2330facf23ff36812a0eac4ff` |
| AN15 | xrefs `0x5971dc` (ModifierFrame vtable, 10 sites) | `ev_a4393266192ce8e2d856db7232b683c10a94bd2ea406167fd6e19e43ea272707` |
| AN16 | xrefs `0x59440c` (ModifierAlpha vtable, 9 sites) | `ev_11f4759b306aa40632374c3a0ce9eedc68ebbfde8af77ac216756cb1c1814b80` |
| AN17 | xrefs `0x59446c` (ModifierLocationLine vtable, 9 sites) | `ev_70754c85700da9c0e3a11e6fff2dce2591d91abaa91de450d69524812bda5b9d` |
| AN18 | xrefs `0x59768c` (ModifierVisible vtable, 5 sites) | `ev_cd6d6c9cacf645b020d34d580dc0b2af00542be93f82237b2d8ff6568da3a2a1` |
| AN19 | callers `0x4310f0` (`0x432b20`, `0x4333a0`, `0x436ff0`) | `ev_37fa0de01b95d3044c492a4db83b320329fa86e26c7b1f150f5172afb7f4f8f3` |
| AN20 | callers `0x43a820` (`0x401290`, `0x409100`, `0x43c940`) | `ev_d8975dda67c8c659b15609daff45f13b30429d088979e74561747fae67b7e765` |
| AN21 | callers `0x43a4c0` (`0x43bad0`, `0x43c370`, `0x43c6a0`) | `ev_69aa7fefc1cad9827d04a4ef443aeb2fa16b94b464c6841d9cea07fd7174e840` |
| AN22 | callers `0x43a310` (only `0x43c890`) | `ev_6a0cc1cf74ce2811534e429b377e51af4f9a100c82335faa0e3371dd1f2e3af1` |
| AN23 | callers `0x43ce60`, `0x43d310` (both `0x41fed0`), `0x43cc30` (`0x41c740`), `0x43cb60` (`0x4131f0`) | `ev_475d31209fdbdf61c3e5ac19cde6a837ecebdb897f9e7c226bdf780b5b303452`, `ev_c1706d63016dcf060e7b4d6587ab994a13a019eb330271d90c7b71ba0db3f6fa`, `ev_59b713be4d9fa19b3475426a15357caa802352ff925aee36fc5cf7314db5a768`, `ev_ae6888de3e3277fb40d3c078365cab414425e95d9c417a50e828abe7d072ee08` |
| AN24 | containing procedure of `0x43ca37` (`0x43c990`), `0x43cef5` (`0x43ce60`), `0x43d38f` (`0x43d310`), `0x42bc03` (`0x42b080`) | `ev_12677c97ba450fbbdbcead011f56d7af733853543881915fd30a8244a7545e21`, `ev_3f67f418de2e362e756381e1fca51e6dcfe83c0c723ad9694f06e36e14f8e36e`, `ev_f0a904374621d4e31b1765a45bc86011081df4565785820681209263824f0688`, `ev_f467946e7c0883ba94ffe6d6c17cfaff080eb7eef7cdf46c301a3620edc71bc0` |
| AN25 | callers / xrefs `0x42b080` (none / the `LoadScene` vtable slot `0x590c20`) | `ev_755b91cc2acc55d08afbcca12e48aa6f0b54647ce9ebfdb1cf108053a6b25b54`, `ev_0a66e9e5bb3e4eeb67267e57f3a9e5f61ac3f2e619f52155cf1213e4654466f8` |
| AN26 | callers `0x45bb60` (Blink), `0x441bb0` (none), `0x444e70` (XML sprite parser, from `0x47b4a0`), `0x4433e0` (from `0x442ac0`) | `ev_ee6d9ce6e9c747fd0f35f18ac88e7964aac2385d83d1335ca80d4451ab2e3df9`, `ev_0c90b32d9af99a9c457f98b502597bc62be25685f6227b9eea0efd84c455b8df`, `ev_cd4f3f076e94b73a53656b09802f4d87e4e9f885cca5e0a5388f5895141f484d`, `ev_885f62ae00400ce2685d18010f55af8d132443e53d559593034febe70ee9600f` |
| AN27 | callers / xrefs `0x477410` (none / `SceneTransitionAlpha` vtable slot `0x596c18`), callers `0x42fe30` (`0x42fb20`) | `ev_1e8220977ef7d52cf6d5665db9fc37c6cc552a4145fe44424af474d949b0b1a7`, `ev_6d920b7570aa462d116ea26400926aeba52c3fd380c8cc1d1c3c4918d49d7029`, `ev_83c96fd1eba60b9dfbe949cfaf230d7c7deab020a9d2731b5da058631feeb2e2` |
| AN28 | RTTI walk: `.?AVModifier*@GF2@@` type descriptors → complete object locators → vtables | none (Python over the exe) |
| AN29 | `objdump -d` of `.text`: every vtable store and constructor call of AN28's classes, grouped by function | none (local listing) |
| AN30 | asm `0x478390` (`ModifierClipRect`: `XOR EDX, EDX`, then `0x47ece0`) | `ev_980f5c0007c26f44ef7c4923124c0ff182b92fa6266a9988dfd4a2a9ba827a9a` |

## 10. Address index

| Address | What |
| --- | --- |
| `0x47ece0` / `0x47ed40` | `ModifierFraction` base ctor (`ESI` = duration, `EDX` = easing) / its update (§9.1) |
| `0x450b10` / `0x451030` / `0x486560` | `ModifierDelay` / `ModifierAlpha` / `ModifierLocation` ctors, `EDX` = duration (§9.1) |
| `0x4793c0` / `0x479420` / `0x479710` | `ModifierFrame` ctors (`EDX` = passes, stack = ms per frame) / its update (§9.1) |
| `0x478390` | `ModifierClipRect` ctor: always linear (§9.1) |
| `0x4310f0` | Word-appear flip (§2, §9.2) |
| `0x429510` | Trophy popup (§9.2, `trophies.md`) |
| `0x423a20` | `TextTwist2Scene` ctor: looks up the components, loads both `.tree` files |
| `0x43e070` | Recursive `.tree` node reader (letters, `0`/`1`, children); anchor check 2026-10-14 `ev_b0fd200dbf3523b14f18941871fe88b17c0aed2eac8ed5f6991650daf2055e70` |
| `0x425c50` | Round start: timer / round / score reset, rack pick, `AnagramTable` ctor |
| `0x426360` | After setup: score / round / timer texts, clock hidden in Untimed only, panel pieces fitted (§8.10) |
| `0x423d65` | Ctor: the only `LABEL_ROUND` (`0x5905d8`) lookup, kept in `+0x240`; "ROUND:" is never looked up (§8.10) |
| `0x424730` | ENTER: guess, sounds, score, popup kind per result |
| `0x428e20` / `0x428e60` | Popup queue push / pop (`+0x334`) |
| `0x428ec0` | Popup: sprite by kind at (400, 150), fade / scale / hold / fade chain |
| `0x427680` | Mouse down on the letter rows (place / take back) |
| `0x431e50` | `AnagramTable` layout (columns / rows) |
| `0x430800` | One word of the grid (tiles + small letters) |
| `0x4396e0` | `AnagramTable` ctor: grid position, two `LetterballContainer`s, balls |
| `0x43ab90` | `LetterBox` ctor (the letter row container): cell sprites, frame by row |
| `0x439d00` | `LetterBall` ctor: `IMAGE_LETTERBALL_ANIM` frame = letter × 17; `+0x184 = 0`, `+0x188` = the letter (§4.1) |
| `0x43bad0` | Put a ball into a container cell (first free cell for index −1) |
| `0x43c370` / `0x43c940` | Container relayout / reveal animation |
| `0x43a820` / `0x43a940` | Ball flip (12 frames, 60 ms) / its `SOUND_letter_load`, as the flip starts |
| `0x425240` / `0x43c280` | Twist (sound, then relayout) / may the rack twist now |
| `0x43a2f0` | End of a ball move: clears `+0x184` |
| `0x4634b0` | Play a sound on a free voice (FMOD 3 shape) |
| `0x484770` / `0x46de50` | `WM_KEY*` / `WM_CHAR` → key events (repeats kept) / main message loop |
| `0x43a4c0` | Ball move: 300 ms into a container, 400 ms on relayout |
| `0x407f40` / `0x408320` | Pause menu ctor / "Exit Game?" confirmation |
| `0x40b470` | Yes/no dialog: buttons below the message, height grows |
| `0x4242a0` / `0x42dc80` | Scene start (load or new game) / "Continue the game?" prompt |
| `0x429dd0` / `0x42a460` | Save / load the running game |
| `0x427d10` | Game over: high score, then `DIALOG_CONGRATS` or `DIALOG_ENDGAME` |
| `0x426860` / `0x427058` | Scene state machine (`+0x20c`) / its jump table |
| `0x425b60` | Button dispatcher (Menu, NextRound, Pass / GiveUp, Clear, Enter, LastWord, Twist) |
| `0x425710` | Button refresh: letter buttons, NEXT ROUND / Pass_2 / Pass / GIVE UP |
| `0x4253a0` / `0x425420` | Pass / Give Up YES / miss path (reveal or game over) |
| `0x409100` | `DIALOG_NEXTROUND` letterballs and particles |
| `0x4513d0` | Smoothstep easing `(3 − 2t)·t²` |
| `0x468ea0` | Label XML parser (`wrapstyle`, `alignh`, `autofitheight`) |
| `0x468b20` / `0x595df4` | Label ctor / Label vtable |
| `0x4698c0` / `0x469a40` / `0x46a7b0` | Label setText / layout tokenizer / word flush with the wrap rule |
| `0x428a80` | Sets state 6 (`DIALOG_PASSROUND`) after a Timed time-up miss |
| `0x43d690` | WOTD rack: global LCG reseeded with 1337, shuffled pool, `pool[utc_day - 14125]` (§8.2) |
| `0x4262b0` | WOTD board cap: first 5 common sub-anagrams per length 3-8 (§8.3) |
| `0x427cc0` | Delete the save; in WOTD `player.WOD = +0x1ec` (§8.6) |
| `0x43ff60` | UTC `"yyyy-MM-dd HH-mm-ss"` (`GetSystemTime`); WOTD high score keeps the date (§8.7) |
| `0x4099c0` / `0x404540` | WOTD bodies for `DIALOG_NEXTROUND` / `DIALOG_ENDGAME` (§8.5) |
| `0x409a70` | `DIALOG_NEXTROUND` OK: owner `+0xd8` (next round) or, in WOTD, `+0xdc` = `0x427b20` (leave) |
| `0x428a60` | Round-end delay callback: `+0x2f9 = 0`, state 3 |
| `0x428840` | Round finished: `+0x2f9 = +0x264 = 1`, Speed Speller window, reveal, `ModifierDelay(arg)` (1000 / 3000), `+0x224` disabled, refresh |
| `0x432330` / `0x432850` | Table list `+0x180` = the board words (count `+0x184` = slots) / guess check: "uncommon" = not in that list (§8.3) |
| `0x427b10` / `0x427b40` | Clear `+0x260` and refresh (vtable `+0xe0`: pause menu, tutorial) / leave for `SCENE_SELECTION` |
| `0x423640` | `GameScene` ctor: `+0x260 = 0`, trophy queue emptied (§8.11) |
| `0x43c370` / `0x439370` | Deal and twist relayout: shuffle until the row differs from before and from the word / MSVC `random_shuffle` on the CRT `_rand()` (§4.1, §8.2) |
| `0x4261c1`-`0x426266` | Round start: `AnagramTable` ctor, `0x43c370([table+0xb0], table+0x1c, 0)`, `0x43c940(1)` (§4.1) |
| `0x43c170` | The row's letters as a UTF-8 string, empty cells skipped (§4.1) |
| `0x45fa60` / `0x45f8c0` / `0x460410` / `0x422aa0` | UTF-8 string: code-point count (cached at `+0x20`) / byte offset of code point `i` / decode one code point, no case change / append one (§4.1) |
| `0x402da0` | Byte compare behind the string `==` (§4.1) |
| `0x413090` / `0x414340` | Lightning TWIST / word build: both deal through `0x43c370` (§4.2) |
| `0x41b060` / `0x434f40` | Crossword TWIST / word object: both deal through `0x43c370` (§4.2) |
