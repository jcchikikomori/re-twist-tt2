# Text Twist 2 — how the game works (architecture + runtime)

The game is a 2008-era Win32 casual title by GameHouse, built on GameHouse's
in-house **GF2** engine (C++ / Borland C++Builder). This document is the
behavioural summary that the reconstruction has to match. Behavioural
assertions trace to `binary-fingerprint.md` (this binary) and
`gameplay-notes.md` (disassembly of the same code, older copy).

## 1. Process boot

```
TextTwist2.exe
  └─ TextTwist2Main::run()           // WinMain
       ├─ engine init (Application, Renderer, DataManager, SettingsManager,
       │   SoundManager, HighScoreManager, PlayerManager, CheatManager)
       ├─ TextTwist2GameSettings::load()   // HKCU\Software\... (advapi32)
       ├─ TextTwist2Player::loadCurrent()  // last player, or create "Player 1"
       ├─ load Lua ("scripts/?.lua", embedded)
       └─ SceneManager::push(new SceneSplash())
            └─ SceneSplash → LoadScene → SelectionScene
                 ├─ "Timed"     → new TextTwist2Scene(TIMED)
                 ├─ "Untimed"   → new TextTwist2Scene(UNTIMED)
                 ├─ "Lightning" → new BlitzScene()
                 ├─ "Crossword" → new CrisscrossScene()
                 ├─ "Word of the Day" → reuses a single shared TextTwist2Scene(WOTD)
                 └─ "Trophies"  → TrophyScene (read-only)
```

The settings live in the registry. The strings `currentplayer`,
`highscoremanager`, `playermanager`, `highscoretables` recovered from the
binary line up with the `PlayerManager` / `HighScoreManager` iInstance
singletons.

## 2. The dictionary (the core data structure)

Two `.tree` files, both shipped next to the exe as `data/<name>.tree`. The
file format is a **pre-order serialized trie** with a one-byte alphabet:

```
Node := [a-z]* ('0' | '1') Node*
```

`a`..`z` are the child letters, in sorted order; `1` means "the path to this
node is a complete word", `0` means it isn't; the children follow in the
same order. There is no header, no length, no checksum, no trailing newline.
The pre-order walk enumerates the words in alphabetical order; the count of
`1` bytes is the dictionary size.

For Text Twist 2:

| File | Words | Length range | Role |
|---|---:|---|---|
| `data/TextTwist2Words.tree` | 25,004 | 3–15 | **common** — supplies the rack/root word and the on-screen word slots. Rack candidates are 5–8 letters; the board shows 3–8 letter sub-anagrams of the rack. |
| `data/DamBeaversWords.tree` | 116,757 | 2–16 | **full** — used **only to validate guesses**. A word that is in the full list but not in the common list is accepted as an "uncommon" word (Human Dictionary trophy). |

The two files are linked: `TextTwist2Words` is a strict subset of
`DamBeaversWords`. Round-tripping either file through the canonical encoder
is byte-identical, so the dictionary can be updated by rebuilding the trie
from a flat word list.

## 3. The random-number generator

The game uses Java's `java.util.Random` (a 48-bit linear congruential
generator) but only with its byte-level formula. A single `VRandom` object
lives at a fixed address and is reseeded every time a new "bag" of rack
words is needed.

```c
static const uint64_t MULTIPLIER = 0x5DEECE66DULL;   // Java's magic
static const uint64_t ADDEND     = 0xB;
static const uint64_t MASK       = (1ULL << 48) - 1;

void setSeed(int64_t s) { seed = ((uint64_t)s ^ MULTIPLIER) & MASK; }
int32_t nextInt() { seed = (seed * MULTIPLIER + ADDEND) & MASK;
                    return (int32_t)(seed >> 16); }            // top 32 bits, sign-extended
int32_t rand(int32_t lo, int32_t hiExclusive) {               // half-open, made non-negative
    int32_t r = nextInt() % (hiExclusive - lo);
    if (r < 0) r += (hiExclusive - lo);
    return lo + r;
}
```

The shuffle is `std::random_shuffle` semantics with the LCG as the RNG:

```c
for (int i = 1; i < n; i++) swap(a[i], a[rand(0, i + 1)]);
```

`timeGetTime()` (WinMM) is the millisecond clock used to seed the bag and
to drive round timers. On a non-Windows build `clock_gettime(CLOCK_MONOTONIC)`
on the wall-clock-millisecond counter is the equivalent.

## 4. The "bag" of rack words

A *persistent* shuffled bag of every common word of length 5..8 is what
guarantees the player never sees the same rack twice in a row and that two
players using the same dictionary see the same racks in the same order.

```
bag = { seed: int32, index: int32 }      // persisted in HKCU
```

On each rack request:

```c
if (bag.index >= pool.size) {
    bag.seed  = (uint32_t)timeGetTime();
    bag.index = 0;
    shuffle_in_place(pool, seed);        // in-place, using the LCG
}
for (int tries = 0; tries < pool.size; tries++) {
    string word = pool[bag.index % pool.size];
    bag.index += 1;
    if (subAnagramsCount(word, commonList) >= 15) return word;   // playable
}
throw NoRackError();
```

The `subAnagramsCount >= 15` filter is the game's "interesting rack" rule:
a rack is only worth playing if it has at least 15 sub-anagrams in the
common list (the rack word itself included). That guarantees the board will
have around 48 slots to fill.

The bag is what the player perceives as "the order of words in the game".
Saving and restoring the bag across sessions is exactly what makes two
runs of the game identical — it is the **save-game** mechanism.

## 5. Classic mode (Timed and Untimed)

`TextTwist2Scene` is one class that handles both classic variants, plus Word
of the Day. The differences are just three booleans: `timed`, `wotd`, and
the dictionary they load.

### 5.1 Picking the rack

```c
L = min(rand(5, 9), maxWordLength);          // maxWordLength in {6, 7, 8}
attempt = 0;
best    = null;
for (int i = 0; i < 5; i++) {
    word = nextWordFromBag();                 // see §4
    if (word.length > L) continue;            // skip longer words for free
    if (word.length == L) return word;        // exact match: keep
    attempt += 1;
    if (best == null || word.length > best.length) best = word;
}
return best;                                  // closest shorter word after 5 attempts
```

`maxWordLength` is the player's setting (default 8). The default in the
binary is 6 (the `+0x2f0` field in the scene ctor).

### 5.2 Building the board

The board is the **sub-anagrams of the rack in the common list, length 3..8**.
If the count exceeds 48, each length bucket gets the quota
`max(1, trunc(count * 48 / total))`. Slots fill in tree-walk order, which is
alphabetical for a canonical trie.

The full DamBeavers list is the **guess validator**; a guess that is in
DamBeavers but not in TextTwist2Words is accepted as an "uncommon" word
(Human Dictionary trophy). Two-letter DamBeavers words validate but have
no slot to land in, so they score nothing.

### 5.3 Scoring (CONFIRMED at 0x4247fe)

```c
points += 10 * len * len;            // per word, always
if (bingo && !qualified_this_round) { // bingo = word uses every rack letter
    points += 1000;
    qualified = true;
}
if (found == totalSlots) {
    points += 100 * totalSlots;
    endRoundEarly();
}
```

There is **no time bonus** in Classic. The "Speed Speller" trophy is
awarded when a round ends with at least 5 s still on the clock — i.e. the
remaining time must be in `[115000, 120000]` ms.

### 5.4 Timed round

- Round duration: **120,000 ms** (`0x1d4c0`), set in `scene+0x2fc`.
- A 10 s warning starts at `0x2710` (10 000 ms) and steps down 1 s at a time.
- Time-up while not qualified → `DialogPassRound` ("Sorry, you didn't
  qualify"), end game.
- Time-up while qualified → next round.
- The timer ticks only in `CLASSIC_TIMED` and `WORD_OF_THE_DAY`.

## 6. Lightning / Blitz mode

`BlitzScene` is a separate class. The rack and the anagram enumeration
are shared with Classic, but the rules differ:

- **Only bingo words are accepted** (guess length == rack length).
- Words per round: `n = min(10, floor(5 + (round - 1) * 0.2))`. So 5 on round
  1, growing by 1 every 5 rounds.
- Per-word length: `len = min(maxWordLength, max(5, floor(7 + (round - 1) * 0.2 - U[0, 2])))`.
- Per-word score: `10 * len * (len - min(len, hintLettersUsed))`. Hints cost
  points.
- Round complete: `+1000`.
- Timer: 80,300 ms at round start, **+8,000 ms per solved word**, capped at
  80,300 ms.
- Hints come by themselves: 9 s on a word, then every 4 s, each one a ghost
  letter of the answer that costs `10 * len` points. `button_hint.PNG` ships
  but `dialogs.xml` places no hint button. See `lightning.md`.

## 7. Crossword mode

`CrisscrossScene` builds a single crossword grid per round and the player
fills it in.

- Grid: **18 × 11** cells, each cell 8 bytes (`int letter; byte blocked`),
  stride 11. The blocked cells come from standard crossword "no-touch"
  rules around the root word's first/last cell.
- Root length: `L = min(5 + rand() % 4, maxWordLength)`, picked with the
  same bag loop.
- The root is **rejected unless it has exactly one full-length anagram** in
  the common list.
- Word count per puzzle: `min(10, 4 + rand() % floor(4 + (round - 1) * 0.2))`.
- The remaining N words are the first N non-root sub-anagrams of the root
  in the common list, shuffled with the CRT's `rand()`. The root is appended
  last.
- Placement: pick a random already-placed word, list every letter-pair
  match, shuffle, place perpendicular at the first pair that passes the
  fit check. Up to **10 000 attempts** per new word.
- Guesses validate against the full DamBeavers list.
- Scoring: each newly filled word `+10 * len²` once. Completing the puzzle
  adds `+1000`.

## 8. Word of the Day

Reuses the same `TextTwist2Scene` class with a different bag:

```c
seed   = 1337;                                  // fixed, 0x539
dayIdx = (time(NULL) / 86400) - 14125;          // days since 2008-09-03
shuffled = shuffle(commonPool5to8, seed);
word = shuffled[dayIdx];   // no modulo, no bounds check, no 15-sub-anagram test (game-scene.md §8.2)
```

After picking, `0x4262b0` keeps the first **5 common sub-anagrams per length** (3..8), before the
48-slot quota. Rules, round end and day numbers: `game-scene.md` §8.

## 9. Persistence

Three places store state:

1. **HKCU\Software\...\Text Twist 2\\** — current player, settings, high
   scores. The strings in the binary are the exact subkey names:
   `currentplayer`, `highscoremanager`, `playermanager`,
   `highscoretables`.
2. **Bag state** — saved with the player profile as `seed` and `index`.
   This is the cross-session continuity that lets a player resume exactly
   where they were.
3. **Trophies** — flags stored in the same registry, one bit per trophy
   (Speed Speller, Human Dictionary, Word of the Day streak, etc.).
   The image names `trophy01.png` through `trophy17.png` enumerate them.

## 10. What the reconstruction has to preserve

1. **Dictionary format byte-identity.** A new `.tree` produced from a new
   word list must encode and decode back to the same bytes. The format is
   canonical.
2. **LCG + shuffle.** Any other RNG breaks the bag and the persisted
   high-score replay value. Java's LCG is mandatory.
3. **Sub-anagram DFS with "first index only" duplicate skip.** This is
   the order the original enumerates; the board depends on it.
4. **15-sub-anagram rack filter.** Without it, the rack and the board can
   disagree.
5. **48-slot cap with the `max(1, trunc(count*48/total))` quota.** This
   is what keeps the board UI consistent across dictionaries of different
   sizes.
6. **The four scoring formulas** (Classic, Lightning, Crossword, WOTD)
   exactly. They are the part of the game the player perceives most.
7. **Bag persistence.** Cross-session replay depends on the exact
   `(seed, index)` tuple.
8. **Registry round-trip.** The settings panel reads back what it wrote.

Everything else (the C++Builder RT, the GF2 engine, the VCL-style UI
widgets, the `.ogg` audio, the OS window) is an implementation detail that
the rewrite is free to replace.
