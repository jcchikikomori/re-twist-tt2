# Text Twist 2 (TextTwist2.exe) — gameplay logic notes

Static analysis only. Target: `TextTwist2.exe`, PE32 i386, MSVC, unpacked, image base 0x400000
(PDB path string: `c:\GHF\development\texttwist2\bin\win32\TextTwist2_Final.pdb`).
DRM/licensing components were deliberately skipped.

Tools: objdump (`full.asm`), capstone/pefile helper (`tt.py`), `rng.py` (address-range dump),
`decode_tree.py` (dictionary decoder). Decoded word lists: `TextTwist2Words.tree.txt`, `DamBeaversWords.tree.txt`.

Labels: **CONFIRMED** = I saw it directly in code or data. **INFERRED** = the most likely reading, not fully traced.

---

## 0. Shared infrastructure

| Thing | Address | Notes |
| --- | --- | --- |
| RNG object | `[0x5fde90]` | Java-style LCG. **CONFIRMED**: `0xDEECE66D`/`0xB` constants @0x44eb79..0x44eba7 |
| `rand(min,max)` | 0x402b10 | `seed = (seed*0x5DEECE66D + 0xB) & (2^48-1)`, `r = (int)(seed>>16)`, returns `min + (r mod (max-min))` (made non-negative). Upper bound is **exclusive**. **CONFIRMED** |
| shuffle (game RNG) | 0x43d950 → 0x43da00 | `std::random_shuffle` that uses the LCG via callback 0x43dad0 → `rand(0,i+1)`. **CONFIRMED** |
| shuffle (CRT rand) | 0x422080 → 0x4228f0 | `std::random_shuffle` that uses MSVC `rand()` (0x4c570d). **CONFIRMED** |
| UTF-8 string length | 0x45fa60 | used as "word length" everywhere. **CONFIRMED** |
| Player settings | `[[0x5fde98]+0x70]` | `+0x3c` = max word length (6/7/8, default 8 @0x40f0d5; set by SWITCH_LENGTH_6/7/8 handler @0x40c678..0x40c6e3). `+0x30` = persisted SEED, `+0x34` = persisted INDEX. **CONFIRMED** |
| Score field | `scene+0x308` | **CONFIRMED** |
| Timer (ms remaining) | `scene+0x2fc` | **CONFIRMED** |
| Round number | `scene+0x300` | **CONFIRMED** |

---

## 1. `.tree` dictionary loader

### Which file is which (CONFIRMED)

In the scene constructor (around 0x424069..0x424145):

```
0042406b: push 0x590658        ; "data/TextTwist2Words.tree"
00424074: mov dword ptr [esi + 0x2f0], 6
0042407e: mov dword ptr [esi + 0x2f4], 8
004240be: push 0x59276c        ; "r"
004240c4: call 0x4c5cdd        ; fopen
004240ce: lea ecx, [esi + 0x268]  ; WordTree #1 (common words)
004240d4: call 0x43e070        ; WordTree::load(FILE*, 0)
004240f6: push 0x590674        ; "data/DamBeaversWords.tree"
0042413f: lea ecx, [esi + 0x2ac]  ; WordTree #2 (full dictionary)
00424145: call 0x43e070
```

- `scene+0x268` = **TextTwist2Words.tree** (25,004 words, lengths 3–15). It gives:
  - the pool of rack/root words (0x43d550 / 0x43d690 always run on `+0x268`), and
  - the **board** list (the word slots shown on screen).
- `scene+0x2ac` = **DamBeaversWords.tree** (116,757 words, lengths 2–16). It is the **guess-validation** list.
  A valid word that isn't in the common list bumps the "uncommon words" stat (the trophy "Human Dictionary: Spell 500 uncommon words").

### File format (CONFIRMED, 0x43e070, recursive)

Grammar: `Node := [a-z]* ('0' | '1') Node*`

- Bytes `a`..`z` are the child letters of the current node.
- The terminator `'1'` means the path to this node is a complete word. `'0'` means it isn't.
- Then the child subtrees follow, in the same order as the letters.
- The "alphabet header" is just the root node's child list (TT2Words has no `x` at the root because no word starts with x).

```
0043e0f0: mov al, byte ptr [esp + 0x1c]   ; byte just read (fread 1)
0043e0f4: cmp al, 0x31                    ; '1'
0043e0f6: je  0x43e136
0043e0f8: cmp al, 0x30                    ; '0'
0043e0fa: je  0x43e134
0043e0fc: mov cl, al ; sub cl, 0x61 ; cmp cl, 0x19 ; ja error   ; must be a-z
0043e10a: mov byte ptr [esi + edx], al    ; store child letter
0043e136: sete bl                         ; bl = terminal flag
0043e149: call 0x43e070                   ; recurse once per child
0043e162: and ebp,0x7f ; shl ebp,0x18     ; header = size(24b) | nchild<<24 | terminal<<31
```

In-memory node layout: a 4-byte header (`bits0-23` = subtree byte size, `bits24-30` = child count, `bit31` = word end), then the child letters, then the child subtrees.
Child lookup is 0x43db60. My Python decoder (`decode_tree.py`) uses all 95,477 and 470,839 bytes exactly, so the grammar checks out.

---

## 2. Rack / root word selection

### Candidate pool (CONFIRMED, 0x43dcb0 → 0x43dbd0)

A DFS over the **TextTwist2Words** tree collects every word with `5 <= len <= 8` (`push 8; push 5` @0x43dd2e).

### Pick from a persistent shuffled bag (CONFIRMED, 0x43d550)

```
0043d59c: mov ecx, [esi+0x34]          ; player INDEX
0043d59f: cmp ecx, [eax+0x8]           ; >= pool size ?
0043d5a4: call [0x5763cc]              ; timeGetTime -> new SEED
0043d5aa: mov [esi+0x30], eax ; mov [esi+0x34], 0
0043d5bb: xor ecx,[eax+0x20] ...       ; setSeed(seed ^ 0x5DEECE66D)
0043d5f5: call 0x43d950                ; shuffle pool with that seed
0043d607: idiv dword ptr [ecx+8]       ; word = pool[INDEX % size]
0043d628: add dword ptr [esi+0x34], 1  ; INDEX++
0043d640: call 0x43dd80                ; enumerate sub-anagrams in TT2 tree
0043d64c: cmp dword ptr [eax+8], 0xf   ; need >= 15 sub-words
0043d650: jl  0x43d600                 ; else take the next word
```

So the rack word has to have **at least 15 sub-anagrams in the common list** (the word itself counts).

### Target length per mode

- **Classic (Timed/Untimed), CONFIRMED** @0x426008..0x426104: `L = min(rand(5,9) → 5..8, playerMaxLen)`.
  It draws up to 5 candidates. A word with `len > L` is skipped without using up an attempt. `len == L` is accepted. A shorter word uses up an attempt and is kept if it's the closest to L so far. After 5 attempts the closest one wins.
- **Word of the Day, CONFIRMED** @0x43d690: the same pool, shuffled with the **fixed seed 1337 (0x539)**. The index is `floor(time()/86400) - 14125`, i.e. days since **2008-09-03**:

  ```
  0043d6c8: xor ecx, 0x539                 ; fixed seed
  0043d70a: call 0x4c6766                  ; time(NULL)
  0043d711: mov eax,0xc22e4507 ; mul ecx ; shr edx,0x10   ; /86400
  0043d71e: sub edx, 0x372d                ; - 14125 days
  0043d727: lea eax,[edx*8] ; sub eax,edx ; lea eax,[base+eax*4] ; pool[day] (0x1c-byte strings)
  ```

  REA batch W (2026-10-22, [game-scene.md](game-scene.md) §8.2, evidence W1-W3) corrects and extends this
  reading. The whole mode (board, buttons, round flow, `player.WOD`, high scores) is in
  [game-scene.md](game-scene.md) §8.
  - **No modulo, no bounds check:** the index goes straight into the shuffled pool. With the original list
    (22,048 words of 5-8 letters) day 22,048 (2069-01-14) reads past the end; a date before 2008-09-03 gives a
    negative index. **CONFIRMED**
  - **No 15-sub-anagram test:** the WOTD word is taken as is; only the Classic bag (0x43d550) skips words with
    fewer than 15 sub-anagrams. About 26 % of the original pool would fail that test (INFERRED estimate). The
    port used to walk forward past such words, which changed the word on roughly one day in four; since the WOTD
    mode landed, `Bag::word_of_the_day_for_day` takes `pool[day]` as is, like the exe (wrapped into the pool,
    deviation D4). **CONFIRMED** (the missing test), INFERRED (the share).
  - The seed is written into the **global** Java LCG (`0x5fde90`), not a private one, and WOTD skips the
    MAX WORD LENGTH loop (`player+0x3c`) entirely. **CONFIRMED**
  - Three clocks: the word uses the UTC day (`time()`), the menu gate, the save `DAY` and `player.WOD` use the
    local standard-time day (`_ftime64_s` `timezone`, `dstflag` never read), and the high-score date is the UTC
    calendar date as `YYYY-MM-DD` (`GetSystemTime`). **CONFIRMED** ([game-scene.md](game-scene.md) §8.1, §8.6,
    §8.7)

  After that, 0x4262b0 keeps the **first 5 common sub-anagrams of each length 3..8** (list order) and runs
  **before** the 48-slot quota, so the quota never trims a WOTD board. **CONFIRMED** ([game-scene.md](game-scene.md)
  §8.3)
- **Lightning/Blitz:** see §3. **Crossword:** see §4 and [crossword.md](crossword.md) §3.2.

### Sub-anagram enumeration (CONFIRMED, 0x43dd80 → 0x43de40)

A recursive walk over the rack letters that skips duplicate letters at each depth (0x45f9b0 = first-index check). It descends via 0x43db60 and collects every terminal node.
The rack is enumerated against **both** trees (0x43d7b0 called with `+0x268` and with `+0x2ac`, @0x426157..0x426176). Both lists go into `AnagramTable` (ctor 0x4396e0, args: rack, commonList, fullList).

### Board building and word cap (CONFIRMED, 0x432330)

```
004324a3: add dword ptr [esp + eax*4 + 0x3c], 1   ; histogram by length
004324b6: cmp esi, 0x30                           ; total > 48 ?
004324c9: fdivr qword ptr [0x59ba48]              ; factor = 48.0 / total
004324de: fild [len bucket] ; fmul factor ; (if 0<v<1 -> 1) ; _ftol (truncate)
00432729: sub len,3 ; cmp 5 ; ja skip             ; only lengths 3..8 get slots
```

- Board = words from the common list with lengths 3..8.
- If there are more than 48, each length bucket gets the quota `max(1, trunc(count*48/total))`. So the cap is **about 48 slots**.
- Slots fill in list order. The order is the rack-letter DFS order, not alphabetical (INFERRED).
- The full list is copied to `table+0x18c` (0x4336f0 @0x4327ff).
- The **effective minimum word length is 3**. That's because no slot has length < 3. 2-letter words from DamBeavers validate but have nowhere to go, so they score nothing (INFERRED from 0x432b20).

### Guess check (CONFIRMED, 0x432850; return values feed the jump table @0x424d4c)

1. Entry gate: a linear `std::find` (0x434450) over the word list copied to `table+0x190` at board build (0x4336f0 @0x4327ff) — the rack's **full-tree sub-anagram enumeration** (the list built by 0x43d7b0 and handed to the `AnagramTable` ctor). Miss → returns **0** (reject sound, sets the "mistake" flag at `scene+0x371`). So a guess must be in the full dictionary **and** spellable from the rack letters.
2. It's already sitting in a filled slot → returns **-1**. The comparison (0x4318f0) reads the slot's word string at `slot+0x19c` (MSVC `std::string`; size at `+0x1b0`, capacity at `+0x1b4`; a slot is "empty" while the string is empty), so the check matches the **placed** word.
3. It isn't one of the board's words (find over the list at `table+0x180`) → sets the out-flag "uncommon".
   0x432330 rebuilds that list from the quota-selected board words only (count `table+0x184` = the slot count),
   so a common word the 48-slot quota or the WOTD cap left off the board is "uncommon" too
   (W5 audit 2026-10-22, [game-scene.md](game-scene.md) §8.3; this line used to say "the common list").
4. 0x432b20 scans the slot vector at `table+0x1b8` for the first slot with `slot+0x178 == len` and an empty word string, then **writes the guessed word into `slot+0x19c`** (0x431880) and queues the appear animation (0x4310f0). On success it returns **1** and 0x432850 does `found++` (`table+0x178`). When no slot of that length is free it returns **0** (2026-10-14 re-check; the caller's jump table maps the failure to the "no room" message).

This means any valid dictionary word of the right length that the rack can spell can fill a slot, even if it isn't the exact word the board was built from. The slot then **displays the guessed word**: re-guessing it is a duplicate, while the board word it covered stays guessable (it fills the next free slot of its length, or has nowhere to go).

Note: step 3 sets the uncommon out-flag **before** step 4 runs, so in principle an "uncommon" word that finds no free slot still carries the flag. What the caller does with it on the no-room path was not traced (stat / trophy wiring is undecoded); this port counts the uncommon stat only on successful placement. **INFERRED**

### Address index (guess path; re-verified 2026-10-14 with Ghidra 12.1.4 via REA)

| Address | What |
| --- | --- |
| `0x432850` | Guess check (steps 1–4 above), body `0x432850..0x432b17` |
| `0x434450` | `std::find` over a counted `std::string` list (28-byte elements) |
| `0x4336f0` | Counted string-list copy (fills `table+0x190` at board build) |
| `0x432b20` | Slot placement: first empty slot of the length, body `0x432b20..0x432caf` |
| `0x431880` | Slot word setter: assigns into `slot+0x19c` |
| `0x4318f0` | Slot word getter: copies `slot+0x19c` out |
| `0x4310f0` | Word-appear flip: per letter delay i*100 ms, tile squash 150 ms, frame set, grow 150 ms (game-scene.md §2) |
| `table+0x180` / `+0x184` | Board word list (begin / count = slot count), rebuilt by 0x432330; used for the uncommon flag |
| `table+0x190` / `+0x194` | Full sub-anagram list (begin / count) used as the entry gate |
| `table+0x178` | Found counter (`found++` after a placement) |
| `slot+0x178` | Slot word length |
| `slot+0x19c` / `+0x1b0` / `+0x1b4` | Slot word string (pointer/buffer, size, capacity) |

---

## 3. Scoring and timers

### Classic (GameScene, case 1 @0x4247fe) — CONFIRMED

```
00424856: call 0x45fa60 ; mov ebx,eax ; call 0x45fa60   ; len, len
0042486b: imul ebx, eax
00424874: lea ecx,[ebx+ebx*4] ; add ecx,ecx               ; 10*len^2
00424879: add dword ptr [ebp+0x308], ecx
00424897: cmp ebx, eax                                    ; len == rack length?
004248bd: cmp byte ptr [eax+0x88], 0                      ; first bingo this round?
004248c6: add dword ptr [ebp+0x308], 0x3e8                ; +1000
004248d5: mov byte ptr [eax+0x88], 1                      ; "qualified" flag
004248fd: cmp [eax+0x178], ecx                            ; found == total slots?
0042490b: imul eax, eax, 0x64 ; add [ebp+0x308], eax      ; +100 * totalWords
```

- Each word scores `10 × len²`: 3→90, 4→160, 5→250, 6→360, 7→490, 8→640.
- The **first** bingo (a word using every rack letter) in a round adds **+1000** and sets the qualified flag. Later bingos still give `10·len²` and bump the consecutive-bingo trophy counter.
- Finding **all words** adds `100 × number_of_slots`, then the round ends (0x428840: reveal, end).
- I found **no remaining-time bonus** in Classic. High-score entries just store score, time and round (0x427e73.., 0x427fa2..). **CONFIRMED (absence within traced paths)**
- Timer: `scene+0x2fc = 0x1d4c0` = **120,000 ms (2:00)** per round. The countdown warning starts at `0x2710` = 10 s and steps down 1 s at a time (@0x425f47, 0x426d79..0x426d99).
  The timer runs down only in CLASSIC_TIMED and WORD_OF_THE_DAY (mode checks @0x426d59/0x426d67). **CONFIRMED**
  A WOTD game is that one timed round; its round end and game over are in [game-scene.md](game-scene.md) §8.5.
- Trophy "Speed Speller": round finished while the timer is still in `[115000,120000]` ms, i.e. under 5 s (@0x42888b). **CONFIRMED**

### Lightning / Blitz (BlitzScene) — CONFIRMED

- Words per round: `n = min(10, floor(5 + (round-1) × BLITZ_WORD_NUMBER_INCREMENT))`. The XML value is 0.2.

  ```
  00413c60: push 0x58fd90       ; "BLITZ_WORD_NUMBER_INCREMENT"
  00413cb0: fild (round-1) ; fmul inc ; fadd qword [0x59ba88] (=5.0)
  00413cca: call 0x419900       ; floor (CRT, RC=down)
  00413cfc: cmp edi, 0xa        ; min(n,10)
  ```

- Per-word length: `len = min(playerMaxLen, max(5, trunc(7 + (round-1) × BLITZ_WORD_SIZE_INCREMENT − U[0,2))))`. The increment is 0.2. Constants: 7.0 @0x59ba70, 2.0/0.0 range, floor 5.0 (@0x413dd3..0x413ef6). Words come from the same 0x43d550 pool and go through the same "≤L / 5 attempts" loop.
- **Only bingo words are accepted** (guess length must equal rack length, @0x41179a).
- Score per word: `10 × len × (len − min(len, hintLettersUsed))` (@0x4117dd..0x411825), so hints cost points. Round complete adds **+1000** (@0x411bc0).
- Timer: **80,300 ms** at round start (0x139ac @0x413980). Each solved word adds **+8,000 ms**, capped at 80,300 (@0x4119a4..0x4119d0).
- Hints are **automatic**: 9 s on the current word, then one every 4 s (0x415300 -> 0x4131f0). The shipped
  `dialogs.xml` has no hint button. Full flow, layout and timings: [lightning.md](lightning.md).
- The strings `BLITZ_TIME_DECREMENT` and `CROSSWORD_WORD_NUMBER_INCREMENT` from `properties/default.xml` **don't appear in the exe**, so they're unused. **CONFIRMED**

### Crossword (CrisscrossScene) — CONFIRMED

- Each newly filled word scores `10 × len²` once (`+0x85` flag @0x41a637..0x41a66b). Completing the puzzle adds **+1000** (@0x41a7e0).
- There is **no timer** (the update `0x41ebc0` never counts; init hides `TIME_LABEL`), no lives, and no NEXT ROUND
  button: the next-round dialog opens by itself 2.5 s after the puzzle is solved, and GIVE UP is the only way the
  game ends. A guess goes into the **selected** word slot and may be any full-list word of that length made from the
  root's letters. Full flow, layout and timings: [crossword.md](crossword.md) (2026-10-22).

---

## 4. Round advance and Crossword grid

The Crossword scene around this grid (selecting a word, typing, zoom, round end, save, trophies) is in
[crossword.md](crossword.md).

### Round advance

- Classic: when time runs out, if the qualified flag (`table+0x88`, set by any bingo) is set, you advance to the next round. Otherwise 0x425420 runs the pass / end-game path ("Sorry, you didn't qualify…") (@0x426f2b..0x42701b). **CONFIRMED**
  Untimed works the same way: the bingo enables Next Round. Finding all words also ends the round early. **INFERRED for the button wiring**
- Crossword: the round ends when every slot is filled; `DIALOG_NEXTROUND` opens by itself and closing it starts
  round + 1 (`0x41fcf0` → `0x41b000`). No timer, no lives; only GIVE UP ends the game ([crossword.md](crossword.md)
  §5). **CONFIRMED**
- Lightning: the round ends when all n bingo words are solved (0x411680 is the "every word's `+0x88` flag set" check; the +1000 is at 0x411bc0). If the timer hits 0, the game is lost: the update 0x415300 @0x41597f calls 0x411e10 when `scene+0x2fc <= 0`. There are no lives and no time-up sound (2026-10-16, [lightning.md](lightning.md) §3). **CONFIRMED**

### Crossword word set (CONFIRMED, 0x41ba40 setup; re-verified 2026-10-13 with Ghidra 12.1.4)

- Root length `L = min(5 + rand%4, playerMaxLen)` from the game's Java-style LCG (`DAT_005fde90`), then the same 0x43d550 pick loop as Classic (free skip of longer words, exact hit accepted, otherwise the closest shorter word over 5 tries).
- Sub-anagrams come from the **common** tree only (0x43d7b0). The list is shuffled with `std::random_shuffle` driven by
  **CRT `rand()`** (0x422080 → 0x4228f0: 15-bit chunks of `rand() & 0x7fff`, reduced `% (i+1)`, `swap(a[i], a[j])` for
  `i = 1..n-1`). The CRT is seeded from the clock (`srand(timeGetTime())` in the application constructor @0x46d5bd,
  `srand(_time64(NULL))` @0x482b04), so the original's grids are not reproducible; the default seed 1 never applies
  ([crossword.md](crossword.md) §9, corrected 2026-10-22).
- The root is **rejected unless exactly one entry has the root's length** (`cmp edi,1` @0x41beb7), i.e. the root has no other full-length anagram. Rejection restarts the whole setup (new `L`, new root).
- Word count: `N = min(10, 4 + rand % floor(4 + (round-1) × BLITZ_WORD_NUMBER_INCREMENT))` (0x41bf23..0x41c030; the LCG again).
- The **first N entries** of the shuffled list are walked; the root is skipped when it is among them (so the set has N or N−1 non-root words). The root is appended **last** (0x41c1bd).
- The grid builder runs on that list. If it fails, the entire setup restarts (`cStack_f1 == 0` loops back to the top).
- Guesses are validated against the **full-list sub-anagrams of the root** (`+0x2ac` → `0x43d7b0` → `table+0x198`,
  @0x41c262..0x41c2a1); `table+0x188` holds the common list and only sets the "uncommon" flag (corrected 2026-10-22,
  [crossword.md](crossword.md) §0, §3.5).

### Grid construction (CONFIRMED, 0x436090 builder / 0x435f90 fit / 0x435d30 commit; re-verified 2026-10-13)

- Cell array at `table+0x1c0`, 8 bytes per cell (`int letter`, `byte blocked` @+4), stride **11**: `cell = a*11 + b` with `a` along the 18-long axis. A horizontal word advances `a`; a vertical word advances `b`.
- Words are consumed **from the back of the list**: the root first, then the remaining words in reverse order.
- The root goes in horizontal and centred: `a = (18 − len)/2`, `b = 5`. The cells just before and after it are marked blocked; no other cells.

  ```
  00436180: mov eax, 0x12 ; sub eax, len ; cdq ; sub eax,edx ; sar eax,1
  00436194: mov [edi+0x24], eax         ; start
  00436197: mov dword ptr [edi+0x28], 5 ; middle line
  004361cf: imul edx, edx, 0xb          ; cell = a*11 + b
  004361ed: mov byte ptr [edx+ecx*8+0x1c4], 1   ; block cell at -1 / len
  ```

- Each remaining word gets up to **10,000 attempts** (`cmp [esp+0x20],0x2710`). One attempt:
  1. Pick an already-placed word with the LCG: `k = next % placedCount` (negative fixed up).
  2. For every `i` in the new word and `j` in the placed word with equal letters whose placed cell is **not blocked**, push `j` (duplicates allowed; `i` outer, `j` inner).
  3. Shuffle that list with the CRT `random_shuffle` (0x439370).
  4. Pop from the **back**. For the popped `j`, scan `i` in order; where the letters match, position the new word **perpendicular**: placed horizontal → new vertical at `a = placed.a + j`, `b = placed.b − i`; placed vertical → new horizontal at `a = placed.a − i`, `b = placed.b + j`. The first candidate that passes the fit check is committed.
  5. When no candidate fits, the attempt counts and the next one picks a new placed word.
- **Fit check (0x435f90)**, with `crossing = i`:
  - start must satisfy `1 ≤ a ≤ 18` and `1 ≤ b ≤ 9`; the cell **after** the word (`start + len`) must satisfy the same bounds;
  - the cell **before** and the cell **after** the word must be **empty** (blocked is allowed there);
  - every other letter cell, except the crossing cell, must be **empty and not blocked**.
  - There is **no explicit side-neighbour test**; the no-touch behaviour comes from the blocks written at commit time.
- **Commit (0x435d30)**: write the letters, block the cell before and the cell after, and block the full **3×3 neighbourhood of the crossing cell** (`0x164 = 0x1c4 − 12 cells`, 3 rows of stride `0x58`, 3 cells of stride 8). This forbids later crossings on the placed word's letters adjacent to an existing crossing.
- The 10,000-attempt cap is per word. Hitting it makes the builder return 0 and the round setup starts over with a fresh root.

### Lose-a-life path (CONFIRMED, 0x425420; new 2026-10-13)

- Classic non-qualify runs 0x425420. It increments `scene+0x1e8`, plays `SOUND_game_lose_life`, and **only when that counter reaches 3** marks the game over (`scene+0x260 = 1`, high-score save 0x428c80). Below 3 it reveals the missing words (`SOUND_word_pass_reveal`, 3,000 ms `GF2::ModifierDelay`) and continues. Where `+0x1e8` is reset was not traced. **CONFIRMED for the threshold; INFERRED that it behaves as three lives.**

---

## 5. Misc constants

- `properties/default.xml`: `BLITZ_WORD_NUMBER_INCREMENT=0.2`, `BLITZ_WORD_SIZE_INCREMENT=0.2` (read at runtime through the data manager, @0x413c60 / 0x413dd3 / 0x41bef9).
- `scene+0x2f0=6`, `scene+0x2f4=8` (ctor) are probably UI min/max rack sizes. I didn't trace where they're used. **INFERRED**
- WordTree member `+0x14=5`, `+0x18=8`: the length bounds of the cached pool (@0x43d58e). **CONFIRMED**
