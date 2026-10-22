# Lightning / Blitz mode (BlitzScene, SCENE_BLITZ)

Decompiled with Ghidra 12.1.4 through REA on 2026-10-16, binary `TextTwist2.exe`
(sha256 `0c6e962c…a9e801`). The port is `engine/include/tt2/round_lightning.hpp` (rules) and
`ui/include/tt2ui/blitz_scene.hpp` (scene); §8 lists where it differs.
Tags: **CONFIRMED** (read in code or shipped XML), **INFERRED** (strong reading, not proven),
**UNKNOWN** (not found). Constants marked "file read" were read from the exe's data sections with
`/usr/bin/python3` (same bytes Ghidra decodes), not through an REA call.

## 0. Corrections to the older notes (now fixed there)

- `gameplay-notes.md` §4 said the Blitz timer-out loss path was not found. It is the update
  `0x415300` @`0x41597f`: `if (scene+0x2fc <= 0) 0x411e10()`. There is no `SOUND_game_clock_timeup`
  in Blitz, so the old xref search could not find it. **CONFIRMED**
- `gameplay-notes.md` §3 / `architecture.md` §6 "Hint button … reveals one rack letter for free":
  in the shipped `dialogs.xml` there is **no `BUTTON_Hint` in SCENE_BLITZ**. Hints are given
  **automatically** by a per-word timer (9 s, then every 4 s). The code still handles a
  `BUTTON_Hint` click (`0x4135e0` → `0x4131f0`), but no such component exists. **CONFIRMED**
- `profiles.md` §4.3 named `0x416440` "Round end"; it is the **Blitz game-over** handler (submits
  to the `blitz` table). **CONFIRMED**
- The `max(5, …)` floor on the word length is real (`fcomp [0x59ba88]=5.0` @`0x413eb5`, then the
  float `5.0` at `0x59ba78`). The decompiler hides it. **CONFIRMED**

## 1. Scene layout (dialogs.xml `<BlitzScene id="SCENE_BLITZ" background="BACKGROUND_BLITZ" section="blitzscene">`)

All positions are from the shipped XML (**CONFIRMED**). Images are named by id only.

| Component | Image / font | Position | Notes |
| --- | --- | --- | --- |
| `UI_BAR` | `IMAGE_UI_BAR` (gamescene) | (0, 547) | shared bottom bar |
| `ANAGRAMS` | AnagramTable | (100, 75) | present in XML; Blitz builds its own word objects (§2) |
| `LABEL_Score` / `SCORE_LABEL` | FONT_UI_NUMBERS / FONT_UI_LABELS | (251, 574) left / (244, 574) right | `TEXT_UIBAR_SCORE` |
| `LABEL_TimerMinutes` | FONT_UI_NUMBERS, text "00:" | (749, 574) anchor right | gets `"%d"` minutes |
| `LABEL_TimerSeconds` | FONT_UI_NUMBERS, text "00" | (752, 574) anchor left | gets `":%02d"` seconds |
| `TIME_LABEL` | FONT_UI_LABELS | (707, 574) anchor right | `TEXT_UIBAR_TIME` |
| `LABEL_ROUND (copy)` / `LABEL_ROUND` | labels / numbers | (537, 574) / (542, 574) | `TEXT_UIBAR_ROUND` |
| `LABEL_HINTNUMBER` | FONT_UI_NUMBERS "0" | (551, 481) | `visible="false"`; no code reference found (UNKNOWN use) |
| `BUTTON_Menu` | `BUTTON_MENU` | (10, 554) | command `BUTTON_Menu` |
| `BUTTON_Twist` | `BUTTON_Twist` | (253, 390) | |
| `BUTTON_Clear` | `BUTTON_Clear` | (375, 390) | |
| `BUTTON_LastWord` | `BUTTON_LastWord` | (498, 390) | |
| `BUTTON_Enter` | `BUTTON_Enter` | (622, 390) | |
| `BUTTON_PREVWORD` | `IMAGE_BTN_ARROW_UP` (blitzscene) | (60, 12) | command `BUTTON_PrevWord`, tooltip `CTOOLTIP_Clear` (sic) |
| `BUTTON_NEXTWORD` | `IMAGE_BTN_ARROW_DOWN` (blitzscene) | (60, 491) | command `BUTTON_NextWord` |
| `BUTTON_NextRound` | `BUTTON_NextRound` | (417, 456) | always hidden + disabled in Blitz (§4) |
| `BUTTON_GiveUp` | `BUTTON_GiveUp` | (417, 456) | |
| `COMMENTARY`, `GAME_OVER` | gamescene sprites | (316,106), (291,264) | alpha 0 (unused, as in Classic) |
| `SPRITE_TIMER` | `IMAGE_TIMERBAR` (2 frames) | (343, 110), scalex 1 | the time bar |
| `PANEL_BG` | `IMAGE_PANEL_BG` | (0, 275), anchor/pivot left | word-list panel, resized per round |
| `PANEL_BOTTOM` / `PANEL_LEFT` | `IMAGE_PANEL_BOTTOM` / `IMAGE_PANEL_TOP` | (0, 500) / (0, 100) | |
| `PANEL_LEFTCORNER` / `PANEL_RIGHTCORNER` | `IMAGE_PANEL_CORNERTOPR` / `IMAGE_PANEL_CORNERR` | (197, 100) / (197, 500) | |
| `PANEL_RIGHT` | `IMAGE_PANEL_SIDER` | (197, 300) | |
| `PAUSE_VEIL` | `IMAGE_PAUSEVEIL` | (0, 0) | hidden at init, shown with DIALOG_MENU |

- Component lookup (CONFIRMED, `0x423a20` shared ctor + `0x410f00` Blitz extra): `+0x210` BUTTON_Menu,
  `+0x214` BUTTON_Twist, `+0x218` BUTTON_Enter, `+0x21c` BUTTON_LastWord, `+0x220` BUTTON_Clear,
  `+0x224` BUTTON_NextRound, `+0x228` BUTTON_GiveUp, `+0x234` LABEL_TimerMinutes, `+0x238`
  LABEL_TimerSeconds, `+0x240` LABEL_ROUND, `+0x244` TIME_LABEL, `+0x23c` LABEL_Score, `+0x248`
  PANEL_LEFT, `+0x24c` PANEL_LEFTCORNER, `+0x250` PANEL_RIGHT, `+0x254` PANEL_RIGHTCORNER, `+0x258`
  PANEL_BOTTOM, `+0x25c` PANEL_BG; Blitz: `+0x37c` SPRITE_TIMER, `+0x380` BUTTON_NEXTWORD, `+0x384`
  BUTTON_PREVWORD, `+0x378` PAUSE_VEIL (setVisible(0) at init). Popup anchor for Blitz is
  **(400, 55)** (`+0x34c = 400.0`, `+0x350 = 55.0` @`0x410f00`), not Classic's (400, 150).
- Resources (`resources.xml` section `blitzscene`, CONFIRMED) and PNG sizes (PIL):

| Id | File | Size | Frames |
| --- | --- | --- | --- |
| `BACKGROUND_BLITZ` | background_blitz.png | 800×600 | 1 |
| `IMAGE_TIMERBAR` | timerBar.PNG | 321×92 | 2 (1 col) → 321×46 each |
| `IMAGE_TIMERBOX` | timerBox.PNG | 304×24 | 1 (not placed in XML) |
| `IMAGE_BLITZ_MARK` | x.png | 22×44 | 2 (1 col) → 22×22 each |
| `IMAGE_HINTBUTTON` | button_hint.PNG | 119×200 | 4 (not placed in XML) |
| `IMAGE_HINTCIRCLE` | hintCircle.png | 60×60 | 1 (not placed) |
| `IMAGE_PANEL_BG` | panel_bg.png | 195×600 | 1 |
| `IMAGE_PANEL_TOP` / `_BOTTOM` | panel_top/bottom.png | 1×10 | 1 |
| `IMAGE_PANEL_SIDER` | panel_sideR.png | 10×1 | 1 |
| `IMAGE_PANEL_CORNERR` / `_CORNERTOPR` | panel_cornerR / cornertopR.png | 10×10 | 1 |
| `IMAGE_PAUSEVEIL` | pauseshield.png | 220×545 | 1 |
| `IMAGE_BTN_ARROW_UP` / `_DOWN` | btn_arrow_up/down.png | 76×248 | 4 → 76×62 each |
| (unreferenced files) | button_next.png 119×200, button_previous.png 157×208, check.png 22×22, pauseVeil.png 210×600 | | |

### 1.1 How the n words are shown (CONFIRMED unless tagged)

- Every word of the round is a separate object in the vector `scene+0x3a0/+0x3a4` (built by `0x414340`).
  Each has a scrambled **rack** LetterBox `word+0xb0`, an **entry row** LetterBox `word+0xb4`, a solved
  flag `word+0x88`, hint count `word+0xc0`, an auto-hint timer `word+0xbc`, the per-word last word at
  `word+0x64`, and the answer letters at `word+0x10/+0x14`. The current word is `scene+0x398`, its index
  `word+4`.
- **List**: all racks sit on the left at x = 10, y = `base + i*40`, scale 0.33, where
  `base = 285 − n*40*0.5` (`scene+0x390`, @`0x413d19`). Worked example: n = 5 → base 185, rows at
  185, 225, 265, 305, 345.
- **Current word**: its rack slides to x = `493 − L*69*0.5`, y = 281 at scale 1.0; its entry row is at
  the same x, y = 212 (`0x414340`, `0x414780`). Example L = 6 → x = 286. Letters are 69 px apart.
  Switching animates the old rack back to (10, base+i*40) at scale 0.33 and fades its entry row to
  alpha 0; the new entry row fades to 1 (`0x414780`). All three run for **400 ms** (`EDX = 0x190`): the
  rack slides with `ModifierLocationLine` (linear, `0x4307e0`) and scales with `ModifierScale` (0.33
  `0x3ea8f5c3` / 1.0) with **smoothstep** (`0x4513d0`); the entry row fades with `ModifierSimpleAlpha`
  (`0x4304d0`, alpha 0 / 1, linear). CONFIRMED (`ev_673e0237…`). With letters to clear first, a 500 ms
  `ModifierDelay` and the clear
  method `0x410d20` run before the move (`0x414780`, `param_4 > 0`).
- **Solved word**: `0x439cb0` stops the entry row's running moves and `0x43c890(1)` runs `0x43a310(1)`
  on each ball: a `ModifierFrame` at 60 ms from the ball's base frame to base + 5, so the pearl
  (frame 11) turns into the square tile (frame 16). The letters stay in the entry row, which fades
  out when the word goes back to the list, so a solved word shows an empty rack until the reveal.
- **Marks**: one `IMAGE_BLITZ_MARK` sprite per word, built at (207, rack y) with alpha 0 and no anchor
  call (`0x414340`, vector `scene+0x3b0`), then moved to x = `w + 10` by `0x414c70` (below). At round
  end solved words get frame 1, unsolved frame 0 (`0x411ef0`). INFERRED: frame 0 = cross, frame 1 =
  check (the file is named x.png).
- **Panel** (`0x414c70`, CONFIRMED from the asm, `ev_fb6ae5fa…`): `maxL = 0x415280()` is the longest
  word of the round (`ev_b36e5b4c…`), `base = 285 − 20n`, `top = base − 10`, `h = 40n`,
  `w = (27·maxL − 10) − maxL = 26·maxL − 10`.

  | Sprite | Position | Scale |
  | --- | --- | --- |
  | `PANEL_LEFT` (1×10 top strip) | (0, `top`) | x `w` |
  | `PANEL_LEFTCORNER` | (`w`, `top`) | |
  | `PANEL_RIGHT` (10×1) | (`w`, `top + 10`) | y `h − 20` |
  | `PANEL_RIGHTCORNER` | (`w`, `top + h − 10`) | |
  | `PANEL_BOTTOM` | (0, `top + h − 10`) | x `w` |
  | `PANEL_BG` | spans `top + 10 … top + h − 10` | x `w/195`, y `(h − 20)/600` |
  | marks | (`w + 10`, rack y) | |
  | `BUTTON_PREVWORD` | (`w/2 − 38`, `top − 62`) | |
  | `BUTTON_NEXTWORD` | (`w/2 − 38`, `top + h + 10`) | |

  Worked example n = 5, maxL = 6: top 175, w 146, the right edge 185 … 365, the marks at x 156, PREV at
  (35, 113), NEXT at (35, 385). The arrow x goes through the rounding helper `0x419900` → `0x554580`,
  INFERRED to be floor (`ev_c9653392…`, `ev_23e71e9c…`).

### 1.2 The list look (CONFIRMED)

- Each `LetterBall` (`0x439d00`, built with flag 1 in Blitz by `0x4396e0`) carries a hidden child at
  `ball+0x17c`: `IMAGE_LETTERBALL_40` (`letterball_anim_40.png` 408×624, 17×26 frames of 24×24), anchor
  centre, scale 2.5, so it covers the same 60 local px. Its frame follows the ball's (the solved flip
  `0x43a310` runs on both). Each `LetterBox` cell (`0x43ab90`) also holds a hidden `IMAGE_BLITZDOT`
  (16×16) at `(j·69 + 34.5, 34.5)`, scale 2.5 (`ev_a422c204…`, `ev_6ac36c58…`).
- `0x43d280(flag)`: ball children visible when `flag != 1`, blitz dots visible when `flag == 0`
  (`ev_b945e31e…`). `0x414340` calls it with 0 on every new rack (the list look). The switch
  `0x414780` calls it with 1 on the new word as its move starts and with 0 on the old word once its
  400 ms scale ends (`ev_673e0237…`). So in the list the crisp 24 px ball draws over the 60 px one at
  0.33, and a solved word's empty rack shows a row of blitz dots (about 13 px on screen).

## 2. Round flow

### 2.1 Game start (CONFIRMED)

1. Scene enter `0x415a70`: plays `MUSIC_in-game_1` (section `music`) at **0.7** (`0x3f333333`), then
   `0x4110f0`.
1. `0x4110f0`: if the player has a saved game tagged **`"BLITZ"`** → load it (`0x4174c0`, §4.4);
   else start round 1 (`0x4138e0(1)`).
1. Round start `0x4138e0(round)`:
   - `+0x260 = 0` (dialog lock), `+0x264 = 0` (round state: 0 play, 1 won, 2 lost).
   - **Tutorial**: player byte `player[0xc0 + g_gameMode]` (mode 2 = Blitz) is tested and set to 1; if it
     was 0, `+0x20c = 5` → the next update shows `DIALOG_TUTORIAL` (INFERRED body
     `TEXT_TUTORIAL_TITLE_2` "Lightning Mode" / `TEXT_TUTORIAL_BODY_2`). Shown once per player.
   - Round 1 resets score `+0x308 = 0`. Sets `+0x300 = round`, timer `+0x2fc = 80300`, warning mark
     `+0x304 = 10000`, LABEL_ROUND = round, SPRITE_TIMER scale reset.
   - **Clears the four trophy flags** `+0x3bc` (twisted), `+0x3bd` (passed), `+0x3be` (hinted), `+0x3bf`.
   - Builds n words (§2.2), highlights word 0 (`0x43c940(i==0)`), plays **`SOUND_lightning_start_round`**,
     selects word 0 (`0x414660(0)`), lays out (`0x414c70`).

### 2.2 Dealing the words (CONFIRMED)

- `n = min(10, floor(5 + (round−1)·BLITZ_WORD_NUMBER_INCREMENT))`, increment 0.2 (`default.xml`).
  Rounds 1–5: 5, 6–10: 6, 11–15: 7, 16–20: 8, 21–25: 9, 26+: 10.
- For each word: `u = (LCG next 24 bits)·2^-24·2.0` → U[0,2); `v = 7 + (round−1)·0.2 − u`;
  `L = trunc(v <= 5 ? 5.0 : v)`; `L = min(L, playerMaxLen)` (player `+0x3c`). Same Java LCG as Classic
  (`DAT_005fde90`).
  - Worked example, round 1: `v = 7 − u`. u ∈ [0,1) → L = 6 (u = 0 exactly gives 7, odds 2^-24);
    u ∈ [1,2) → L = 5. So round-1 words are 5 or 6 letters, 50/50. Round 6: L ∈ {6, 7}; round 11:
    L ∈ {7, 8}, then capped by the 6/7/8 setting.
- Pick: the Classic loop (`0x43d550` pool, 5 attempts, longer words skipped for free, exact hit taken,
  else closest shorter). INFERRED: same persistent rack-word bag as Classic.
- Sub-anagrams of the rack from the common tree (`+0x268`) and the full tree (`+0x2ac`); the common list
  is filtered to **full-length entries only** (`0x418840` erase when `len < L`) and passed with the
  unfiltered full list to `0x414340` (`AnagramTable` ctor `0x4396e0`). So each word has slots only for
  common full-length anagrams.

### 2.3 Solving a word (ENTER, `0x4116e0`, CONFIRMED)

1. Copies the entry row into the word's last word (`word+0x64`).
1. If `len(typed) != L`: **nothing happens** (no sound). Only bingos count.
1. Shared guess check `0x432850` (full-dictionary gate + free slot). Result ≠ 1 → `SOUND_word_reject`,
   popup kind **0** ("not in dictionary", `0x428e20(0)`), entry row cleared (`0x4113b0`,
   `SOUND_letter_clear`). INFERRED: any full-dictionary anagram using all letters solves the word.
1. Accept: `score += 10·L·(L − min(L, word.hints))`; `word+0x88 = 1`; `0x439cb0`, `0x43c890(1)` (solved
   visuals); `0x40f1e0` (Word Nerd counter); uncommon flag → `0x40f3a0` (Human Dictionary counter);
   `SOUND_word_bingo`; `all = 0x411680()` (every word's `+0x88` set).
1. Not the last word: timer `+= 8000`, capped at **80300**; `+0x304 = min(t,10000) − min(t,10000)%1000`;
   popup kind **6** ("extra time"); `SOUND_lightning_time_added`; SPRITE_TIMER frame 1 and a
   `ModifierClipRect` (`0x478390`) that grows the clip to `321 · t/80300` over **`(L + 4) · 100` ms**
   (`EDI` @`0x411af4`, passed as `ESI` @`0x411b6d`; L = 6 → 1000 ms; easing **linear**, CONFIRMED in
   batch O4: `0x478390` starts with `XOR EDX, EDX`, so `0x47ece0` stores `0x4307e0`, `game-scene.md` §9.1;
   `ev_3e8a19be…`, `ev_7f4fa7b7…`); input/timer lock `+0x2f9 = 1`; when the animation ends `0x411520` clears the lock and
   moves to the **next unsolved word** (forward, wrapping). This auto-advance does **not** set the pass flag.
1. Last word: §4.1.
1. LABEL_Score updated (`+0x23c`).

### 2.4 Other inputs (CONFIRMED unless tagged)

Click dispatcher `0x4135e0` (by command name), only while `+0x264 == 0` (except Menu/NextRound/GiveUp):

| Command | Handler | Effect |
| --- | --- | --- |
| BUTTON_Menu | `+0x20c = 1` | DIALOG_MENU + PAUSE_VEIL visible |
| BUTTON_NextRound | `+0x20c = 3` | next-round dialog (button itself hidden, §4) |
| BUTTON_GiveUp | `+0x20c = 4` | Give-up confirm (§4.2) |
| BUTTON_Clear | `0x4113b0` | `SOUND_letter_clear`, letters back to the rack |
| BUTTON_Enter | `0x4116e0` | §2.3 |
| BUTTON_LastWord | `0x412e90` | `SOUND_word_last` once, moves the word's last-word letters into the row |
| BUTTON_Twist | `0x413090` | `SOUND_word_twist`, `+0x3bc = 1`, shuffle current rack, `0x40f170` twist counter |
| BUTTON_Hint | `0x4131f0` | manual hint; **no such button in the shipped XML** |
| BUTTON_NextWord | `+0x3bd = 1`, `SOUND_lightning_skip`, `0x411520` | next unsolved word (forward, wraps) |
| BUTTON_PrevWord | `+0x3bd = 1`, `SOUND_lightning_skip`, `0x4115e0` | previous unsolved word (backward, wraps) |

- Next/Prev set the pass flag and play the sound **even when no other unsolved word exists** (no move).
- Clicking another **unsolved** word's rack in the list (`0x417f50`, play state) also passes:
  `+0x3bd = 1`, `SOUND_lightning_skip`, `0x414660(i)`. After the round is over (`+0x264 != 0`),
  clicking a rack opens `http://dictionary.reference.com/browse/<word>` with `ShellExecuteA`.
- Switching words (`0x414660`): ignored while a switch runs (`+0x394`); if the current word is unsolved
  and its entry row has letters, they are cleared (`SOUND_letter_clear`) and the move waits **500 ms**.
- Button enable refresh `0x413340`: while `+0x260` or `+0x2f9` is set everything (incl. arrows) is
  disabled. Otherwise Menu, GiveUp and both arrows on; Clear on iff the row has letters; LastWord on iff
  the row is empty and a last word exists; Enter/Twist from string lengths (INFERRED: Enter iff row is
  full length); **NextRound disabled and hidden** always.
- Keyboard `0x415c50` (only when `+0x2f9 == 0` and `+0x264 == 0`; code → char table `0x5d3228`):
  1 Backspace → `0x412d30` (take back last letter, `SOUND_letter_clear`); 4 Delete → Clear;
  5 Return → Enter if the row has letters, else LastWord; `0x11` Space → Twist; 2 Tab, `0x16`, `0x17`
  → NextWord (**counts as a pass**); `0x14`, `0x15` → PrevWord (pass); anything else types a letter
  (`0x412bd0`, `SOUND_letter_place`). INFERRED: `0x14..0x17` are the arrow keys (no char in the table).
- Focus loss / system events `0x4169d0`: event `0x206` opens DIALOG_MENU (`+0x20c = 2`) unless it is
  already up; event `0x402` (INFERRED: quit) saves the game (`0x416af0`).

### 2.5 Hints (automatic, CONFIRMED)

- Each word starts with `word+0xbc = 9000` ms, hints `word+0xc0 = 0` (`0x414340`).
- In the update, only the **current** word's `+0xbc` counts down by the frame time, and only while the
  round clock runs (same gates as §3). At `<= 0`: if the entry row holds any letter whose `+0x180` byte
  is 0 the hint is **deferred** (checked again next frame); otherwise `0x4131f0` fires. INFERRED: the
  hint waits while the player is mid-typing.
- `0x4131f0`: `0x43c990` reveals the next unrevealed hint letter of the entry row, left to right (a
  ghost letter, frame `char − 0x60`, alpha 0 → **0.392** over **500 ms**, `IMAGE_HINT_LETTERS` sprites at
  `row+0x1c4`); plays **`SOUND_lightning_hint`** only if a letter was revealed; then always
  `word+0xbc = 4000`, `+0x3be = 1`, `word+0xc0 += 1`.
- The ghosts fade with their entry row: `0x430440` sets the alpha on a node and its children and clamps
  any child named `HINT_LETTER` (INFERRED name, `0x4972a0`) to `min(alpha, 0.392)`. A word coming back
  from the list shows its hints at `min(row alpha, 0.392)`, not the product (`ev_b945e31e…`).
- So the 1st hint comes after 9 s on a word, then one every 4 s; each costs `10·L` points (score formula).
  Worked example L = 6: 0 hints 360, 1 hint 300, 2 hints 240, 6+ hints 0.

## 3. Timer (CONFIRMED)

- Start 80,300 ms (`0x139ac`); +8,000 ms per non-final solve, capped at 80,300 (`0x139ab < t → 80300`).
- Update `0x415300(dt)`: pending dialog switch first; trophy popup queue (`0x429510`); message popup
  queue (`0x428ec0`). Then **if `+0x260` (dialog up) or `+0x2f9` (busy animation) → return**: the clock
  is frozen during every dialog, the tutorial, the time-added animation and the round-end reveal.
  Else `t −= dt`.
- Warning: `if t < +0x304: +0x304 −= 1000; play SOUND_lightning_time_low`. Starts at 10,000, so a beep
  as t crosses 10 s, 9 s … 1 s (10 beeps). After a solve the mark re-arms to `min(t,10000)` rounded down
  to a second. Example: t = 1,500 → +8 s → 9,500 → mark 9,000 → next beep at 9 s.
- Display: LABEL_TimerMinutes = `"%d"` of `max(0, t/60000)`; LABEL_TimerSeconds = `":%02d"` of
  `max(0, (t/1000)%60)` (truncating). 80,300 shows `1:20`. SPRITE_TIMER is never scaled: round start
  `0x4138e0` gives it a clip rect of the whole frame (`0x419930`, sprite `+0x14c`), and every running
  frame sets frame 0 and the clip `(0, 0, 321 · t / 80300, 46)` @`0x4158c8`–`0x41597a`
  (`[0x59b9a0] = 80300.0`), so the art shows from the left and its right end is cut off. Frame 1 only
  during the +8 s animation (`ev_86da62d4…`, `ev_d382a9ae…`, `ev_23e71e9c…`).
- `t <= 0` → `0x411e10` (§4.2). After that the per-word hint timer is not touched.
- `+0x3bf` is set to 1 when `t <= 3000` (and the hint timer has not expired) @`0x415a06`; it is reset at
  round start/resume. No reader found in the traced Blitz code. **UNKNOWN use** (Nick of Time does not
  read it).

## 4. Round end and game over

### 4.1 Round won (CONFIRMED)

1. Last word solved (`0x4116e0`): `score += 1000`, `+0x264 = 1`, trophies 3–6 (§5), `0x411ef0`.
1. `0x411ef0` reveal: `+0x2f9 = 1`; first `0x414780(current, none)` sends the current word back to
   the list. Per word i (stagger: first at 400 ms, +100 ms each): a solved word's entry-row balls go
   into rack slot j in order (`0x43bad0(0, j, 1.0)`, a snap, no slide); their 24 px children show at
   alpha 1 at once and only the 60 px balls fade 0 → 1 over 400 ms (linear, `ev_3e4d8a2b…`); an unsolved
   word's rack ball list is replaced by the answer's letters and `0x43c6a0` lays them out at
   `j * 69 + 34.5`, so the rack spells the answer. Each mark sprite gets its frame (`+0x90`) and
   scale-x 0, then alpha 0 → 1 over 150 ms and a sideways grow: `ModifierScale` (0, 1) → (1.2, 1) over
   300 ms, then (1.2, 1) → (1, 1) over 100 ms, both smoothstep `0x4513d0`; scale-y stays 1, the pivot is
   the sprite default (INFERRED top-left), so it grows from its left edge (`ev_2d60e904…`). The
   **2000 ms** delay is chained to the last word's stagger delay, so the whole reveal lasts
   `400 + 100 (n − 1) + 2000` ms (n = 5: 2,800 ms).
1. `+0x264 == 1` → method `0x428a60` (`mov [+0x2f9],0; mov [+0x20c],3`) → next update case 3:
   **`SOUND_game_next_round_acheived`** + **`DIALOG_NEXTROUND`** (the Classic dialog, no button press).
1. NEXTROUND closed → `0x416910`: closes the dialog, `+0x260 = 0`, refresh, `0x413030`: Well Rounded
   tracker `0x40f2b0` with **EAX = 2** (Lightning) + trophy push, then `0x4138e0(round + 1)`.
   So the round count fires when the player continues, like Classic.

### 4.2 Time out / Give up = game over (CONFIRMED)

- `0x411e10`: `+0x264 = 2`, `+0x260 = 1`; if the entry row has letters → Clear (`SOUND_letter_clear`)
  and a 300 ms delay; then `0x411ef0` (same reveal, 400 ms + 100 ms stagger, 2000 ms delay).
- There are **no lives** in Blitz: one time-out ends the game. No time-up sound (only the warning beeps).
- `0x411ef0` with `+0x264 == 2`: deletes the saved game (`0x461840`), then copies `scene+0x1ec` into player
  `+0x38` when a 15-char string at `scene+0x320` compares equal; then `0x416440`. Player `+0x38` is `WOD`, the
  day of the last Word of the Day played, and the compare is a Word of the Day mode test, so this is a leftover
  `WOD` write from the code Blitz shares with Word of the Day. Blitz's mode string never matches, so the copy is
  dead in Blitz. **CONFIRMED** ([game-scene.md](game-scene.md) §8.6, W42).
- `0x416440` game over: `+0x2f9 = 0`; opens table **`"blitz"`**; if `score > 0`: entry
  `0x4108a0(score, 2, t, "%d" of round)` (INFERRED arg meaning; extra = round number per `profiles.md`),
  `insert 0x457cd0`; index ≠ −1 → **`SOUND_game_high_score_achieved`** + **`DIALOG_CONGRATS`**; else
  (or score 0) **`DIALOG_ENDGAME`**.
- GiveUp: BUTTON_GiveUp → `+0x20c = 4` → `0x40b310` confirm (header `TEXT_DIALOG_GIVEUP_HEADER`
  "Give Up?", body `TEXT_DIALOG_GIVEUP` "Are you sure you wish\nto end this game?"); Yes `0x4167a0` →
  `0x411e10` (identical to a time-out); No `0x416850` → resume.

### 4.3 Pause / menu (CONFIRMED)

- `+0x20c` 1 (Menu) / 2 (focus loss) → `DIALOG_MENU` and PAUSE_VEIL visible (`0x378` setVisible(1)),
  hiding the racks. 5 → `DIALOG_TUTORIAL`. The update sets `+0x260 = 1` while a dialog is pending/up.
- Focus loss (`0x4169d0`: event `0x601`, or `0x206` while `DIALOG_MENU` is not up) only sets
  `+0x20c = 2`; the update acts on it once `+0x260 == 0`, so under another dialog the menu opens after
  that dialog closes (`ev_a0122af2…`).
- The hover hand (`0x4162b0`) shows over the balls of unsolved words while the round is in play and over
  every word after it (`ev_fe6575a3…`).

### 4.4 Save / resume (CONFIRMED)

- Save `0x416af0`, called on scene exit `0x416420` **only while `+0x264 == 0`** (round in play) and on
  system event `0x402`. Tree: `DATA` → `GAME` { `ROUND` = round, `TIME` = t, `SCORE` = score,
  `GAME_LIST` [ `GAME_DATA` { `WORD` = the rack word (`word+0x1c`), `COMPLETED` = `word+0x88` } × n ] }.
  Hint counts, hint timers, the current word and the trophy flags are **not** saved.
- Load `0x4174c0` (tag `"BLITZ"` checked in `0x4110f0`): restores round, time, score, rebuilds each
  word from `WORD` (same full-length common filter) with its `COMPLETED` flag, sets the warning mark,
  clears the four trophy flags, selects the first unsolved word, then **deletes the save file**.
  Consequence: resuming resets hints (9 s timers, 0 hints) and the per-round trophy flags.

## 5. Trophies 3–6 (CONFIRMED, `0x4116e0` last-word branch)

Checked once, at the moment the round's last word is accepted, **before** the +1000 is shown and with no
time added (the +8 s branch is skipped). Player block `p = *(DAT_005fde98 + 0x70)`.

| Id | Trophy | Condition | Player slot |
| --- | --- | --- | --- |
| 3 | Brainstorm | `+0x3bc == 0` (no Twist button / Space this round) | `p+0x4c` |
| 4 | Rolling Thunder | `+0x3bd == 0` (no Next/Prev button, Tab/arrow key, or rack click) | `p+0x50` |
| 5 | Lightning Reflexes | `+0x3be == 0` (no hint, auto or manual, this round) | `p+0x54` |
| 6 | Nick of Time | `t < 3001`, i.e. **t <= 3000 ms** remaining | `p+0x58` |

- Each: if the slot is 0 → set 1 and push id via `0x429480`; else push −1. Popup timing/sound as in
  `trophies.md` §1 (shared `0x429510`, called from the Blitz update).
- Flags are **per round**: cleared in `0x4138e0` and `0x4174c0`; set by `0x413090` (twist), `0x4135e0` /
  `0x415c50` / `0x417f50` (pass), `0x4131f0` (hint).
- Practical reading of Lightning Reflexes: every word must be solved within 9 s of time spent as the
  current word (or the hint must stay deferred). INFERRED from §2.5.
- Other counters fed by Blitz: Word Nerd family `0x40f1e0` per solve, Human Dictionary `0x40f3a0` when
  uncommon, With a Twist family `0x40f170` per twist, Well Rounded family `0x40f2b0`(2) per round.

## 6. Scoring (CONFIRMED)

- Per solved word: `10 · L · (L − min(L, hints))`. L = 5: 250; L = 6: 360; L = 7: 490; L = 8: 640
  (no hints).
- Round complete: **+1000**. No time bonus, no "all words" bonus beyond that, no penalty on time-out.
- Score label `LABEL_Score` = `"%d"` after every solve. Score carries across rounds; reset only at
  round 1 of a new game.

## 7. Sounds, popups and reuse

- Music `MUSIC_in-game_1` at 0.7 on enter. Sounds used (section `default`, all `volue="0.5"` in XML):
  `SOUND_lightning_start_round`, `SOUND_lightning_time_added`, `SOUND_lightning_time_low`,
  `SOUND_lightning_hint`, `SOUND_lightning_skip`, `SOUND_word_bingo`, `SOUND_word_reject`,
  `SOUND_word_twist`, `SOUND_word_last`, `SOUND_letter_place`, `SOUND_letter_clear`,
  `SOUND_game_next_round_acheived`, `SOUND_game_high_score_achieved`.
- Popups: only kind 0 (reject) and kind 6 (extra time); no bingo banner. Anchor (400, 55).
- Mode id 2 (`g_gameMode`, tutorial flag index, high-score tab BLITZ), tracker index 2, table `blitz`.
- Dialogs reused from Classic: DIALOG_MENU, DIALOG_TUTORIAL, DIALOG_NEXTROUND, DIALOG_ENDGAME,
  DIALOG_CONGRATS, the yes/no confirm (`0x40b310`). DIALOG_PASSROUND is not used.
- UNKNOWN: the purpose of `+0x3bf`, `LABEL_HINTNUMBER` and `+0x2fb`. The `player+0x38` copy is a dead Word of
  the Day `WOD` write (§4.2, [game-scene.md](game-scene.md) §8.6).

## 8. The port (deviations and choices)

- **Time-bar grow easing** (linear, CONFIRMED in batch O4, §2.3): the clip grows linearly over `(L + 4) · 100` ms
  (`blitz_time_added_ms`); input waits for it, as `+0x2f9` does.
- **Panel ids**: the `blitzscene` panel images share their ids with `gamescene`'s, so the port names
  them `blitzscene/IMAGE_PANEL_*`. The arrow x is floored (`0x419900`, INFERRED floor).
- **The list look** (§1.2) is a per-word flag (`BlitzWordView::list_look`) instead of a visibility
  switch on child sprites. Without the imported art (the placeholder build) the 24 px balls and the
  blitz dots are not drawn; the 60 px placeholder pearls stand in.
- **The round-start flip**: the original flips every word's balls, but a list word's 24 px child covers
  its flip, so the port flips only the current word, which looks the same.
- **Rack order**: the racks are shuffled like Classic's (`GameScene::scrambled`); the deal order was
  not traced.
- **Twist sound**: as in Classic, only when the rack moves (a held Space does not stack clicks).
- **Resumed solved words** show their tiles in the entry row like a word solved in play; the original's
  `0x4174c0` rebuild of a `COMPLETED` word was not traced in that detail.
- **`+0x3bf`, `LABEL_HINTNUMBER`**: not ported (no reader found).
- **The `player+0x38` copy**: not ported. It is a Word of the Day `WOD` write that never runs in Blitz (§4.2,
  [game-scene.md](game-scene.md) §8.6).
- **Music**: `MUSIC_in-game_1` plays at the music volume; the port has no per-stream gain for 0.7.
- **Placeholders**: without the imported art the scene draws outlines and the popup text
  "+8 SECONDS"; the hint letters fall back to placeholder letters.

## 9. Address index

| Address | What |
| --- | --- |
| `0x410f00` | Blitz init: SPRITE_TIMER, arrows, PAUSE_VEIL, popup anchor (400, 55) |
| `0x415a70` / `0x4110f0` | scene enter (music) / resume-or-start |
| `0x4138e0` | round start |
| `0x414340` | word object build (9000 ms hint timer, positions, mark sprite) |
| `0x415280` | the round's longest word (panel width) |
| `0x43d280` / `0x439d00` / `0x43ab90` | list look switch / LetterBall (24 px child) / LetterBox (blitz dots) |
| `0x430440` | alpha on a node and its children (`HINT_LETTER` capped at 0.392) |
| `0x4162b0` | hover hand cursor |
| `0x478390` | `ModifierClipRect` (the +8 s grow) |
| `0x414660` / `0x414780` / `0x414c70` | select word / switch animation / layout + labels |
| `0x415300` | update: pending dialogs, popups, timer, warning, hint timer, time-out |
| `0x4135e0` / `0x415c50` / `0x417f50` | button / keyboard / rack-click handlers |
| `0x4116e0` | ENTER (score, +8 s, trophies 3–6, +1000) |
| `0x411680` | all words solved? |
| `0x411520` / `0x4115e0` | next / previous unsolved word |
| `0x4113b0` / `0x412d30` / `0x412bd0` / `0x412e90` | clear / backspace / type / last word |
| `0x413090` / `0x4131f0` / `0x43c990` | twist / hint / reveal hint letter |
| `0x439cb0` / `0x43c890` / `0x43a310` | solved word: stop moves / tile flip |
| `0x43c6a0` | rack laid out in answer order (reveal) |
| `0x413340` | button enable refresh |
| `0x411e10` / `0x411ef0` / `0x416440` | lose / reveal / game over |
| `0x428a60` / `0x416910` / `0x413030` | auto NEXTROUND / NEXTROUND closed / tracker + next round |
| `0x4167a0` / `0x416850` | Give up yes / no |
| `0x416420` / `0x4169d0` / `0x416af0` / `0x4174c0` | exit / system events / save / load |

## Evidence ids

| Call | evidence_id |
| --- | --- |
| pseudo `0x4116e0` | `ev_33e491cc0cf3a1dbce5d3e7f4fb9257b59791db3e76c8b34f0d33eaa888a2fba` |
| batch `0x411680 0x411520 0x4113b0 0x413340 0x411ef0` | `ev_ad1cc2d60106c294dfa2187d4fa692d9d9a0cb3124f75977e1b31f2de8c9313b` |
| callers `0x411ef0` | `ev_3fb5cabc36860f00a65685df72600fc63280d571c00d9d251f8548b68289456a` |
| pseudo `0x415300` | `ev_5e5a2dc445005d72c7994d1e9dd1beb3bbdd2787b05d3f02e1d3b17107fd3ad8` |
| asm `0x415300` | `ev_86da62d413971d48a8cbd68513d33cd47e683c162305d4046b374fa699f9ce1f` |
| bytes jump table `0x415a5c` | `ev_12ea047e528d2cea948a2ec41e9a39d84a862964258ec5577e506a81bdb44547` |
| batch `0x411e10 0x4131f0 0x413090 0x416910 0x413030` | `ev_900360fcf4becce79e67c3b1d948e22112c12d69f8a56ab0fb402e48ec4baa73` |
| asm `0x411e10` | `ev_c6dcf2d6b0e7c39de9f9161cf41aacf080ec465a5d146d82cf3338e68e1b9776` |
| batch `0x416440 0x414660` | `ev_89e9b20c9a5cfbe7ce6a0a12ac12037cbaa8a898fea1eeeb8457bf9d20baaa33` |
| asm `0x416440` | `ev_6fb12e96d1471a76142b84f629f3bd4a782b6a3e7eae86f5cd15bc501cf5559b` |
| strings lightning/blitz/HINT/PASS | `ev_0452552a4648f8cde8eb1bb63b502f4228aa9dce8a2ec60e38aaae44290cc4f8` |
| xrefs SOUND_lightning_skip | `ev_3091a04664c0757a9326d187aedc29fb65ed334620f3edeb671c4f4b81157f8a` |
| xrefs SOUND_lightning_start_round | `ev_77cc8197402b1198e53c24652bb5fad4b359d72a557142a060846cd334817cf6` |
| xrefs SOUND_lightning_time_low | `ev_a6b8919e72e4b35baab8c3ea884d6fa10b605ea126247d6e7e4cdefa4df40ca0` |
| xrefs IMAGE_BLITZ_MARK | `ev_8e47d2035bf6a8bdc24b2fa2d92dd34da59c7b9a53e40428a3391a1a83b02ffe` |
| xrefs HINT_LETTER / blitzscene | `ev_22b08385fbed199225d3d9160a6616763e71567e1d83c6ddc3f9a956890d6a12`, `ev_563caa0de6170f13537dff2d9755c298c9316764979866492f33ee1cd0b2c7cf` |
| batch `0x4135e0 0x415c50 0x417f50` | `ev_3aaab43d88fa1398586e9da23bba34cbd0f35095bd38c3c067eff8f02dce779a` |
| asm `0x4135e0` | `ev_9870ae0b6633995ed72e3d01a411fecc19fe70c81037318bd33240a71b81c615` |
| batch `0x4167a0 0x416850 0x412e90 0x4115e0 0x412d30` | `ev_7edb7ad1856aa72ac1d41fe4a625b037f4848bc119fd3fdbbfad274a2c88f8bb` |
| bytes `0x428a60` | `ev_97f389a47b457cfddbd3c4113917f22afd62cbb66a60af1ac5f0e1dd4d035ea5` |
| pseudo / asm `0x4138e0` | `ev_a0edd1c2b17c62ab68f4aab6e22c5db099d6d34a0888a22906786197eaffbc28`, `ev_d382a9ae08f1d5bc455a49e233cc2db49ece3ad086b4e17c28778a24d51604df` |
| batch `0x414340 0x414c70 0x43c990` | `ev_caff4a5dd0b6999030f3715736da43739fc9c614f2d0807c33f4b1573e6fc7a9` |
| batch `0x415a70 0x4110f0 0x410f00 0x412bd0` | `ev_4d2baa9eae26e1591efc9b989884f740e167d0d3faeb445f3a702029dbcd32eb` |
| pseudo `0x4174c0` (load) | `ev_d2d0d4889c44bc8f2f04ba3908093fbd9319abf020d07675d332e19edc2c3c17` |
| pseudo `0x416af0` (save) / callers | `ev_aa77c4aadf19b5cba3d91ee471cfb79a1a5324710f0744ba2016773321e2b70d`, `ev_58f6f181eaaa1a652daf2fc6a9493cf62087570878b59b43d0c4eedba7004664` |
| batch `0x416420 0x4169d0` | `ev_c9de7045041357167fddf9519b2cd8ae5f6af3488d5a0915d7dc232e3ffb170a` |
| pseudo `0x461840` (delete save) | `ev_6f6cfb39ba030ca0f8f2f882a29fd4769747b38864a60c9437922704a2b0cc0b` |
| pseudo `0x414780` | `ev_4663c27ba967c383f515817f21663390dd572cc13863d4361c33bdb788aa58f6` |
| asm `0x423a20` (component offsets) | `ev_79be952146c636bd4321e15aa343e208c6b3be945c4d64b9e56d243ba488c1ca` |
| asm `0x414340` (word build) | `ev_92a42547ae5101fcd038e73429bf52f80a9bc68d8cfb834876b39ec98b1bd26c` |
| pseudo `0x43d280 0x430440 0x43c370` | `ev_b945e31e2692c1e50ae1de98d0ead011e4aaea6f98d11e264895cbc5479d46ec` |
| pseudo `0x43ab90` (LetterBox ctor) | `ev_6ac36c583be43ffde9ab40c207c87c9c55b4193701b80d0bea5472a3d59ccb33` |
| pseudo `0x439d00` (LetterBall ctor) | `ev_a422c2045af1a354489aa8206144b044937a7d4a80e1b090fe490bfade75c204` |
| asm `0x4396e0` (word object) | `ev_81a2810316ba8e08cbdbedd605c4445faace9458b216040f9d1efe10e96e2ef3` |
| pseudo `0x4972a0 0x4304d0 0x4131f0` | `ev_5696c4a789424aaa78cf2ed8eb850befed209c03279f015282e1843eb35df9cb` |
| asm `0x414780` (switch) | `ev_673e02376dec1280698f08cd300ab673afd3e6538c87e6605923b727fb533480` |
| asm `0x414c70` (panel) | `ev_fb6ae5fa7dbda9458457b9b09e3f6de289eb76b28670f38c61fd3edb5f123de8` |
| asm `0x415280` (longest word) | `ev_b36e5b4c5102d6a36e2a3a979d4815adf3f62dc43ba4b5668c5bdb394be0d775` |
| bytes `0x415110` (arrow-x FPU) | `ev_c9653392d17673d292a18c07a22eba273e178237bb7835ede01cf1eb7ab2be53` |
| pseudo `0x411ef0` (reveal) | `ev_2d60e904e8936b161f126d4b824bc99e16c89153ba766b5f0940cdb5994a8b99` |
| pseudo `0x43c6a0 0x43bad0` | `ev_3e4d8a2be29270a0f03313ab6c321c696be1703a9c86ad5919000bd63784b605` |
| pseudo `0x419930 0x419900` (clip, rounding) | `ev_23e71e9ce4472ee97a84c94e6bc33d37f09b85536a4ec935f0915b59b85e2046` |
| asm `0x4116e0` (+8 s clip grow) | `ev_3e8a19be7e6490f7cb91d3b5b794e535d3b830328146b3ae0dbd7dbee69f215e` |
| pseudo `0x478390` (ModifierClipRect) | `ev_7f4fa7b75e43b30c23d0a0ad1a271db18ee0d472259acb944727c4163548ed0a` |
| pseudo `0x4162b0` (hover) and neighbours | `ev_fe6575a3e4cf043af52640367f553e5e70618b663553036ed4cda9a577c4f74b` |
| pseudo `0x4169d0` (focus) and neighbours | `ev_a0122af278cf98b382b4406ce2b7d970a827b5e4a8f34c5507b47db652b21d40` |
| xrefs MUSIC_in-game_1 / "BLITZ" | `ev_ebac8cca503ff820fd864ee7abe0f56ca2b8fff8473ef9d5f1df59a87cda9d5e`, `ev_05fe8d398fc08139bc4df65dbe7875a6629affc4cf3eeca7f8875a3e98a650c1` |
| xrefs BUTTON_GiveUp / BUTTON_NEXTWORD | `ev_4d8f0752746057269c8ef7d7c41bd508f5bfeeb70fc01ccc684f9a87a3465e89`, `ev_7e0d8ea4d7086c0b9c97538d1e0afe0fcd1d52e0e0a12e05e0a33a90b7798091` |
| xrefs GAME_DATA / dictionary URL | `ev_7a4bbf59bf628c5af6db796f4250ca4eb34d82b12613890048e2b2b058cabb59`, `ev_1a0889fae6546a6d62a057bc7f2935ef1b8988480f6758c0f93c85dbedda3bc0` |
| batch `0x439cb0 0x43c890 0x43c6a0` (solved look, answer layout) | `ev_82c9899bd003b44aff6ebd0a8ad6b9818b2cc8ad2b3a7d4cf1881df5ab2f0bcd` |
| batch `0x411ef0 0x43a310 0x414780` (reveal, tile flip, switch) | `ev_7756f220522405996fecf76108da3499a651b42668a9cdcd3335ace4c9272cf8` |
| pseudo `0x414c70` (panel layout) | `ev_b6df484af016bf1317503aa00c46b62c8b4bc86bd6cf14b9903bf73770ead6e7` |
