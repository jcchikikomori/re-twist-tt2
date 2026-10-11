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
|---|---|---|
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
  ```
  After that, 0x4262b0 caps the WOTD board at **5 words per length** (3..8). **CONFIRMED**
- **Lightning/Blitz:** see §3. **Crossword:** see §4.

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
1. The guess isn't in the full list (`+0x18c`) → returns **0** (reject sound, sets the "mistake" flag at `scene+0x371`).
2. It's already sitting in a filled slot → returns **-1**.
3. It isn't in the common list (`+0x17c`) → sets the out-flag "uncommon".
4. 0x432b20 looks for the first **empty slot with the same length** and places the word. On success it does `found++` (`+0x178`) and returns **1**. If no slot is free it returns **2** (message only, no score).

This means any valid dictionary word of the right length can fill a slot, even if it isn't the exact word the board was built from.

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
- The strings `BLITZ_TIME_DECREMENT` and `CROSSWORD_WORD_NUMBER_INCREMENT` from `properties/default.xml` **don't appear in the exe**, so they're unused. **CONFIRMED**

### Crossword (CrisscrossScene) — CONFIRMED
- Each newly filled word scores `10 × len²` once (`+0x85` flag @0x41a637..0x41a66b). Completing the puzzle adds **+1000** (@0x41a7e0).

---

## 4. Round advance and Crossword grid

### Round advance
- Classic: when time runs out, if the qualified flag (`table+0x88`, set by any bingo) is set, you advance to the next round. Otherwise 0x425420 runs the pass / end-game path ("Sorry, you didn't qualify…") (@0x426f2b..0x42701b). **CONFIRMED**
  Untimed works the same way: the bingo enables Next Round. Finding all words also ends the round early. **INFERRED for the button wiring**
- Lightning: the round ends when all n bingo words are solved (0x411680 → +1000). If the timer hits 0, the game is lost. **INFERRED for the loss path**

### Crossword word set (CONFIRMED, 0x41bcd6..0x41c1b8)
- Root length `L = min(5 + rand%4, playerMaxLen)`, picked with the same 0x43d550 loop.
- Sub-anagrams come from the **common** tree only. The list is shuffled with CRT `rand()` (0x422080).
- The root is **rejected unless exactly one full-length anagram exists** (`cmp edi,1` @0x41beb7).
- Word count: `N = min(10, 4 + rand % floor(4 + (round-1) × BLITZ_WORD_NUMBER_INCREMENT))`. So it's 4 words in early rounds and grows slowly.
- The first N shuffled non-root words are taken, then the root word is appended last.
- Guesses are validated against the full DamBeavers list (`+0x2ac` → `table+0x188`, @0x41c262..0x41c293).

### Grid construction (0x436090, CrosswordTable 0x8b0 bytes)
- Cell array at `table+0x1c0`, 8 bytes per cell (`int letter`, `byte blocked` @+4), stride **11** → an 18×11 grid. **CONFIRMED (stride/offsets)**
- The root (bingo) word goes in first, centered: `start = (18 − len)/2` on the 18-long axis, fixed index **5** on the 11-long axis, so it sits on the middle line.
  The cells just before and after it get marked blocked:
  ```
  00436180: mov eax, 0x12 ; sub eax, len ; cdq ; sub eax,edx ; sar eax,1
  00436194: mov [edi+0x24], eax         ; start
  00436197: mov dword ptr [edi+0x28], 5 ; middle line
  004361cf: imul edx, edx, 0xb          ; cell = a*11 + b
  004361ed: mov byte ptr [edx+ecx*8+0x1c4], 1   ; block cell at -1 / len
  ```
- The remaining words go through up to **10,000 attempts** (`cmp [esp+0x20],0x2710`). Each attempt:
  1. Pick a random already-placed word.
  2. List every letter-match pair whose crossing cell isn't blocked.
  3. Shuffle that list (0x439370).
  4. Place the new word **perpendicular** to the placed one at the first pair that passes the fit check 0x435f90, then commit with 0x435d30.

  **CONFIRMED for the flow.** The exact adjacency rules inside 0x435f90 are **INFERRED** (standard crossword no-touch checks).

---

## 5. Misc constants
- `properties/default.xml`: `BLITZ_WORD_NUMBER_INCREMENT=0.2`, `BLITZ_WORD_SIZE_INCREMENT=0.2` (read at runtime through the data manager, @0x413c60 / 0x413dd3 / 0x41bef9).
- `scene+0x2f0=6`, `scene+0x2f4=8` (ctor) are probably UI min/max rack sizes. I didn't trace where they're used. **INFERRED**
- WordTree member `+0x14=5`, `+0x18=8`: the length bounds of the cached pool (@0x43d58e). **CONFIRMED**
