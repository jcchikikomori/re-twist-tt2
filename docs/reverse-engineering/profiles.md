# Profiles, progression and high scores

How the original keeps players, their progression (bag position, trophies,
counters) and the five high-score tables, recovered from `TextTwist2.exe`
(installed build, SHA-256 `0c6e962c…`, image base `0x400000`; see
`binary-fingerprint.md` §0) and from the install's `properties/*.xml`.
**CONFIRMED** was read from decompiled code, **XML** comes straight from
the data files, **INFERRED** is a reading the code does not spell out.

Legal note: identifiers, numbers and layout only. No art, audio or font
data is reproduced. Registry/file _formats_ are described so the port can
offer an importer later; the port itself writes its own plain-text files.

## 0. The three managers

`properties/application.xml` boots, in this order, `settingsmanager`,
`highscoremanager` and `playermanager` (XML). Each is a `GF2::iInstance`
singleton stored in a global:

| Class | Global | Ctor / init | Role |
| --- | --- | --- | --- |
| `GF2::SettingsManager` | `0x5fde9c` | `0x466300`, load `0x4666b0` | sound/fullscreen/word-length options, in the **registry** |
| `GF2::HighScoreManager` | `0x5fde94` | `0x457fc0`, load `0x458730`, save `0x458510` / `0x458bd0` | five `HighScoreTable`s, in a **file** |
| `GF2::PlayerManager` | `0x5fde98` | `0x460ac0`, load `0x4622c0`, save `0x462080` / `0x462550` | up to five `TextTwist2Player`s, in **files** |

### 0.1 Where things are stored (CONFIRMED)

- **Settings**: `HKEY_CURRENT_USER\<subkey>` read with `RegOpenKeyExA` /
  `RegEnumValueA` at `0x466f70`; `REG_SZ` values become string nodes,
  `REG_DWORD` values integer nodes. The sub-key string is built at
  `0x4665d0` from the game id (`gameid="TextTwist2"`, XML) under the
  `Software\GameHouse\` prefix seen in the binary. Known value names:
  `shutdowncorrect`, `GameSettngs` (sic, node name), `version`.
- **Save directory** (`0x47e2f0`): `SHGetSpecialFolderPathA` with
  `CSIDL_COMMON_APPDATA` (`0x23`), falling back to `CSIDL_APPDATA`
  (`0x1a`), then `"/" + <game folder>` and `CreateDirectoryA`. The
  folder name is the runtime string the resource manager holds at
  `0x5d2414` (empty in the image; set from the game id at start-up,
  INFERRED).
- **Players** (`0x4619d0`, `0x461b10`): one index file plus one file
  **per player, named after the player**. Removing a player calls
  `DeleteFileW` on that file (`0x460c30`).
- **High scores** (`0x458510`): one file holding all tables.
- Files are GF2 property documents written by the XML writer at
  `0x4541c0`. When a flag bit (`version != 0` on the manager, +0x74 /
  +0x1d) is set the writer reads the file back, passes the bytes through
  `0x48a840` and rewrites them (INFERRED: obfuscation). Not analysed.

Nothing from these locations is read by the port. See §5 for the port's
own format.

## 1. Player profile (`TextTwist2Player`)

Base class `GF2::AbstractPlayer` (name at +0x08). The TT2 subclass adds
the fields below; `0x40f060` is the "new player" initializer and
`0x40f3d0` the serializer (node name `TextTwist2Player`). All CONFIRMED.

| Offset | Key | Default (`0x40f060`) | Meaning |
| --- | --- | --- | --- |
| +0x2c | — | 0 | "returning player" flag; drives `TEXT_WELCOME_BACK` (§3.3) |
| +0x30 | `SEED` | `timeGetTime()` | bag seed (`BagState::seed`) |
| +0x34 | `INDEX` | 0 | bag index (`BagState::index`) |
| +0x38 | `WOD` | -1 | day number of the last Word-of-the-Day played |
| +0x3c | `MAX` | 8 | maximum word length setting (6 / 7 / 8) |
| +0x40..+0x9c | `ACHIEVEMENT/0..23` | 0 | 24 trophy slots (int, non-zero = won) |
| +0xa0 | `TRACKER/TWISTS` | 0 | twist count |
| +0xa4 | `TRACKER/SOLVED` | 0 | words spelled |
| +0xa8 | `TRACKER/HARD_SOLVED` | 0 | uncommon words spelled |
| +0xac..+0xbc | `TRACKER/0..4` | 0 | five counters, one per game mode (INFERRED: rounds completed) |
| +0xc0..+0xc4 | `TRACKER/0..4` (bool) | 0 | five flags, one per mode: tutorial seen (CONFIRMED, `0x425c50`; see `game-scene.md` §5) |

The serializer writes the counters with `sprintf("%d")` keys
(`0x449c90`), so the node looks like `TRACKER/0 … TRACKER/4` twice: once
as integers, once as booleans.

### 1.1 Trophies (XML `text.xml`, `TEXT_TROPHY_TITLES` / `_DESCRIPTIONS`)

Slot order is the XML order (1-based in the ids, 0-based in the array).
The conditions below are the on-screen descriptions; the code paths that
set each slot are **not yet decompiled** (follow-up).

| # | Title | Condition |
| --- | --- | --- |
| 1 | Quick Draw | 5 words without twisting (Untimed/Timed/WOTD) |
| 2 | Straight Shooter | 10 words without twisting |
| 3 | Bull's-eye | 20 words without twisting |
| 4 | Brainstorm | Lightning round with no twisting |
| 5 | Rolling Thunder | Lightning round with no passing |
| 6 | Lightning Reflexes | Lightning round with no hints |
| 7 | Nick of Time | Lightning round finished with ≤ 3 s left |
| 8 | Speed Speller | Round finished in < 5 s (Untimed/Timed/WOTD) |
| 9 | Puzzle Pro | Crossword with no twisting |
| 10 | Perfect Puzzler | Crossword with no mistakes |
| 11 | Look Ma, No Twisting! | All words in one round, no twisting |
| 12 | Bingo Blaster | Only bingo words, 5 levels in a row |
| 13 | Bingo Boss | Only bingo words, 10 levels in a row |
| 14 | Bonkers for Bingos! | Only bingo words, 20 levels in a row |
| 15 | With a Twist | Twist 1,000 times (`TWISTS`) |
| 16 | Dizzy | Twist 5,000 times |
| 17 | Twisted Twister | Twist 10,000 times |
| 18 | Word Nerd | Spell 1,000 words (`SOLVED`) |
| 19 | Wordsmith | Spell 5,000 words |
| 20 | SUPER SPELLER | Spell 10,000 words |
| 21 | Well Rounded | Complete a round in every mode (`TRACKER/0..4`) |
| 22 | Text Twist Maniac | 10 rounds in every mode |
| 23 | Text Twist Fanatic | 30 rounds in every mode |
| 24 | Human Dictionary | Spell 500 uncommon words (`HARD_SOLVED`) |

Unwon trophies show `HIDDEN_ACHIEVEMENT` ("Secret Trophy!…") in the
trophy room (`SCENE_TROPHY`, `MUSIC_trophy_room`, `SOUND_game_trophy_won`).

## 2. Player manager rules (CONFIRMED)

| Rule | Where |
| --- | --- |
| At most **5** players. The Add button only opens the name dialog when `count < 5` (`0x40ad00`); the dialog enables Add when `count < 5` and Remove when `count > 0` (`0x40aa40`). | `0x40ad00`, `0x40aa40` |
| Name length 1..**15** characters. Empty input is ignored (`0x4088c0` returns before creating). The 15 is `maxlength="15"` on `INPUT_NAME` (XML). Length is counted in UTF-8 code points (`0x45fa60`). | `0x4088c0`, XML |
| Duplicate check is an **exact, case-sensitive** string compare against every existing name (`0x460d10`). On a hit the game shows `CTEXT_PLAYER_EXISTS_HEADER` / `CTEXT_PLAYER_EXISTS` and keeps the input dialog open. | `0x460d10` |
| A new player is created with the defaults in §1, appended (`0x460bb0`), **becomes the current player** (`0x460e60`) and everything is saved (`0x462080`). The first player added also becomes current implicitly. | `0x4088c0` |
| Selecting a player (`0x460e60`) first saves the previous current player's file, then loads the selected one (`0x460eb0`). | `0x460e60` |
| Removing (`0x460c30`): if it is the current player, current becomes none; the player's file is deleted; the list is saved. The dialog then selects `min(old + 1, count - 2)` and, when that is < 0, nothing (`0x40aef0`). | `0x460c30`, `0x40aef0` |
| When the list becomes **empty** the name-input dialog opens immediately (`0x40aef0`), and the main menu opens it on entry (`0x42d9b0`). | |
| Loading (`0x4622c0`): read `playernames[]`, construct each player via the class factory (`0x44f5f0`), then `currentplayer` (a name) is looked up; an unknown name leaves no current player. | `0x4622c0` |
| Saving (`0x462550`): `version`, `currentplayer` (only when one is set), `playernames[]`, then each player's own node. | `0x462550` |

## 3. Dialogs (XML `dialogs.xml`)

### 3.1 `DIALOG_PLAYERSELECT` (`DialogPlayerSelect`) 422 × 367, centred

| Component | Image / text | Position (dialog space) |
| --- | --- | --- |
| `SELECTNAME_HEADER` | `CTEXT_PLAYER_SELECT` "Select your name", `FONT_DIALOG_HEADERS2` | x 211, y 17, anchor top, centred |
| `BORDER` sprite | `PLAYER_BORDER` | x 61, y 0 |
| name buttons ×5 | `NAME_SELECT_BUTTON`, label font `FONT_SETTINGSTEXT` | first at y 80, `spacing="40"`, anchor top, x 400 (template), label y 4 |
| `BUTTON_ADD` | `BUTTON_ADD`, tooltip `CTOOLTIP_NAMESELECT_ADD` | x 146, y 282, anchor top-right |
| `BUTTON_REMOVE` | `BUTTON_REMOVE`, tooltip `CTOOLTIP_NAMESELECT_DELETE` | x 153, y 282, anchor top-left |
| `BUTTON_OK` | `BUTTON_OK`, tooltip `CTOOLTIP_NAMESELECT_OK` | x 337, y 282, anchor top |

The five name buttons are created from the `namebutton` template
(`0x40aa40` loops `5 - count` to blank the unused ones). The current
player's button is highlighted and its y is remembered (+0x204).
Remove asks `CTEXT_REMOVE_PLAYER_HEADER` / `CTEXT_REMOVE_PLAYER` first
(yes/no dialog, INFERRED from the strings; the confirm path is `0x40aef0`).
That `DIALOG_YESNO` word-wraps its `MESSAGE` (`width="390"`, `wrapstyle="wordwrap"`) and moves
YES / NO below it (`0x40b470`, `game-scene.md`); the port builds it with the same `build_yes_no()`
as the in-game confirms, so the question takes two lines above the buttons.

### 3.1.1 Buttons (`GF2::Button`, CONFIRMED loader)

Every profile button uses `setup="DEFAULT_BUTTON"` (`clicksound="SOUND_BUTTON"`) and a 4-row sheet
(`rowcount="4"`: normal / hover / pressed / disabled, the row order the welcome flow already uses). The
Button XML loader `0x48d390` (`ev_9868528458dbaeab13eb36a04bd7b1af6768b2b2cb5d6673cf84ed1484f9bbf4`) reads
`enabled`, `clicksound` (event `click`), `oversound` (event `over`), `disabledsound` (event `disabled`),
`soundsection` and `firekey`; the optional pixel hit mask (`hitmask`, `hitmaskthreshold`) is
`0x48c9c0`. No profile button sets `oversound`, so hovering plays nothing; the tooltips
(`CTOOLTIP_NAMESELECT_*`, text in `commontext.xml`) come from the shared `tooltip` attribute and a
`GF2::Tooltip` (`DEFAULT_TOOLTIP`: `FONT_TOOLTIP`, black, padding 3, alpha 220, max width 200).
UNKNOWN: the code that picks the row from the state, and the tooltip's delay and placement.

**Port:** every profile dialog (player select, name input, yes / no, message) now shows the hover,
pressed and disabled rows; before, only the name input refreshed them. Tooltips are not drawn yet.

### 3.2 `DIALOG_NEWPLAYER` (`DialogNameInput`) 402 × 223, centred

| Component | Image / text | Position |
| --- | --- | --- |
| `NEWPLAYER_HEADER` | `CTEXT_ENTER_NAME` "What is your name?", `FONT_DIALOG_HEADERS2` | x 200, y 19, anchor top |
| `INPUT_NAME` | `INPUT_BOX`, cursor `CURSOR`, `maxwidth=140`, `maxlength=15`, blink 300 ms on / 300 ms off | x 198, y 101, pivot+anchor centre, cursor y -8 |
| `BUTTON_OK` | `BUTTON_OK` | x 180, y 137, anchor top-right |
| `BUTTON_CANCEL` | `BUTTON_CANCEL` | x 220, y 137, anchor top-left |

OK runs `0x4088c0` (§2). Cancel just closes; with zero players the main
menu re-opens the dialog (`0x42d9b0`).

### 3.3 Main-menu name label (`0x42f1a0`, CONFIRMED)

- Text = `TEXT_WELCOME` ("Welcome") when the current player's +0x2c flag
  is 0, else `TEXT_WELCOME_BACK`; followed by the player name
  (`LABEL_PLAYERNAME`). No player: the label is hidden.
- The Word-of-the-Day button (+0x1e8) is enabled only when
  `player.WOD != today`, where
  `today = (time64() - tz_minutes * 60) / 86400 - 14125`
  (`_ftime64_s` timezone, epoch day 14125 = 2008-09-03, same as
  `bag.hpp::WOTD_EPOCH_DAY`).

## 4. High scores

### 4.1 Tables (CONFIRMED)

`HighScoreManager::table(name)` (`0x406e90`) creates a missing table
with `descending = true`, `lastscore = -1`, `maxentries = 10`, and
**fills it with 10 default entries whenever its size is not 10**
(`0x406f70`):

- scores `1000, 2000, …, 10000` in that insertion order;
- name = `CTEXT_HIGHSCORE_NAMES[rand(0, n)]` using the global Java LCG
  (`random` command), redrawn up to 10 times while the index was
  already used (a `std::set`), keeping the last draw otherwise;
- `isprefilledscore = true`.

Table keys used by the results dialog (`0x4068d0`): `timed`, `untimed`,
`blitz`, `crossword`, `wotd`. The `wotd` table shows `TEXT_HIGHSCORE_DATE`
("Date:") instead of `TEXT_HIGHSCORE_EXTRA` ("Round:") for the extra
column.

Serialized (`0x457910`, `0x4570e0`): `ScoreTable { descending,
maxentries, lastscore, entries[] }`, `HighScoreEntry { scorevalue,
playername, isprefilledscore }`; the manager node (`0x458bd0`) holds
`version` (= `HIGHSCORES_VERSION`), `uid` and `scoretables[]`.

### 4.2 Entry and ordering (CONFIRMED)

`HighScoreEntry` (`0x456cf0`): name (+0x08), has-name flag, score (+0x2c),
`isprefilledscore` (+0x30, **default true**), `uid` (+0x34, copied from
the manager counter), rank override (+0x38, default -1), extra text
(+0x44, the round number or the date).

`compare(other)` (`0x4572f0`):

```text
if (rank < 0)  return score <= other.score;   // ordinary entries
else           return rank  <  other.rank;    // forced order
```

`insert(entry)` (`0x457cd0`) returns the index or -1:

```text
for i, e in entries:
    if (e.compare(entry) == descending)      // first e with e.score <= entry.score
        insert at i
        if (maxentries > 0 && size > maxentries) drop last
        lastscore = i; return i
if (maxentries > 0 && size >= maxentries)  lastscore = -1; return -1
push_back(entry); lastscore = size - 1; return lastscore
```

So a tie goes **above** the existing equal score, and with the default
10 prefilled entries a new score must beat 1000 to get in. Worked
example: table `[10000 … 1000]`, insert 2000 → index 8, `1000` dropped.

### 4.3 Round end (`0x416440`, CONFIRMED)

When the round's score is > 0 the scene builds an entry (current player's
name, score, extra = round number via `%d`), inserts it into the mode's
table, and shows the "new high score" dialog when the returned index is
not -1, otherwise the plain end-of-game dialog.

## 5. The port

The engine module is `engine/include/tt2/profile.hpp` (pure, no IO) and
`engine/include/tt2/profile_store.hpp` (plain-text files under a
directory the UI chooses, e.g. `$XDG_DATA_HOME/re-twist-tt2/`). Rules in
§2 and §4 are implemented verbatim; the deviations are deliberate and
listed here:

- Storage is two UTF-8 text files, `profiles.txt` and `highscores.txt`,
  not registry + per-player XML. There is no obfuscation pass.
- Names may not contain tab or newline (the file format's separators).
  The original has no such limit; it is a 15-character box either way.
- Trophy _conditions_ are not evaluated yet (slots and counters are
  stored). See §1.1.

### 5.1 The UI (`ui/include/tt2ui/profile_dialogs.hpp`)

The dialogs use the §3 layout verbatim. Points that are inferred or
simplified:

- The welcome label joins `TEXT_WELCOME` / `TEXT_WELCOME_BACK` and the
  name with a single space (INFERRED; 0x42f1a0 was not read to the
  byte).
- "Player exists." is shown in the `DIALOG_YESNO` frame with one OK
  button; the original's message dialog blueprint was not decoded. The
  typed name survives the message.
- A player is made current as soon as its row is clicked; OK only
  closes. Cancel on the very first name is disabled instead of hidden.
- The text cursor sits right after the typed text with `cursory="-8"`
  applied to the label's baseline (INFERRED placement).
- Fonts: `fonts/*.xml` describe glyphs as `sx sy sw sh` in an unpadded
  sheet plus `ix iy` cell indexes; every layer PNG stores the cell at
  `(sx + 2p*ix, sy + 2p*iy)` with size `(sw + 2p, sh + 2p)` for its
  `padding` p. That formula reproduces the exact pixel size of all nine
  shipped sheets (`font_uibar.png` 344×338 at p=4,
  `font_uibar_stroke.png` 392×382 at p=6, ...). Layers draw last to
  first (shadow, stroke, face). Without imported fonts a 5×5 procedural
  font stands in.
- `DEFAULT_PAINTER` frame: the nine `images/gui/dialogdefault/*.png`
  borders are opaque only in a thin outer band and fade to transparent
  on their inner side (an inner shadow meant to fall on the centre
  image), so the centre (`images/gui/background.png`, cropped around
  its middle) is painted under the whole dialog, not just inside the
  border boxes. Measured from the alpha channels: the fill starts at
  (1, 1), stops 7 px short of the right edge and 16 px short of the
  bottom (those strips are drop shadows outside the frame), and leaves
  the ~10 px rounded corners uncovered (`dialog_fill_rects`). The
  `offsetleft/top/right/bottom` values of the painter (14/56/14/26) are
  content margins, not fill bounds.

## 6. Address index

| Address | What |
| --- | --- |
| `0x40aa40` | PlayerSelect dialog: refresh buttons, enable Add/Remove |
| `0x40ad00` | Add button → `DIALOG_NEWPLAYER` if `count < 5` |
| `0x40aef0` | Remove confirmed → remove, reselect, reopen input if empty |
| `0x40f060` | `TextTwist2Player` defaults |
| `0x40f3d0` | `TextTwist2Player` serialize |
| `0x4088c0` | Name-input OK: validate, duplicate check, create, select, save |
| `0x406e90` | `HighScoreManager::table(name)` + default fill trigger |
| `0x406f70` | default table fill |
| `0x4068d0` | results dialog: mode → table key |
| `0x416440` | round end: insert score, pick dialog |
| `0x42d9b0` | main menu init: open name input when no players |
| `0x42f1a0` | main menu welcome label / WOTD button |
| `0x456cf0` | `HighScoreEntry` ctor |
| `0x4572f0` | `HighScoreEntry::compare` |
| `0x457cd0` | `HighScoreTable::insert` |
| `0x4574b0` | `HighScoreTable` load |
| `0x457910` | `HighScoreTable` save |
| `0x457fc0` | `HighScoreManager` init (`HIGHSCORES_VERSION`) |
| `0x458510` / `0x458bd0` | `HighScoreManager` save |
| `0x458730` | `HighScoreManager` load |
| `0x45fa60` | UTF-8 code-point length |
| `0x460bb0` | `PlayerManager::add` |
| `0x460c30` | `PlayerManager::remove` (+ `DeleteFileW`) |
| `0x460d10` | `PlayerManager::exists` |
| `0x460e60` / `0x460eb0` | `PlayerManager::select` / load player file |
| `0x461160` | save one player file |
| `0x461ca0` | find player by name |
| `0x4622c0` / `0x462550` | `PlayerManager` load / save |
| `0x462080` | `PlayerManager` save-all |
| `0x466f70` | `SettingsManager` registry read |
| `0x47e2f0` | save directory |
