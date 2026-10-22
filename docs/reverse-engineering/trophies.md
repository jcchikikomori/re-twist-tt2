# Trophies

Evidence for trophy awards, the in-game trophy popup and the trophy room (`SCENE_TROPHY`).
The port: `engine/include/tt2/trophies.hpp` (award rules), `ui/include/tt2ui/game_scene.hpp`
(counters, streaks, popup) and `ui/include/tt2ui/trophy_room.hpp` (the room).

Target: `TextTwist2.exe` (SHA-256 `0c6e962c…`), decompiled with Ghidra 12.1.4 through REA on
2026-10-16. Tags: **C** = CONFIRMED (read in
code/data), **I** = INFERRED, **U** = UNKNOWN. Evidence ids are listed in full in the table at the end
(E1..E33). Offsets: `scene+X` = GameScene object (Blitz/Crisscross scenes share these offsets),
`player+X` = current profile `[0x5fde98]+0x70`.

## 0. Trophy slot values (important for the port)

- C: slot `player+0x40+4*i` (i = 0..23, order = TEXT_TROPHY_DESCRIPTIONS order) is **tri-state**:
  `0` = not won, `1` = won but not yet seen in the trophy room, `2` = won and revealed. Award code
  writes 1 (E15, E16, E20); the trophy room writes 2 after playing the reveal (0x42fe30 @0x42ffbc, E28).
- C: every award site checks `slot == 0` before writing 1 and returns -1 otherwise, so a trophy is
  never re-awarded (E14, E15, E16, E19, E20, E22, E23).
- U: when the profile is written to disk after an award (no save call in any award path read).

## 1. In-game popup

Code: GameScene init 0x423a20 (E1), update 0x426860 (E10, E11), show 0x429510 (E2, E3).

- C: queue. `scene+0x354` is a FIFO of trophy ids guarded by a mutex object at `scene+0x364`
  (created from the name "Achievement" by 0x4790b0; lock = vtbl+8, unlock = vtbl+4) (E1, E4, E12).
  Push = 0x42abb0 on `scene+0x354` (wrapper 0x429480) (E12, E13). Pop-front = 0x4294b0 (E12).
  Award sites push the return value even when it is -1 (E14, E17).
- C: dequeue in update 0x426860 @0x426ce6: only when `scene+0x260 == 0` (no pending dialog/round-end
  action), queue non-empty (`scene+0x35c != 0`) and busy flag `scene+0x368 == 0`; then pops one id
  and calls 0x429510(scene, id) (E11). One popup at a time, so trophies won together play one after
  another (I: next one starts on the first frame after the previous finishes).
- C: 0x429510 ignores id < 0 (`JL` at 0x42954d) (E3).
- C: the timer is **not paused** by the popup: the `scene+0x2fc` decrement @0x426d4d does not test
  `scene+0x368` (E11). I: input is not blocked (no input gate on `scene+0x368` seen; not exhaustive).
- C: what it shows: no `achievement.png`. It builds
  1. a sprite (0x174-byte object, ctor 0x442be0) from image `"IMAGE_TROPHY" + (id+1)` in section
     `trophyscene` (0x402e30 with 0x590830 "IMAGE_TROPHY") (E3); i.e. the same art as the trophy room.
  1. a label (0x1f0-byte object, ctor 0x47a170) with font `FONT_TROPHYNAMES` (section `default`), text
     `TEXT_TROPHY_TITLES[id]` (vector cached at `scene+0x330` by 0x423a20) (E1, E3).
  - I: `IMAGE_ACHIEVEMENT` (achievement.png, 21 frames) is unused: no "IMAGE_ACHIEVEMENT" string exists
    in the exe's string table (E29); "ACHIEVEMENT"/"ACHIEVEMENT_" are save-file keys.
- C: position/size (floats read from .rdata, values from E2/E3):
  - sprite: x = 725.0 (0x59ba40), y = 475.0 (0x59ba3c); pivot mode 4 (I: center); alpha 0; scale 0.05.
  - label: x = 725.0, y = 535.0 (0x59ba38); pivot mode 4 and vtbl+0xbc(1) (I: centered); alpha 0;
    scale 0. Both are bottom-right of the 800x600 screen.
- C: animation (GF2 modifiers; 0x4506b0 appends a modifier as a child of the previous one, I: a
  child starts when its parent finishes, so each list below is sequential; durations are the EDX/ESI
  constants) (E3, E4):
  - sprite: Alpha 0→1, 200 ms → Scale 0.05→0.6, 500 ms, easing fn 0x4513d0 → Scale 0.6→0.5, 300 ms,
    linear → Delay 2000 ms → Alpha 1→0, 500 ms, flag |2 (I: remove node when done) → ModifierMethod
    0x4235d0, which is `mov byte [ecx+0x368],0; ret` (clears busy) (E5).
  - label (runs in parallel, also rooted at `scene+0x1e0`): Alpha 0→1, 200 ms → Scale 0.05→1.2, 500 ms,
    easing 0x4513d0 → Scale 1.2→1.0, 300 ms → Delay 2000 ms → Alpha 1→0, 500 ms, flag |2.
  - Total 3500 ms per trophy; final rest scale 0.5 (sprite) / 1.0 (label). No slide; no frame animation.
- C: sound `SOUND_game_trophy_won` (section `default`) plays when the popup starts (dequeue time),
  via 0x4634b0(1.0, 0.0, 1.0, 0) (I: volume 1, pan 0, pitch 1) (E3). resources.xml gives it
  volume 0.5.
- C: `scene+0x368 = 1` set at 0x429a13 (busy) (E3).
- C: shared by all modes: callers 0x426860 (Classic/WOTD), 0x415300, 0x41ebc0 (I: Blitz, Crisscross) (E6).

## 2. Award logic (ids 0-based = TEXT_TROPHY_DESCRIPTIONS order)

Player counters (C): TWISTS `+0xa0`, SOLVED `+0xa4`, uncommon/HARD_SOLVED `+0xa8`, TRACKER[0..4]
`+0xac..+0xbc`.

| id | Trophy | Site | Rule (C unless tagged) |
| --- | --- | --- | --- |
| 0/1/2 | Quick Draw / Straight Shooter / Bull's-eye | 0x40f120 called from word-accept 0x424730 @0x42481f with EDX=`scene+0x36c` (E14, E15) | exact `== 5 / 10 / 20` |
| 3-6 | Brainstorm, Rolling Thunder, Lightning Reflexes, Nick of Time | Blitz ENTER 0x4116e0, the round's last word, slots `+0x4c..+0x58` (lightning.md §5) | per round: no twist (`+0x3bc`), no pass (`+0x3bd`), no hint (`+0x3be`), time left `<= 3000` ms |
| 7 | Speed Speller | 0x428840 (round finished) @0x42888b, slot `+0x5c` (E18) | `115000 <= scene+0x2fc <= 120000` ms |
| 8 | Puzzle Pro | Crisscross solved 0x41e930, slot `+0x60` (E24) | `scene+0x370 == 0` (never twisted) |
| 9 | Perfect Puzzler | 0x41e930, slot `+0x64` (E24) | `scene+0x371 == 0`: no solved word taken back (`0x41eaf0`, [crossword.md](crossword.md) §3.6); rejected guesses don't count |
| 10 | Look Ma, No Twisting! | 0x424730 @0x424914, slot `+0x68` (E14) | all slots filled (`+0x178 == +0x184`) and `scene+0x370 == 0` |
| 11/12/13 | Bingo Blaster / Boss / Bonkers | 0x40f260 from 0x424730 @0x4248aa, EDX=`scene+0x374` (E14, E15) | exact `== 5 / 10 / 20` |
| 14/15/16 | With a Twist / Dizzy / Twisted Twister | 0x40f170, `++player+0xa0` (E20) | exact `== 1000 / 5000 / 10000` |
| 17/18/19 | Word Nerd / Wordsmith / SUPER SPELLER | 0x40f1e0, `++player+0xa4` (E15) | exact `== 1000 / 5000 / 10000` |
| 20/21/22 | Well Rounded / Maniac / Fanatic | 0x40f2b0 (E7) | see below |
| 23 | Human Dictionary | 0x40f3a0, `++player+0xa8` when the word is uncommon (flag from 0x432850) (E14, E15) | exact `== 500` |

Details:

- **No-twist word streak `scene+0x36c`** (C): `++` on every accepted word in 0x424730 (@0x4247fe);
  reset to 0 by the twist handler 0x425240 (@0x425352) and by new-game init 0x425c50 when round == 1
  (E14, E19, E21). Not reset per round, so it carries across rounds of one game. Checked only in
  GameScene (Timed, Untimed, WOTD). Exact-equality means 5, 10, 20 accepted words in a row without a
  twist.
- **Bingo streak `scene+0x374`** (C): in 0x424730, a word is a bingo when its length equals the rack
  length (`+0x64` vs `+0x1c` lengths); bingo → `++` and check 5/10/20; any other accepted word → reset
  to 0 (@0x4248e4). Reset at new game (round 1) in 0x425c50; not reset per round (E14, E21). So the
  real rule is "N bingo words in a row", not "N levels". No mode check in 0x424730, so WOTD also counts
  (I: description says Untimed/Timed only).
- **Twisted-this-round `scene+0x370`** (C): set to 1 in twist handler 0x425240 (@0x425339), cleared to 0
  at every round start 0x425c50. `scene+0x371` (rejected-word flag) set in 0x424730 reject branch,
  cleared at round start (E19, E21).
- **Twist counter** (C): 0x40f170 callers 0x425240 (Classic/WOTD), 0x413090, 0x41b060 (I: Blitz,
  Crisscross twist) (E22). Words counter 0x40f1e0 and uncommon 0x40f3a0 callers: 0x424730, 0x4116e0,
  0x41a510 (I: all three word handlers) (E22, E23).
- **Well Rounded family 0x40f2b0** (C, E7): `++TRACKER[mode]`, then `m = min(99, T0..T4)`. Returns
  only one id: `m >= 30` and Fanatic unset → 22; else `10 <= m < 30` and Maniac unset → 21; else
  `1 <= m < 10` and Well Rounded unset → 20; else -1 (if `m >= 30` and Fanatic is already won it returns
  -1 without checking lower tiers).
- **TRACKER index → mode** (C via EAX at call sites):
  - 0 = CLASSIC_TIMED, 1 = CLASSIC_UNTIMED: 0x425190 (Next Round), `scene+0x264 == 1` and mode string
    `scene+0x30c` == "CLASSIC_TIMED" (0x5906b8) → 0 / == "CLASSIC_UNTIMED" (0x58fe00) → 1 (E8, E9).
    `scene+0x264 = 1` is set by round-finished 0x428840 (all words found, or timer expired with a
    bingo) (E18). So it counts a round that was passed, at the moment the player continues.
  - 2 = Lightning: 0x413030 `mov eax,2`, called from 0x416910 when DIALOG_NEXTROUND closes (E25, E26;
    lightning.md §4.1).
  - 3 = Crossword: 0x41b000 `mov eax,3`, called when DIALOG_NEXTROUND closes (`0x41fcf0`) (E25;
    [crossword.md](crossword.md) §5.2, confirmed 2026-10-22).
  - 4 = WORD_OF_THE_DAY: 0x426860 pending-action case 3, mode == 0x5906c8 "WORD_OF_THE_DAY",
    `mov eax,4` @0x426b74 (E11).
- **Speed Speller** (C, E18, E21): round start sets `scene+0x2fc = 120000` in every mode; update
  decrements it before the Timed/WOTD check (@0x426d4d), so Untimed counts down too (hidden). Check
  runs in 0x428840 at round finish: window 115000..120000 → finished within 5 s.
- **Popup timing of awards**: Quick Draw / bingo / counters / Look Ma are pushed at word-accept time;
  Speed Speller at round finish; Well Rounded on Next Round (Classic) or case 3 (WOTD) (C).

## 3. Trophy room (TrophyScene)

Code: init 0x42f880 (E27), enter 0x42fb20 and reveal 0x42fe30 (E28), hover 0x430000 (E28),
open 0x42e390 (E30), exit 0x42fcb0 (E31).

- C (dialogs.xml): 24 sprites `TROPHY_0..23`, image IMAGE_TROPHY(i+1), scale 0.5, anchor center, grid
  x = 87, 211, 334, 457, 580, 703 and y = 171, 277, 383, 489 (row-major, TROPHY_0 top-left).
  `LABEL_TROPHY` (FONT_SETTINGSTEXT, x 400, y 65, top anchor, centered, lineheight 22, default
  `TEXT_TROPHY_DESCRIPTION_DEFAULT`). `SPARKLE`: IMAGE_BURST (20 frames, 10x2), frametime 40, color
  #FFA800, additive, anchor/pivot center, starts `visible="false"`. HEADER "TROPHIES"
  (FONT_DIALOG_HEADERS, charspacing 4, x 400 y 9). Ribbons at (210,8) flipped and (528,8).
  BUTTON_Menu (BUTTON_PAUSE_END) at (400,547). Note IMAGE_TROPHY18/19/20 map to trophy20/18/19.png.
- C: init 0x42f880 looks up `TROPHY_<i>` (24), TEXT_TROPHY_DESCRIPTIONS, LABEL_TROPHY, SPARKLE (E27).
- C: **no trophy names in the room**: FONT_TROPHYNAMES has one xref (0x42985c, the popup) (E32), and
  TrophyScene never loads TEXT_TROPHY_TITLES (E27).
- C: on enter 0x42fb20: music `MUSIC_trophy_room` (section `music`) at 0.7 (0x3f333333), then
  0x42fe30(i) for i = 0..23 (E28).
- C: per-trophy state in 0x42fe30 (E28):
  - slot 0: `setVisible(0)`, alpha 0 → unwon trophies are hidden (no silhouette in code; I: the
    background art may show empty plinths).
  - slot 2: visible, alpha 1.0, XML scale 0.5.
  - slot 1 (new): alpha 0, scale 0.05, visible; Alpha 0→1 200 ms → Scale 0.05→0.6 500 ms (easing
    0x4513d0) → Scale 0.6→0.5 300 ms; then slot := 2. All new trophies animate together (each chain is
    rooted at `scene+0x1e0`). No sound here.
- C: hover 0x430000 (mouse move; tests each TROPHY sprite with vtbl+0x4c hit-test) (E28):
  - hovered i == 23 (Human Dictionary) and slot 0 → label = `HIDDEN_ACHIEVEMENT` (secret trophy).
  - any other hovered i (won or not) → label = TEXT_TROPHY_DESCRIPTIONS[i].
  - if slot != 0: SPARKLE moved to that trophy's position and made visible; if slot 0: SPARKLE hidden.
  - nothing hovered → label = TEXT_TROPHY_DESCRIPTION_DEFAULT, SPARKLE hidden.
  - I: hovering also bumps a global hand-cursor counter `DAT_005fdf00[...]` (+1 on enter, -1 on leave).
  - I: hit-testing ignores visibility, so hidden (unwon) trophies still show their description.
  - C: no scale-on-hover in this code.
- C: open from main menu (0x42e390, E30): clears scene resources, loads section `trophyscene`, sets
  transition `TRANSITION_DEFAULT` (dialogs.xml: FadeBlackTransition 500 ms), switches to SCENE_TROPHY.
  BUTTON_TROPHY on the selection scene: IMAGE_BUTTON_TROPHIES at (482,539) (dialogs.xml). I: the
  BUTTON_TROPHY xref at 0x42d468 is the selection scene's lookup (E33).
- C: 0x42fcb0 loads section `sceneselection`, TRANSITION_DEFAULT, SCENE_SELECTION (E31). I: this is the
  TrophyScene BUTTON_Menu handler (sits next to the scene's other methods; caller not traced).

## 4. The port

Rules above are implemented as read, in all five modes: Lightning's ids 3-6 ([lightning.md](lightning.md) §5),
Crossword's ids 8 and 9 when the puzzle is solved and its round count (tracker 3) when `DIALOG_NEXTROUND` closes
([crossword.md](crossword.md) §5.2, §6; `award_crossword_round`, called from `ui/include/tt2ui/crossword_scene.hpp`),
and the Word of the Day round count (tracker 4) at state 3 ([game-scene.md](game-scene.md) §8.5). So Well Rounded,
Maniac and Fanatic can be won. The deliberate differences:

- **Round count timing.** The port counts a completed Classic round (Well Rounded / Maniac / Fanatic)
  when `DIALOG_NEXTROUND` opens, after the reveal, not when its OK is pressed (0x425190). The popup
  waits for that dialog either way, so it shows at the same moment. WOTD counts at state 3, as the
  original does.
- **Speed Speller.** The port times the round itself (`round_elapsed_ms_`, counted while a round is
  played and the scene is not paused) instead of keeping a hidden 120 s clock in Untimed. It is
  checked on every 0x428840 path: every word found, the time running out qualified, and NEXT ROUND /
  DONE (0x425b60 → 0x428840(3000); W5 audit 2026-10-22, game-scene.md §8.11, which corrects an
  earlier "NEXT ROUND does not end a round through 0x428840" here).
- **Dequeue gate.** The original dequeues whenever `scene+0x260 == 0` (§1). A reveal started by
  0x428840 or by a time-up miss with lives left leaves it at 0, so a trophy already won shows during
  that reveal; a Pass / GIVE UP reveal comes from its YES with `+0x260` still 1. The port follows this
  (`reveal_unblocked_`). In WOTD, state 3 sets `+0x260` and nothing clears it before the scene goes,
  so the round's own trophy is won but its popup never shows (game-scene.md §8.11); the port matches.
- **Seen trophies.** The room marks a new trophy seen (slot 2) when the room opens, not when its
  reveal ends, so leaving mid-reveal still counts it as seen.
- **Music volume.** `MUSIC_trophy_room` plays at the music volume; the port has no per-stream gain
  for the original's 0.7.
- **Popup art.** No placeholder exists for `FONT_TROPHYNAMES` or the trophy images; they need the
  imported assets, like the rest of the game today.

## Evidence ids

| # | Call | evidence_id |
| --- | --- | --- |
| E1 | pseudo 0x423a20 | ev_99837eddc83cd9faa306b32c189fa3e9a20235998255822d338f76b4ebba426c |
| E2 | pseudo 0x429510 | ev_8639e73663459ee1989d32149079ba55eaf0ef214e58e4f7b2909852e9951ed6 |
| E3 | asm 0x429510 | ev_9e7e9c1a9669366f40ee633ae83687817fc341ab1650db63b55a0a1fd9e8dc13 |
| E4 | decompile 0x4506b0/0x451030/0x42ad30/0x4790b0 | ev_495a1485f33fa5c069f18ee2f2d6a2868d9d026bd98299073c8904dd38d8d3a7 |
| E5 | bytes 0x4235d0 | ev_e4dfa00710b53806306f8697a83d9d65d2afd82f856f6c18cea5e82df55b087b |
| E6 | callers 0x429510 | ev_50804372c3d4e60f51a3044af424bcbfd20aa6d33507559e0959601681aece2f |
| E7 | pseudo 0x40f2b0 | ev_9357020f2d5ed4954497d61f8aa40e49a451a4921e0adcd9e2aeff9c479ec29a |
| E8 | asm 0x425190 | ev_b99921a7ef06b170a80fa45ff43ebe5ead339da694ff288095304a9c86a0c432 |
| E9 | bytes 0x58fe00 / 0x5906b8 | ev_0e0775ba47ddb793fa91a81c2ee048fcd728d4569566048fd82e47230e8fbac6, ev_6567e32496b71b3aa83d2bfc457ec45710313e9d31e1420fe031a57c029ab67f |
| E10 | pseudo 0x426860 | ev_f5625215aea38c76cfd0a11f37c1024737f1adef063e566f82fe909c0346064b |
| E11 | asm 0x426860 | ev_faa42e184823a6f1cb814bcda1ce22fb7171ed5f5d9f154bb2232d012abcaa66 |
| E12 | decompile 0x4294b0/0x429480 | ev_8af795dc505cb061cc393bb939ba28bf6a1274d596a5158ad9c10f347dce1124 |
| E13 | callers 0x429480 / xrefs 0x42abb0 | ev_81fc75cc01ce3013716b6aafc0d892c85aee66b36a36e74b526173250ca7ae42, ev_5d881eb99a978e67996d520ff67421e98253b2de4a4744ac4502c225fc0c6c0e |
| E14 | asm 0x424730 | ev_dbf47191191768ed5f06295c84cdfb66bc0e49d4f75c410e34117d6872f3b1e1 |
| E15 | decompile 0x40f120/0x40f1e0/0x40f260/0x40f3a0 | ev_47f87ea76184126e74b2d704626428244e3061c6d7d61560cc4f96f6b9044242 |
| E16 | pseudo 0x424730 | ev_e7f08e2c9eafb9f3ee109e9c87349a34776e6be1dafadff3ee385925c698ea1a |
| E17 | xrefs 0x40f2b0 | ev_f2bbba044ef32e8e97d2ab0a69f4d96a285f01882864cb45770636d5d9435071 |
| E18 | asm 0x428840 | ev_3eedebc8567245135e24eba1fbb142f4f3242768d73d4ed950e888319ccf019e |
| E19 | asm 0x425240 | ev_2578a0cf4926b3b781a5130dc8480d9bae7b7daeb47d5648f5f8f1ac8ba004dc |
| E20 | decompile 0x40f170/0x425c50 | ev_8ed03fc9b78484f66cbe6de9d14a6b68e67ccfa2f475545a662359ab8a77f4e8 |
| E21 | (same as E20, round-start resets) | ev_8ed03fc9b78484f66cbe6de9d14a6b68e67ccfa2f475545a662359ab8a77f4e8 |
| E22 | callers 0x40f170 / 0x40f1e0 | ev_2b6b3d38673475890dde721237dc2ab0f87ddee67f60b321a1a0b5dff6048655, ev_b8522e9a711e02c800d35756c07115f8991ed7ce58a7adcb461137d98aba71ea |
| E23 | callers 0x40f3a0 | ev_bf0fd79b8532990a1c4b4cb56f9d7445b6d58115e61b8327bf2378a9f2969802 |
| E24 | decompile 0x414340/0x41e930 | ev_82d9fcaa3cfcd09535b705e65cd2b267022c668acd80854f6b4dbcaede85faca |
| E25 | bytes 0x413030 / 0x41b000 | ev_08a884726aa6a77d396a4856554e5ac2175fc28d46c37f5fd03f2bb7a3a9debd, ev_3096265fa099fdadcdedde4ab6d6243006375a90ad8fc3d0bed10e75b790758d |
| E26 | callers 0x413030 | ev_59c1ce1778de42072f22ad065351b100890ea9fff77937e5d0ff40dcbc98dc93 |
| E27 | pseudo 0x42f880 | ev_466738ed42340cebda7d98b8d1c9bd617f59fc6358acbb8128948f67ae9be706 |
| E28 | decompile 0x42fb20/0x430000; asm + pseudo 0x42fe30 | ev_6c281363c8f98ff97febd48133634988e0d6e6a9faa97bc74f9e31e0a0997034, ev_39c15e92fbd86232cd62e4d9edc5fc54bd4e210d7e999d3b16ee8b76efbee30e, ev_fbc25de0ec09178f937d810de4e8395ebf0ae5746ffbfae3b9d69120317a522b |
| E29 | strings "ACHIEVEMENT" | ev_481e0d4e2981294d4c4ab460a7d16a7b0927d93ac092bfc9ee8c6e69fe215a8b |
| E30 | pseudo 0x42e390 | ev_c0376eefc525b7c4585facb2ff5145dfec62fbf9ef8311c707cb7178de347a2c |
| E31 | pseudo 0x42fcb0 | ev_e12d69163c70d3687b7675a8b1c8aa7eaad1c59b1fdcb3c8685d295c15cf81e6 |
| E32 | xrefs FONT_TROPHYNAMES / HIDDEN_ACHIEVEMENT / MUSIC_trophy_room / DESCRIPTION_DEFAULT | ev_2c359c319114af04c73cb2f91e155b3936c351d994c715937ae83edcc288927f, ev_062e09d877d836c5cf04e9d2a3819b7270eab3f7187a56e09b5f1a4fedcd24eb, ev_97bcd332b2e056ebe0b2e868e4c5aedd8da86961d8c78ed0da87be53ef492c11, ev_628ba2d730e8b178ff3446ebacf5f7686dbbb0416553575dbbf28356a8a3e7b2 |
| E33 | containing proc 0x42d468 | ev_60fbf593604904777d17d23024560e111e79df7a41122c9f50b0a7b9fcd5e4c2 |
