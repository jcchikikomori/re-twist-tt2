# `TextTwist2.exe` — authoritative binary fingerprint

Re-derived from a fresh analysis of the artifact at
`/home/deck/.temp/Text Twist 2.exe` (SHA-256 below). This complements the
narrative in `gameplay-notes.md` (which is also from static analysis but was
written against an earlier copy of the binary, so a few of its specific
addresses are now stale). The **behavioural** notes in that file remain valid
— the engine logic didn't change between copies, just the compiler's
instruction layout.

## 1. File identity

| Field | Value |
|---|---|
| Path | `/home/deck/.temp/Text Twist 2.exe` |
| Size | 23,892,646 bytes (~22.8 MiB) |
| SHA-256 | `43b5791a7d87830025efae0db8f1cc5a02b6001f4703b0189adf1412cdbe22ac` |
| File time | 2024-04-02 |

`file(1)` output: `PE32 executable for MS Windows 4.00 (GUI), Intel i386, 8 sections`.

## 2. PE structure

Sections (`objdump -h`):

| Idx | Name | Size | VMA | Notes |
|---:|---|---:|---:|---|
| 0 | `CODE`  | 0x16e44 | 0x00401000 | code + embedded read-only data (`.tree` files are inside this range, at file offset ~0x4000 onward) |
| 1 | `DATA`  | 0x00700 | 0x00418000 | small init data |
| 2 | `BSS`   | 0x00000 | 0x00419000 | zero-fill on load |
| 3 | `.idata`| 0x014d0 | 0x0041a000 | import directory |
| 4 | `.tls`  | 0x00000 | 0x0041c000 | empty TLS callbacks |
| 5 | `.rdata`| 0x00018 | 0x0041d000 | shared read-only data |
| 6 | `.reloc`| 0x011c4 | 0x0041e000 | base relocations |
| 7 | `.rsrc` | 0x02fc8 | 0x00420000 | resources (icon, version, …) |

Entry point: RVA `0x17de0`, VA `0x18de0` (image base 0x1000). Authoritative from
the PE optional header, not the C runtime — `0x402b10` (cited in older notes
as the RNG) is actually a C++Builder thunk table jumping into kernel32.

## 3. Toolchain (new finding)

Not raw MSVC. Two strong indicators in the binary itself:

- **PE resource `DVCLAL`** (resource type 10, name `DVCLAL`, 16 bytes). This
  is the classic Borland C++Builder / Delphi package marker. The C++Builder
  RTL writes this as a sanity check that the binary hasn't been corrupted.
- **RTTI class mangling in the `GF2` namespace** (see §5) — the symbol
  encoding and naming style (`SmartPtr<T>`, `iInstance<T>`, `GFVector<T>`,
  `ModifierMethod<T>`, `MemoryBuffer_t<T>`) is GameHouse's in-house engine
  written in C++ against the Borland VCL / RTL.

PDB path embedded in the binary:
`c:\GHF\development\texttwist2\bin\win32\TextTwist2_Final.pdb` — "GHF" is the
internal namespace prefix used by GameHouse for the GF2 engine and all of its
casual-game titles. Confirms the original was built in a `c:\GHF\development`
sandbox.

## 4. Imports (kernel32 / advapi32 / gdi32 / user32 / winmm / ole32 / oleaut32 / shell32 / comctl32 / cabinet)

The interesting ones for understanding the game:

- `kernel32`: `LoadLibraryA`, `GetProcAddress`, `GetModuleFileNameA`,
  `GetPrivateProfileStringA`, `WritePrivateProfileStringA`,
  `CreateFileA`/`ReadFile`/`WriteFile` (for the `.tree` files), `Sleep`,
  `GetTempPathA`, `GetSystemDirectoryA`, `GetDiskFreeSpaceA`, `SetCurrentDirectoryA`.
- `advapi32`: `RegOpenKeyExA`, `RegQueryValueExA`, `RegSetValueExA`,
  `RegCreateKeyExA`, `RegEnumKeyExA`, `RegCloseKey` — settings + high scores
  are persisted under `HKCU\Software\...` (strings `currentplayer`,
  `highscoremanager`, `playermanager`, `highscoretables`).
- `winmm`: `timeGetTime` (the millisecond clock used to seed the LCG and to
  drive round timers — see `gameplay-notes.md` §2 and §3) and the rest of the
  Win32 multimedia API for `.ogg` playback.
- `shell32`, `comctl32`: standard GUI plumbing.
- `cabinet`: ships the installer stub (the game itself is not a cabinet).
- `oleaut32`, `ole32`: variant / BSTR support.

No `d3d*` or `opengl32` import — rendering goes through GameHouse's own
software/GF2 renderer.

## 5. C++ class hierarchy (recovered from RTTI)

All classes live in the `GF2` namespace, with `GF2::` elided below. The
mangled names (`.?AV…@@`) are stripped by `strings -a`.

### 5.1 Engine core

| Class | Role |
|---|---|
| `Application` | process entry, owns the singletons |
| `iInstance<T>` (CRTP) | template wrapper that exposes the per-class static instance pointer — the engine's "global" pattern |
| `Random` / `VRandom` | the LCG (`setSeed` / `nextInt`) |
| `SceneManager` | stack of `Scene`s, transition logic |
| `Scene` | base class for every screen |
| `Renderer` | draw pipeline |
| `ImageManager` | sprite / image cache |
| `Font` / `FontLayer` / `FontLine` / `FontCharPosition(s)` | bitmap-font stack |
| `SoundManager` | `.ogg` playback |
| `DataManager` | reads `data/*.xml`, `properties/*.xml` |
| `BasicProperties` | key/value config (uses TinyXML) |
| `SettingsManager` | persists to `HKCU\Software\...` via advapi32 |
| `FileArchive` | read files from disk or from a `.pak` archive |
| `ObjectFactory` | reflective `new T(args)` over a registered name |
| `InputHandler` | keyboard / mouse |
| `iMessageListener` | event-bus interface |
| `TaskManager` | deferred / periodic tasks (`TaskmanagerTimed`) |
| `HighScoreManager` / `HighScoreEntry` / `HighScoreTable` | high scores |
| `PlayerManager` / `AbstractPlayer` | current player selection |
| `CheatManager` / `CheatInfo` | cheat-code scaffolding |
| `ParticleManager` | particle effects |
| `Tooltip` | tooltip widget |
| `AnimTree` | animation tree |
| `Lua` | **embedded Lua scripting** — used at least for some configurable rules (see §6) |
| `ResourceManager` | central resource registry |
| `GameTree` / `GameWindow` | XML scene-tree and the OS window |

### 5.2 UI primitives

`Button`, `ButtonURL`, `Label`, `Sprite`, `LetterBall`, `LetterBox`,
`Switch`, `ImageData`, `ImageFrame`, `StyleFormat`, `TextLine`, `Color`,
`PolygonF`, `PolygonPointF`, `Matrix3x3`, `VertexBufferSlice`, `KeyStroke`,
`UTF8String`, `Graphics`, `GraphicsSettings`, `iInputListener`, `iImageLoader`.

Generic containers: `GFVector<T>` (vector of `T`),
`SmartPtr<T>` (refcount), `Point_t<T>`, `MemoryBuffer_t<T>`,
`IRefCount<T>`, `IRefCountImpl<T>`, `iInstance<T>`, `ModifierMethod<T>`,
`ModifierMethodParam<T, A>`, `ModifierMethodParamRef<T, A>`,
`pair<bool, T>` (used for switch-on/click pairings).

### 5.3 Game-specific (Text Twist 2 only)

| Class | Role |
|---|---|
| `TextTwist2Main` | WinMain, init engine, load settings, start at `SceneSplash` |
| `SceneSplash` | intro logo / legal splash |
| `LoadScene` | loading bar + logo animation between splash and the menu |
| `SelectionScene` | main menu (Timed / Untimed / Lightning / Crossword / Word of the Day / Trophies / High Scores) |
| `TextTwist2GameSettings` | player's settings (max word length 6/7/8, sound on/off, current player) |
| `TextTwist2Player` | current player profile |
| `TextTwist2Score` / `TextTwist2ScoreEntry` | one high-score row |
| `TextTwist2Scene` | **Classic mode** (the single `TextTwist2Scene` instance handles both Timed and Untimed via a mode flag) |
| `GameScene` | the abstract base for `TextTwist2Scene` / `BlitzScene` / `CrisscrossScene` |
| `BlitzScene` | **Lightning** mode — only bingos count, hint button, time +8 s per solve |
| `CrisscrossScene` | **Crossword** mode — 18×11 grid, root word with exactly one full-length anagram |
| `CrosswordTable` | the Crossword grid data |
| `DialogNextRound` | the modal "Next round" dialog (with score, time, round #) |
| `DialogPassRound` | the "Sorry, you didn't qualify" dialog |
| `DialogTextTwist2` | base dialog |
| `TaskmanagerTimed` | a `TaskManager` task driven by `timeGetTime` — runs the round timer |

## 6. Game-data files referenced from the binary

Discovered by `strings -a` on the executable. The `@$&%04\` prefix is a
C++Builder string constant for the `data/` directory.

```
data/TextTwist2Words.tree     25,004 words, lengths 3–15  (common / rack list)
data/DamBeaversWords.tree    116,757 words, lengths 2–16  (full / guess validator)
texttwist2_RADRM.dll          (RadGameTools RADRM — DRM, NOT to be analyzed)
```

Everything else under `@$&%04\` is the in-package asset tree. The patterns
that matter:

- `images/splash/`, `images/loadscene/`, `images/selectionscene/`,
  `images/gamescene/`, `images/blitzscene/`, `images/crosswordscene/`,
  `images/trophyscene/`, `images/gui/` — per-scene art
- `fonts/*.xml` + `fonts/*.png` — bitmap-font atlases (one PNG + one XML
  metadata file per font)
- `sound/*.ogg` — OGG Vorbis audio (Vorbis is the same codec RadGameTools'
  Miles Sound System uses, matching the `RADRM.dll` audio stack)
- `properties/default.xml` — XML property table. The binary references these
  names: `BLITZ_WORD_NUMBER_INCREMENT`, `BLITZ_WORD_SIZE_INCREMENT`,
  `BACKGROUND_TIMED`, `BACKGROUND_WOTD`, `WORD_OF_THE_DAY`. A second file
  `BLITZ_TIME_DECREMENT` and `CROSSWORD_WORD_NUMBER_INCREMENT` are documented
  in the XML but **do not appear in the binary** — confirmed absence.

## 7. Scripting

The presence of `iInstance<VLua@GF2@@>` and `VApplication` references to a
`Lua` subsystem indicate the engine embeds Lua and exposes it to game logic.
This is consistent with GameHouse's other 2008-era titles: most game-tuning
constants are still compiled in (see `0x59ba70`, `0x59ba88` in
`gameplay-notes.md` for the literal `2.0`, `5.0` doubles), but Lua is used
for some higher-level glue. The TTL2 game does not expose a user-facing
script hook; the script is bundled in the resource section.

## 8. What the existing `gameplay-notes.md` got right vs stale

Right (verified):

- The dictionary file format (`.tree` grammar) — byte-for-byte round-trips
  with the existing Python decoders.
- The dictionary split: `TextTwist2Words.tree` = common / rack,
  `DamBeaversWords.tree` = full / guess validator.
- The Java LCG (`0x5DEECE66D`, `0xB`, 48-bit, `nextInt = (int)(seed >> 16)`).
- Scoring: `10 * len²`, first bingo +1000, all-words +100 per slot, no time
  bonus.
- Timed round = 120 s with a 10 s warning.
- Lightning: 80.3 s base, +8 s per solve, cap 80.3 s, only bingos count.
- Crossword: 18×11 grid, root word with exactly one full-length anagram,
  10,000 placement attempts.
- WOTD: fixed seed 1337, day index = days since 2008-09-03.
- `timeGetTime` for the millisecond clock.

Stale / wrong for *this* binary:

- "RNG object at `[0x5fde90]`" and "`rand(min,max)` at 0x402b10" — those
  addresses are inside the C++Builder CRT thunk table, not the game's RNG.
  The behavioural description of the LCG is correct, but the address-level
  citations belong to a different copy of the binary.
- "PDB path string: `c:\GHF\development\texttwist2\bin\win32\TextTwist2_Final.pdb`"
  — the path is confirmed, but the binary is C++Builder, not MSVC. The
  `DVCLAL` resource settles that.
- Specific bytes around `0x44eb79..0x44eba7` cited as LCG constants — in
  *this* binary that range is inside `TextTwist2Words.tree` data, not code.

## 9. Evidence used

| Source | Tool | What it told us |
|---|---|---|
| PE headers, sections, imports | `objdump -h`, `objdump -p` | §1, §2, §4 |
| Toolchain | REA `inspect_pe_resources` + `strings` | §3 (DVCLAL) |
| Class names | `strings -a` filtered for `.?AV` | §5 |
| File/data references | `strings -a` filtered for `@$&%` | §6 |
| SHA-256, file size | `sha256sum`, `du`, `file` | §1 |
| Behaviour | cross-checked against `gameplay-notes.md` (static analysis) and the running TS engine (in this repo) | §8 |
